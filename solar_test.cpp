/*
 * Solar calibration test mode (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Single test:
 *
 *   TEST — TRAVEL (TRACKER motor, PB_0): reverse to a known home for
 *   SOLAR_TEST_HOME_MS, then drive the tracker motor forward at
 *   TRACKER_FWD_DUTY and print a progress tick every 1000ms up to
 *   SOLAR_TEST_TRAVEL_MS. Watch the panel: the tick number where it hits
 *   the west mechanical stop is your east->west travel time in ms
 *   (= SUN_POS_WEST in config.h).
 *
 * After you report the result, set SOLAR_TEST_MODE back to 0 and update
 * SUN_POS_WEST (and the LDR sweep times) accordingly.
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
    printf("\n=== SOLAR CALIBRATION TEST MODE ===\n");

    // CRITICAL: tracker_init() -> begin_sweep() already started the tracker
    // motor forward before we got here. Stop it NOW so the panel doesn't
    // run into the stop and stall (which makes the travel test look dead
    // because reverse can't un-stall a jammed motor).
    tracker_test_stop();

    // ---- TEST: TRAVEL ----
    printf("[TEST] TRAVEL: homing reverse %d ms\n", SOLAR_TEST_HOME_MS);
    tracker_test_reset_pos();
    tracker_test_drive(0.050f);            // reverse (clamped min; below 0.050 is <1ms, out of range)
    thread_sleep_for(SOLAR_TEST_HOME_MS);
    tracker_test_stop();
    printf("[TEST] Homed. Now driving forward at TRACKER_FWD_DUTY=%0.3f, tick every 1000ms:\n", (double)TRACKER_FWD_DUTY);

    tracker_test_drive(TRACKER_FWD_DUTY);  // forward at the FINAL speed
    for (int t = 1000; t <= SOLAR_TEST_TRAVEL_MS; t += 1000) {
        thread_sleep_for(1000);
        printf("[TRAVEL] t=%d ms\n", t);
    }
    tracker_test_stop();
    printf("[TEST] TRAVEL done. Report the t= value where the panel hit the west stop.\n");

    printf("[TEST] All calibration tests complete. Reflash with SOLAR_TEST_MODE=0 for normal operation.\n");
    blink_led(5, 250);
}
