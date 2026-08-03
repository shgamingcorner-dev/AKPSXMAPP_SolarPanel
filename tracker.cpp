/*
 * Solar panel tracker — 360° continuous motor on a pulley (see tracker.h).
 *
 * Pin: TRACKER_MOTOR_PIN = PB_0 = TIM3_CH3, DEFAULT remap — same remap
 * family as the PA_6 door, PA_7 blind, and PB_1 light. NEVER PC_8/PC_9
 * (TIM3 full remap reroutes every TIM3 channel and kills door/blind/light).
 * DHT11VCC was repointed to PB_12 (config.h) to free PB_0.
 *
 * Phase 1: time-driven pulley cycle. Each phase sets the motor duty and
 * runs until its duration elapses, then advances. Phases:
 *   0: FWD  to FLAT   (TRACKER_MS_TO_FLAT)      -> stop, hold flat
 *   1: HOLD flat      (TRACKER_HOLD_FLAT_MS)
 *   2: FWD  to max    (TRACKER_MS_FWD_MAX)      -> stop
 *   3: HOLD max       (TRACKER_HOLD_MAX_MS)
 *   4: REV  to home   (TRACKER_MS_RETURN)       -> stop
 *   5: HOLD home      (TRACKER_HOLD_HOME_MS)    -> back to phase 0
 */
#include "mbed.h"
#include "config.h"
#include "tracker.h"

static PwmOut trackerMotor(TRACKER_MOTOR_PIN);

// millisecond clock (replaces deprecated Kernel::get_ms_count on mbed 6)
static uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

enum TrkPhase {
    TRK_FWD_TO_FLAT,
    TRK_HOLD_FLAT,
    TRK_FWD_TO_MAX,
    TRK_HOLD_MAX,
    TRK_REV_TO_HOME,
    TRK_HOLD_HOME,
    TRK_PHASE_COUNT
};

static TrkPhase g_phase = TRK_FWD_TO_FLAT;
static uint64_t g_phase_start = 0;
static bool     g_moving = false;

static void motor_stop(void)
{
    trackerMotor.write(TRACKER_STOP_DUTY);
    g_moving = false;
}

static void motor_run(float duty)
{
    trackerMotor.write(duty);
    g_moving = true;
}

static void start_phase(TrkPhase p)
{
    g_phase = p;
    g_phase_start = now_ms();

    switch (p) {
        case TRK_FWD_TO_FLAT:
            motor_run(TRACKER_FWD_DUTY);
            break;
        case TRK_HOLD_FLAT:
            motor_stop();
            break;
        case TRK_FWD_TO_MAX:
            motor_run(TRACKER_FWD_DUTY);
            break;
        case TRK_HOLD_MAX:
            motor_stop();
            break;
        case TRK_REV_TO_HOME:
            motor_run(TRACKER_REV_DUTY);
            break;
        case TRK_HOLD_HOME:
            motor_stop();
            break;
        default:
            break;
    }
}

void tracker_init(void)
{
    trackerMotor.period_ms(PERIOD_WIDTH);   // 50Hz, same as fan/door servos
    start_phase(TRK_FWD_TO_FLAT);
    printf("[TRK] tracker init: 360 motor on PB_0, time-driven pulley cycle\n");
}

bool tracker_is_moving(void) { return g_moving; }
uint8_t tracker_get_phase(void) { return (uint8_t)g_phase; }

void tracker_tick(void)
{
    uint64_t now = now_ms();
    uint64_t elapsed = now - g_phase_start;

    switch (g_phase) {
        case TRK_FWD_TO_FLAT:
            if (elapsed >= TRACKER_MS_TO_FLAT)  start_phase(TRK_HOLD_FLAT);
            break;
        case TRK_HOLD_FLAT:
            if (elapsed >= TRACKER_HOLD_FLAT_MS) start_phase(TRK_FWD_TO_MAX);
            break;
        case TRK_FWD_TO_MAX:
            if (elapsed >= TRACKER_MS_FWD_MAX)  start_phase(TRK_HOLD_MAX);
            break;
        case TRK_HOLD_MAX:
            if (elapsed >= TRACKER_HOLD_MAX_MS) start_phase(TRK_REV_TO_HOME);
            break;
        case TRK_REV_TO_HOME:
            if (elapsed >= TRACKER_MS_RETURN)   start_phase(TRK_HOLD_HOME);
            break;
        case TRK_HOLD_HOME:
            if (elapsed >= TRACKER_HOLD_HOME_MS) start_phase(TRK_FWD_TO_FLAT);
            break;
        default:
            break;
    }
}
