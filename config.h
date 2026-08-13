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
#define TS_FIELD_BATTERY       5

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
// LDR light sensor — BARE LDR + resistor divider (no module):
//   LDR_PIN (PA_4 = ADC1_IN4) reads the divider midpoint, 0..3.3V -> read() 0.0..1.0.
//   NOT PA_0 -- PA_0 is the ACS712 current sensor (keep untouched).
//   Wiring: 3.3V -> LDR -> PA_4 -> R(10k) -> GND  (LDR on top, R to GND).
//   Brighter light -> LDR resistance drops -> voltage at PA_4 rises -> higher %.
//   NO DO pin: with a bare LDR there is no comparator output; "dark" is
//   derived from the analog reading (see tracker_is_dark()).
//   VCC -> 3.3V, GND -> GND.
#define LDR_PIN           PA_4

// Phase 1: time-driven pulley cycle (exact Arduino sketch timings)
#define TRACKER_STOP_DUTY      0.075f   // neutral/stop (1500us equivalent)
#define TRACKER_FWD_DUTY       0.100f   // full speed one direction (nominal; user testing 0.100)
#define TRACKER_REV_DUTY       0.050f   // full speed other direction (1.0ms; below 0.050 is <1ms, out of range)
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

// ============================================================
// Sun Tracker mode (astronomical, time + location based)
// ============================================================
// TRACKER_MODE selects how the solar panel aims:
//   0 = LDR sweep-and-hold (existing behavior, sensor based)
//   1 = SUN mode (astronomical: compute sun azimuth from clock + SG location)
// In SUN mode the motor is driven to the position matching the sun's current
// azimuth (mapped to the same 0..1900ms position space as the LDR sweep),
// re-aiming every SUN_UPDATE_MS. At night (sun below horizon) it parks at
// SUN_NIGHT_PARK_POS. Time comes from the relay /time endpoint (fetched at
// boot + every SUN_TIME_REFRESH_MS by the network thread); if no time is
// available it stays parked (safe default, no random motion).
#define TRACKER_MODE             0       // 0 = LDR sweep, 1 = SUN (astronomical)

// Singapore location (WGS84) + timezone
#define SUN_LATITUDE             1.35f   // deg N
#define SUN_LONGITUDE            103.82f // deg E
#define SUN_TIMEZONE_UTC_OFFSET  8       // UTC+8 (Singapore, no DST)

// Azimuth -> motor position mapping (same position space as the LDR sweep).
// The panel physically sweeps from home (morning/east end) to max
// (afternoon/west end). Sun azimuth: east=90, south=180, west=270 (0=north).
// CALIBRATE these to your rig: set SUN_POS_EAST = motor position when the
// panel faces due east, SUN_POS_WEST = motor position when it faces due
// west (max physical travel = TRACKER_MS_TO_FLAT + TRACKER_MS_FWD_MAX = 5500).
#define SUN_POS_EAST             0       // motor pos at azimuth 90 (east)
#define SUN_POS_WEST             5500    // motor pos at azimuth 270 (west)
#define SUN_POS_TOTAL            5500    // SUN_POS_WEST - SUN_POS_EAST (span)

// Cadence + night behavior
#define SUN_UPDATE_MS            300000  // re-aim every 5 min (sun moves ~1.25°/min)
#define SUN_TIME_REFRESH_MS      600000  // re-fetch epoch time every 10 min
#define SUN_NIGHT_PARK_POS       0       // parked position at night (home)

// Elevation gate: sun must be above this altitude for SUN mode to track
// (prevents chasing the sun below the horizon at dawn/dusk).
#define SUN_MIN_ELEVATION        3.0f    // deg

// ============================================================
// SOLAR CALIBRATION TEST MODE (SolarBugFixes branch)
// ============================================================
// Set SOLAR_TEST_MODE 1 to run the motor calibration test at boot:
//   Test (TRAVEL): drives the TRACKER motor (PB_0) forward from home and
//                    prints a progress tick every 1000ms so you can read off
//                    the east->west travel time on your pulley rig.
// After you report the results, set back to 0 for normal operation.
#define SOLAR_TEST_MODE          1
#define SOLAR_TEST_TRAVEL_MS     20000   // Test 2: max forward run to find travel time
#define SOLAR_TEST_HOME_MS       3000    // Test 2: reverse-to-home settle time first

// ============================================================
// Simulated solar battery (ThingSpeak field5, 0-100%)
// ============================================================
// A virtual battery that charges when "sunlight" is detected and drains
// otherwise (-1%/tick). Purely for demo — no physical battery. The charge
// source is a COMPILE-TIME toggle (reflash to switch), same pattern as
// TRACKER_MODE:
//   BATTERY_SOURCE 0 = LDR (bright LDR = charging) — works now, no panel
//   BATTERY_SOURCE 1 = ACS712 current (|A| >= threshold = charging) — real
//                      once the solar panel is wired in series with the sensor
#define BATTERY_START_PCT       50      // boot value (0-100)
#define BATTERY_CHARGE_STEP     1       // % gained per tick while charging
#define BATTERY_DRAIN_STEP      1       // % lost per tick while NOT charging
#define BATTERY_TICK_MS         15000   // same cadence as the ThingSpeak send
#define BATTERY_SOURCE          0       // 0 = LDR, 1 = ACS712 current
#define BATTERY_LDR_CHARGE_MIN  50.0f   // LDR% >= this = "sun out" -> charging
#define BATTERY_ACS712_MIN_A    0.5f    // |current| >= this = charging (above 20A-module noise floor)

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
// Tuned 2026-08-09 (2nd): real finger-cover on the bare LDR only drops it to
// ~47-51% (from the logs), so DARK must be ABOVE that (~50) for a finger test
// to close the blinds. BRIGHT stays above DARK (~60) so flashing (~71%+) re-opens.
#define SMART_BLIND_BRIGHT_LDR   60.0f   // LDR% >= -> blinds UP (open)
#define SMART_BLIND_DARK_LDR     50.0f   // LDR% <= -> blinds DOWN (closed)

// Smart Mode update cadence (ms). Re-evaluates lighting/fan/blinds.
// Every 5s is fine for this codebase (main loop is non-blocking ~10ms).
#define SMART_UPDATE_MS          5000

// Manual override window (ms). While Smart Mode is ON, a keypad press on
// blind/lighting/fan pauses Smart Mode actuation for this long so the
// manual change sticks ("last change wins" — same rule as keypad vs
// dashboard). After the window Smart Mode resumes driving from sensors.
#define SMART_MANUAL_OVERRIDE_MS 60000

// ACS712 20A Current Sensor — powered from 5V (module VCC pin).
// Zero-current output = VCC/2 = 2.5V nominal. Sensitivity 100mV/A (20A variant).
// CALIBRATED 2026-08-09: this module's actual zero measures ~2.6V with the
// light OFF (module tolerance), not the textbook 2.5V. If you re-zero it,
// read the sensor output with NO load and update ACS712_ZERO_V.
#define ACS712_SENSITIVITY_V_PER_A  0.100f
#define ACS712_ZERO_V               2.6f
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