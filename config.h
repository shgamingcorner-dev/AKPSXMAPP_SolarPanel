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
#define MAIN_LIGHT_PIN    PC_9
#define MOTOR_PIN         PA_7
#define FAN_SERVO_PIN     PA_1
#define DOOR_LOCK_PIN     PC_6
#define BUZZER_PIN        PB_14     // changed: was PA_2 (PA_2 conflicts with RST_PIN)

// Motor / Servo Timings
#define WAIT_TIME_MS_0        2000
#define PERIOD_WIDTH          20
#define PULSE_WIDTH_90_DEGREE   2400
#define PULSE_WIDTH_0_DEGREE    1500
#define PULSE_WIDTH_N_90_DEGREE 600


// Fan Servo (360° continuous rotation)
#define FAN_SERVO_NEUTRAL_US     1500
#define FAN_SERVO_MAX_FWD_US     2000
#define FAN_SERVO_MAX_REV_US     1000

// Door Lock
#define DOOR_LOCK_LOCKED       1
#define DOOR_LOCK_UNLOCKED     0

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