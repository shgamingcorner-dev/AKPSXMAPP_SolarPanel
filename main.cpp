#undef __ARM_FP

//Libraries
#include "mbed.h"
#include "MFRC522.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <new>
#include <atomic>
#include "DHT11.h"
#include "lcd.h"
#include "keypad.h"
#include "config.h"

#define BUF      512
#define RX_BUF   1024

// Helper: replaces deprecated Kernel::get_ms_count() (mbed-os-6.0.0)
static inline uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

static BufferedSerial esp(ESP_TX, ESP_RX, 115200);
static char *g_tx = nullptr;
static char *g_rx = nullptr;
static char tagID[21];

// Hardware: Fan servo (360° continuous rotation) + Door lock (SG90)
static PwmOut fanServo(FAN_SERVO_PIN);
static PwmOut doorLock(DOOR_LOCK_PIN);  // SG90 servo for door lock

MFRC522             mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

PwmOut motor(MOTOR_PIN);
static DigitalOut led_tx(PB_14);
static DigitalOut led_rx(PB_15);
static DigitalOut led_Blue(PC_0);
static DigitalOut led_Red(PB_6);
static DigitalOut led_Green(PC_1);
static DigitalOut DHT11VCC(PB_0);
static AnalogIn   current_sensor(CURRENT_SENSOR_PIN);

// LCD
unsigned char key2, outChar, outChar2, outChar3;
unsigned char passWord[] = {'0', '0', '0', '0'};
char MessageLocked [ ] = "Door Locked              ";
char MessageLocked2 [ ] = "Tagg RFID               ";
char Message1 [ ] = "1.Blind 2.Window              ";
char Message2 [ ] = "3.Lighting 4.Fans             ";
char Message3 [ ] = "Invalid try again             ";
char FanspeedM [ ] = "1:Low 2:Med                  ";
char FanspeedM2 [ ] = "3:High 4:Off                ";

static uint64_t last_lighting_local_change = 0;
static uint64_t last_blind_local_change = 0;
static uint64_t last_fan_local_change = 0;
static uint64_t last_fan_remote_change = 0;
static uint64_t last_door_local_change = 0;

static bool confirmed_lighting = false;
static bool confirmed_blind = false;
static uint8_t confirmed_fan_speed = 0;
static bool confirmed_fan_power = false;
static bool confirmed_door_locked = true;

static Mutex lighting_timestamp_mutex;
static Mutex blind_timestamp_mutex;
static Mutex fan_timestamp_mutex;
static Mutex door_timestamp_mutex;

static Mutex confirmed_lighting_mutex;
static Mutex confirmed_blind_mutex;
static Mutex confirmed_fan_mutex;
static Mutex confirmed_door_mutex;

DHT11 dht11(DHT11_PIN);

//LCD functions
static void lcdmessage(const char *MessageS, int Line)
{
    if (Line == 1)
    {
        lcd_write_cmd(0x80);
        for (int i = 0; i < (int)strlen(MessageS); i++)
        {
            outChar = MessageS[i];
            lcd_write_data(outChar);
        }
    }

    if (Line == 2)
    {
        lcd_write_cmd(0xC0);
        for (int i = 0; i < (int)strlen(MessageS); i++)
        {
            outChar2 = MessageS[i];
            lcd_write_data(outChar2);
        }
    }
}

// ============================================================
// RFID AUTHENTICATION STATE MACHINE
// ============================================================
static Mutex   auth_mutex;
static volatile bool g_keypad_unlocked = false;
static volatile uint64_t g_auth_expiry_time = 0;

#define RFID_UNLOCK_DURATION_MS  16000

static void set_keypad_unlocked(bool unlocked)
{
    auth_mutex.lock();
    g_keypad_unlocked = unlocked;
    if (unlocked) {
        g_auth_expiry_time = now_ms() + RFID_UNLOCK_DURATION_MS;
    } else {
        g_auth_expiry_time = 0;
    }
    auth_mutex.unlock();
}

static bool is_keypad_unlocked(void)
{
    auth_mutex.lock();
    bool unlocked = g_keypad_unlocked;
    uint64_t now = now_ms();
    if (unlocked && now >= g_auth_expiry_time) {
        g_keypad_unlocked = false;
        g_auth_expiry_time = 0;
        unlocked = false;
    }
    auth_mutex.unlock();
    return unlocked;
}

static uint64_t get_auth_remaining_ms(void)
{
    auth_mutex.lock();
    uint64_t expiry = g_auth_expiry_time;
    auth_mutex.unlock();
    uint64_t now = now_ms();
    if (expiry > now) return expiry - now;
    return 0;
}

// ============================================================
// FAN SPEED SELECTION STATE MACHINE (Keypad sub-menu)
// ============================================================
static Mutex   fan_menu_mutex;
static volatile bool g_in_fan_menu = false;
static volatile uint64_t g_fan_menu_expiry = 0;
#define FAN_MENU_TIMEOUT_MS  10000

static void enter_fan_menu(void)
{
    fan_menu_mutex.lock();
    g_in_fan_menu = true;
    g_fan_menu_expiry = now_ms() + FAN_MENU_TIMEOUT_MS;
    fan_menu_mutex.unlock();
    lcdmessage(FanspeedM, 1);
    lcdmessage(FanspeedM2, 2);
}

static void exit_fan_menu(void)
{
    fan_menu_mutex.lock();
    g_in_fan_menu = false;
    g_fan_menu_expiry = 0;
    fan_menu_mutex.unlock();
    // Return to options screen if unlocked
    if (is_keypad_unlocked()) {
        lcdmessage(Message1, 1);
        lcdmessage(Message2, 2);
    } else {
        lcdmessage(MessageLocked, 1);
        lcdmessage(MessageLocked2, 2);
    }
}

static bool is_in_fan_menu(void)
{
    fan_menu_mutex.lock();
    bool in_menu = g_in_fan_menu;
    uint64_t now = now_ms();
    if (in_menu && now >= g_fan_menu_expiry) {
        g_in_fan_menu = false;
        g_fan_menu_expiry = 0;
        in_menu = false;
        // Auto-exit fan menu
        if (is_keypad_unlocked()) {
            lcdmessage(Message1, 1);
            lcdmessage(Message2, 2);
        } else {
            lcdmessage(MessageLocked, 1);
            lcdmessage(MessageLocked2, 2);
        }
    }
    fan_menu_mutex.unlock();
    return in_menu;
}

static void refresh_fan_menu_timeout(void)
{
    fan_menu_mutex.lock();
    if (g_in_fan_menu) {
        g_fan_menu_expiry = now_ms() + FAN_MENU_TIMEOUT_MS;
    }
    fan_menu_mutex.unlock();
}

static uint8_t fan_menu_selection_to_speed(char key)
{
    switch (key) {
        case '1': return 33;   // Low
        case '2': return 66;   // Medium
        case '3': return 100;  // High
        case '4': return 0;    // Off
        default: return 255;   // Invalid
    }
}

// ============================================================
// MAIN LIGHTING PWM (0-100% -> PWM duty cycle)
// ============================================================
// PB_1 = TIM3_CH4 (default remap) - active in NUCLEO_F103RB pinmap.
// NOT PC_9: PC_9 forces TIM3 full remap which silently kills the PA_7
// blind motor (TIM3_CH2 default). Keeping every TIM3 user on default
// remap lets all three channels (PA_6 door, PA_7 motor, PB_1 light) output.
static PwmOut led_mainLighting_pwm(MAIN_LIGHT_PIN);  // PB_1

static Mutex   brightness_mutex;
static volatile uint8_t g_brightness = 100;  // 0-100%

static void set_brightness(uint8_t brightness)
{
    brightness_mutex.lock();
    if (brightness > 100) brightness = 100;
    g_brightness = brightness;
    brightness_mutex.unlock();

    float duty = (float)g_brightness / 100.0f;
    led_mainLighting_pwm.write(duty);
}

static uint8_t get_brightness(void)
{
    brightness_mutex.lock();
    uint8_t b = g_brightness;
    brightness_mutex.unlock();
    return b;
}

static void set_main_lighting(bool on)
{
    if (on) {
        set_brightness(get_brightness());
    } else {
        led_mainLighting_pwm.write(0.0f);
    }
}

static bool get_main_lighting(void)
{
    brightness_mutex.lock();
    bool on = (g_brightness > 0);
    brightness_mutex.unlock();
    return on;
}


// RFID match results shared between threads, protected by a mutex
static Mutex   rfid_mutex;
static volatile int g_latest_rfid = 0;

static void set_latest_rfid(int value)
{
    rfid_mutex.lock();
    g_latest_rfid = value;
    rfid_mutex.unlock();
}

static int get_latest_rfid(void)
{
    rfid_mutex.lock();
    int v = g_latest_rfid;
    rfid_mutex.unlock();
    return v;
}

// Keypad presses happen on the main thread, but all ESP-01 AT traffic
// only ever runs on networkThread -- so key presses just request actions.
static Mutex   device_state_mutex;
static volatile bool pending_blind_toggle = false;
static volatile bool pending_lighting_toggle = false;

static Mutex   blind_actuate_mutex;
static volatile bool pending_blind_actuate = false;
static volatile bool pending_blind_open = false;

static Mutex   fan_mutex;
static volatile uint8_t fan_speed = 0;
static volatile bool fan_power = false;

static Mutex   door_mutex;
static volatile bool door_locked = true;

static Mutex   fan_actuate_mutex;
static volatile bool pending_fan_actuate = false;
static volatile uint8_t pending_fan_speed = 0;
static volatile bool pending_fan_power = false;

// Door lock: mirrors the blind system. Two separate flags avoid a
// double-consume race:
//   toggle  (keypad '2' -> network task): request_door_toggle/consume_pending_door_toggle
//   actuate (network/poll -> main loop):  request_door_actuate/consume_pending_door_actuate
// request_door_toggle stamps last_door_local_change (local source); the poll's
// 15s grace reads it so a keypad push isn't overwritten by a stale remote read.
static Mutex   door_actuate_mutex;
static volatile bool pending_door_actuate = false;
static volatile bool pending_door_locked = false;
static Mutex   door_toggle_mutex;
static volatile bool pending_door_toggle = false;

static void request_door_toggle(void)
{
    door_toggle_mutex.lock();
    pending_door_toggle = true;
    door_toggle_mutex.unlock();

    door_timestamp_mutex.lock();
    last_door_local_change = now_ms();
    door_timestamp_mutex.unlock();
}

static bool consume_pending_door_toggle(void)
{
    door_toggle_mutex.lock();
    bool v = pending_door_toggle;
    pending_door_toggle = false;
    door_toggle_mutex.unlock();
    return v;
}

static void request_door_actuate(bool locked)
{
    door_actuate_mutex.lock();
    pending_door_actuate = true;
    pending_door_locked = locked;
    door_actuate_mutex.unlock();
}

static bool consume_pending_door_actuate(bool *out_locked)
{
    door_actuate_mutex.lock();
    bool v = pending_door_actuate;
    if (v) {
        *out_locked = pending_door_locked;
        pending_door_actuate = false;
    }
    door_actuate_mutex.unlock();
    return v;
}

static void request_fan_actuate(uint8_t speed, bool power, bool from_remote = false)
{
    fan_actuate_mutex.lock();
    pending_fan_actuate = true;
    pending_fan_speed = speed;
    pending_fan_power = power;
    fan_actuate_mutex.unlock();

    if (from_remote) {
        fan_timestamp_mutex.lock();
        last_fan_remote_change = now_ms();
        fan_timestamp_mutex.unlock();
    } else {
        fan_timestamp_mutex.lock();
        last_fan_local_change = now_ms();
        fan_timestamp_mutex.unlock();
    }
}

static bool consume_pending_fan_actuate(uint8_t *out_speed, bool *out_power)
{
    fan_actuate_mutex.lock();
    bool v = pending_fan_actuate;
    if (v) {
        *out_speed = pending_fan_speed;
        *out_power = pending_fan_power;
        pending_fan_actuate = false;
    }
    fan_actuate_mutex.unlock();
    return v;
}

static void set_fan_speed(uint8_t speed)
{
    fan_mutex.lock();
    fan_speed = speed;
    fan_mutex.unlock();

    if (fan_power) {
        // Convert speed (0-100) to duty cycle (0.05 to 0.10 for most servos)
        // 0.075 = neutral (stop), 0.05 = full reverse, 0.10 = full forward
        float duty = 0.075f + (speed - 50) * 0.0005f;
        fanServo.write(duty);
    }
}

static void set_fan_power(bool on)
{
    fan_mutex.lock();
    fan_power = on;
    fan_mutex.unlock();

    if (on) {
        set_fan_speed(fan_speed);
    } else {
        // OFF = neutral position (stop)
        fanServo.write(0.075f);  // 7.5% duty cycle = neutral for most servos
    }
}

static void set_door_lock(bool locked)
{
    printf("[DOOR] set_door_lock called with locked=%d\n", locked);

    door_mutex.lock();
    door_locked = locked;
    door_mutex.unlock();

    // SG90 servo control with pulse widths
    uint16_t pulse = locked ? PULSE_WIDTH_0_DEGREE : PULSE_WIDTH_180_DEGREE;
    printf("[DOOR] Setting pulse to %dus (%s)\n", pulse, locked ? "LOCKED" : "UNLOCKED");
    doorLock.pulsewidth_us(pulse);
    thread_sleep_for(500);  // Wait for movement
}

// Diagnostic sweep: verifies the PA_6 door servo hardware end-to-end.
// REMOVE this call from main() once the servo is confirmed moving.
static void test_door_servo(void)
{
    printf("\n=== TESTING DOOR SERVO (PA_6) ===\n");
    printf("LOCKED (600us)...\n");
    doorLock.pulsewidth_us(600);
    thread_sleep_for(2000);
    printf("UNLOCKED (2400us)...\n");
    doorLock.pulsewidth_us(2400);
    thread_sleep_for(2000);
    printf("LOCKED (600us)...\n");
    doorLock.pulsewidth_us(600);
    thread_sleep_for(2000);
    printf("=== DOOR SERVO TEST COMPLETE ===\n");
}

static uint8_t get_fan_speed(void)
{
    fan_mutex.lock();
    uint8_t v = fan_speed;
    fan_mutex.unlock();
    return v;
}

static bool get_fan_power(void)
{
    fan_mutex.lock();
    bool v = fan_power;
    fan_mutex.unlock();
    return v;
}

static bool get_door_locked(void)
{
    door_mutex.lock();
    bool v = door_locked;
    door_mutex.unlock();
    return v;
}

static void request_blind_toggle(void)
{
    device_state_mutex.lock();
    pending_blind_toggle = true;
    device_state_mutex.unlock();

    blind_timestamp_mutex.lock();
    last_blind_local_change = now_ms();
    blind_timestamp_mutex.unlock();
}

static void request_lighting_toggle(void)
{
    device_state_mutex.lock();
    pending_lighting_toggle = true;
    device_state_mutex.unlock();

    lighting_timestamp_mutex.lock();
    last_lighting_local_change = now_ms();
    lighting_timestamp_mutex.unlock();
}

static bool consume_pending_blind_toggle(void)
{
    device_state_mutex.lock();
    bool v = pending_blind_toggle;
    pending_blind_toggle = false;
    device_state_mutex.unlock();
    return v;
}

static void request_blind_actuate(bool open)
{
    blind_actuate_mutex.lock();
    pending_blind_actuate = true;
    pending_blind_open = open;
    blind_actuate_mutex.unlock();
}

static bool consume_pending_blind_actuate(bool *out_open)
{
    blind_actuate_mutex.lock();
    bool v = pending_blind_actuate;
    if (v) {
        *out_open = pending_blind_open;
        pending_blind_actuate = false;
    }
    blind_actuate_mutex.unlock();
    return v;
}

static bool consume_pending_lighting_toggle(void)
{
    device_state_mutex.lock();
    bool v = pending_lighting_toggle;
    pending_lighting_toggle = false;
    device_state_mutex.unlock();
    return v;
}

static void fmt_float(char *out, int out_sz, float v)
{
    bool neg = v < 0.0f;
    float abs_v = neg ? -v : v;
    int whole = (int)abs_v;
    int frac  = (int)((abs_v - (float)whole) * 10.0f);
    snprintf(out, out_sz, "%s%d.%d", neg ? "-" : "", whole, frac);
}

//  RFID  (runs on the main thread)

static bool rfid_readID(void)
{
    char HexString[3];
    uint8_t uid_size = mfrc522.uid.size;
    for (uint8_t i = 0; i < uid_size && i < 10; i++) {
        sprintf(HexString, "%02X", mfrc522.uid.uidByte[i]);
        tagID[2*i]   = HexString[0];
        tagID[2*i+1] = HexString[1];
    }
    tagID[2*uid_size] = '\0';
    return true;
}

static int read_RFID(void)
{
    if (!mfrc522.PICC_IsNewCardPresent()) return 0;
    if (!mfrc522.PICC_ReadCardSerial())   return 0;
    if (!rfid_readID())                   return 0;

    if (memcmp(tagID, RFID_UID_CARD, 8) == 0) {
        printf("[RFID] Card matched! UID: %s\n", tagID);
        return 1;
    }
    if (memcmp(tagID, RFID_UID_TAG, 8) == 0) {
        printf("[RFID] Tag matched! UID: %s\n", tagID);
        return 2;
    }

    printf("[RFID] No match. UID: %s\n", tagID);
    return 0;
}

//  SENSOR FUNCTIONS

static int g_dht_temperature = 2634;
static int g_dht_humidity = 4001;

static void read_dht11(void)
{
    DHT11VCC = 1;
    int temperature = 0, humidity = 0;
    int error = dht11.readTemperatureHumidity(temperature, humidity);
    if (error == 0)
    {
        printf("Temperature: %d C\n", temperature);
        printf("humidity: %d %%\n", humidity);
        g_dht_temperature = temperature;
        g_dht_humidity = humidity;
    }
    else
    {
        printf("%s\n", dht11.getErrorString(error));
    }
    DHT11VCC = 0;
}

static float read_temperature(void) { return g_dht_temperature; }
static float read_humidity(void)    { return g_dht_humidity; }

static float read_current(void)
{
    const int samples = 20;
    float sum = 0.0f;
    for (int i = 0; i < samples; i++) {
        sum += current_sensor.read();
        wait_us(100);
    }
    float pin_voltage = (sum / samples) * ADC_VREF;
    float current = (pin_voltage - ACS712_ZERO_V) / ACS712_SENSITIVITY_V_PER_A;

    char cur_s[16], volt_s[16];
    fmt_float(cur_s, sizeof(cur_s), current);
    fmt_float(volt_s, sizeof(volt_s), pin_voltage);
    printf("Current: %s A (pin=%sV)\n", cur_s, volt_s);
    return current;
}

//Motor functions

static float motor_init(void)
{
    motor.period_ms(PERIOD_WIDTH);
    motor.pulsewidth_us(PULSE_WIDTH_0_DEGREE);
    printf("[MOTOR] Initialized to %dus (CLOSED/0°)\n", PULSE_WIDTH_0_DEGREE);
    thread_sleep_for(WAIT_TIME_MS_0);
    return 0.0f;
}

//  THINGSPEAK FIELD TABLE

typedef struct {
    int        field;
    char       value[12];
    const char *label;
} ts_field_t;

#define TS_NUM_FIELDS 4
static ts_field_t ts_fields[TS_NUM_FIELDS] = {
    { TS_FIELD_TEMPERATURE, "", "Temperature" },
    { TS_FIELD_HUMIDITY,    "", "Humidity"    },
    { TS_FIELD_CURRENT,     "", "Current"     },
    { TS_FIELD_RFIDQ,       "", "RFID"        },
};

//  ESP-01 LOW-LEVEL  (all called only from the network task)

static void esp_drain(void)
{
    char scratch[64];
    int count;
    do {
        count = 0;
        while (esp.readable()) {
            int r = esp.read(scratch, sizeof(scratch));
            if (r <= 0) break;
            count += r;
        }
    } while (count > 0);
}

static void esp_send(const char *cmd)
{
    esp_drain();
    esp.write(cmd, strlen(cmd));
    led_tx = !led_tx;
}

static int esp_read(int wait_ms = 1000, const char *stop1 = NULL, const char *stop2 = NULL) {
      uint32_t start = now_ms();
      int n = 0;
      while (now_ms() - start < (uint32_t)wait_ms) {
          if (esp.readable()) {
              int chunk = esp.read(g_rx + n, RX_BUF - 1 - n);
              if (chunk > 0) {
                  n += chunk;
                  if (n >= (int)(RX_BUF - 1)) break;
                  g_rx[n] = '\0';
                  if ((stop1 && strstr(g_rx, stop1)) || (stop2 && strstr(g_rx, stop2))) {
                      break;
                  }
              }
          }
          thread_sleep_for(5);
      }

      if (n > 0) {
          g_rx[n] = '\0';
          led_rx = !led_rx;
          printf("[ESP] %s\n", g_rx);
      }
      return n;
  }

static int at(const char *cmd, int wait_ms = 1000, const char *stop1 = NULL, const char *stop2 = NULL)
{
    printf(">> %s", cmd);
    esp_send(cmd);
    return esp_read(wait_ms, stop1, stop2);
}

static bool esp_close(int id)
{
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d\r\n", id);
    return at(cmd, 500, "OK", "ERROR") > 0;
}

static bool esp_open_tcp(int id, const char *host, const char *ip_fallback, int port, const char *tag)
{
    snprintf(g_tx, BUF,
        "AT+CIPSTART=%d,\"TCP\",\"%s\",%d\r\n", id, host, port);
    at(g_tx, 8000, ",CONNECT", "ERROR");
    if (strstr(g_rx, ",CONNECT")) {
        return true;
    }

    printf("%s TCP open failed (%s)\n", tag, host);
    esp_close(id);

    if (ip_fallback == NULL) {
        return false;
    }

    printf("%s retrying via literal IP %s -- bypasses ESP DNS\n", tag, ip_fallback);
    snprintf(g_tx, BUF,
        "AT+CIPSTART=%d,\"TCP\",\"%s\",%d\r\n", id, ip_fallback, port);
    at(g_tx, 8000, ",CONNECT", "ERROR");
    if (strstr(g_rx, ",CONNECT")) {
        printf("%s connected via IP -- hostname lookup is what is failing\n", tag);
        return true;
    }

    printf("%s IP retry failed too -- not a DNS problem\n", tag);
    esp_close(id);
    return false;
}

static void urlencode(char *dst, int dst_sz, const char *src)
{
    int j = 0;
    for (int i = 0; src[i] != '\0' && j < dst_sz - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') {
            dst[j++] = c;
        } else if (c == ' ') {
            dst[j++] = '+';
        } else {
            snprintf(dst + j, 4, "%%%02X", c);
            j += 3;
        }
    }
    dst[j] = '\0';
}

//  THINGSPEAK SENDER

static bool send_to_thingspeak(void)
{
    if (!esp_open_tcp(0, TS_HOST, NULL, TS_PORT, "[TS]")) {
        return false;
    }

    char query[BUF] = {0};
    snprintf(query, sizeof(query), "GET /update?api_key=%s", TS_API_KEY);
    for (int i = 0; i < TS_NUM_FIELDS; i++) {
        if (ts_fields[i].field == 0) continue;
        char part[32];
        snprintf(part, sizeof(part), "&field%d=%s",
                 ts_fields[i].field, ts_fields[i].value);
        strncat(query, part, sizeof(query) - strlen(query) - 1);
    }
    strncat(query, " HTTP/1.1\r\nHost: " TS_HOST "\r\nConnection: close\r\n\r\n",
            sizeof(query) - strlen(query) - 1);

    int req_len = strlen(query);

    snprintf(g_tx, BUF, "AT+CIPSEND=0,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[TS] No > prompt\n");
            at("AT+CIPCLOSE=0\r\n", 1000, "OK", "ERROR");
            return false;
        }
    }

    printf("[TS] Sending: %s\n", query);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) {
        at("AT+CIPCLOSE=0\r\n", 1000, "OK", "ERROR");
        return false;
    }

    if (strstr(g_rx, "SEND OK") || strstr(g_rx, "200 OK")) {
        printf("[TS] Upload OK\n");
    } else {
        printf("[TS] Unexpected response — check API key / rate limit\n");
    }

    at("AT+CIPCLOSE=0\r\n", 1000, "OK", "ERROR");
    return (strstr(g_rx, "SEND OK") != NULL) || (strstr(g_rx, "200 OK") != NULL);
}

//  TELEGRAM SENDER (via the HTTPS relay)

static bool send_telegram_via_relay(const char *message)
{
    if (!esp_open_tcp(1, RELAY_HOST, RELAY_IP, RELAY_PORT, "[TG]")) {
        return false;
    }

    char encoded_text[128];
    urlencode(encoded_text, sizeof(encoded_text), message);

    char query[BUF];
    snprintf(query, sizeof(query),
        "GET /telegram?text=%s&secret=%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n\r\n",
        encoded_text, RELAY_SECRET, RELAY_HOST);

    int req_len = strlen(query);

    snprintf(g_tx, BUF, "AT+CIPSEND=1,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[TG] No > prompt\n");
            at("AT+CIPCLOSE=1\r\n", 500, "OK", "ERROR");
            return false;
        }
    }

    printf("[TG] Sending: %s\n", query);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) {
        at("AT+CIPCLOSE=1\r\n", 500, "OK", "ERROR");
        return false;
    }

    if (strstr(g_rx, "200 OK")) {
        printf("[TG] Message sent OK\n");
    } else {
        printf("[TG] Unexpected response — check relay logs / RELAY_SECRET\n");
    }

    at("AT+CIPCLOSE=1\r\n", 500, "OK", "ERROR");
    return strstr(g_rx, "200 OK") != NULL;
}

static std::atomic<uint32_t> g_seq{0};

//SUPABASE
static bool send_sensor_telemetry_via_relay(float temperature, float humidity, float power)
{
    char temp_s[16], hum_s[16], pow_s[16];
    fmt_float(temp_s, sizeof(temp_s), temperature);
    fmt_float(hum_s,  sizeof(hum_s),  humidity);
    fmt_float(pow_s,  sizeof(pow_s),  power);

    // NOTE: telemetry carries SENSOR data only. Device states (door_locked,
    // fan_power, fan_speed) must NOT be sent here -- pushing local states every
    // 15s was overwriting remote (dashboard) changes in Supabase, causing the
    // door lock to "revert" ~15s after being changed remotely. Device states
    // are pushed only on change in network_task().
    char body[BUF];
    snprintf(body, sizeof(body),
        "secret=%s&temperature=%s&humidity=%s&power=%s&seq=%lu",
        RELAY_SECRET, temp_s, hum_s, pow_s,
        (unsigned long)g_seq++);
    int body_len = strlen(body);

    if (!esp_open_tcp(2, RELAY_HOST, RELAY_IP, RELAY_PORT, "[SB]")) {
        return false;
    }

    char query[BUF];
    snprintf(query, sizeof(query),
        "POST /sensor-telemetry HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        RELAY_HOST, body_len, body);

    int req_len = strlen(query);

    snprintf(g_tx, BUF, "AT+CIPSEND=2,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[SB] No > prompt\n");
            at("AT+CIPCLOSE=2\r\n", 500, "OK", "ERROR");
            return false;
        }
    }

    printf("[SB] Sending telemetry: %s\n", body);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) {
        at("AT+CIPCLOSE=2\r\n", 500, "OK", "ERROR");
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[SB] Telemetry logged OK\n" : "[SB] Unexpected response — check relay logs\n");

    at("AT+CIPCLOSE=2\r\n", 500, "OK", "ERROR");
    return ok;
}

static bool send_alert_log_via_relay(const char *level, const char *message, const char *category)
{
    char encoded_msg[128];
    urlencode(encoded_msg, sizeof(encoded_msg), message);

    char body[BUF];
    snprintf(body, sizeof(body),
        "secret=%s&level=%s&message=%s&category=%s&seq=%lu",
        RELAY_SECRET, level, encoded_msg, category, (unsigned long)g_seq++);
    int body_len = strlen(body);

    if (!esp_open_tcp(3, RELAY_HOST, RELAY_IP, RELAY_PORT, "[SB]")) {
        return false;
    }

    char query[BUF];
    snprintf(query, sizeof(query),
        "POST /alert-log HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        RELAY_HOST, body_len, body);

    int req_len = strlen(query);

    snprintf(g_tx, BUF, "AT+CIPSEND=3,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[SB] No > prompt\n");
            at("AT+CIPCLOSE=3\r\n", 500, "OK", "ERROR");
            return false;
        }
    }

    printf("[SB] Sending alert: %s\n", body);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) {
        at("AT+CIPCLOSE=3\r\n", 500, "OK", "ERROR");
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[SB] Alert logged OK\n" : "[SB] Unexpected response — check relay logs\n");

    at("AT+CIPCLOSE=3\r\n", 500, "OK", "ERROR");
    return ok;
}

//  DEVICE STATE PUSH

static bool send_device_state_via_relay(const char *field, bool value)
{
    char body[BUF];
    snprintf(body, sizeof(body),
        "secret=%s&field=%s&value=%s",
        RELAY_SECRET, field, value ? "true" : "false");
    int body_len = strlen(body);

    if (!esp_open_tcp(4, RELAY_HOST, RELAY_IP, RELAY_PORT, "[DS]")) {
        return false;
    }

    char query[BUF];
    snprintf(query, sizeof(query),
        "POST /device-state HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        RELAY_HOST, body_len, body);

    int req_len = strlen(query);

    snprintf(g_tx, BUF, "AT+CIPSEND=4,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[DS] No > prompt\n");
            at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
            return false;
        }
    }

    printf("[DS] Pushing %s=%s\n", field, value ? "true" : "false");
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) {
        at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[DS] Push OK\n" : "[DS] Unexpected response — check relay logs\n");

    if (ok) {
        if (strcmp(field, "main_lighting") == 0) {
            confirmed_lighting_mutex.lock();
            confirmed_lighting = value;
            confirmed_lighting_mutex.unlock();
        } else if (strcmp(field, "blind") == 0) {
            confirmed_blind_mutex.lock();
            confirmed_blind = value;
            confirmed_blind_mutex.unlock();
        } else if (strcmp(field, "fan_power") == 0) {
            confirmed_fan_mutex.lock();
            confirmed_fan_power = value;
            confirmed_fan_mutex.unlock();
        } else if (strcmp(field, "smart_lock") == 0) {
            confirmed_door_mutex.lock();
            confirmed_door_locked = value;
            confirmed_door_mutex.unlock();
        }
    }

    at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
    return ok;
}

//  NEW: Send integer state to relay (for fan_speed)
static bool send_device_state_int_via_relay(const char *field, int value)
{
    char body[BUF];
    snprintf(body, sizeof(body),
        "secret=%s&field=%s&value=%d",
        RELAY_SECRET, field, value);
    int body_len = strlen(body);

    if (!esp_open_tcp(4, RELAY_HOST, RELAY_IP, RELAY_PORT, "[DS]")) {
        return false;
    }

    char query[BUF];
    snprintf(query, sizeof(query),
        "POST /device-state HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        RELAY_HOST, body_len, body);

    int req_len = strlen(query);

    snprintf(g_tx, BUF, "AT+CIPSEND=4,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[DS] No > prompt\n");
            at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
            return false;
        }
    }

    printf("[DS] Pushing %s=%d\n", field, value);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) {
        at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[DS] Push OK\n" : "[DS] Unexpected response — check relay logs\n");
    at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
    return ok;
}

//  DEVICE STATE POLL

static bool last_main_lighting = false;
static bool main_lighting_known = false;
static bool last_blind_open = false;
static bool blind_known = false;
static uint8_t last_fan_speed = 0;
static bool last_fan_power = false;
static bool last_door_locked = true;
static bool door_locked_known = false;

static void apply_main_lighting(bool on)
{
    if (on) {
        set_brightness(get_brightness());
    } else {
        led_mainLighting_pwm.write(0.0f);
    }
    last_main_lighting = on;
    main_lighting_known = true;
    printf("[DS] mainLighting -> %s\n", on ? "ON" : "OFF");
}

static void apply_blind(bool open)
{
    printf("[DS] apply_blind called with open=%d\n", open);

    // Direct servo control with correct SG90 pulse values:
    // OPEN = 180° (2400us), CLOSED = 0° (600us)
    uint16_t pulse = open ? PULSE_WIDTH_180_DEGREE : PULSE_WIDTH_0_DEGREE;
    printf("[DS] Blind: setting pulse to %dus (%s)\n", pulse, open ? "OPEN" : "CLOSED");

    motor.pulsewidth_us(pulse);
    thread_sleep_for(1000);  // Wait for movement

    last_blind_open = open;
    blind_known = true;
    printf("[DS] blind -> %s\n", open ? "OPEN" : "CLOSED");
}

static void apply_fan(uint8_t speed, bool on)
{
    printf("\n=== FAN APPLY ===\n");
    printf("Requested: speed=%u, on=%d\n", speed, on);
    
    // Get timestamps to determine which is most recent
    fan_timestamp_mutex.lock();
    uint64_t local_time = last_fan_local_change;
    uint64_t remote_time = last_fan_remote_change;
    fan_timestamp_mutex.unlock();
    
    bool remote_is_newer = (remote_time > local_time);
    printf("local_time=%llu, remote_time=%llu, remote_is_newer=%d\n", local_time, remote_time, remote_is_newer);
    
    uint8_t final_speed;
    bool final_power;
    
    if (remote_is_newer) {
        final_speed = speed;
        final_power = on;
        printf("Using REMOTE state\n");
    } else {
        fan_mutex.lock();
        final_speed = fan_speed;
        final_power = fan_power;
        fan_mutex.unlock();
        printf("Keeping LOCAL state\n");
    }
    
    printf("Final: speed=%u, on=%d\n", final_speed, final_power);
    
    // Update state
    fan_mutex.lock();
    fan_speed = final_speed;
    fan_power = final_power;
    fan_mutex.unlock();

    // CONTROL THE SERVO USING PWM write() INSTEAD OF pulsewidth_us()
    if (final_power && final_speed > 0) {
        // Convert speed (0-100) to duty cycle
        // For most servos: 0.05 = full reverse, 0.075 = stop, 0.10 = full forward
        // Map speed 0-100 to duty cycle 0.05-0.10
        float duty = 0.050f + (final_speed * 0.0005f);
        // Clamp to safe range
        if (duty < 0.05f) duty = 0.05f;
        if (duty > 0.10f) duty = 0.10f;
        fanServo.write(duty);
        printf("Fan ON - speed:%u%%, duty:%.3f\n", final_speed, duty);
    } else {
        // OFF - send neutral (stop)
        fanServo.write(0.075f);  // 7.5% duty cycle = neutral/stop for most servos
        printf("Fan OFF - neutral duty:0.075\n");
    }
    
    printf("=== END FAN APPLY ===\n\n");
    
    last_fan_speed = final_speed;
    last_fan_power = final_power;
}

static void apply_door_lock(bool locked)
{
    printf("[DS] apply_door_lock called with locked=%d\n", locked);

    door_mutex.lock();
    door_locked = locked;
    door_mutex.unlock();

    // Direct servo control with correct SG90 pulse values:
    // LOCKED = 0° (600us), UNLOCKED = 180° (2400us)
    uint16_t pulse = locked ? PULSE_WIDTH_0_DEGREE : PULSE_WIDTH_180_DEGREE;
    printf("[DS] Door: setting pulse to %dus (%s)\n", pulse, locked ? "LOCKED" : "UNLOCKED");
    doorLock.pulsewidth_us(pulse);
    thread_sleep_for(500);  // Wait for movement

    last_door_locked = locked;
    door_locked_known = true;
    printf("[DS] door -> %s\n", locked ? "LOCKED" : "UNLOCKED");
}

static bool poll_device_state_via_relay(void)
{
    if (!esp_open_tcp(4, RELAY_HOST, RELAY_IP, RELAY_PORT, "[DS]")) {
        return false;
    }

    char query[BUF];
    snprintf(query, sizeof(query),
        "GET /device-state?secret=%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n\r\n",
        RELAY_SECRET, RELAY_HOST);

    int req_len = strlen(query);

    snprintf(g_tx, BUF, "AT+CIPSEND=4,%d\r\n", req_len);
    at(g_tx, 1000, ">", "ERROR");
    if (!strstr(g_rx, ">")) {
        esp_read(1000, ">", "ERROR");
        if (!strstr(g_rx, ">")) {
            printf("[DS] No > prompt\n");
            at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
            return false;
        }
    }

    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) {
        at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    if (ok) {
        bool main_lighting = strstr(g_rx, "\"main_lighting\":true") != NULL
                           || strstr(g_rx, "\"main_lighting\": true") != NULL;

        uint8_t brightness_state = 100;
        char *brightness_ptr = strstr(g_rx, "\"lighting_brightness\":");
        if (brightness_ptr) {
            char *num_start = brightness_ptr + strlen("\"lighting_brightness\":");
            brightness_state = (uint8_t)atoi(num_start);
        }

        uint64_t now = now_ms();
        lighting_timestamp_mutex.lock();
        bool lighting_recent = (now - last_lighting_local_change <= 15000);
        lighting_timestamp_mutex.unlock();

        if (!main_lighting_known || main_lighting != last_main_lighting) {
            if (!lighting_recent) {
                if (main_lighting) {
                    set_brightness(brightness_state);
                }
                apply_main_lighting(main_lighting);
            } else {
                printf("[DS] Ignoring remote main_lighting (local change < 15s ago)\n");
            }
        }

        bool blind = strstr(g_rx, "\"blind\":true") != NULL
                   || strstr(g_rx, "\"blind\": true") != NULL;

        if (!blind_known || blind != last_blind_open) {
            blind_timestamp_mutex.lock();
            bool blind_recent = (now_ms() - last_blind_local_change <= 15000);
            blind_timestamp_mutex.unlock();

            if (!blind_recent) {
                request_blind_actuate(blind);
            } else {
                printf("[DS] Ignoring remote blind (local change < 15s ago)\n");
            }
        }

        // FAN POLLING
        bool fan_power_state = strstr(g_rx, "\"fan_power\":true") != NULL
                            || strstr(g_rx, "\"fan_power\": true") != NULL;
        uint8_t fan_speed_state = 0;
        char *fan_speed_ptr = strstr(g_rx, "\"fan_speed\":");
        if (fan_speed_ptr) {
            char *num_start = fan_speed_ptr + strlen("\"fan_speed\":");
            fan_speed_state = (uint8_t)atoi(num_start);
        }

        uint8_t current_speed = get_fan_speed();
        bool current_power = get_fan_power();

        fan_timestamp_mutex.lock();
        bool local_recent = (now_ms() - last_fan_local_change <= 15000);
        fan_timestamp_mutex.unlock();

        if ((fan_power_state != current_power || fan_speed_state != current_speed) && !local_recent) {
            fan_timestamp_mutex.lock();
            last_fan_remote_change = now_ms();
            fan_timestamp_mutex.unlock();
            
            request_fan_actuate(fan_speed_state, fan_power_state, true);
            printf("[DS] Applied remote fan state: %s, %u%%\n", 
                   fan_power_state ? "ON" : "OFF", fan_speed_state);
        } else if (local_recent && (fan_power_state != current_power || fan_speed_state != current_speed)) {
            printf("[DS] Local fan change recent - pushing local state to relay\n");
            send_device_state_via_relay("fan_power", current_power);
            send_device_state_int_via_relay("fan_speed", current_speed);
        }

        // Door lock: mirrors the blind system. Poll smart_lock -- the WEBSITE
        // writes smart_lock, and the relay does NOT alias it to door_locked
        // anymore (verified live: POST field=smart_lock leaves door_locked
        // unchanged). Polling the stale door_locked column made the firmware
        // "unlock" the door against the website within one poll cycle.
        // The 15s grace below protects a fresh keypad '2' push from being
        // overwritten by a stale remote read while its push is in flight.
        bool door_locked_state = strstr(g_rx, "\"smart_lock\":true") != NULL
                              || strstr(g_rx, "\"smart_lock\": true") != NULL;

        printf("[DS] Door poll: relay says %s, local is %s\n",
               door_locked_state ? "LOCKED (true)" : "UNLOCKED (false)",
               get_door_locked() ? "LOCKED (true)" : "UNLOCKED (false)");

        if (door_locked_state != get_door_locked() || !door_locked_known) {
            // Grace: if the keypad just toggled the door (and its push may
            // still be in flight over the 3-8s AT link), don't apply a stale
            // remote read on top of it.
            door_timestamp_mutex.lock();
            bool door_recent = (now_ms() - last_door_local_change <= 15000);
            door_timestamp_mutex.unlock();

            if (!door_recent) {
                printf("[DS] Applying door state from relay: %s\n",
                       door_locked_state ? "LOCKED" : "UNLOCKED");
                request_door_actuate(door_locked_state);
            } else {
                printf("[DS] Door poll: local change < 15s ago - not applying remote\n");
            }
        }
    } else {
        printf("[DS] Unexpected response — check relay logs\n");
    }

    at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
    return ok;
}

//  ESP-01 INIT

static bool wifi_connected = false;

static void esp_init(void)
{
    printf("=== ESP-01 init ===\n");
    at("AT+RST\r\n",      2000, "ready");
    at("AT\r\n",          500,  "OK");
    at("AT+CWMODE=1\r\n", 500,  "OK");

    printf(">> Joining WiFi...\n");
    snprintf(g_tx, BUF,
        "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    esp_send(g_tx);
    esp_read(8000, "GOT IP", "FAIL");

    if      (strstr(g_rx, "GOT IP")) { wifi_connected = true;  printf("[WIFI] Connected!\n"); }
    else if (strstr(g_rx, "FAIL"))   { wifi_connected = false; printf("[WIFI] FAILED — check SSID/password\n"); }

    thread_sleep_for(2000);
    at("AT+CIFSR\r\n",    500, "OK");
    at("AT+CIPMUX=1\r\n",  500, "OK");
    printf("=== ESP-01 ready ===\n");
}

static void wifi_reconnect(void)
{
    printf("[WIFI] Reconnecting...\n");
    esp_init();
}

//  NETWORK TASK

static void network_task(void)
{
    esp_init();

    if (!wifi_connected) {
        printf("[ERROR] No WiFi — network thread halting. RFID scanning still runs.\n");
        led_Green = 0;
        return;
    }
    led_Green = 1;

    uint64_t last_send    = 0;
    uint64_t last_tg_send = 0;
    uint64_t last_device_state_poll = 0;
    int consecutive_failures = 0;

    while (1) {
        uint64_t now = now_ms();

        int rfid_now = get_latest_rfid();
        if (rfid_now != 0 && now - last_tg_send >= TG_COOLDOWN_MS) {
            set_latest_rfid(0);
            last_tg_send = now;
            const char *msg = (rfid_now == 1) ? "RFID card scanned!" : "RFID tag scanned!";
            if (send_telegram_via_relay(msg)) {
                consecutive_failures = 0;
            } else {
                printf("[WARN] Telegram send failed\n");
                consecutive_failures++;
            }

            if (send_alert_log_via_relay("success", msg, "rfid")) {
                consecutive_failures = 0;
            } else {
                printf("[WARN] Alert log send failed\n");
                consecutive_failures++;
            }
        }

        if (consume_pending_blind_toggle()) {
            bool new_blind_open = !last_blind_open;
            request_blind_actuate(new_blind_open);
            if (!send_device_state_via_relay("blind", new_blind_open)) {
                printf("[WARN] Blind state push failed\n");
                consecutive_failures++;
            }
        }

        if (consume_pending_lighting_toggle()) {
            apply_main_lighting(!last_main_lighting);
            if (!send_device_state_via_relay("main_lighting", last_main_lighting)) {
                printf("[WARN] Lighting state push failed\n");
                consecutive_failures++;
            }
        }

        uint8_t fan_speed_req;
        bool fan_power_req;
        if (consume_pending_fan_actuate(&fan_speed_req, &fan_power_req)) {
            if (!send_device_state_int_via_relay("fan_speed", fan_speed_req)) {
                printf("[WARN] Fan speed push failed\n");
                consecutive_failures++;
            }
            if (!send_device_state_via_relay("fan_power", fan_power_req)) {
                printf("[WARN] Fan power push failed\n");
                consecutive_failures++;
            }
        }

        // Door lock: mirrors the blind system. The keypad '2' sets the toggle
        // flag (request_door_toggle); we consume it here, compute the new
        // state from last_door_locked, push it to the relay, and queue the
        // actuation for the MAIN loop (which owns apply_door_lock + servo).
        // Push smart_lock: the relay aliases it to BOTH door_locked and
        // smart_lock, keeping the columns in sync.
        if (consume_pending_door_toggle()) {
            bool new_door_locked = !last_door_locked;
            printf("[NET] Door toggle: %s -> %s\n",
                   last_door_locked ? "LOCKED" : "UNLOCKED",
                   new_door_locked ? "LOCKED" : "UNLOCKED");
            request_door_actuate(new_door_locked);
            if (!send_device_state_via_relay("smart_lock", new_door_locked)) {
                printf("[WARN] Door state push failed\n");
                consecutive_failures++;
            } else {
                printf("[NET] Door push successful\n");
            }
        }

        if (now - last_device_state_poll >= DEVICE_STATE_POLL_MS) {
            last_device_state_poll = now;
            if (poll_device_state_via_relay()) {
                consecutive_failures = 0;
            } else {
                printf("[WARN] Device state poll failed\n");
                consecutive_failures++;
            }
        }

        if (now - last_send >= SEND_INTERVAL_MS) {
            last_send = now;

            read_dht11();
            float temperature = read_temperature();
            float humidity    = read_humidity();
            float current     = read_current();
            int   rfid        = get_latest_rfid();

            fmt_float(ts_fields[0].value, sizeof(ts_fields[0].value), temperature);
            fmt_float(ts_fields[1].value, sizeof(ts_fields[1].value), humidity);
            fmt_float(ts_fields[2].value, sizeof(ts_fields[2].value), current);
            snprintf(ts_fields[3].value, sizeof(ts_fields[3].value), "%d", rfid);

            bool ts_ok = send_to_thingspeak();
            bool sb_ok = send_sensor_telemetry_via_relay(temperature, humidity, current);

            if (ts_ok && sb_ok) {
                consecutive_failures = 0;
            } else {
                printf("[WARN] One or more sends failed\n");
                consecutive_failures++;
            }

            printf("\n[DATA]\n");
            for (int i = 0; i < TS_NUM_FIELDS; i++) {
                if (ts_fields[i].field == 0) continue;
                printf("  field%d  %-15s = %s\n",
                        ts_fields[i].field, ts_fields[i].label, ts_fields[i].value);
            }

            if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
                consecutive_failures = 0;
                wifi_reconnect();
            }

            thread_sleep_for(10);
        }
    }
}

//  MAIN — owns RFID polling + LEDs only

static Thread networkThread(osPriorityNormal, 4096);

int main(void)
{
    mfrc522.PCD_Init();
    motor_init();

    // Initialize fan servo with PWM write
    fanServo.period_ms(20);
    fanServo.write(0.075f);
    
    // Initialize door lock servo (SG90)
    doorLock.period_ms(PERIOD_WIDTH);
    doorLock.pulsewidth_us(PULSE_WIDTH_0_DEGREE);  // Start locked (600us / 0°)
    
    printf("\n=== SERVOS INITIALIZED ===\n");
    printf("Fan neutral duty: 0.075 (7.5%%)\n");
    printf("Door lock locked pulse: %dus (0°)\n", PULSE_WIDTH_0_DEGREE);
    printf("Door lock unlocked pulse: %dus (180°)\n\n", PULSE_WIDTH_180_DEGREE);

    // test_door_servo();  // DISABLED after hardware verification (was 6s boot sweep).
                          // Re-enable to re-verify the PA_6 servo end-to-end.

    // Main light PWM init on PB_1 (TIM3_CH4, default remap): 100Hz, full brightness.
    // NOT PC_9: PC_9's full remap reroutes TIM3_CH2 away from the PA_7 blind motor.
    led_mainLighting_pwm.period_ms(10);
    led_mainLighting_pwm.write(1.0f);
    g_brightness = 100;

    lcd_init();
    keypad_init();

    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    printf("\n=== STM32 + ESP-01 -> ThingSpeak ===\n");

    // Show locked screen initially
    lcdmessage(MessageLocked, 1);
    lcdmessage(MessageLocked2, 2);

    g_tx = new (std::nothrow) char[BUF];
    g_rx = new (std::nothrow) char[RX_BUF];
    if (!g_tx || !g_rx) {
        printf("[ERROR] Heap allocation failed for buffers\n");
        while (1) thread_sleep_for(1000);
    }

    networkThread.start(network_task);

    int rfid = 0;
    bool last_auth_state = false;

    while (1) {
        bool auth_unlocked = is_keypad_unlocked();
        bool in_fan_menu = is_in_fan_menu();

        // Update LCD when authentication state changes
        if (auth_unlocked != last_auth_state) {
            last_auth_state = auth_unlocked;
            if (auth_unlocked) {
                // Just unlocked - show options screen
                lcdmessage(Message1, 1);
                lcdmessage(Message2, 2);
                printf("[LCD] Unlocked - showing options\n");
            } else {
                // Locked - show locked screen
                lcdmessage(MessageLocked, 1);
                lcdmessage(MessageLocked2, 2);
                printf("[LCD] Locked\n");
            }
        }

        if (key_pending) {
            key_pending = false;
            char key = last_key;

            // Only process keys if unlocked
            if (auth_unlocked) {
                if (in_fan_menu) {
                    // Fan menu handler
                    uint8_t speed = fan_menu_selection_to_speed(key);
                    if (speed != 255) {
                        // Update LOCAL timestamp
                        fan_timestamp_mutex.lock();
                        last_fan_local_change = now_ms();
                        fan_timestamp_mutex.unlock();
                        
                        if (speed == 0) {
                            fan_mutex.lock();
                            fan_power = false;
                            fan_mutex.unlock();
                            request_fan_actuate(0, false, false);
                            lcdmessage("Fan: OFF", 1);
                            lcdmessage("Speed Set", 2);
                            printf("Keypad: Fan OFF\n");
                        } else {
                            fan_mutex.lock();
                            fan_power = true;
                            fan_speed = speed;
                            fan_mutex.unlock();
                            request_fan_actuate(speed, true, false);
                            char msg[17];
                            snprintf(msg, sizeof(msg), "Fan: %s (%u%%)",
                                    (speed <= 33) ? "LOW" : (speed <= 66) ? "MED" : "HIGH", speed);
                            lcdmessage(msg, 1);
                            lcdmessage("Speed Set", 2);
                            printf("Keypad: Fan %s (%u%%)\n", 
                                   (speed <= 33) ? "LOW" : (speed <= 66) ? "MED" : "HIGH", speed);
                        }
                        exit_fan_menu();
                        thread_sleep_for(1000);
                        // Return to options screen
                        lcdmessage(Message1, 1);
                        lcdmessage(Message2, 2);
                    } else {
                        lcdmessage("Invalid Speed", 1);
                        lcdmessage("", 2);
                        printf("Keypad: Invalid fan speed\n");
                        thread_sleep_for(500);
                        lcdmessage(FanspeedM, 1);
                        lcdmessage(FanspeedM2, 2);
                    }
                    refresh_fan_menu_timeout();
                } else {
                    // Main menu handler
                    switch (key) {
                        case '1':
                            printf("1 is pressed -- toggling Blind\n");
                            request_blind_toggle();
                            lcdmessage("Blind Toggled", 1);
                            lcdmessage("", 2);
                            thread_sleep_for(500);
                            lcdmessage(Message1, 1);
                            lcdmessage(Message2, 2);
                            break;
                        case '2':
                            printf("2 is pressed -- toggling Door Lock\n");
                            request_door_toggle();
                            lcdmessage("Door Toggled", 1);
                            lcdmessage("", 2);
                            thread_sleep_for(500);
                            lcdmessage(Message1, 1);
                            lcdmessage(Message2, 2);
                            break;
                        case '3':
                            printf("3 is pressed -- toggling Lighting\n");
                            request_lighting_toggle();
                            lcdmessage("Light Toggled", 1);
                            lcdmessage("", 2);
                            thread_sleep_for(500);
                            lcdmessage(Message1, 1);
                            lcdmessage(Message2, 2);
                            break;
                        case '4':
                            printf("4 is pressed -- Fan Speed Menu\n");
                            enter_fan_menu();
                            break;
                        default:
                            printf("Keypad: Invalid key '%c'\n", key);
                            lcdmessage("Invalid Key", 1);
                            lcdmessage("", 2);
                            thread_sleep_for(500);
                            lcdmessage(Message1, 1);
                            lcdmessage(Message2, 2);
                            break;
                    }
                }
            } else {
                // Key pressed but locked
                printf("Keypad: Locked - RFID required\n");
                // Show locked message briefly
                lcdmessage("RFID Required!", 1);
                lcdmessage(MessageLocked2, 2);
                thread_sleep_for(1000);
                lcdmessage(MessageLocked, 1);
                lcdmessage(MessageLocked2, 2);
            }
        }

        // ---- Consume pending blind actuation request from network thread ----
        bool blind_open_requested;
        if (consume_pending_blind_actuate(&blind_open_requested)) {
            apply_blind(blind_open_requested);
        }

        // ---- Consume pending door actuation request from network thread ----
        bool door_locked_req;
        if (consume_pending_door_actuate(&door_locked_req)) {
            apply_door_lock(door_locked_req);
        }

        // ---- Consume pending fan actuation request from network thread ----
        uint8_t fan_speed_req;
        bool fan_power_req;
        if (consume_pending_fan_actuate(&fan_speed_req, &fan_power_req)) {
            apply_fan(fan_speed_req, fan_power_req);
        }

        // ---- Door lock is applied directly inside poll_device_state_via_relay() ----
        // (remote-only control; no main-loop consume needed)

        // ---- RFID read every loop iteration (every 10ms) --------
        rfid = read_RFID();
        // Only latch non-zero matches. Writing 0 unconditionally would clobber
        // a pending event before the network thread (possibly blocked in a 3-8s
        // AT call) gets a chance to consume it -- silently dropping the
        // Telegram/alert for that scan. The network thread clears the flag.
        if (rfid != 0) set_latest_rfid(rfid);

        if (rfid == 1 || rfid == 2) {
            set_keypad_unlocked(true);
            printf("[AUTH] Keypad unlocked for %d seconds\n", RFID_UNLOCK_DURATION_MS / 1000);
            // Show options screen
            lcdmessage(Message1, 1);
            lcdmessage(Message2, 2);
            last_auth_state = true;
        }

        if (rfid == 1) {
            led_Blue = 1;
            led_Red  = 0;
        }
        if (rfid == 0) {
            led_Blue = 0;
            led_Red  = 1;
        }

        thread_sleep_for(10);
    }
}