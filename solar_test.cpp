/*
 * Positional (180°) servo characterization test (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Solely for a 180° positional
 * servo on PA_7 (MOTOR_PIN):
 *
 *   Sweeps the pulse width from SOLAR_SERVO_PULSE_MIN to
 *   SOLAR_SERVO_PULSE_MAX in SOLAR_SERVO_PULSE_STEP increments, holding
 *   each for SOLAR_SERVO_PULSE_HOLD_MS, then sweeps back down.
 *
 * Watch the servo horn:
 *   - below ~500us  -> it sits at one mechanical stop (the "0°" end)
 *   - between the stops -> it moves proportionally (positional servo)
 *   - past ~2500us  -> it hits the other mechanical stop (the "180°" end)
 *
 * The pulse widths where the horn FIRST leaves a stop and FIRST hits the
 * far stop define the servo's real 0° and 180° pulse widths. Report those
 * two numbers (e.g. "starts moving at 700us, stops moving at 2200us") and
 * we set PULSE_WIDTH_0_DEGREE / PULSE_WIDTH_180_DEGREE accordingly.
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
    printf("\n=== POSITIONAL 180 SERVO SWEEP (PA_7) ===\n");

    // Stop the tracker motor first (tracker_init() started it).
    tracker_test_stop();

    PwmOut blindMotor(MOTOR_PIN);
    blindMotor.period_ms(PERIOD_WIDTH);   // 50Hz

    printf("[TEST] Sweeping pulse %dus -> %dus step %dus, hold %d ms\n",
           SOLAR_SERVO_PULSE_MIN, SOLAR_SERVO_PULSE_MAX,
           SOLAR_SERVO_PULSE_STEP, SOLAR_SERVO_PULSE_HOLD_MS);
    printf("[TEST] Watch the horn. Note where it STARTS moving (0 deg) and STOPS moving (180 deg).\n");

    // Sweep up
    int step = 0;
    for (int p = SOLAR_SERVO_PULSE_MIN; p <= SOLAR_SERVO_PULSE_MAX; p += SOLAR_SERVO_PULSE_STEP) {
        step++;
        printf("[SWEEP] pulse=%d us\n", p);
        blindMotor.pulsewidth_us(p);
        thread_sleep_for(SOLAR_SERVO_PULSE_HOLD_MS);
    }

    // Sweep down
    for (int p = SOLAR_SERVO_PULSE_MAX; p >= SOLAR_SERVO_PULSE_MIN; p -= SOLAR_SERVO_PULSE_STEP) {
        step++;
        printf("[SWEEP] pulse=%d us\n", p);
        blindMotor.pulsewidth_us(p);
        thread_sleep_for(SOLAR_SERVO_PULSE_HOLD_MS);
    }

    printf("[TEST] Sweep done (%d steps). Report where the horn started/stopped moving.\n", step);
    printf("[TEST] All tests complete. Set SOLAR_TEST_MODE=0 and reflash for normal operation.\n");
    thread_sleep_for(2000);
}
