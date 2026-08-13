/*
 * Solar calibration test mode (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Single test:
 *
 *   TEST — DUTY SCAN (TRACKER motor, PB_0): steps the duty from
 *   SOLAR_TEST_DUTY_MIN to SOLAR_TEST_DUTY_MAX in SOLAR_TEST_DUTY_STEP
 *   increments, holding SOLAR_TEST_DUTY_HOLD_MS at each and printing
 *   [SCAN] duty=0.XXX. Watch the panel and note which duties:
 *     - STOP (dead-zone / neutral — no motion)
 *     - move FORWARD
 *     - move REVERSE
 *
 * This tells us the servo's usable range: the neutral dead-zone and the
 * minimum duty that reliably moves it each direction.
 *
 * After you report the results, set SOLAR_TEST_MODE back to 0 and update
 * TRACKER_FWD_DUTY / TRACKER_REV_DUTY / TRACKER_STOP_DUTY accordingly.
 */
#include "solar_test.h"
#include "mbed.h"
#include <cstdio>

#include "config.h"
#include "tracker.h"

static uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

static void blink_led(int times, int period_ms)
{
    DigitalOut led(PB_14);   // PB_14 LED (led_tx pin; unused during test since network thread doesn't start)
    for (int i = 0; i < times; i++) {
        led = 1;
        thread_sleep_for(period_ms / 2);
        led = 0;
        thread_sleep_for(period_ms / 2);
    }
}

void solar_test_run(void)
{
    printf("\n=== SOLAR DUTY CHARACTERIZATION TEST ===\n");

    // CRITICAL: tracker_init() -> begin_sweep() already started the tracker
    // motor forward before we got here. Stop it NOW.
    tracker_test_stop();

    printf("[TEST] Scanning duty %0.3f -> %0.3f step %0.3f, hold %d ms\n",
           (double)SOLAR_TEST_DUTY_MIN, (double)SOLAR_TEST_DUTY_MAX,
           (double)SOLAR_TEST_DUTY_STEP, SOLAR_TEST_DUTY_HOLD_MS);
    printf("[TEST] Watch the panel. Note: STOP / FORWARD / REVERSE at each duty.\n");

    int step = 0;
    for (float d = SOLAR_TEST_DUTY_MIN; d <= SOLAR_TEST_DUTY_MAX + 0.0001f; d += SOLAR_TEST_DUTY_STEP) {
        step++;
        printf("[SCAN] duty=%0.3f -- running %d ms\n", (double)d, SOLAR_TEST_DUTY_HOLD_MS);
        tracker_test_drive(d);
        thread_sleep_for(SOLAR_TEST_DUTY_HOLD_MS);
        tracker_test_stop();
        printf("[SCAN] duty=%0.3f stopped\n", (double)d);
        blink_led(step, 100);   // blink 'step' times (fast) so you can count which duty
        thread_sleep_for(SOLAR_TEST_PAUSE_MS);
    }

    printf("[TEST] Scan complete (%d steps). Report which duties STOP / FORWARD / REVERSE.\n", step);
    blink_led(5, 250);
}
