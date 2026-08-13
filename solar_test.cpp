/*
 * Solar calibration test mode (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Two tests in one flash:
 *
 *   TEST 1 — SPEED: run the tracker motor forward at each duty in
 *   SOLAR_TEST_DUTIES, SOLAR_TEST_EACH_MS each, pausing in between.
 *   Watch the panel: pick the fastest duty that still moves reliably.
 *   We print [SPEED] duty=0.100 3s ... so you can note the winner.
 *
 *   TEST 2 — TRAVEL: reverse to a known home for SOLAR_TEST_HOME_MS, then
 *   drive forward and print a progress tick every 1000ms up to
 *   SOLAR_TEST_TRAVEL_MS. Watch the panel: the tick number where it hits
 *   the west mechanical stop is your east->west travel time in ms
 *   (= SUN_POS_WEST in config.h).
 *
 * After you report the results, set SOLAR_TEST_MODE back to 0 and update
 * TRACKER_FWD_DUTY / TRACKER_REV_DUTY / SUN_POS_WEST (and the LDR sweep
 * times) accordingly.
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
    DigitalOut led(PA_5);   // NUCLEO green LED
    for (int i = 0; i < times; i++) {
        led = 1;
        thread_sleep_for(period_ms / 2);
        led = 0;
        thread_sleep_for(period_ms / 2);
    }
}

void solar_test_run(void)
{
    printf("\n=== SOLAR CALIBRATION TEST MODE ===\n");

    // ---- TEST 1: SPEED ----
    const float duties[] = SOLAR_TEST_DUTIES;
    const int n = sizeof(duties) / sizeof(duties[0]);
    printf("[TEST] Test 1 SPEED: %d duties, %d ms each\n", n, SOLAR_TEST_EACH_MS);
    printf("[TEST] Watch the panel, note which duty is FASTEST but smooth.\n");
    for (int i = 0; i < n; i++) {
        printf("[SPEED] duty=%0.3f -- running %d ms\n", (double)duties[i], SOLAR_TEST_EACH_MS);
        tracker_test_drive(duties[i]);
        thread_sleep_for(SOLAR_TEST_EACH_MS);
        tracker_test_stop();
        printf("[SPEED] duty=%0.3f stopped\n", (double)duties[i]);
        blink_led(i + 1, 200);   // blink i+1 times so you can count which duty
        thread_sleep_for(SOLAR_TEST_PAUSE_MS);
    }
    printf("[TEST] Test 1 SPEED done.\n");

    // ---- TEST 2: TRAVEL ----
    printf("[TEST] Test 2 TRAVEL: homing reverse %d ms\n", SOLAR_TEST_HOME_MS);
    tracker_test_reset_pos();
    tracker_test_drive(0.050f);            // reverse (mirror of 0.100 fwd)
    thread_sleep_for(SOLAR_TEST_HOME_MS);
    tracker_test_stop();
    printf("[TEST] Homed. Now driving forward, tick every 1000ms:\n");

    tracker_test_drive(0.100f);            // forward at current speed
    for (int t = 1000; t <= SOLAR_TEST_TRAVEL_MS; t += 1000) {
        thread_sleep_for(1000);
        printf("[TRAVEL] t=%d ms\n", t);
    }
    tracker_test_stop();
    printf("[TEST] Test 2 TRAVEL done. Report the t= value where the panel hit the west stop.\n");

    printf("[TEST] All calibration tests complete. Reflash with SOLAR_TEST_MODE=0 for normal operation.\n");
    blink_led(5, 250);
}
