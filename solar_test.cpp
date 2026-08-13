/*
 * Solar calibration test mode (SolarBugFixes branch).
 *
 * Runs at boot when SOLAR_TEST_MODE == 1. Two tests in one flash:
 *
 *   TEST 1 — SPEED (FAN servo, PA_1): run the FAN servo (a 360° continuous
 *   servo, same family as the tracker motor) forward at each duty in
 *   SOLAR_TEST_DUTIES, SOLAR_TEST_EACH_MS each, pausing in between. Watch
 *   the fan spin: pick the fastest duty that still moves reliably. We print
 *   [SPEED] duty=0.100 ... and blink PB_14 i+1 times so you can count which
 *   duty just ran.
 *
 *   TEST 2 — TRAVEL (TRACKER motor, PB_0): reverse to a known home for
 *   SOLAR_TEST_HOME_MS, then drive the tracker motor forward and print a
 *   progress tick every 1000ms up to SOLAR_TEST_TRAVEL_MS. Watch the panel:
 *   the tick number where it hits the west mechanical stop is your east->west
 *   travel time in ms (= SUN_POS_WEST in config.h).
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
    // motor forward at 0.100 before we got here. Stop it NOW so the panel
    // doesn't run into the stop during Test 1 and stall (which makes Test 2
    // look dead because reverse can't un-stall a jammed motor).
    tracker_test_stop();

    // ---- TEST 1: SPEED (FAN SERVO, PA_1) ----
    // The fan servo is a 360° continuous servo on PA_1 — easy to see spin.
    // Same servo family as the tracker motor, so the fastest smooth duty
    // measured here applies to the tracker too.
    PwmOut fanServo(FAN_SERVO_PIN);
    fanServo.period_ms(PERIOD_WIDTH);      // 50Hz like the fan driver

    const float duties[] = SOLAR_TEST_DUTIES;
    const int n = sizeof(duties) / sizeof(duties[0]);
    printf("[TEST] Test 1 SPEED (FAN servo PA_1): %d duties, %d ms each\n", n, SOLAR_TEST_EACH_MS);
    printf("[TEST] Watch the FAN, note which duty is FASTEST but smooth.\n");
    for (int i = 0; i < n; i++) {
        printf("[SPEED] duty=%0.3f -- running %d ms\n", (double)duties[i], SOLAR_TEST_EACH_MS);
        fanServo.write(duties[i]);
        thread_sleep_for(SOLAR_TEST_EACH_MS);
        fanServo.write(TRACKER_STOP_DUTY); // stop (neutral)
        printf("[SPEED] duty=%0.3f stopped\n", (double)duties[i]);
        blink_led(i + 1, 200);   // blink i+1 times so you can count which duty
        thread_sleep_for(SOLAR_TEST_PAUSE_MS);
    }
    printf("[TEST] Test 1 SPEED done.\n");

    // ---- TEST 2: TRAVEL ----
    printf("[TEST] Test 2 TRAVEL: homing reverse %d ms\n", SOLAR_TEST_HOME_MS);
    tracker_test_reset_pos();
    tracker_test_drive(0.050f);            // reverse (clamped min; mirror of 0.125 would be 0.025 < 1ms)
    thread_sleep_for(SOLAR_TEST_HOME_MS);
    tracker_test_stop();
    printf("[TEST] Homed. Now driving forward at TRACKER_FWD_DUTY=%0.3f, tick every 1000ms:\n", (double)TRACKER_FWD_DUTY);

    tracker_test_drive(TRACKER_FWD_DUTY);  // forward at the FINAL speed
    for (int t = 1000; t <= SOLAR_TEST_TRAVEL_MS; t += 1000) {
        thread_sleep_for(1000);
        printf("[TRAVEL] t=%d ms\n", t);
    }
    tracker_test_stop();
    printf("[TEST] Test 2 TRAVEL done. Report the t= value where the panel hit the west stop.\n");

    printf("[TEST] All calibration tests complete. Reflash with SOLAR_TEST_MODE=0 for normal operation.\n");
    blink_led(5, 250);
}
