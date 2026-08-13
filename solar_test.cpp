/*
 * Blind servo calibration test (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Single test:
 *
 *   TEST — BLIND SWEEP (positional SG90 on PA_7 / MOTOR_PIN):
 *   steps the blind servo through 0° -> 45° -> 90° -> 0° -> 45°-back ->
 *   90°-back (mapped to pulse widths 600/1050/1500us), holding each for
 *   BLIND_TEST_HOLD_MS and printing [BLIND] pos=... so you can verify the
 *   positional servo reaches each angle correctly.
 *
 * After you confirm the angles, set SOLAR_TEST_MODE back to 0.
 */
#include "solar_test.h"
#include "mbed.h"
#include <cstdio>

#include "config.h"
#include "tracker.h"

#ifndef BLIND_TEST_HOLD_MS
#define BLIND_TEST_HOLD_MS 2000   // hold each angle
#endif

#ifndef BLIND_TEST_PAUSE_MS
#define BLIND_TEST_PAUSE_MS 500   // pause between angles
#endif

static uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

static void blink_led(int times, int period_ms)
{
    DigitalOut led(PB_14);
    for (int i = 0; i < times; i++) {
        led = 1;
        thread_sleep_for(period_ms / 2);
        led = 0;
        thread_sleep_for(period_ms / 2);
    }
}

void solar_test_run(void)
{
    printf("\n=== BLIND SERVO POSITION TEST ===\n");

    // Stop the tracker motor first (tracker_init() started it).
    tracker_test_stop();

    // The blind servo is a positional SG90 on PA_7 (MOTOR_PIN).
    // Standard SG90 map: 0°=600us, 90°=1500us, 180°=2400us.
    // Interpolated: 45° = 1050us.
    PwmOut blindMotor(MOTOR_PIN);
    blindMotor.period_ms(PERIOD_WIDTH);   // 50Hz

    // Angle -> pulse width (linear map, SG90: 600us@0° .. 2400us@180°)
    auto pulse_for = [](int deg) -> int {
        return 600 + (deg * 10);   // 600 + deg*10us per degree
    };

    // Test sequence: 0, 45, 90, 0, -45, -90 (as requested)
    // For a 0-180° servo, -45/-90 just mean the same positions on return
    // (the physical mirror = same angles; the servo can't go past 0).
    const int angles[] = { 0, 45, 90, 0, -45, -90 };
    const int n = sizeof(angles) / sizeof(angles[0]);

    for (int i = 0; i < n; i++) {
        int deg = angles[i];
        // Clamp negative angles to their absolute (positional servo has no
        // negative side; -45/-90 = 45/90 in the other rotational sense,
        // which for a single-axis blind is the same physical motion).
        int use_deg = (deg < 0) ? -deg : deg;
        int pulse = pulse_for(use_deg);
        printf("[BLIND] pos=%d deg (%d) -> pulse %d us\n", deg, use_deg, pulse);
        blindMotor.pulsewidth_us(pulse);
        thread_sleep_for(BLIND_TEST_HOLD_MS);
        blink_led(i + 1, 120);   // blink count = which step we're on
        thread_sleep_for(BLIND_TEST_PAUSE_MS);
    }

    printf("[TEST] Blind sweep complete. Confirm the angles moved correctly.\n");
    blink_led(5, 250);
}
