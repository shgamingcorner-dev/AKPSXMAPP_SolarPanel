/*
 * Solar panel tracker — servo + LDR + hill-climb (see tracker.h).
 *
 * Pins (all verified against NUCLEO_F103RB PeripheralPins.c):
 *   TRACKER_SERVO_PIN = PB_0 = TIM3_CH3, DEFAULT remap — same remap family as
 *     the PA_6 door, PA_7 blind, and PB_1 light. NEVER PC_8/PC_9 (TIM3 full
 *     remap reroutes every TIM3 channel and kills door/blind/light).
 *   LDR_PIN = PA_5 = ADC1_IN5 (SPI1 is remapped to PB_3/4/5, so PA_5 is free).
 */
#include "mbed.h"
#include "config.h"
#include "utils.h"
#include "tracker.h"

static PwmOut   trackerServo(TRACKER_SERVO_PIN);
static AnalogIn ldr(LDR_PIN);

static volatile uint8_t g_angle = 90;        // 0..180
static volatile int     g_sun_target = -1;   // relay-computed target; -1 = unknown

static void servo_to_angle(uint8_t angle)
{
    if (angle < TRACKER_MIN_ANGLE) angle = TRACKER_MIN_ANGLE;
    if (angle > TRACKER_MAX_ANGLE) angle = TRACKER_MAX_ANGLE;
    g_angle = angle;
    // SG90: 0° = 600us, 180° = 2400us (linear map)
    float pulse = PULSE_WIDTH_0_DEGREE +
                  (PULSE_WIDTH_180_DEGREE - PULSE_WIDTH_0_DEGREE) *
                  ((float)angle / 180.0f);
    trackerServo.pulsewidth_us(pulse);
}

static float read_ldr_pct(void)
{
    float sum = 0.0f;
    for (int i = 0; i < 10; i++) {
        sum += ldr.read();
        wait_us(100);
    }
    return (sum / 10.0f) * 100.0f;   // 0..100%
}

// One feedback source, selected at compile time in config.h.
static float read_feedback(void)
{
#if TRACKER_FEEDBACK_CURRENT
    return read_current();          // amps — only after Phase 0.4 (wiring + calibration)
#else
    return read_ldr_pct();          // 0..100% — default (ACS712 untested)
#endif
}

void tracker_init(void)
{
    trackerServo.period_ms(20);
    servo_to_angle(90);             // center
    printf("[TRK] tracker init: tilt servo centered at 90deg (1500us) on PB_0\n");
}

uint8_t tracker_get_angle(void) { return g_angle; }
void    tracker_set_sun_target(int a) { g_sun_target = a; }

// --- Hill-climb state machine ---------------------------------------------
enum TrkState { TRK_TRACKING, TRK_SWEEPING, TRK_HOLD };

static TrkState  g_state = TRK_TRACKING;
static int8_t    g_dir = 1;             // +1/-1
static float     g_last_fb = -1.0f;     // last feedback value (LDR% or A)
static uint64_t  g_last_step = 0;
static uint64_t  g_last_resweep = 0;

void tracker_tick(void)
{
    uint64_t now = now_ms();

    // Periodic re-sweep: go back to the sun target to escape local maxima.
    if (now - g_last_resweep >= TRACKER_RE_SWEEP_MS) {
        g_last_resweep = now;
        if (g_sun_target >= 0) {
            servo_to_angle((uint8_t)g_sun_target);
            g_dir = 1;
            g_last_fb = -1.0f;
        }
    }

    if (now - g_last_step < TRACKER_STEP_MS) return;
    g_last_step = now;

    float fb = read_feedback();

    // Dark / cloud / signal collapse: sweep toward the sun target, don't
    // chase noise. With no target (e.g. night, relay down) just hold still.
    if (fb < TRACKER_CLOUD) {
        if (g_sun_target >= 0) servo_to_angle((uint8_t)g_sun_target);
        g_last_fb = -1.0f;
        g_state = TRK_SWEEPING;
        DBG_PRINTF("[TRK] low feedback %.1f -> sweep to %d\n", fb, g_sun_target);
        return;
    }

    if (g_last_fb < 0.0f) {          // first reading: pick a direction and step
        g_last_fb = fb;
        servo_to_angle(g_angle + g_dir * TRACKER_STEP_DEG);
        g_state = TRK_TRACKING;
        return;
    }

    float delta = fb - g_last_fb;
    g_last_fb = fb;

    if (delta >  TRACKER_DEADBAND)       { /* improved: keep direction */ }
    else if (delta < -TRACKER_DEADBAND)  { g_dir = -g_dir; }   // worse: reverse
    else                                 { g_state = TRK_HOLD; return; }  // deadband: stop

    servo_to_angle(g_angle + g_dir * TRACKER_STEP_DEG);
    DBG_PRINTF("[TRK] angle=%d fb=%.1f d=%.2f dir=%+d\n", g_angle, fb, delta, g_dir);
}
