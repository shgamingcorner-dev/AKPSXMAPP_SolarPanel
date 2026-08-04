/*
 * Solar panel tracker — 360° continuous motor on a pulley + LDR (see tracker.h).
 *
 * Pin: TRACKER_MOTOR_PIN = PB_0 = TIM3_CH3, DEFAULT remap — same remap
 * family as the PA_6 door, PA_7 blind, and PB_1 light. NEVER PC_8/PC_9
 * (TIM3 full remap reroutes every TIM3 channel and kills door/blind/light).
 * DHT11VCC was repointed to PB_12 (config.h) to free PB_0.
 * LDR on LDR_PIN (PA_4, ADC1_IN4) + module DO on LDR_DO_PIN (PD_2).
 *
 * SWEEP phases (identical to the Arduino sketch):
 *   0: FWD  to FLAT   (TRACKER_MS_TO_FLAT)      -> stop, hold flat
 *   1: HOLD flat      (TRACKER_HOLD_FLAT_MS)
 *   2: FWD  to max    (TRACKER_MS_FWD_MAX)      -> stop
 *   3: HOLD max       (TRACKER_HOLD_MAX_MS)
 *   4: REV  to home   (TRACKER_MS_RETURN)       -> stop
 *   5: HOLD home      (TRACKER_HOLD_HOME_MS)
 * During these phases the LDR is sampled every TRACKER_LDR_SAMPLE_MS and the
 * best (highest) reading + its position is remembered.
 *
 * After the sweep:
 *   - if best LDR >= TRACKER_LDR_FLOOR: RETURN_TO_BEST — drive the motor
 *     back to the remembered position, then HOLD_BEST for
 *     TRACKER_HOLD_BEST_MS (15 min), then re-sweep.
 *   - if best LDR < floor (dark/night): park at home (HOLD_BEST with the
 *     home position), wait TRACKER_HOLD_BEST_MS, re-sweep.
 *
 * Position model: signed cumulative run time in ms (FWD adds, REV subtracts).
 * "Return to best" runs the motor until the accumulated position reaches the
 * remembered best position (with a small tolerance). No encoder is needed.
 */
#include "mbed.h"
#include "config.h"
#include "tracker.h"

static PwmOut   trackerMotor(TRACKER_MOTOR_PIN);
static AnalogIn ldr(LDR_PIN);
static DigitalIn ldrDo(LDR_DO_PIN);   // module DO: 1 = dark, 0 = light (tutorial)

// millisecond clock (replaces deprecated Kernel::get_ms_count on mbed 6)
static uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

enum TrkPhase {
    TRK_SWEEP_FWD_TO_FLAT,
    TRK_SWEEP_HOLD_FLAT,
    TRK_SWEEP_FWD_TO_MAX,
    TRK_SWEEP_HOLD_MAX,
    TRK_SWEEP_REV_TO_HOME,
    TRK_SWEEP_HOLD_HOME,
    TRK_RETURN_TO_BEST,
    TRK_HOLD_BEST,
    TRK_PHASE_COUNT
};

static TrkPhase g_phase = TRK_SWEEP_FWD_TO_FLAT;
static uint64_t g_phase_start = 0;
static bool     g_moving = false;

// LDR sampling + best-angle tracking
static float   g_best_ldr = 0.0f;        // highest LDR reading this sweep
static int32_t g_best_pos = 0;           // motor position (ms) of best LDR
static int32_t g_pos = 0;                // signed cumulative run time (ms)
static uint64_t g_last_sample = 0;
static uint64_t g_last_pos_update = 0;   // last position integration tick
static bool    g_ldr_enabled = true;

static float read_ldr_pct(void)
{
    float sum = 0.0f;
    for (int i = 0; i < TRACKER_LDR_AVG_SAMPLES; i++) {
        sum += ldr.read();               // 0.0..1.0
    }
    return (sum / TRACKER_LDR_AVG_SAMPLES) * 100.0f;   // 0..100%
}

// Module DO comparator output: 1 = dark, 0 = light (per tutorial sample code)
bool tracker_is_dark(void)
{
    return ldrDo.read() == 1;
}

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

// Integrate motor travel into g_pos while running. Uses its own timestamp so
// it does NOT clobber g_phase_start (which drives phase-transition timing).
static void update_position(void)
{
    uint64_t now = now_ms();
    uint64_t dt = now - g_last_pos_update;
    if (dt > 100) dt = 100;               // clamp so a long stall can't drift
    if (g_phase == TRK_SWEEP_FWD_TO_FLAT || g_phase == TRK_SWEEP_FWD_TO_MAX ||
        (g_phase == TRK_RETURN_TO_BEST && g_best_pos > g_pos)) {
        g_pos += (int32_t)dt;
    } else if (g_phase == TRK_SWEEP_REV_TO_HOME ||
               (g_phase == TRK_RETURN_TO_BEST && g_best_pos < g_pos)) {
        g_pos -= (int32_t)dt;
    }
    g_last_pos_update = now;
}

static void start_phase(TrkPhase p)
{
    g_phase = p;
    g_phase_start = now_ms();
    g_last_pos_update = g_phase_start;

    switch (p) {
        case TRK_SWEEP_FWD_TO_FLAT:
        case TRK_SWEEP_FWD_TO_MAX:
            motor_run(TRACKER_FWD_DUTY);
            break;
        case TRK_SWEEP_REV_TO_HOME:
            motor_run(TRACKER_REV_DUTY);
            break;
        case TRK_SWEEP_HOLD_FLAT:
        case TRK_SWEEP_HOLD_MAX:
        case TRK_SWEEP_HOLD_HOME:
            motor_stop();
            break;
        case TRK_RETURN_TO_BEST:
            // Drive toward the remembered best position.
            if (g_best_pos > g_pos) {
                motor_run(TRACKER_FWD_DUTY);
            } else {
                motor_run(TRACKER_REV_DUTY);
            }
            break;
        case TRK_HOLD_BEST:
            motor_stop();
            break;
        default:
            break;
    }
}

static void sample_ldr(void)
{
    if (!g_ldr_enabled) return;
    uint64_t now = now_ms();
    if (now - g_last_sample < TRACKER_LDR_SAMPLE_MS) return;
    g_last_sample = now;

    float v = read_ldr_pct();
    if (v > g_best_ldr) {
        g_best_ldr = v;
        g_best_pos = g_pos;
    }
}

static void begin_sweep(void)
{
    g_best_ldr = 0.0f;
    g_best_pos = 0;
    g_pos = 0;                 // home = 0 by construction (was never reset:
                               // sweep ends at 2500+3000-3600=1900, so the
                               // model drifted and night-return hit phantom 0)
    g_ldr_enabled = true;
    start_phase(TRK_SWEEP_FWD_TO_FLAT);
}

void tracker_init(void)
{
    g_pos = 0;                          // start the model at home
    trackerMotor.period_ms(PERIOD_WIDTH);   // 50Hz, same as fan/door servos
    begin_sweep();
    printf("[TRK] tracker init: 360 motor PB_0 + LDR PA_4, LDR sweep-and-hold\n");
}

bool tracker_is_moving(void) { return g_moving; }
uint8_t tracker_get_phase(void) { return (uint8_t)g_phase; }
float tracker_get_best_ldr(void) { return g_best_ldr; }

void tracker_tick(void)
{
    uint64_t now = now_ms();
    uint64_t elapsed = now - g_phase_start;

    switch (g_phase) {
        case TRK_SWEEP_FWD_TO_FLAT:
            update_position();
            sample_ldr();
            if (elapsed >= TRACKER_MS_TO_FLAT)  start_phase(TRK_SWEEP_HOLD_FLAT);
            break;
        case TRK_SWEEP_HOLD_FLAT:
            sample_ldr();
            if (elapsed >= TRACKER_HOLD_FLAT_MS) start_phase(TRK_SWEEP_FWD_TO_MAX);
            break;
        case TRK_SWEEP_FWD_TO_MAX:
            update_position();
            sample_ldr();
            if (elapsed >= TRACKER_MS_FWD_MAX)  start_phase(TRK_SWEEP_HOLD_MAX);
            break;
        case TRK_SWEEP_HOLD_MAX:
            sample_ldr();
            if (elapsed >= TRACKER_HOLD_MAX_MS) start_phase(TRK_SWEEP_REV_TO_HOME);
            break;
        case TRK_SWEEP_REV_TO_HOME:
            update_position();
            sample_ldr();
            if (elapsed >= TRACKER_MS_RETURN)   start_phase(TRK_SWEEP_HOLD_HOME);
            break;
        case TRK_SWEEP_HOLD_HOME:
            sample_ldr();
            if (elapsed >= TRACKER_HOLD_HOME_MS) {
                // Sweep complete: pick the best angle or park at night.
                printf("[TRK] sweep done: best LDR %.1f%% at pos %ld, DO=%s\n",
                       g_best_ldr, (long)g_best_pos,
                       tracker_is_dark() ? "dark" : "light");
                if (g_best_ldr >= TRACKER_LDR_FLOOR && !tracker_is_dark()) {
                    start_phase(TRK_RETURN_TO_BEST);
                } else {
                    // Night/dark: no useful LDR peak. Drive back to the home
                    // position (pos 0) and wait there until daylight.
                    printf("[TRK] dark (LDR %.1f < floor %.1f): returning home\n",
                           g_best_ldr, TRACKER_LDR_FLOOR);
                    g_best_pos = 0;
                    start_phase(TRK_RETURN_TO_BEST);
                }
            }
            break;
        case TRK_RETURN_TO_BEST:
            update_position();
            // Stop when we reach the remembered position (small tolerance).
            if (g_best_pos > g_pos) {
                motor_run(TRACKER_FWD_DUTY);
            } else if (g_best_pos < g_pos) {
                motor_run(TRACKER_REV_DUTY);
            }
            if (abs(g_best_pos - g_pos) <= 150) {   // tolerance > max 100ms step
                printf("[TRK] reached best pos %ld (LDR %.1f%%)\n",
                       (long)g_pos, g_best_ldr);
                start_phase(TRK_HOLD_BEST);
            }
            break;
        case TRK_HOLD_BEST:
            if (elapsed >= TRACKER_HOLD_BEST_MS) {
                if (tracker_is_dark()) {
                    // Night fell during the hold: don't waste a sweep in the
                    // dark. Stay parked at home and re-check next cycle.
                    printf("[TRK] hold done, but DO=dark: staying parked\n");
                    g_best_pos = 0;
                    start_phase(TRK_RETURN_TO_BEST);   // go home, then hold
                } else {
                    printf("[TRK] hold done, re-sweeping\n");
                    begin_sweep();
                }
            }
            break;
        default:
            break;
    }
}
