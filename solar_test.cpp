/*
 * Blind servo on/off test (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Mirrors the real apply_blind()
 * in main.cpp (PA_7 / MOTOR_PIN):
 *   OPEN   -> pulsewidth_us(2400)  (PULSE_WIDTH_180_DEGREE)
 *   CLOSED -> pulsewidth_us(600)   (PULSE_WIDTH_0_DEGREE)
 *
 * Toggles OPEN/CLOSED a few times so you can verify the blind servo moves.
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
}
