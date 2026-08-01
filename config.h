#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Hardware Configuration
// ============================================================

// WiFi
#define WIFI_SSID        "SSID_NAME"
#define WIFI_PASSWORD    "PASSWORD"

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
#define SEND_INTERVAL_MS  15000  // minimum 15s on free tier
#define TS_FIELD_TEMPERATURE   1
#define TS_FIELD_HUMIDITY      2
#define TS_FIELD_CURRENT       3
#define TS_FIELD_RFIDQ         4

// RFID UIDs (change to your own cards/tags)
#define RFID_UID_CARD  "15828045"
#define RFID_UID_TAG   "E09F8E21"

// Hardware Pins
#define RST_PIN           PA_2
#define SS_PIN            PB_2
#define ESP_TX            PC_10
#define ESP_RX            PC_11
#define DHT11_PIN         PC_4
#define CURRENT_SENSOR_PIN PA_0
#define MAIN_LIGHT_PIN    PC_2
#define MOTOR_PIN         PA_7        // Curtain/Blind servo (180°): 0°=closed, 90°=open
#define FAN_SERVO_PIN     PA_1        // Fan servo (360° continuous): speed 0-100%
#define DOOR_LOCK_PIN     PC_15        // Door lock (relay or servo): HIGH=locked, LOW=unlocked HAVE TO CHANGE***

// Motor / Servo Timings
#define WAIT_TIME_MS_0        2000  // time for servo to reach position
#define PERIOD_WIDTH          20    // servo period in ms
#define PULSE_WIDTH_90_DEGREE   2400
#define PULSE_WIDTH_0_DEGREE    1500
#define PULSE_WIDTH_N_90_DEGREE 600

// Fan Servo (360° continuous rotation)
// Neutral (stop) = 1500us, Full forward = 2000us, Full reverse = 1000us
#define FAN_SERVO_NEUTRAL_US     1500
#define FAN_SERVO_MAX_FWD_US     2000
#define FAN_SERVO_MAX_REV_US     1000

// Door Lock
#define DOOR_LOCK_LOCKED       1   // HIGH = locked
#define DOOR_LOCK_UNLOCKED     0   // LOW = unlocked

// ACS712 20A Current Sensor (via 10k/15k divider = 0.6 ratio)
// Sensor: 100mV/A at 5V -> 60mV/A at MCU pin (3.3V ADC)
// Zero-current = 2.5V at sensor -> 1.5V at pin
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
#define LCD_STROBE_US           1   // HD44780 needs >450ns, 1us is safe

// LCD Messages
#define MESSAGE_1  "1.Blind 2.Window"
#define MESSAGE_2  "3.Lighting 4.Fans "
#define MESSAGE_3  "Invalid try again"
#define MESSAGE_4  "4.Fan 5.Spd 6.Lock"
#define MESSAGE_5  "Fan: Off"
#define MESSAGE_6  "Lock: Locked"

#endif // CONFIG_H