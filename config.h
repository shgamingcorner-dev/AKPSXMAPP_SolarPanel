#ifndef CONFIG_H
#define CONFIG_H

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
                                    // (motor PA_7 CH2, light PB_1 CH4) so every
                                    // channel actually outputs.
#define MOTOR_PIN         PA_7
#define FAN_SERVO_PIN     PA_1
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

// Solar Tracker (360° continuous motor on a pulley, time-driven)
// DHT11VCC repointed to PB_12 (was PB_0) so PB_0 is free for the tracker
// motor. PB_0 = TIM3_CH3 DEFAULT remap — same family as PA_7/PB_1
// (blind/light), no AFIO fight. NEVER use PC_8/PC_9 (TIM3 full remap
// kills blind/light).
#define DHT11VCC_PIN      PB_12
#define TRACKER_MOTOR_PIN PB_0      // 360° continuous motor, pulley-driven
// LDR light sensor module (per "How to use LDR Sensor Module" tutorial):
//   AO (analog out) -> PA_4 = ADC1_IN4 (free). NOT PA_0 -- PA_0 is the
//   ACS712 current sensor (existing telemetry, keep untouched). PA_4 is
//   electrically identical (same ADC1, 0..3.3V -> read() 0.0..1.0).
//   DO (digital out, module comparator) -> PD_2 (free).
//   VCC -> 3.3V, GND -> GND.
#define LDR_PIN           PA_4
#define LDR_DO_PIN        PD_2

// Phase 1: time-driven pulley cycle (exact Arduino sketch timings)
#define TRACKER_STOP_DUTY      0.075f   // neutral/stop (1500us equivalent)
#define TRACKER_FWD_DUTY       0.100f   // full speed one direction
#define TRACKER_REV_DUTY       0.050f   // full speed other direction
#define TRACKER_MS_TO_FLAT     2500     // fwd 2.5s -> stop at FLAT (180°)
#define TRACKER_HOLD_FLAT_MS   30000    // hold flat 30s
#define TRACKER_MS_FWD_MAX     3000     // fwd 3s -> stop at ~315/135
#define TRACKER_HOLD_MAX_MS    3000     // hold 3s
#define TRACKER_MS_RETURN      3600     // rev 3.6s -> back to ~225/60
#define TRACKER_HOLD_HOME_MS   1000     // hold 1s

// Phase 2: LDR sweep-and-hold. While the sweep runs (moving AND at the
// stops) the LDR is sampled every TRACKER_LDR_SAMPLE_MS; the highest reading
// and its motor position are remembered. When the sweep finishes, the motor
// returns to that position and holds for TRACKER_HOLD_BEST_MS, then re-sweeps.
#define TRACKER_LDR_SAMPLE_MS   100     // LDR sample interval during sweep
#define TRACKER_LDR_AVG_SAMPLES 10      // ADC samples averaged per LDR read
#define TRACKER_LDR_FLOOR       5.0f    // below this = dark/night -> park at home
#define TRACKER_HOLD_BEST_MS    900000  // hold best-LDR angle 15 minutes

// Smart Mode: automatic LDR/temperature-driven house automation.
// When ON, the firmware ignores remote lighting/fan/blind settings and
// drives them from its own sensors. The solar tracker is INDEPENDENT
// (Option A) -- Smart Mode never touches it.
#define SMART_MODE_DEFAULT       0       // 0 = off (current behavior), 1 = on

// LDR polarity: 0 = AO reads HIGH in bright light (original assumption),
// 1 = AO reads HIGH in dark (inverted module). Flip if serial shows wrong.
#define SMART_LDR_INVERT         0

// Main lighting: LDR% -> brightness 0-100 (darker outside = brighter inside)
#define SMART_LIGHT_DARK_LDR     20.0f   // LDR% at/below -> 100% brightness
#define SMART_LIGHT_BRIGHT_LDR   70.0f   // LDR% at/above -> 0% (off)

// Fan: room temperature (C) -> speed (hotter = faster)
#define SMART_FAN_TEMP_OFF       24.0f   // at/below -> fan OFF (speed 0)
#define SMART_FAN_TEMP_MAX       32.0f   // at/above -> fan speed 100

// Blinds: LDR% -> up/down (with hysteresis)
#define SMART_BLIND_BRIGHT_LDR   60.0f   // LDR% >= -> blinds UP (open)
#define SMART_BLIND_DARK_LDR     15.0f   // LDR% <= -> blinds DOWN (closed)

// Smart Mode update cadence (ms). Re-evaluates lighting/fan/blinds.
// Every 5s is fine for this codebase (main loop is non-blocking ~10ms).
#define SMART_UPDATE_MS          5000

// Manual override window (ms). While Smart Mode is ON, a keypad press on
// blind/lighting/fan pauses Smart Mode actuation for this long so the
// manual change sticks ("last change wins" — same rule as keypad vs
// dashboard). After the window Smart Mode resumes driving from sensors.
#define SMART_MANUAL_OVERRIDE_MS 60000

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