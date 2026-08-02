/*
 * Solar panel tracker — tilt servo (PB_0) + LDR (PA_5).
 *
 * Feedback-agnostic hill-climb: the optimizer perturbs the servo angle and
 * keeps the direction that raises the feedback signal. Feedback source is a
 * compile-time switch in config.h:
 *   - LDR % (TRACKER_FEEDBACK_LDR 1, default): ACS712 is wired but untested
 *   - panel current in amps (TRACKER_FEEDBACK_CURRENT 1): after the ACS712 is
 *     wired through the panel and calibrated (Phase 0.4)
 *
 * Non-blocking: tracker_tick() is called from the main loop and only acts
 * when TRACKER_STEP_MS has elapsed.
 */
#ifndef TRACKER_H
#define TRACKER_H

#include <cstdint>

// read_current() is defined in main.cpp (ACS712 read shared with telemetry).
// now_ms() comes from utils.h (included by tracker.cpp).
float read_current(void);

void     tracker_init(void);            // center servo at 90 deg
void     tracker_tick(void);            // hill-climb step (call from main loop)
uint8_t  tracker_get_angle(void);
void     tracker_set_sun_target(int angle);   // -1 = unknown (e.g. night)

#endif // TRACKER_H
