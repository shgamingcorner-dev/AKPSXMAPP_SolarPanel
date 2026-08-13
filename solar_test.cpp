/*
 * Calibration test: positional 180 + LED + 360 servo (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Three tests:
 *
 *   TEST 1 — POSITIONAL 180 SWEEP (PA_7 / MOTOR_PIN): sweeps the blind
 *   servo pulse width SOLAR_SERVO_PULSE_MIN -> SOLAR_SERVO_PULSE_MAX in
 *   SOLAR_SERVO_PULSE_STEP steps (up then down). Watch the horn: where it
 *   FIRST starts moving = real 0°, where it STOPS = real 180°. Those two
 *   values become PULSE_WIDTH_0_DEGREE / PULSE_WIDTH_180_DEGREE.
 *
 *   TEST 2 — MAIN LIGHT (PB_1 / TIM3_CH4): ramps brightness 0->100->0%
 *   through the REAL production path (main_light_test_set in main.cpp).
 *
 *   TEST 3 — 360 DUTY SCAN (PA_1 / FAN_SERVO_PIN): scans duty from
 *   SOLAR_TEST_DUTY_MIN to SOLAR_TEST_DUTY_MAX in SOLAR_TEST_DUTY_STEP,
 *   holding each for SOLAR_TEST_DUTY_HOLD_MS. Watch the fan: duties where
 *   it stops (neutral) vs moves forward/reverse define the 360 servo's
 *   usable range for the tracker motor (TRACKER_STOP/FWD/REV_DUTY).
 */
#include "solar_test.h"
#include "mbed.h"
#include <cstdio>
#include <cstdlib>

#include "config.h"
#include "tracker.h"

#ifndef SOLAR_SERVO_PULSE_MIN
#define SOLAR_SERVO_PULSE_MIN  500    // start pulse (us)
#endif
#ifndef SOLAR_SERVO_PULSE_MAX
#define SOLAR_SERVO_PULSE_MAX  2500   // end pulse (us)
#endif
#ifndef SOLAR_SERVO_PULSE_STEP
#define SOLAR_SERVO_PULSE_STEP 100    // increment (us)
#endif
#ifndef SOLAR_SERVO_PULSE_HOLD_MS
#define SOLAR_SERVO_PULSE_HOLD_MS 1200   // hold each pulse
#endif

static uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

void solar_test_run(void)
{
    printf("\n=== CALIBRATION TEST: POS-180 + LED + 360 ===\n");

    // Stop the tracker motor first (tracker_init() started it).
    tracker_test_stop();

    // ---- TEST 1: POSITIONAL 180 SWEEP (PA_7) ----
    PwmOut blindMotor(MOTOR_PIN);
    blindMotor.period_ms(PERIOD_WIDTH);   // 50Hz

    printf("\n=== TEST 1: POSITIONAL 180 SWEEP (PA_7) ===\n");
    printf("[TEST] Sweeping pulse %dus -> %dus step %dus, hold %d ms\n",
           SOLAR_SERVO_PULSE_MIN, SOLAR_SERVO_PULSE_MAX,
           SOLAR_SERVO_PULSE_STEP, SOLAR_SERVO_PULSE_HOLD_MS);
    printf("[TEST] Watch the horn: note where it STARTS moving (0 deg) and STOPS moving (180 deg).\n");

    int step = 0;
    for (int p = SOLAR_SERVO_PULSE_MIN; p <= SOLAR_SERVO_PULSE_MAX; p += SOLAR_SERVO_PULSE_STEP) {
        step++;
        printf("[SWEEP] pulse=%d us\n", p);
        blindMotor.pulsewidth_us(p);
        thread_sleep_for(SOLAR_SERVO_PULSE_HOLD_MS);
    }
    for (int p = SOLAR_SERVO_PULSE_MAX; p >= SOLAR_SERVO_PULSE_MIN; p -= SOLAR_SERVO_PULSE_STEP) {
        step++;
        printf("[SWEEP] pulse=%d us\n", p);
        blindMotor.pulsewidth_us(p);
        thread_sleep_for(SOLAR_SERVO_PULSE_HOLD_MS);
    }
    printf("[TEST] Pos-180 sweep done (%d steps).\n", step);

    // ---- TEST 2: MAIN LIGHT (PB_1) ----
    // Uses the REAL led_mainLighting_pwm via main_light_test_set() in
    // main.cpp (period_ms(PERIOD_WIDTH) already set in main() before test).
    printf("\n=== TEST 2: MAIN LIGHT (PB_1 / TIM3_CH4) ===\n");
    for (int i = 0; i <= 10; i++) {
        main_light_test_set(i * 10);
        thread_sleep_for(500);
    }
    for (int i = 10; i >= 0; i--) {
        main_light_test_set(i * 10);
        thread_sleep_for(500);
    }
    main_light_test_set(0);
    printf("[TEST] Light test done.\n");

    // ---- TEST 3: 360 DUTY SCAN (PA_1 FAN) ----
    // The fan is a known 360° continuous servo. Scanning its duty tells us
    // the neutral (stop) duty and the range where it moves. The same range
    // applies to the tracker 360 motor (PB_0) — its TRACKER_*_DUTY values
    // should sit inside this range.
    printf("\n=== TEST 3: 360 DUTY SCAN (PA_1 FAN) ===\n");
    PwmOut fanMotor(FAN_SERVO_PIN);
    fanMotor.period_ms(PERIOD_WIDTH);   // 50Hz
    printf("[TEST] Scanning duty %0.3f -> %0.3f step %0.3f, hold %d ms\n",
           (double)SOLAR_TEST_DUTY_MIN, (double)SOLAR_TEST_DUTY_MAX,
           (double)SOLAR_TEST_DUTY_STEP, SOLAR_TEST_DUTY_HOLD_MS);
    printf("[TEST] Note duties where it STOPS (neutral), moves FWD, moves REV.\n");

    for (float d = SOLAR_TEST_DUTY_MIN; d <= SOLAR_TEST_DUTY_MAX + 0.0001f; d += SOLAR_TEST_DUTY_STEP) {
        printf("[DUTY] duty=%0.3f -- running %d ms\n", (double)d, SOLAR_TEST_DUTY_HOLD_MS);
        fanMotor.write(d);
        thread_sleep_for(SOLAR_TEST_DUTY_HOLD_MS);
        fanMotor.write(TRACKER_STOP_DUTY);   // back to neutral
        printf("[DUTY] duty=%0.3f stopped\n", (double)d);
        thread_sleep_for(SOLAR_TEST_PAUSE_MS);
    }

    printf("[TEST] 360 scan done. Report the FWD/REV/STOP duty ranges.\n");
    printf("[TEST] All tests complete. Set SOLAR_TEST_MODE=0 and reflash for normal operation.\n");
    thread_sleep_for(2000);
}
