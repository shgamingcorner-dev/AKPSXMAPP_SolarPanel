/*
 * Blind angle-step + light + 360-scan test (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1:
 *   TEST 1 — BLIND ANGLE STEPS: drives the blind servo (PA_7 / MOTOR_PIN,
 *   SG90 positional) through 0° -> 45° -> 90° -> 135° -> 180° -> 135° ->
 *   90° -> 45° -> 0° in BLIND_TEST_STEP_MS increments, printing each angle
 *   and its pulse width so you can verify the servo moves angle-by-angle.
 *
 *   TEST 2 — MAIN LIGHT: ramps brightness 0->100->0% on PB_1 (TIM3_CH4)
 *   to verify the light works.
 *
 *   TEST 3 — 360 DUTY SCAN (PA_7): scans the blind pin from
 *   SOLAR_TEST_DUTY_MIN to SOLAR_TEST_DUTY_MAX in SOLAR_TEST_DUTY_STEP.
 *   If the servo SPINS continuously at duties away from neutral (0.075),
 *   it is a 360° continuous servo, not a positional SG90.
 *
 * SG90 map: 0°=600us, 90°=1500us, 180°=2400us (linear).
 */
#include "solar_test.h"
#include "mbed.h"
#include <cstdio>
#include <cstdlib>

#include "config.h"
#include "tracker.h"

#ifndef BLIND_TEST_STEP_MS
#define BLIND_TEST_STEP_MS 1500   // hold each angle step
#endif

static uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

// SG90 linear map: 600us @ 0°, +10us per degree (2400us @ 180°)
static int pulse_for_angle(int deg)
{
    return 600 + deg * 10;
}

void solar_test_run(void)
{
    printf("\n=== BLIND ANGLE-STEP + LIGHT + 360-SCAN TEST ===\n");

    // Stop the tracker motor first (tracker_init() started it).
    tracker_test_stop();

    // ---- TEST 1: BLIND ANGLE STEPS (PA_7) ----
    PwmOut blindMotor(MOTOR_PIN);
    blindMotor.period_ms(PERIOD_WIDTH);   // 50Hz

    const int angles[] = { 0, 45, 90, 135, 180, 135, 90, 45, 0 };
    const int n = sizeof(angles) / sizeof(angles[0]);

    printf("[TEST] Blind angle steps on PA_7 (SG90):");
    for (int i = 0; i < n; i++) printf(" %d", angles[i]);
    printf(" deg\n");

    for (int i = 0; i < n; i++) {
        int pulse = pulse_for_angle(angles[i]);
        printf("[BLIND] angle=%3d deg -> pulse %d us\n", angles[i], pulse);
        blindMotor.pulsewidth_us(pulse);
        thread_sleep_for(BLIND_TEST_STEP_MS);
    }
    printf("[TEST] Blind step test done.\n");

    // ---- TEST 2: MAIN LIGHT (PB_1 / TIM3_CH4) ----
    // Verify the light works (PB_1 is in the F103 pinmap — PB_7/TIM4 is
    // NOT, causing error 0x80010130 at boot). Uses the REAL production
    // led_mainLighting_pwm via main_light_test_set() in main.cpp — the
    // 20ms period was initialized in main() before solar_test_run().
    printf("\n=== MAIN LIGHT TEST (PB_1 / TIM3_CH4) ===\n");
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

    // ---- TEST 3: 360° DUTY SCAN (PA_7) ----
    // If the blind is actually a 360° continuous servo, it will SPIN at
    // duties away from neutral (0.075) instead of holding an angle. This
    // tells us whether the PA_7 device is positional (SG90) or continuous.
    printf("\n=== 360 DUTY SCAN (PA_7) ===\n");
    printf("[TEST] Scanning duty %0.3f -> %0.3f step %0.3f, hold %d ms\n",
           (double)SOLAR_TEST_DUTY_MIN, (double)SOLAR_TEST_DUTY_MAX,
           (double)SOLAR_TEST_DUTY_STEP, SOLAR_TEST_DUTY_HOLD_MS);
    printf("[TEST] If it SPINS continuously -> 360 servo. If it holds -> positional.\n");

    int step = 0;
    for (float d = SOLAR_TEST_DUTY_MIN; d <= SOLAR_TEST_DUTY_MAX + 0.0001f; d += SOLAR_TEST_DUTY_STEP) {
        step++;
        printf("[SCAN] duty=%0.3f -- running %d ms\n", (double)d, SOLAR_TEST_DUTY_HOLD_MS);
        blindMotor.pulsewidth_us((uint16_t)(d * 20000.0f));   // duty -> us (20ms period)
        thread_sleep_for(SOLAR_TEST_DUTY_HOLD_MS);
        blindMotor.pulsewidth_us(PULSE_WIDTH_0_DEGREE);       // back to 0° stop
        printf("[SCAN] duty=%0.3f stopped\n", (double)d);
        thread_sleep_for(SOLAR_TEST_PAUSE_MS);
    }

    printf("[TEST] 360 scan complete (%d steps). Positional or continuous?\n", step);
    printf("\n[TEST] All tests complete. Set SOLAR_TEST_MODE=0 and reflash for normal operation.\n");
    thread_sleep_for(2000);
}
