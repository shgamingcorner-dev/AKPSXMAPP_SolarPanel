/*
 * Solar panel tracker — 360° continuous motor on a pulley (cake branch).
 *
 * Phase 1 (this file): TIME-DRIVEN pulley cycle, mirroring the Arduino
 * sketch exactly:
 *     fwd 2.5s -> stop at FLAT (180°) -> hold 30s
 *     fwd 3.0s -> stop at ~315/135    -> hold 3s
 *     rev 3.6s -> back to ~225/60     -> hold 1s
 * The motor is a 360° continuous servo driven by PWM duty
 * (0.075 = stop, 0.100 = full fwd, 0.050 = full rev) — same class as the
 * fan servo. PB_0 = TIM3_CH3 DEFAULT remap, same family as PA_6/PA_7/PB_1.
 *
 * Non-blocking: tracker_tick() is called from the main loop every ~10ms and
 * only acts when the current phase's time has elapsed, so the network
 * thread, keypad, and RFID are never blocked.
 *
 * Phase 2 (planned): LDR on PA_5 — sweep once across the range, find the
 * highest LDR reading, hold there ~15 min.
 */
#ifndef TRACKER_H
#define TRACKER_H

#include <cstdint>

void    tracker_init(void);        // center: stop, home position
void    tracker_tick(void);        // non-blocking phase machine, call from main loop
bool    tracker_is_moving(void);   // true while the motor is spinning
uint8_t tracker_get_phase(void);   // debug: current phase index

#endif // TRACKER_H
