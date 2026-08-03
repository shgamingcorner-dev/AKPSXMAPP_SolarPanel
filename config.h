#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Debug printf gating
// ============================================================
// DEBUG 0 = production: clean serial (no per-poll [DS]/[TRK]/[ESP] spam,
// no per-actuation fan-apply internals). Critical errors ([WARN]/[ERROR]),
// security events ([RFID]/[AUTH]), telemetry (Temperature/Current/[DATA])
// and keypad state changes stay visible in both modes.
// DEBUG 1 = verbose: everything prints (including the noisy per-cycle lines).
#define DEBUG 0
#if DEBUG
#define DBG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DBG_PRINTF(...) ((void)0)
#endif

// ============================================================
// Hardware Configuration
// ============================================================

// WiFi
#define WIFI_SSID        "SINGTEL-AE6C"
#define WIFI_PASSWORD    "97874001lim"

//Thinkspeak api key
#define TS_API_KEY       "WFQQ2K9I14E30IE3"

// Relay server (HTTPS bridge for ESP-01)
#define RELAY_HOST    "shgam.pythonanywhere.com"
#define RELAY_IP      "35.173.69.207"
#define RELAY_PORT    80
#define RELAY_SECRET  "ab805d0429869cfc507b54bd1921a2ae"

// ThingSpeak
#define TS_HOST       "api.thingspeak.com"
#define TS_PORT       80
#define SEND_INTERVAL_MS  15000
#define TS_FIELD_TEMPERATURE   1
#define TS_FIELD_HUMIDITY      2
#define TS_FIELD_CURRENT       3
#define TS_FIELD_RFIDQ         4

// RFID UIDs
#define RFID_UID_CARD  "15828045"
#define RFID_UID_TAG   "E09F8E21"

// Hardware Pins
#define RST_PIN           PA_2
#define SS_PIN            PB_2
#define ESP_TX            PC_10
#define ESP_RX            PC_11
#define DHT11_PIN         PC_4
#define CURRENT_SENSOR_PIN PA_0
#define MAIN_LIGHT_PIN    PB_1      // Main light PWM. PB_1 = TIM3_CH4 DEFAULT
                                    // remap -- NOT PC_9! PC_9 forces TIM3 full
                                    // remap which re-routes TIM3_CH2 away from
                                    // PA_7, silently killing the blind motor.
                                    // PB_1 keeps ALL TIM3 users on default remap
                                    // (motor PA_7 CH2, door PA_6 CH1, light PB_1
                                    // CH4) so every channel actually outputs.
#define MOTOR_PIN         PA_7
#define FAN_SERVO_PIN     PA_1
#define DOOR_LOCK_PIN     PA_6      // SG90 door-lock servo. PA_6 = TIM3_CH1
                                    // DEFAULT remap -- same mode as the PA_7
                                    // blind motor and PB_1 light (no AFIO fight).
                                    // NOT PC_8 (full-remap pin would re-trigger
                                    // the remap conflict). NOT PA_3/PB_7 (excluded
                                    // from pinmap -> 0x80010130), NOT PC_6 (LCD_WR).
#define BUZZER_PIN        PB_14     // changed: was PA_2 (PA_2 conflicts with RST_PIN)

// Motor / Servo Timings - SG90 standard servo (0° to 180°)
#define WAIT_TIME_MS_0        2000
#define PERIOD_WIDTH          20

// SG90 servo - actual pulse widths (calibrated)
#define PULSE_WIDTH_0_DEGREE    600     // 0°   - CLOSED/LOCKED
#define PULSE_WIDTH_90_DEGREE   1500    // 90°  - MID/NEUTRAL
#define PULSE_WIDTH_180_DEGREE  2400    // 180° - OPEN/UNLOCKED
#define PULSE_WIDTH_N_90_DEGREE 600     // -90° (reverse, kept for compat)


// Fan Servo (360° continuous rotation)
#define FAN_SERVO_NEUTRAL_US     1500
#define FAN_SERVO_MAX_FWD_US     2000
#define FAN_SERVO_MAX_REV_US     1000

// Door Lock
#define DOOR_LOCK_LOCKED       1
#define DOOR_LOCK_UNLOCKED     0

// Solar Tracker
#define DHT11VCC_PIN      PB_12     // repointed off PB_0 (hygiene; PB_0 is the tracker servo now)
#define TRACKER_SERVO_PIN PB_0      // SG90 tilt servo. PB_0 = TIM3_CH3 DEFAULT remap —
                                    // same remap family as PA_6/PA_7/PB_1 (no AFIO fight).
                                    // NEVER use PC_8/PC_9 (TIM3 full remap kills door/blind/light).
#define LDR_PIN           PA_5      // LDR voltage divider -> ADC1_IN5 (SPI1 remapped away, free)
#define TRACKER_MIN_ANGLE 10
#define TRACKER_MAX_ANGLE 170
#define TRACKER_STEP_DEG  5
#define TRACKER_STEP_MS   2000      // one perturb step every 2s (servo settle + feedback avg)

// Feedback source. LDR is default: the ACS712 is wired but untested/measuring nothing.
// Switch to CURRENT only after Phase 0.4 (panel wired through sensor + calibration) passes.
#define TRACKER_FEEDBACK_LDR    1
#define TRACKER_FEEDBACK_CURRENT 0
#if TRACKER_FEEDBACK_CURRENT
#define TRACKER_DEADBAND        0.02f   // amps: ignore deltas < 20mA
#define TRACKER_CLOUD           0.15f   // amps: below this => cloud/sweep mode
#else
#define TRACKER_DEADBAND        2.0f    // LDR %: ignore deltas < 2%
#define TRACKER_CLOUD           10.0f   // LDR %: below this => dark/cloud -> sweep/park
#endif
#define TRACKER_RE_SWEEP_MS 600000      // full re-sweep every 10 min (escape local maxima)

// ACS712 20A Current Sensor
#define ACS712_SENSITIVITY_V_PER_A  0.060f
#define ACS712_ZERO_V               1.5f
#define ADC_VREF                    3.3f

// Network Timeouts
#define ESP_DRAIN_TIMEOUT_MS    100
#define ESP_AT_TIMEOUT_MS       1000
#define ESP_CIPSTART_TIMEOUT_MS 8000
#define ESP_CIPSEND_TIMEOUT_MS  1000
#define ESP_CIPCLOSE_TIMEOUT_MS 500
#define ESP_DATA_READ_TIMEOUT_MS 3000
#define WIFI_JOIN_TIMEOUT_MS    8000

// Polling Intervals
#define TG_COOLDOWN_MS          5000
#define DEVICE_STATE_POLL_MS    7000

// Failure Thresholds
#define MAX_CONSECUTIVE_FAILURES 3

// DHT11
#define DHT11_DEFAULT_DELAY_MS  500
#define DHT11_TIMEOUT_MS        1000

// LCD
#define LCD_STROBE_US           1

#endif // CONFIG_H