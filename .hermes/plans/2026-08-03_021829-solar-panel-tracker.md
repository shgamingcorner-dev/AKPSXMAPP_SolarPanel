# Solar Panel Tracker Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Add an autonomous solar-panel tracker to the AKPS firmware: a 180° SG90 servo tilts the panel left/right toward the sun, driven by a hybrid controller — **LDR hill-climb as the primary optimizer until the ACS712 is wired + calibrated**, current-sensor hill-climb once it is, sun-position (computed relay-side) as the coarse anchor/recovery.

**Architecture:** Hybrid of all four proposed ideas, layered to survive real conditions:
- **Idea 1 (hill-climb)** = primary optimizer — **feedback-agnostic**: perturb the angle, keep the direction that raises the feedback signal. Feedback source is switched by `TRACKER_FEEDBACK_*`: **LDR % (default today — ACS712 is wired but untested and measures nothing)**; **panel current (A) once the ACS712 is wired through the panel + calibrated** (`read_current()` on PA_0).
- **Idea 2 + 3 (sun position + time)** = merged server-side. The board has **no GPS and no RTC**, so the **PythonAnywhere relay computes the sun azimuth from its own clock + a fixed lat/lon** and exposes a `tracker_target` (0–180) in `GET /device-state`. The firmware just polls it — same pattern as every other device-state field. Time-based tracking (idea 3) is the same math with a lookup table; it is a subset of idea 2.
- **Idea 4 (all together)** = the actual design: sun target = coarse anchor, hill-climb = fine optimizer, cloud/re-sweep logic = recovery, LDR = primary feedback (and instant coarse sensor) until current is verified.

**Tech Stack:** Mbed OS 6 / NUCLEO_F103RB, SG90 servo (`PwmOut`), ACS712 (already on PA_0 via `read_current()`), LDR (`AnalogIn`), PythonAnywhere Flask relay (sun-position math), Supabase/ThingSpeak for state/telemetry.

---

## Current Context / Assumptions (verify before coding)

- **Hardware on hand:** ACS712 current sensor (wired to PA_0 but **never tested / not measuring anything meaningful** — no panel current flows through it yet), LDR (not wired yet), solar panel, one 180° servo (not wired yet).
- **PB_0 is physically free** (user-confirmed): the DHT11 VCC is not switched by PB_0 in the real wiring. The tracker servo can use PB_0 directly; the `DHT11VCC` `DigitalOut` define is repointed to a free GPIO (PB_12) purely for pin hygiene so the PWM pin is never also written as GPIO.
- **Pin reality (NUCLEO_F103RB pinmap — already verified this session):**
  - ALL of TIM1/TIM2/TIM3 are allocated. The **only clean PWM slot left is PB_0 = TIM3_CH3 (default remap)** — confirmed free on the physical board.
  - ⚠️ **Do NOT use PC_8/PC_9 for the tracker servo** (TIM3 full remap) — that reroutes ALL TIM3 channels and silently kills door (PA_6), blind (PA_7), and light (PB_1). This exact trap cost a session already.
  - **PB_12 is free** (no references anywhere) → `DHT11VCC` define moves there (hygiene only).
  - **PA_5 is free** for the LDR (ADC1_IN5; its default SPI1_SCK function is dead because MFRC522 remaps SPI1 to PB_3/4/5). PA_4 (ADC1_IN4) is an alternative.
- **Existing helpers to reuse:** `read_current()` (20-sample average, main.cpp:615 — works, but its input currently measures nothing), `PwmOut` pattern `period_ms(20)` + `pulsewidth_us(600..2400)`, `send_device_state_int_via_relay()` for integer pushes, the door/blind two-flag pipeline as the state-handling reference, the MSVC+mbed-stub compile script and Python behavior-sim approach for host-side verification.

---

## Approach Decision

| Idea | Verdict | Why |
|------|---------|-----|
| 1. Current hill-climb | **Primary** | Uses the real output signal; self-calibrating; works in clouds. |
| 2. Weather/sun position | **Coarse anchor** | Board has no GPS/RTC → compute azimuth relay-side, poll it. |
| 3. Hardcoded time | Subset of 2 | Same math; ignore as standalone. |
| 4. All combined | **Chosen** | Sun target avoids local maxima & gives recovery; hill-climb fine-tunes; LDR adds instant coarse feedback if current is noisy. |

Fallback ladder at runtime:
```
normal             -> hill-climb on LDR (brighter = better) — current once ACS712 verified
feedback collapses -> sweep toward tracker_target (don't chase noise)
night (LDR ~0 + no target) -> park at TRACKER_MIN_ANGLE until dawn
periodic           -> full re-sweep (every RE_SWEEP_MS) to escape local maxima
no tracker_target (relay down) -> hold last angle; hill-climb still runs
```

---

## Phase 0 — Pin Setup + ACS712 Wiring/Calibration (prereq)

**Objective:** Put the tracker servo on PB_0 (physically free), LDR on PA_5, repoint the `DHT11VCC` define to PB_12 for hygiene; separately, wire + calibrate the ACS712 so current feedback becomes usable later. **The ACS712 work is NOT a gate** — the tracker starts on LDR feedback.

**Files:**
- Modify: `config.h` (add defines), `main.cpp:44` (DHT11VCC define repoint)

**Task 0.1 — Add config defines**
```cpp
// config.h — Solar Tracker
#define DHT11VCC_PIN      PB_12     // repointed off PB_0 (hygiene; PB_0 is the tracker servo now)
#define TRACKER_SERVO_PIN PB_0      // SG90 tilt servo. PB_0 = TIM3_CH3 DEFAULT remap —
                                    // same remap family as PA_6/PA_7/PB_1 (no AFIO fight).
                                    // NEVER use PC_8/PC_9 (TIM3 full remap kills door/blind/light).
#define LDR_PIN           PA_5      // LDR voltage divider -> ADC1_IN5 (SPI1 remapped away, free)
#define TRACKER_MIN_ANGLE 10
#define TRACKER_MAX_ANGLE 170
#define TRACKER_STEP_DEG  5
#define TRACKER_STEP_MS   2000      // one perturb step every 2s (servo settle + feedback avg)

// Feedback source. LDR is default: the ACS712 is wired but untested/measuring nothing.
// Switch to CURRENT only after Phase 0.4 (wiring + calibration) passes.
#define TRACKER_FEEDBACK_LDR    1
#define TRACKER_FEEDBACK_CURRENT 0
#if TRACKER_FEEDBACK_CURRENT
#define TRACKER_DEADBAND        0.02f   // amps: ignore deltas < 20mA
#define TRACKER_CLOUD           0.15f   // amps: below this => cloud/sweep mode
#else
#define TRACKER_DEADBAND        2.0f    // LDR %: ignore deltas < 2%
#define TRACKER_CLOUD           10.0f   // LDR %: below this => dark/cloud -> sweep/park
#endif
#define TRACKER_RE_SWEEP_MS 600000      // full re-sweep every 10 min (escape local maxima)
```

**Task 0.2 — Repoint DHT11VCC define (hygiene only)**
```cpp
// main.cpp:44
static DigitalOut DHT11VCC(DHT11VCC_PIN);   // was PB_0; PB_0 is now the tracker servo
```
No wire move needed — PB_0 is physically free; the define change just keeps the PWM pin from being written as GPIO.

**Task 0.3 — Verify pins (quick)**
1. MSVC compile all sources. Flash. Serial still shows `Temperature`/`humidity` → DHT11 unaffected.
2. Temporary `tracker_tick()` debug: servo centers to 90° (1500µs) on PB_0. Regression: door/blind/light/fan still work (TIM3 default remap preserved).

**Task 0.4 — Wire + calibrate the ACS712 (for CURRENT feedback — not a gate)**
1. Pass the **panel's output line through the ACS712 in-line** (panel → IP+/IP- → load/battery). Vout → PA_0 via the existing divider.
2. With **zero current flowing** (panel disconnected), read the serial `pin=X.XV` line and set `ACS712_ZERO_V` to that measured value.
3. Verify: cover the panel → serial `Current:` drops meaningfully; uncover → rises. Only then flip `TRACKER_FEEDBACK_CURRENT 1`.
4. If the panel can't be wired through the sensor yet, leave LDR mode — the tracker still works.

**Commit:** `git commit -m "feat(tracker): PB_0 servo + PA_5 LDR pins, DHT11VCC hygiene, ACS712 cal task"`

---

## Phase 1 — Tracker Servo + LDR drivers

**Objective:** Create `tracker.cpp`/`tracker.h` with servo control, LDR read, and the shared current read; wire into the build.

**Files:**
- Create: `tracker.h`, `tracker.cpp`
- Modify: `CMakeLists.txt` (add `tracker.cpp` to `target_sources`), `config.h` (done above)

**Task 1.1 — tracker.h**
```cpp
#ifndef TRACKER_H
#define TRACKER_H
#include <cstdint>
void  tracker_init(void);
void  tracker_tick(void);          // call from main loop, non-blocking
uint8_t tracker_get_angle(void);
void  tracker_set_sun_target(int angle);   // -1 = unknown
#endif
```

**Task 1.2 — tracker.cpp skeleton**
```cpp
#include "mbed.h"
#include "config.h"
#include "tracker.h"

static PwmOut  trackerServo(TRACKER_SERVO_PIN);
static AnalogIn ldr(LDR_PIN);

static volatile uint8_t  g_angle = 90;          // 0..180
static volatile int      g_sun_target = -1;     // relay-computed target; -1 unknown

void tracker_init(void) {
    trackerServo.period_ms(20);
    trackerServo.pulsewidth_us(PULSE_WIDTH_90_DEGREE);  // center 1500us
}

uint8_t tracker_get_angle(void) { return g_angle; }
void tracker_set_sun_target(int a) { g_sun_target = a; }

static void servo_to_angle(uint8_t angle) {
    if (angle < TRACKER_MIN_ANGLE) angle = TRACKER_MIN_ANGLE;
    if (angle > TRACKER_MAX_ANGLE) angle = TRACKER_MAX_ANGLE;
    g_angle = angle;
    float pulse = PULSE_WIDTH_0_DEGREE +
                  (PULSE_WIDTH_180_DEGREE - PULSE_WIDTH_0_DEGREE) * (angle / 180.0f);
    trackerServo.pulsewidth_us(pulse);
}

static float read_ldr_pct(void) {
    float sum = 0.0f;
    for (int i = 0; i < 10; i++) { sum += ldr.read(); wait_us(100); }
    return (sum / 10.0f) * 100.0f;   // 0..100%
}
```

**Task 1.3 — CMakeLists.txt**
Add `tracker.cpp` after `lcd_utilities.cpp` in `target_sources`.

**Task 1.4 — Verify**
- MSVC compile all sources (extend the mbed stub with nothing new — PwmOut/AnalogIn already stubbed).
- Serial debug print in `tracker_tick` (temporary): `[TRK] angle=90` every 2s; confirm the servo physically centers to 90° (1500µs). Then remove the debug.

**Commit:** `git commit -m "feat(tracker): servo + LDR drivers (tracker.cpp/.h), PB_0 servo, PA_5 LDR"`

---

## Phase 2 — Hill-Climb State Machine (core algorithm)

**Objective:** Non-blocking perturb-and-observe controller running from the main loop.

**Files:**
- Modify: `tracker.cpp` (add state machine), `main.cpp` (call `tracker_tick()` in the main loop)

**Task 2.1 — Write the Python simulation first (test before C)**

Create `hermes-verify-tracker-sim.py` (host-side, mirrors the C 1:1). The model is **feedback-agnostic**: `feedback(angle) = BASE + PEAK * gauss(angle - SUN_ANGLE)` plus per-read noise — the same shape for LDR% or current A.
- Verify: (a) converges to within ±2×step of the peak from a wrong starting angle; (b) a simulated cloud dip (feedback *= 0.05 for 3 steps) triggers sweep toward the sun target and recovers; (c) a local-maximum bump (second smaller gaussian) is escaped by the periodic re-sweep; (d) deadband stops oscillation at the peak; (e) LDR thresholds (`TRACKER_DEADBAND 2.0`, `TRACKER_CLOUD 10.0`) behave like current thresholds.
- This is the TDD oracle: the C implementation must pass the same scenarios.

**Task 2.2 — Feedback-agnostic state machine in tracker.cpp**
```cpp
enum TrkState { TRK_TRACKING, TRK_SWEEPING, TRK_HOLD };

static TrkState  g_state = TRK_TRACKING;
static int8_t    g_dir = 1;             // +1/-1
static float     g_last_fb = -1.0f;     // last feedback value (LDR% or A)
static uint64_t  g_last_step = 0;
static uint64_t  g_last_resweep = 0;

// One feedback source, selected at compile time:
static float read_feedback(void) {
#if TRACKER_FEEDBACK_CURRENT
    return read_current();              // amps — only after Phase 0.4 wiring + calibration
#else
    return read_ldr_pct();              // 0..100% — default
#endif
}

void tracker_tick(void) {
    uint64_t now = now_ms();

    // periodic re-sweep: go back to the sun target to escape local maxima
    if (now - g_last_resweep >= TRACKER_RE_SWEEP_MS) {
        g_last_resweep = now;
        if (g_sun_target >= 0) { servo_to_angle((uint8_t)g_sun_target); g_dir = 1; g_last_fb = -1.0f; }
    }

    if (now - g_last_step < TRACKER_STEP_MS) return;
    g_last_step = now;

    float fb = read_feedback();

    // dark / cloud / signal collapse -> sweep toward sun target, don't chase noise
    if (fb < TRACKER_CLOUD) {
        if (g_sun_target >= 0) servo_to_angle((uint8_t)g_sun_target);
        g_last_fb = -1.0f;
        g_state = TRK_SWEEPING;
        printf("[TRK] low feedback %.1f -> sweep to %d\n", fb, g_sun_target);
        return;
    }

    if (g_last_fb < 0.0f) {              // first reading: pick a direction and step
        g_last_fb = fb;
        servo_to_angle(g_angle + g_dir * TRACKER_STEP_DEG);
        g_state = TRK_TRACKING;
        return;
    }

    float delta = fb - g_last_fb;
    g_last_fb = fb;

    if (delta >  TRACKER_DEADBAND) { /* improved: keep direction */ }
    else if (delta < -TRACKER_DEADBAND) { g_dir = -g_dir; }  // worse: reverse
    else { g_state = TRK_HOLD; return; }                     // deadband: stop

    servo_to_angle(g_angle + g_dir * TRACKER_STEP_DEG);
    printf("[TRK] angle=%d fb=%.1f d=%.2f dir=%+d\n", g_angle, fb, delta, g_dir);
}
```

**Task 2.3 — Call from main loop**
```cpp
// main.cpp, inside while(1), near the other consumers
tracker_tick();
```

**Task 2.4 — Verify (host-side sim + hardware)**
- Run the Python sim: all scenarios pass (LDR + current threshold variants).
- Hardware: LDR + servo wired; serial shows `[TRK] angle=.. fb=..` converging when a lamp/sun is moved; shade the LDR → recovers toward the sun target.
- When Phase 0.4 completes, flip `TRACKER_FEEDBACK_CURRENT 1` and re-run the same checks — the algorithm is unchanged.

**Commit:** `git commit -m "feat(tracker): hill-climb state machine + host-side sim"`

---

## Phase 3 — Sun-Position Target (relay-side math, idea 2+3)

**Objective:** The relay computes the sun azimuth from its clock + fixed lat/lon and exposes `tracker_target` (0–180) so the firmware can coarse-anchor and recover.

**Files:**
- Modify: `PASTE/relay.py` (GET /device-state adds `tracker_target`; optional POST handler `POST /api/device/tracker` for manual override), deploy + **Reload** on PythonAnywhere.
- Modify: `main.cpp` poll (parse `tracker_target`, call `tracker_set_sun_target()`)

**Task 3.1 — relay.py: NOAA-style solar position**
```python
import math, datetime

LAT, LON = 1.3521, 103.8198   # Singapore — REPLACE with the panel's real location

def solar_target_angle(now=None):
    now = now or datetime.datetime.utcnow()
    # day-of-year, fractional UTC hours
    n = now.timetuple().tm_yday
    t = now.hour + now.minute / 60.0 + now.second / 3600.0
    # solar declination (approx, radians)
    decl = -23.44 * math.cos(2 * math.pi * (n + 10) / 365.25)
    # hour angle
    ha = math.radians(15.0 * (t - 12.0) + LON)
    # elevation -> azimuth (approx). Skip night (elev < 0) -> None
    lat_r = math.radians(LAT)
    sin_el = (math.sin(lat_r) * math.sin(math.radians(decl)) +
              math.cos(lat_r) * math.cos(math.radians(decl)) * math.cos(ha))
    el = math.asin(max(-1, min(1, sin_el)))
    if math.degrees(el) < 3:            # below horizon / twilight -> no target
        return None
    cos_az = ((math.sin(math.radians(decl)) - math.sin(el) * math.sin(lat_r)) /
              (math.cos(el) * math.cos(lat_r)))
    az = math.degrees(math.acos(max(-1, min(1, cos_az))))
    if ha > 0: az = 360 - az            # afternoon
    # map azimuth 90..270 (east..south..west) onto servo 0..180 (left..right)
    return max(0, min(180, int((az - 90.0) * 180.0 / 180.0)))
```
- Cache per minute (free tier); add `'tracker_target': solar_target_angle()` to the GET /device-state JSON (or `None` at night).

**Task 3.2 — firmware poll parse**
```cpp
// in poll_device_state_via_relay(), next to lighting_brightness parsing
int trk = -1;
char *tp = strstr(g_rx, "\"tracker_target\":");
if (tp) { char *ns = tp + strlen("\"tracker_target\":"); trk = atoi(ns); }
if (tp && trk >= 0 && trk <= 180) tracker_set_sun_target(trk);
else if (tp) tracker_set_sun_target(-1);       // null at night
```

**Task 3.3 — Verify**
- Host: `python -c` on the formula for 3 known times (e.g., local noon → az ≈ 180 → target ≈ 90 center; morning → east → low angle; night → None).
- Live: `curl ".../device-state?secret=…"` shows `tracker_target` (or `null` at night).
- Serial: `[TRK] ... sweep to <target>` uses the relay value.

**Commit:** `git commit -m "feat(tracker): relay sun-position target + firmware poll"`

---

## Phase 4 — Hybrid control (LDR + sun + current, idea 4)

**Objective:** Tie it together: sun target = coarse anchor, hill-climb = fine, LDR = primary feedback (until current is verified), re-sweep = recovery.

**Files:** Modify `tracker.cpp`, `config.h`

**Task 4.1 — Startup behavior**
- `tracker_init()`: center 90°. First `tracker_tick` after 3s: if `g_sun_target >= 0` → move there, then let hill-climb fine-tune; else hill-climb from 90°.

**Task 4.2 — Night handling (LDR-based)**
- At night `tracker_target` is `null` → `tracker_set_sun_target(-1)`, and the LDR reads near 0 (< `TRACKER_CLOUD`). Add a night check: if `tracker_target < 0 && fb < TRACKER_CLOUD` → park at `TRACKER_MIN_ANGLE` and skip stepping until the LDR rises above the threshold at dawn (the sweep-to-target branch would otherwise pointlessly step in the dark).

**Task 4.3 — Current-feedback upgrade path (when Phase 0.4 passes)**
- Flip `TRACKER_FEEDBACK_CURRENT 1`. Nothing else changes: `read_feedback()` returns amps, thresholds switch via the `#if` block, the LDR stays available as the coarse/dark detector for night parking (`read_ldr_pct() < TRACKER_CLOUD` in the night check).
- Re-run the Phase 2 hardware checks with the panel wired through the ACS712.

**Task 4.4 — Verify**
- Python sim extended: night park (LDR < threshold), dawn resume, LDR vs current threshold variants.
- Hardware: full day-partial test — manual panel shading while tracking; watch angle recover.

**Commit:** `git commit -m "feat(tracker): hybrid LDR/sun/current control + night park"`

---

## Phase 5 — Telemetry & dashboard

**Objective:** Show tracker state in Supabase/ThingSpeak.

**Files:** Modify `main.cpp`, `PASTE/relay.py` (optional), frontend (optional)

**Task 5.1 — Push on change**
```cpp
// network_task() or a tracker-change flag; push only when angle changes by >= 5
static int last_pushed_angle = -1;
if (abs((int)tracker_get_angle() - last_pushed_angle) >= 5) {
    last_pushed_angle = tracker_get_angle();
    send_device_state_int_via_relay("tracker_angle", tracker_get_angle());
}
```
- Relay: add `tracker_angle` to the field_map/POST handling (int, like `fan_speed`); add to GET JSON.
- Frontend (FRONTENDAKPS): display `tracker_angle` + `tracker_target` (optional).

**Task 5.2 — ThingSpeak (optional)**
- Add `tracker_angle` to `ts_fields` (field5) if the channel allows; harmless otherwise.

**Task 5.3 — Verify**
- Dashboard shows live angle; curl GET shows `tracker_angle`; serial shows push only on change.

**Commit:** `git commit -m "feat(tracker): telemetry + dashboard fields"`

---

## Files to Change (summary)

| File | Change |
|------|--------|
| `config.h` | `DHT11VCC_PIN PB_12`, `TRACKER_SERVO_PIN PB_0`, `LDR_PIN PA_5`, tracker constants |
| `main.cpp` | DHT11VCC pin (line 44); `tracker_tick()` in main loop; poll parses `tracker_target`; tracker_angle push |
| `tracker.h` / `tracker.cpp` | NEW: servo, LDR, hill-climb state machine, hybrid logic |
| `CMakeLists.txt` | add `tracker.cpp` to `target_sources` |
| `PASTE/relay.py` | solar-target math, `tracker_target` in GET, `tracker_angle` field support (deploy + Reload) |
| `README.md` | document tracker + keypad/pins |
| `.hermes/plans/…` (this file) | plan of record |

## Tests / Validation

1. **Host-side (no ARM toolchain):** MSVC compile all sources incl. `tracker.cpp` vs the mbed stub; conflict-marker grep; brace/paren check; **Python tracker sim** (convergence, cloud dip, local max re-sweep, night park) as the behavioral oracle.
2. **Relay:** `python -m py_compile relay.py`; curl `/device-state` shows `tracker_target` (null at night); solar-formula spot checks at 3 times of day.
3. **Hardware (in order):** DHT11 still reads after VCC move → servo centers at 90° → hill-climb converges with panel in sunlight → cover panel = recovery → regression: door/blind/light/fan still work (TIM3 default remap preserved) → dashboard shows tracker_angle.

## Risks / Tradeoffs / Open Questions

- **ACS712 is untested and measures nothing today** — the tracker runs on the **LDR** until Phase 0.4 (wire panel output through the sensor + calibrate `ACS712_ZERO_V`) passes. Do not flip `TRACKER_FEEDBACK_CURRENT` early; an uncalibrated zero-point makes the hill-climb see noise, not signal.
- **ACS712 noise** (README notes ~0.5–5 A at rest): even after calibration, keep the deadband/averaging and lean on the LDR/sun target for coarse moves. Use current only for deltas > several hundred mA.
- **Local maxima:** periodic re-sweep (10 min) mitigates, not eliminates.
- **Cloud dips:** treated as "don't chase"; sweep to sun target. Do NOT let a 2-step cloud dip count as "worse" and reverse — the `TRACKER_CLOUD` threshold guards this.
- **LDR is the feedback now:** it must see the same sky as the panel (mount it on the panel, same plane). It measures *light*, not *power* — fine for tracking, but the current upgrade path (Phase 4.3) is still the real objective once wired.
- **Servo:** external 5V supply (never Nucleo rail); clamp 10–170° to avoid stall at mechanical ends; SG90 ~700 mA stall current — the shared power rail is already suspect (see README WiFi instability) — a dedicated supply is strongly advised.
- **TIM3 remap trap:** PB_0 (default) is safe; PC_8/PC_9 (full remap) would break door/blind/light. Documented in config.h comments.
- **Sun formula accuracy:** ±5° is fine for coarse anchoring; the formula is a NOAA approximation — do NOT use it as the sole tracker without the hill-climb fine-tune.
- **LDR placement:** must see the same sky as the panel; shade/mounting angle skews it — treat as coarse only.
- **Open question:** does the ACS712 measure panel output or house load? (Currently: **neither — it's wired but measuring nothing**; Phase 0.4 wires the panel through it. Until then the LDR is the feedback.)
- **Open question:** real deployment location lat/lon for `solar_target_angle()` (hardcoded placeholder is Singapore).
- **Scope:** 1-axis (left/right tilt) only. A 2-axis (adds elevation) tracker is future work — the pin budget for a second servo is currently exhausted.

---

## Execution Order

Phase 0 (pins + wiring verify) → Phase 1 (drivers + build) → Phase 2 (hill-climb + sim) → Phase 3 (sun target) → Phase 4 (hybrid) → Phase 5 (telemetry). Each phase ends with a commit and its own verification; Phase 0's hardware checkpoint gates everything after it.
