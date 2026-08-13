/*
 * Blind servo test (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Two tests on PA_7 (MOTOR_PIN):
 *
 *   TEST 1 — BLIND ON/OFF: mirrors apply_blind() in main.cpp
 *     OPEN   -> pulsewidth_us(2400)  (PULSE_WIDTH_180_DEGREE)
 *     CLOSED -> pulsewidth_us(600)   (PULSE_WIDTH_0_DEGREE)
 *     Toggles a few times so you can verify the blind servo moves.
 *
 *   TEST 2 — 360 DUTY SCAN: scans 0.025->0.125 step 0.005 on the same pin.
 *     If the servo SPINS continuously at duties away from neutral, it is a
 *     360° continuous servo (not a positional SG90) — which explains why
 *     the on/off (hold-angle) code does not work.
 */
#include "solar_test.h"
#include "mbed.h"
#include <cstdio>

#include "config.h"
#include "tracker.h"

#ifndef BLIND_TEST_HOLD_MS
#define BLIND_TEST_HOLD_MS 2000   // hold each state
#endif

#ifndef BLIND_TEST_CYCLES
#define BLIND_TEST_CYCLES 4       // number of OPEN->CLOSED cycles
#endif

static uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

void solar_test_run(void)
{
    printf("\n=== BLIND SERVO ON/OFF TEST (PA_7) ===\n");

    // Stop the tracker motor first (tracker_init() started it).
    tracker_test_stop();

    // Same as apply_blind() in main.cpp: PwmOut motor(MOTOR_PIN) on PA_7.
    PwmOut blindMotor(MOTOR_PIN);
    blindMotor.period_ms(PERIOD_WIDTH);   // 50Hz

    printf("[TEST] Toggling blind OPEN (2400us) / CLOSED (600us) %d times.\n", BLIND_TEST_CYCLES);

    for (int i = 0; i < BLIND_TEST_CYCLES; i++) {
        // OPEN
        printf("[BLIND] -> OPEN (2400us)\n");
        blindMotor.pulsewidth_us(PULSE_WIDTH_180_DEGREE);
        thread_sleep_for(BLIND_TEST_HOLD_MS);

        // CLOSED
        printf("[BLIND] -> CLOSED (600us)\n");
        blindMotor.pulsewidth_us(PULSE_WIDTH_0_DEGREE);
        thread_sleep_for(BLIND_TEST_HOLD_MS);
    }

    printf("[TEST] Blind toggle complete. Confirm it moved OPEN <-> CLOSED.\n");
    thread_sleep_for(2000);

    // ---- TEST 2: 360° DUTY SCAN (same pin) ----
    // If the servo is a 360° continuous one, it will SPIN at duties away
    // from neutral (0.075) instead of holding an angle. This tells us
    // whether the blind is positional (SG90) or continuous.
    printf("\n=== 360 DUTY SCAN (PA_7) ===\n");
    printf("[TEST] Scanning duty %0.3f -> %0.3f step %0.3f, hold %d ms\n",
           (double)SOLAR_TEST_DUTY_MIN, (double)SOLAR_TEST_DUTY_MAX,
           (double)SOLAR_TEST_DUTY_STEP, SOLAR_TEST_DUTY_HOLD_MS);
    printf("[TEST] If it SPINS continuously -> 360 servo. If it holds angles -> positional.\n");

    int step = 0;
    for (float d = SOLAR_TEST_DUTY_MIN; d <= SOLAR_TEST_DUTY_MAX + 0.0001f; d += SOLAR_TEST_DUTY_STEP) {
        step++;
        printf("[SCAN] duty=%0.3f -- running %d ms\n", (double)d, SOLAR_TEST_DUTY_HOLD_MS);
        blindMotor.pulsewidth_us((uint16_t)(d * 20000.0f));   // duty -> us (20ms period)
        thread_sleep_for(SOLAR_TEST_DUTY_HOLD_MS);
        blindMotor.pulsewidth_us(PULSE_WIDTH_0_DEGREE);       // stop/neutral-ish
        printf("[SCAN] duty=%0.3f stopped\n", (double)d);
        thread_sleep_for(SOLAR_TEST_PAUSE_MS);
    }

    printf("[TEST] 360 scan complete (%d steps). Positional or continuous?\n", step);
    thread_sleep_for(2000);
}
