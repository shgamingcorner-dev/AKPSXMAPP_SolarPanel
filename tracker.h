/*
 * Solar panel tracker — 360° continuous motor on a pulley + LDR (cake branch).
 *
 * The motor is a 360° continuous servo driven by PWM duty
 * (0.075 = stop, 0.100 = full fwd, 0.050 = full rev) — same class as the
 * fan servo. PB_0 = TIM3_CH3 DEFAULT remap, same family as PA_6/PA_7/PB_1.
 * LDR on PA_4 (ADC1_IN4, free). No module DO pin — "dark" is derived from
 * the analog reading via tracker_is_dark().
 *
 * Mode 1 — SWEEP (time-driven pulley cycle, mirrors the Arduino sketch):
 *     fwd 2.5s -> stop at FLAT (180°) -> hold 30s
 *     fwd 3.0s -> stop at ~315/135    -> hold 3s
 *     rev 3.6s -> back to ~225/60     -> hold 1s
 * During the whole sweep (moving AND at the stops) the LDR is sampled every
 * TRACKER_LDR_SAMPLE_MS; the highest reading and its motor position are kept.
 *
 * Mode 2 — HOLD BEST: when the sweep finishes, if the best LDR reading was
 * above TRACKER_LDR_FLOOR (daylight), the motor drives back to that position
 * and holds for TRACKER_HOLD_BEST_MS (15 min), then re-sweeps. If the best
 * reading never rose above the floor (dark/night), it parks at home and
 * waits, re-sweeping every 15 min until daylight.
 *
 * Motor position is tracked as signed cumulative run time (FWD += run,
 * REV -= run) since the motor has no encoder; "return to best" runs the
 * motor until the position difference is covered.
 *
 * Non-blocking: tracker_tick() is called from the main loop every ~10ms and
 * only acts when timers elapse, so the network thread, keypad, and RFID are
 * never blocked.
 */
#ifndef TRACKER_H
#define TRACKER_H

#include <cstdint>

void    tracker_init(void);        // stop at home, enter sweep
void    tracker_tick(void);        // non-blocking, call from main loop
bool    tracker_is_moving(void);   // true while the motor is spinning
uint8_t tracker_get_phase(void);   // debug: current phase index
float   tracker_get_best_ldr(void);   // debug: highest LDR seen this sweep
float   tracker_get_ldr_pct(void);    // current LDR% (0-100, invert applied)
bool    tracker_is_dark(void);     // true when LDR% < TRACKER_LDR_FLOOR (derived from analog; no DO pin)

#endif // TRACKER_H
