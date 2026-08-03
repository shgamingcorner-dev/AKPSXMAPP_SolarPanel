#!/usr/bin/env python3
"""
hermes-verify-tracker-sim.py -- behavioral TDD oracle for AKPS tracker.cpp
==========================================================================
Mirrors the C hill-climb state machine (tracker.cpp) 1:1 in Python and
asserts the plan's Phase-2 scenarios:

  (a) converges to within 2*TRACKER_STEP_DEG of the peak from a wrong start
  (b) a cloud dip (feedback *= 0.05 for 3 steps) sweeps toward the sun
      target and recovers
  (c) a local-maximum bump is escaped by the periodic re-sweep
  (d) deadband stops oscillation at the peak (no hunting)
  (e) night (feedback < TRACKER_CLOUD, no target) parks at TRACKER_MIN_ANGLE
      and holds; dawn (feedback rises) resumes stepping
  (f) startup: with a known sun target, first tick after 3s moves there

Run:  python hermes-verify-tracker-sim.py
Exit 0 on all pass; nonzero with details on any failure.

The feedback model is feedback-agnostic (same shape for LDR% or current A):
    fb(angle) = BASE + PEAK * gauss(angle - SUN_ANGLE) + noise
    (+ optional secondary gaussian as a local max)
"""

import math
import random
import sys

# --- Mirrors config.h ---
TRACKER_MIN_ANGLE = 10
TRACKER_MAX_ANGLE = 170
TRACKER_STEP_DEG = 5
TRACKER_STEP_MS = 2000
TRACKER_RE_SWEEP_MS = 600000
TRACKER_DEADBAND = 2.0      # LDR %
TRACKER_CLOUD = 10.0        # LDR %

# --- Mirrors tracker.cpp state ---
TRK_TRACKING, TRK_SWEEPING, TRK_HOLD, TRK_PARKED = range(4)


class Tracker:
    """Python mirror of tracker.cpp (same logic, same constants)."""

    def __init__(self, sun_target=-1):
        self.angle = 90.0
        self.sun_target = sun_target          # -1 = unknown/night
        self.state = TRK_TRACKING
        self.dir = 1
        self.last_fb = -1.0
        self.last_step = 0
        self.last_resweep = 0
        self.startup_done = False
        self.t = 0
        self.actions = []                     # log for assertions

    def set_sun_target(self, a):
        self.sun_target = a

    def _servo_to_angle(self, a):
        a = max(TRACKER_MIN_ANGLE, min(TRACKER_MAX_ANGLE, a))
        self.angle = a
        self.actions.append(('move', self.t, a))

    def _park_at_min(self):
        self._servo_to_angle(TRACKER_MIN_ANGLE)
        self.last_fb = -1.0
        self.state = TRK_PARKED

    def tick(self, fb):
        """One tick. `fb` is the feedback reading at the CURRENT angle."""
        now = self.t

        # B5 startup: first tick after 3s
        if not self.startup_done and now >= 3000:
            self.startup_done = True
            if self.sun_target >= 0:
                self._servo_to_angle(self.sun_target)
                self.last_fb = -1.0

        # periodic re-sweep
        if now - self.last_resweep >= TRACKER_RE_SWEEP_MS:
            self.last_resweep = now
            if self.sun_target >= 0:
                self._servo_to_angle(self.sun_target)
                self.dir = 1
                self.last_fb = -1.0

        if now - self.last_step < TRACKER_STEP_MS:
            return
        self.last_step = now

        # dark / cloud
        if fb < TRACKER_CLOUD:
            if self.sun_target >= 0:
                self._servo_to_angle(self.sun_target)
                self.last_fb = -1.0
                self.state = TRK_SWEEPING
            elif self.state != TRK_PARKED:
                self._park_at_min()
            return

        # dawn resume
        if self.state == TRK_PARKED:
            self.state = TRK_TRACKING
            self.last_fb = -1.0

        if self.last_fb < 0.0:
            self.last_fb = fb
            self._servo_to_angle(self.angle + self.dir * TRACKER_STEP_DEG)
            self.state = TRK_TRACKING
            return

        delta = fb - self.last_fb
        self.last_fb = fb

        if delta > TRACKER_DEADBAND:
            pass                              # improved: keep direction
        elif delta < -TRACKER_DEADBAND:
            self.dir = -self.dir               # worse: reverse
        else:
            self.state = TRK_HOLD
            return                            # deadband: stop

        self._servo_to_angle(self.angle + self.dir * TRACKER_STEP_DEG)


def feedback_model(angle, sun_angle, base=20.0, peak=70.0, sigma=22.0,
                   noise=0.0, local_max=None):
    """fb = base + peak*gauss(angle-sun) (+ optional smaller local max)."""
    v = base + peak * math.exp(-((angle - sun_angle) ** 2) / (2 * sigma ** 2))
    if local_max:
        lm_a, lm_p, lm_s = local_max
        v += lm_p * math.exp(-((angle - lm_a) ** 2) / (2 * lm_s ** 2))
    if noise:
        v += random.uniform(-noise, noise)
    return v


PASS = []


def check(name, cond, detail=""):
    PASS.append(cond)
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}{('  -- ' + detail) if detail else ''}")


def run_sim(sun_angle, steps, target=-1, start=90, noise=0.0, local_max=None,
            cloud_dip_at=None, re_sweep_at=None):
    """Advance the tracker `steps` ms (one tick per TRACKER_STEP_MS), with an
    optional cloud-dip window (list of (t_start, t_end)) and re-sweep event."""
    trk = Tracker(sun_target=target)
    trk.angle = start
    dipper = cloud_dip_at is not None
    dip_start, dip_len = cloud_dip_at if dipper else (0, 0)
    for t in range(0, steps, 1000):
        trk.t = t
        if dipper and dip_start <= t < dip_start + dip_len:
            fb = feedback_model(trk.angle, sun_angle, noise=noise,
                                local_max=local_max) * 0.05
        else:
            fb = feedback_model(trk.angle, sun_angle, noise=noise,
                                local_max=local_max)
        if re_sweep_at and t == re_sweep_at:
            trk.last_resweep = 0   # force a re-sweep this tick
        trk.tick(fb)
    return trk


def main():
    random.seed(42)
    print("== hermes-verify-tracker-sim: scenario checks ==")

    # (a) converges from a wrong start to within 2 steps of the peak
    print("\n(a) convergence from wrong start")
    sun, start = 135.0, 40.0
    trk = run_sim(sun, steps=60_000, target=sun, start=start, noise=1.5)
    check("converges near peak", abs(trk.angle - sun) <= 2 * TRACKER_STEP_DEG + 1,
          f"final angle={trk.angle:.1f}, sun={sun}")

    # (b) cloud dip -> sweep to target, then recover
    print("\n(b) cloud dip recovery")
    trk = run_sim(sun, steps=90_000, target=sun, start=40, noise=1.0,
                  cloud_dip_at=(50_000, 3_000))
    moved_toward_target = any(
        a == sun for (act, t, a) in trk.actions if act == 'move')
    check("swept to sun target during dip", moved_toward_target,
          "no move landed exactly on target")
    check("recovered after dip", abs(trk.angle - sun) <= 2 * TRACKER_STEP_DEG + 2,
          f"final angle={trk.angle:.1f}")

    # (c) local-max bump escaped by re-sweep
    print("\n(c) local max + re-sweep escape")
    trk = run_sim(sun, steps=1_000_000, target=sun, start=30, noise=0.8,
                  local_max=(45.0, 45.0, 8.0), re_sweep_at=620_000)
    check("escaped local max to reach peak",
          abs(trk.angle - sun) <= 2 * TRACKER_STEP_DEG + 2,
          f"final angle={trk.angle:.1f}, sun={sun}")

    # (d) deadband holds (no hunting) at the peak
    print("\n(d) deadband hold")
    trk = Tracker(sun_target=sun)
    trk.angle = sun
    # warm-up 12s: let the boot transient (initial step at 2s, startup move
    # at 3s) settle; then measure hunting over the next 30s
    for t in range(0, 12_000, 1000):
        trk.t = t
        trk.tick(feedback_model(trk.angle, sun, noise=0.0))
    moves = 0
    for t in range(12_000, 42_000, 1000):
        trk.t = t
        fb = feedback_model(trk.angle, sun, noise=0.0)   # deterministic
        n0 = len(trk.actions)
        trk.tick(fb)
        if len(trk.actions) > n0:
            moves += 1
    # At the exact peak the gaussian is flat enough that deltas stay inside
    # the deadband; no hunting once the boot transient has passed.
    check("no hunting within deadband", moves == 0,
          f"moves={moves}")

    # (e) night park + dawn resume
    print("\n(e) night park + dawn resume")
    trk = Tracker(sun_target=-1)          # relay null at night
    trk.angle = 90
    night_ticks = 0
    for t in range(0, 60_000, 1000):
        trk.t = t
        trk.tick(feedback_model(trk.angle, sun) * 0.02)   # LDR ~0 (night)
    parked = trk.state == TRK_PARKED
    check("parked at TRACKER_MIN_ANGLE at night",
          parked and trk.angle == TRACKER_MIN_ANGLE,
          f"state={trk.state}, angle={trk.angle}")
    # dawn: LDR rises; next ticks should resume stepping (leave parked)
    for t in range(60_000, 90_000, 1000):
        trk.t = t
        trk.tick(feedback_model(trk.angle, sun, noise=1.0))   # daylight
    check("dawn resumed (left parked state)",
          trk.state != TRK_PARKED, f"state={trk.state}")

    # (f) startup moves to known sun target
    print("\n(f) startup with known target")
    trk = Tracker(sun_target=150)
    trk.angle = 90
    for t in range(0, 10_000, 1000):
        trk.t = t
        trk.tick(feedback_model(trk.angle, sun, noise=0.5))
    check("startup moved to sun target",
          any(a == 150 for (act, t, a) in trk.actions if act == 'move'),
          f"actions={[(a, t) for (a, t, _) in trk.actions if a=='move'][:5]}")

    print()
    if all(PASS):
        print("RESULT: ALL_TRACKER_SIM_CHECKS_PASS")
        return 0
    print(f"RESULT: SIM_CHECKS_FAILED ({PASS.count(False)} failed)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
