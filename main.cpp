#undef __ARM_FP

//Libraries
#include "mbed.h"
#include "MFRC522.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "DHT11.h"
#include "lcd.h"
#include "keypad.h"
#include "config.h"

//  DONOTEDIT — no need to edit below this line unless programming your own stuff

#define BUF      512
#define RX_BUF   1024

static BufferedSerial esp(ESP_TX, ESP_RX, 115200);
static char g_tx[BUF];
static char g_rx[RX_BUF];
// 10-byte UID -> 20 hex chars + null = 21 bytes
static char tagID[21];

// Hardware: Fan servo (360° continuous rotation) + Door lock
static PwmOut fanServo(FAN_SERVO_PIN);
static DigitalOut doorLock(DOOR_LOCK_PIN);

// Original hardware objects (moved from top of file)
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
// Keypad's InterruptIn/BusIn live in keypad_utilities.cpp -- keypad_init()
// attaches the handler; key_pending/last_key (from keypad.h) are read here.

static DigitalOut led_mainLighting(MAIN_LIGHT_PIN);

// LCD
unsigned char key2, outChar, outChar2, outChar3;
unsigned char passWord[] = {'0', '0', '0', '0'};
char Message1 [ ] = "1.Blind 2.Window";
char Message2 [ ] = "3.Lighting 4.Fans ";
char Message3 [ ] = "Invalid try again";

DHT11 dht11(DHT11_PIN);


//  VALUES SHARED BETWEEN THREADS!!
//
//  main thread  : does RFID scanning + LEDs, runs every ~10ms using threadsleepfor
//  network task : does WiFi/ThingSpeak/Telegram, runs on its own loop



static Mutex   rfid_mutex;
static volatile int g_latest_rfid = 0;

static void set_latest_rfid(int value) //RFID match results that are shared between threads so its protected by a mutex
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
// (esp_send/esp_read/g_tx/g_rx) only ever runs on networkThread -- so a key
// press can't call the relay directly. Instead it just requests a toggle
// here (same mutex-protected-flag shape as g_latest_rfid above), and
// network_task() picks the request up and does the actual relay push.
static Mutex   device_state_mutex;
static volatile bool pending_blind_toggle = false;
static volatile bool pending_lighting_toggle = false;

// Blind motor actuation request from network thread -- actual motor movement
// (which blocks for WAIT_TIME_MS_0) runs on main thread to avoid stalling
// the network task. This flag is set by network_task() and consumed by main().
static Mutex   blind_actuate_mutex;
static volatile bool pending_blind_actuate = false;
static volatile bool pending_blind_open = false;

// Fan servo (360° continuous) + Door lock state -- shared between threads
static Mutex   fan_mutex;
static volatile uint8_t fan_speed = 0;        // 0-100%
static volatile bool fan_power = false;

static Mutex   door_mutex;
static volatile bool door_locked = true;      // true = locked (HIGH)

// Network thread requests fan/door actions; main thread applies them
static Mutex   fan_actuate_mutex;
static volatile bool pending_fan_actuate = false;
static volatile uint8_t pending_fan_speed = 0;
static volatile bool pending_fan_power = false;

static Mutex   door_actuate_mutex;
static volatile bool pending_door_actuate = false;
static volatile bool pending_door_locked = false;

static void request_fan_actuate(uint8_t speed, bool power)
{
    fan_actuate_mutex.lock();
    pending_fan_actuate = true;
    pending_fan_speed = speed;
    pending_fan_power = power;
    fan_actuate_mutex.unlock();
}

static void request_door_actuate(bool locked)
{
    door_actuate_mutex.lock();
    pending_door_actuate = true;
    pending_door_locked = locked;
    door_actuate_mutex.unlock();
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

static void set_fan_speed(uint8_t speed)
{
    fan_mutex.lock();
    fan_speed = speed;
    fan_mutex.unlock();

    if (fan_power) {
        // Map 0-100% to 1500-2000 us (neutral to full forward)
        // Neutral = 1500 us (stop), Full forward = 2000 us
        uint16_t pulse = FAN_SERVO_NEUTRAL_US + (speed * (FAN_SERVO_MAX_FWD_US - FAN_SERVO_NEUTRAL_US)) / 100;
        fanServo.pulsewidth_us(pulse);
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
        fanServo.pulsewidth_us(FAN_SERVO_NEUTRAL_US); // stop
    }
}

static void set_door_lock(bool locked)
{
    door_mutex.lock();
    door_locked = locked;
    door_mutex.unlock();

    doorLock = locked ? DOOR_LOCK_LOCKED : DOOR_LOCK_UNLOCKED;
}

// Getter functions for network thread
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
}

static void request_lighting_toggle(void)
{
    device_state_mutex.lock();
    pending_lighting_toggle = true;
    device_state_mutex.unlock();
}

// Returns true (and clears the flag) if a toggle was requested since the last call.
static bool consume_pending_blind_toggle(void)
{
    device_state_mutex.lock();
    bool v = pending_blind_toggle;
    pending_blind_toggle = false;
    device_state_mutex.unlock();
    return v;
}

// Request blind motor actuation from network thread -- actual movement happens on main thread.
static void request_blind_actuate(bool open)
{
    blind_actuate_mutex.lock();
    pending_blind_actuate = true;
    pending_blind_open = open;
    blind_actuate_mutex.unlock();
}

// Consume blind actuation request (called from main thread).
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
    uint8_t uid_size = mfrc522.uid.size; // Use actual UID size (4, 7, or 10 bytes)
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



//  SENSOR FUNCTIONS all called from network task before sending the data

// reads temperature and humidity from a single DHT11 transaction --
// previously these were two separate independent reads, doubling bus
// traffic and doubling exposure to timing/preemption failures every cycle
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
    DHT11VCC = 0; // Power off DHT11 after reading to save power
}

static float read_temperature(void)
{
    return g_dht_temperature;
}

static float read_humidity(void)
{
    return g_dht_humidity;
}

// ACS712 20A: 100mV/A at the sensor, scaled to 60mV/A by the 10k/15k divider.
// Zero-current point is VCC/2 (2.5V) at the sensor, scaled to 1.5V at the pin.
// Adjust ACS712_ZERO_V if measured current reads non-zero with nothing connected
// -- the sensor's offset and the divider's resistor tolerance both shift this a bit.
// Constants now defined in config.h

static float read_current(void)
{
    // Average several samples -- the ESP-01/RFID reader share a power rail
    // with known instability (see README), so a single ADC sample is noisy.
    const int samples = 20;
    float sum = 0.0f;
    for (int i = 0; i < samples; i++) {
        sum += current_sensor.read();  // normalized 0.0-1.0 over ADC_VREF
        wait_us(100);
    }
    float pin_voltage = (sum / samples) * ADC_VREF;
    float current = (pin_voltage - ACS712_ZERO_V) / ACS712_SENSITIVITY_V_PER_A;

    // %f isn't supported by this board's minimal printf (see fmt_float() note
    // elsewhere in this file) -- format manually instead of silently no-op'ing.
    char cur_s[16], volt_s[16];
    fmt_float(cur_s, sizeof(cur_s), current);
    fmt_float(volt_s, sizeof(volt_s), pin_voltage);
    printf("Current: %s A (pin=%sV)\n", cur_s, volt_s);
    return current;
}


//Motor functions

static float motor_init(void)
{
    motor.period_ms(PERIOD_WIDTH); //period according to the specification, e.g., 20ms
    motor.pulsewidth_us(PULSE_WIDTH_0_DEGREE); //to 0 position, at the middle
    printf("Move to 0 position: Middle\n");

    thread_sleep_for(WAIT_TIME_MS_0); //wait for the motor moving to the position

    return 0.0f;
}

static float motor_position_to_angle(float pulse_width_us)
{
    motor.pulsewidth_us(pulse_width_us); //Move to expected position
    printf("Motor moving\n");
    thread_sleep_for(WAIT_TIME_MS_0); //wait for the motor to reach the position before returning

    //angle calc
    float angle = ((pulse_width_us - PULSE_WIDTH_0_DEGREE) / (float)(PULSE_WIDTH_90_DEGREE - PULSE_WIDTH_0_DEGREE)) * 90.0f;
    return angle;
}


//LCD functions

static void lcdmessage(const char *MessageS, int Line)
{
    if (Line == 1)
    {
    lcd_write_cmd(0x80);			// Move cursor to line 1 position 1
            for (int i = 0; i < (int)strlen(MessageS); i++)		//for 20 char LCD module
            {
                outChar = MessageS[i];
                lcd_write_data(outChar); 	// write character data to LCD
            }
    }

    if (Line == 2)
    {
            lcd_write_cmd(0xC0);			// Move cursor to line 2 position 1

            for (int i = 0; i < (int)strlen(MessageS); i++)		//for 20 char LCD module
            {
                outChar2 = MessageS[i];
                lcd_write_data(outChar2); 	// write character data to LCD
            }
    }
}


//  THINGSPEAK FIELD TABLE


typedef struct {
    int        field;
    char       value[12];
    const char *label;
} ts_field_t;

#define TS_NUM_FIELDS 4 //Change if needed
static ts_field_t ts_fields[TS_NUM_FIELDS] = {
    { TS_FIELD_TEMPERATURE, "", "Temperature" },
    { TS_FIELD_HUMIDITY,    "", "Humidity"    },
    { TS_FIELD_CURRENT,     "", "Current"     },
    { TS_FIELD_RFIDQ,       "", "RFID"        },
};



//  ESP-01 LOW-LEVEL  (all called only from the network task)


// Throw away anything still sitting in the UART before issuing a new
// command. Because esp_read() returns the moment its stop token appears, a
// response's trailing bytes are often left behind -- e.g. CWJAP's "OK"
// arrives after the "GOT IP" we stopped on. Left in place, the NEXT
// command's read picks up that stale text, matches its own stop token
// against it immediately, and returns before its real reply arrives. Every
// read after that is answering the previous command: the whole pipeline
// slips by one and never recovers. Anything pending before we transmit is
// by definition stale, so dropping it is always safe.
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

// Optional stop1/stop2 are substrings that mark a genuine, protocol-level
// end to this specific response (e.g. the actual "CONNECT"/"ERROR" result of
// a CIPSTART, or ",CLOSED" once the far end -- which we always ask to close
// via "Connection: close" -- has finished sending and closed the socket).
// Checking for the real marker instead of guessing from a quiet gap means we
// can safely return the moment the response is actually complete, without
// the risk of returning early on a response that just arrived in bursts
// (e.g. CIPSTART's command echo followed by a delayed CONNECT once the TCP
// handshake completes) and letting the real reply spill into the next call.
static int esp_read(int wait_ms = 1000, const char *stop1 = NULL, const char *stop2 = NULL) {
      uint32_t start = Kernel::get_ms_count();
      int n = 0;
      // Poll until timeout OR buffer full -- do NOT bail out just because a
      // single poll found nothing readable. The response can arrive in more
      // than one chunk with a brief gap between them (e.g. a multi-segment
      // TCP delivery), and breaking early there truncates the buffer mid-body
      // -- this is exactly what caused main_lighting to be misread as OFF
      // right after gate_servo (which sorts first in the JSON and so always
      // landed before any premature cutoff).
      while (Kernel::get_ms_count() - start < (uint32_t)wait_ms) {
          if (esp.readable()) {
              int chunk = esp.read(g_rx + n, sizeof(g_rx) - 1 - n);
              if (chunk > 0) {
                  n += chunk;
                  if (n >= (int)(sizeof(g_rx) - 1)) break;
                  g_rx[n] = '\0'; // null-terminate so strstr below only sees bytes actually received
                  if ((stop1 && strstr(g_rx, stop1)) || (stop2 && strstr(g_rx, stop2))) {
                      break;
                  }
              }
          }
          thread_sleep_for(5); // Short yield (5ms vs previous 20ms+wait_ms)
      }

      if (n > 0) {
          g_rx[n] = '\0';
          led_rx = !led_rx;
          printf("[ESP] %s\n", g_rx);
      }
      return n;
  }

// static int esp_read(int wait_ms = 1000)
// {
//     thread_sleep_for(wait_ms);
//     int n = 0;
//     while (esp.readable()) {
//         int chunk = esp.read(g_rx + n, sizeof(g_rx) - 1 - n);                    //OLD
//         if (chunk <= 0) break;
//         n += chunk;
//         if (n >= (int)(sizeof(g_rx) - 1)) break;
//         thread_sleep_for(20);
//     }
//     if (n > 0) {
//         g_rx[n] = '\0';
//         led_rx = !led_rx;
//         printf("[ESP] %s\n", g_rx);
//     }
//     return n;
// }

static int at(const char *cmd, int wait_ms = 1000, const char *stop1 = NULL, const char *stop2 = NULL)
{
    printf(">> %s", cmd);
    esp_send(cmd);
    return esp_read(wait_ms, stop1, stop2);
}

// Closes a connection id without caring whether it was actually open.
static bool esp_close(int id)
{
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d\r\n", id);
    return at(cmd, 500, "OK", "ERROR") > 0;
}

// Opens a TCP socket on `id`, returning true only when the module actually
// reports "<id>,CONNECT". A bare "OK" is not proof of anything: after a
// reconnect the module answers "busy p..." then a stray OK while the socket
// stays shut, which used to send us charging into CIPSEND and getting
// "link is not valid".
//
// On failure the id is always closed again. That matters more than it looks:
// if the socket opens just *after* we time out, an unclosed link answers the
// next CIPSTART with "ALREADY CONNECTED" -- which is not ",CONNECT", so we
// would reject it and leave without closing once more, wedging that id
// permanently.
//
// If `ip_fallback` is given, a failed hostname attempt is retried once
// against the literal IP. The ESP-01's DNS resolver is slow and unreliable,
// particularly right after a reset has cleared its cache, and from here a
// failed lookup is indistinguishable from a dead server. The HTTP request
// still sends "Host: <hostname>", so name-based virtual hosting on the far
// end keeps working. Which path succeeded is printed, so the serial log says
// outright whether DNS was the problem.
static bool esp_open_tcp(int id, const char *host, const char *ip_fallback, int port, const char *tag)
{
    snprintf(g_tx, sizeof(g_tx),
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
    snprintf(g_tx, sizeof(g_tx),
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

// Percent-encode a string for use in a URL query param
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
    // 1. Open TCP -- no IP fallback needed, ThingSpeak's name resolves fine
    //    and its address is load balanced, so pinning one would age badly.
    if (!esp_open_tcp(0, TS_HOST, NULL, TS_PORT, "[TS]")) {
        return false;
    }

    // 2. Build query string
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

    // 3. CIPSEND
    snprintf(g_tx, sizeof(g_tx), "AT+CIPSEND=0,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) { // Reduced from 2000
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[TS] No > prompt\n");
            at("AT+CIPCLOSE=0\r\n", 1000, "OK", "ERROR"); // Reduced from 2000
            return false;
        }
    }

    // 4. Send
    printf("[TS] Sending: %s\n", query);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) { // Reduced from 5000 -- ",CLOSED" only appears once the server (Connection: close) has fully sent its response and shut the socket
        at("AT+CIPCLOSE=0\r\n", 1000, "OK", "ERROR"); // Reduced from 2000
        return false;
    }

    if (strstr(g_rx, "SEND OK") || strstr(g_rx, "200 OK")) {
        printf("[TS] Upload OK\n");
    } else {
        printf("[TS] Unexpected response — check API key / rate limit\n");
    }

    // 5. Close
    at("AT+CIPCLOSE=0\r\n", 1000, "OK", "ERROR"); // Reduced from 2000
    return (strstr(g_rx, "SEND OK") != NULL) || (strstr(g_rx, "200 OK") != NULL);
}



//  TELEGRAM SENDER (via the HTTPS relay, since the ESP-01's AT firmware can only do plain HTTP and Telegram requires HTTPS)


static bool send_telegram_via_relay(const char *message)
{
    // Connection id 1 — id 0 is used by send_to_thingspeak()
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

    snprintf(g_tx, sizeof(g_tx), "AT+CIPSEND=1,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) { // Reduced from 2000
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[TG] No > prompt\n");
            at("AT+CIPCLOSE=1\r\n", 500, "OK", "ERROR"); // Reduced from 1000
            return false;
        }
    }

    printf("[TG] Sending: %s\n", query);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) { // Reduced from 5000 -- ",CLOSED" only appears once the server (Connection: close) has fully sent its response and shut the socket
        at("AT+CIPCLOSE=1\r\n", 500, "OK", "ERROR"); // Reduced from 2000
        return false;
    }

    // Check the HTTP status line, not the JSON body
    if (strstr(g_rx, "200 OK")) {
        printf("[TG] Message sent OK\n");
    } else {
        printf("[TG] Unexpected response — check relay logs / RELAY_SECRET\n");
    }

    at("AT+CIPCLOSE=1\r\n", 500, "OK", "ERROR"); // Reduced from 2000
    return strstr(g_rx, "200 OK") != NULL;
}



#include <atomic>

//  SUPABASE BRIDGE (via the same relay, POST with a form body)


static std::atomic<uint32_t> g_seq{0}; // 'seq' is an increasing counter shared across send functions to deduplicate retries


//SUPABASE
static bool send_sensor_telemetry_via_relay(float temperature, float humidity, float power)
{
    char temp_s[16], hum_s[16], pow_s[16];
    fmt_float(temp_s, sizeof(temp_s), temperature);
    fmt_float(hum_s,  sizeof(hum_s),  humidity);
    fmt_float(pow_s,  sizeof(pow_s),  power);

    uint8_t fan_speed = get_fan_speed();
    bool fan_power = get_fan_power();
    bool door_locked = get_door_locked();

    char body[BUF];
    snprintf(body, sizeof(body),
        "secret=%s&temperature=%s&humidity=%s&power=%s&fan_speed=%u&fan_power=%s&door_locked=%s&seq=%lu",
        RELAY_SECRET, temp_s, hum_s, pow_s,
        fan_speed, fan_power ? "true" : "false",
        door_locked ? "true" : "false",
        (unsigned long)g_seq++);
    int body_len = strlen(body);

    // Connection id 2 -- id 0 is ThingSpeak, id 1 is Telegram
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

    snprintf(g_tx, sizeof(g_tx), "AT+CIPSEND=2,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) { // Reduced from 2000
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[SB] No > prompt\n");
            at("AT+CIPCLOSE=2\r\n", 500, "OK", "ERROR"); // Reduced from 1000
            return false;
        }
    }

    printf("[SB] Sending telemetry: %s\n", body);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) { // Reduced from 5000 -- ",CLOSED" only appears once the server (Connection: close) has fully sent its response and shut the socket
        at("AT+CIPCLOSE=2\r\n", 500, "OK", "ERROR"); // Reduced from 2000
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[SB] Telemetry logged OK\n" : "[SB] Unexpected response — check relay logs\n"); //ERROR CHECK

    at("AT+CIPCLOSE=2\r\n", 500, "OK", "ERROR"); // Reduced from 2000
    return ok;
}

//Telegram sender?
static bool send_alert_log_via_relay(const char *level, const char *message, const char *category)
{
    char encoded_msg[128];
    urlencode(encoded_msg, sizeof(encoded_msg), message);

    char body[BUF];
    snprintf(body, sizeof(body),
        "secret=%s&level=%s&message=%s&category=%s&seq=%lu",
        RELAY_SECRET, level, encoded_msg, category, (unsigned long)g_seq++);
    int body_len = strlen(body);

    // Connection id 3 -- ids 0-2 are ThingSpeak/Telegram/telemetry
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

    snprintf(g_tx, sizeof(g_tx), "AT+CIPSEND=3,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) { // Reduced from 2000
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[SB] No > prompt\n");
            at("AT+CIPCLOSE=3\r\n", 500, "OK", "ERROR"); // Reduced from 1000
            return false;
        }
    }

    printf("[SB] Sending alert: %s\n", body);
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) { // Reduced from 5000 -- ",CLOSED" only appears once the server (Connection: close) has fully sent its response and shut the socket
        at("AT+CIPCLOSE=3\r\n", 500, "OK", "ERROR"); // Reduced from 2000
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[SB] Alert logged OK\n" : "[SB] Unexpected response — check relay logs\n"); //ERROR CHECK

    at("AT+CIPCLOSE=3\r\n", 500, "OK", "ERROR"); // Reduced from 2000
    return ok;
}



//  DEVICE STATE PUSH (keypad -> relay -> Supabase)
//
//  Pushes a single field/value to the relay's new POST /device-state route,
//  mirroring send_sensor_telemetry_via_relay()'s shape. Reuses connection
//  id 4 -- the same one poll_device_state_via_relay() uses -- since both
//  only ever run sequentially on networkThread, never concurrently.

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

    snprintf(g_tx, sizeof(g_tx), "AT+CIPSEND=4,%d\r\n", req_len);
    if (at(g_tx, 1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
        if (esp_read(1000, ">", "ERROR") <= 0 || !strstr(g_rx, ">")) {
            printf("[DS] No > prompt\n");
            at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
            return false;
        }
    }

    printf("[DS] Pushing %s=%s\n", field, value ? "true" : "false");
    esp_send(query);
    if (esp_read(3000, "CLOSED") <= 0) { // ",CLOSED" only appears once the server (Connection: close) has fully sent its response and shut the socket
        at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[DS] Push OK\n" : "[DS] Unexpected response — check relay logs\n");

    at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
    return ok;
}



//  DEVICE STATE POLL (Command Center Phase 1 -- mainLighting only)
//
//  Polls the relay's /device-state route (reads device_states.main_lighting
//  from Supabase) and drives led_mainLighting to match. Only writes the pin
//  when the value actually changes, so we're not toggling it every cycle.


static bool last_main_lighting = false;
static bool main_lighting_known = false;

static bool last_blind_open = false;
static bool blind_known = false;

// Actuation lives in exactly one place per device, called both by the
// periodic poll below (for dashboard-initiated changes) and by the keypad
// handler in network_task() (for physical-button-initiated changes) --
// keeps a single source of truth for what "apply this state to hardware"
// means, and gives the keypad instant feedback instead of waiting for the
// next poll cycle to notice its own change.
static void apply_main_lighting(bool on)
{
    led_mainLighting = on;
    last_main_lighting = on;
    main_lighting_known = true;
    printf("[DS] mainLighting -> %s\n", on ? "ON" : "OFF");
}

static void apply_blind(bool open)
{
    // Blocks the caller for WAIT_TIME_MS_0 while the curtain moves --
    // acceptable since blind only changes rarely (dashboard/keypad toggle),
    // unlike the RFID/telemetry sends which need to stay snappy.
    motor_position_to_angle(open ? PULSE_WIDTH_90_DEGREE : PULSE_WIDTH_0_DEGREE);
    last_blind_open = open;
    blind_known = true;
    printf("[DS] blind -> %s\n", open ? "OPEN" : "CLOSED");
}

static void apply_fan(uint8_t speed, bool on)
{
    fan_mutex.lock();
    fan_speed = speed;
    fan_power = on;
    fan_mutex.unlock();

    if (on) {
        uint16_t pulse = FAN_SERVO_NEUTRAL_US + (speed * (FAN_SERVO_MAX_FWD_US - FAN_SERVO_NEUTRAL_US)) / 100;
        fanServo.pulsewidth_us(pulse);
    } else {
        fanServo.pulsewidth_us(FAN_SERVO_NEUTRAL_US);
    }
    printf("[DS] fan -> %s, speed %u%%\n", on ? "ON" : "OFF", speed);
}

static void apply_door_lock(bool locked)
{
    door_mutex.lock();
    door_locked = locked;
    door_mutex.unlock();

    doorLock = locked ? DOOR_LOCK_LOCKED : DOOR_LOCK_UNLOCKED;
    printf("[DS] door -> %s\n", locked ? "LOCKED" : "UNLOCKED");
}

static bool poll_device_state_via_relay(void)
{
    // Connection id 4 -- ids 0-3 are ThingSpeak/Telegram/telemetry/alert-log
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

    snprintf(g_tx, sizeof(g_tx), "AT+CIPSEND=4,%d\r\n", req_len);
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
    if (esp_read(3000, "CLOSED") <= 0) { // ",CLOSED" only appears once the server (Connection: close) has fully sent its response and shut the socket
        at("AT+CIPCLOSE=4\r\n", 500, "OK", "ERROR");
        return false;
    }

    bool ok = strstr(g_rx, "200 OK") != NULL;
    if (ok) {
        bool main_lighting = strstr(g_rx, "\"main_lighting\":true") != NULL
                           || strstr(g_rx, "\"main_lighting\": true") != NULL;

        if (!main_lighting_known || main_lighting != last_main_lighting) {
            apply_main_lighting(main_lighting);
        }

        bool blind = strstr(g_rx, "\"blind\":true") != NULL
                  || strstr(g_rx, "\"blind\": true") != NULL;

        if (!blind_known || blind != last_blind_open) {
            request_blind_actuate(blind);
        }

        // Fan state
        bool fan_power_state = strstr(g_rx, "\"fan_power\":true") != NULL
                            || strstr(g_rx, "\"fan_power\": true") != NULL;
        uint8_t fan_speed_state = 0;
        char *fan_speed_ptr = strstr(g_rx, "\"fan_speed\":");
        if (fan_speed_ptr) {
            char *num_start = fan_speed_ptr + strlen("\"fan_speed\":");
            fan_speed_state = (uint8_t)atoi(num_start);
        }

        if (fan_power_state != get_fan_power() || fan_speed_state != get_fan_speed()) {
            request_fan_actuate(fan_speed_state, fan_power_state);
        }

        // Door lock state
        bool door_locked_state = strstr(g_rx, "\"smart_lock\":true") != NULL
                              || strstr(g_rx, "\"smart_lock\": true") != NULL;

        if (door_locked_state != get_door_locked()) {
            request_door_actuate(door_locked_state);
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
    at("AT+RST\r\n",      2000, "ready");         // Reduced from 3000
    at("AT\r\n",          500,  "OK");            // Reduced from 1000
    at("AT+CWMODE=1\r\n", 500,  "OK");            // Reduced from 1000

    printf(">> Joining WiFi...\n");
    snprintf(g_tx, sizeof(g_tx),
        "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    esp_send(g_tx);
    esp_read(8000, "GOT IP", "FAIL"); // Reduced from 12000 -- exits as soon as the real join result is known

    if      (strstr(g_rx, "GOT IP")) { wifi_connected = true;  printf("[WIFI] Connected!\n"); }
    else if (strstr(g_rx, "FAIL"))   { wifi_connected = false; printf("[WIFI] FAILED — check SSID/password\n"); }

    thread_sleep_for(2000);
    at("AT+CIFSR\r\n",    500, "OK");  // Reduced from 1000
    at("AT+CIPMUX=1\r\n",  500, "OK"); // Reduced from 1000
    printf("=== ESP-01 ready ===\n");
}

// Recovers the link after repeated send failures.
//
// This deliberately re-runs the WHOLE init rather than just AT+CWJAP. The
// ESP-01 watchdog-resets on its own fairly often here (the boot banner
// reports "rst cause:4 / wdt reset"), and a reset silently drops CIPMUX
// back to 0. In single-connection mode every "AT+CIPSTART=<id>,..." is
// rejected with "Link type ERROR" and "AT+CIPCLOSE=<id>" answers "MUX=0",
// so a CWJAP-only reconnect rejoins the AP and still cannot open a single
// socket -- the board never recovers until it is power-cycled. Re-running
// esp_init() restores CWMODE and CIPMUX along with the join.
static void wifi_reconnect(void)
{
    printf("[WIFI] Reconnecting...\n");
    esp_init();
}



//  NETWORK TASK  — runs entirely on its own Thread every command to wait is here


static void network_task(void)
{
    esp_init();

    if (!wifi_connected) {
        printf("[ERROR] No WiFi — network thread halting. RFID scanning still runs.\n");
        led_Green = 0;
        return;   // main() keeps running RFID/LEDs regardless
    }
    led_Green = 1;   // simple "network thread alive" indicator, optional


    uint64_t last_send    = 0;
    uint64_t last_tg_send = 0;

    uint64_t last_device_state_poll = 0;

    // If wifi looks dead 3 times rejoin
    int consecutive_failures = 0;

    while (1) {
        uint64_t now = Kernel::get_ms_count();

        //  Telegram alert on RFID scan, rate-limited by TG_COOLDOWN_MS
        int rfid_now = get_latest_rfid();
        if (rfid_now != 0 && now - last_tg_send >= TG_COOLDOWN_MS) {
            last_tg_send = now;                                                             //TELEGRAM MESSAGE YO
            const char *msg = (rfid_now == 1) ? "RFID card scanned!" : "RFID tag scanned!"; //CHANGE THE THINGS HERE TO CHANGE WHAT IS BEING SAID IN TELEGRAM
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

        // ---- Keypad-requested device state changes ----
        // Apply to hardware immediately (instant physical feedback) rather
        // than waiting for the next poll cycle to notice its own change,
        // then push to Supabase so the dashboard stays in sync.
        if (consume_pending_blind_toggle()) {
            // Request blind actuation on main thread (blocks for ~2s)
            request_blind_actuate(!last_blind_open);
            if (!send_device_state_via_relay("blind", last_blind_open)) {
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

        // ---- Fan/Door Lock state changes from keypad ----
        uint8_t fan_speed_req;
        bool fan_power_req;
        if (consume_pending_fan_actuate(&fan_speed_req, &fan_power_req)) {
            // Request fan actuation on main thread
            request_fan_actuate(fan_speed_req, fan_power_req);
            if (!send_device_state_via_relay("fan_speed", fan_speed_req)) {
                printf("[WARN] Fan speed push failed\n");
                consecutive_failures++;
            }
            if (!send_device_state_via_relay("fan_power", fan_power_req)) {
                printf("[WARN] Fan power push failed\n");
                consecutive_failures++;
            }
        }

        bool door_locked_req;
        if (consume_pending_door_actuate(&door_locked_req)) {
            // Request door lock actuation on main thread
            request_door_actuate(door_locked_req);
            if (!send_device_state_via_relay("door_locked", door_locked_req)) {
                printf("[WARN] Door lock push failed\n");
                consecutive_failures++;
            }
        }

        // ---- Command Center Phase 1: poll mainLighting from the dashboard ----
        if (now - last_device_state_poll >= DEVICE_STATE_POLL_MS) {
            last_device_state_poll = now;
            if (poll_device_state_via_relay()) {
                consecutive_failures = 0;
            } else {
                printf("[WARN] Device state poll failed\n");
                consecutive_failures++;
            }
        }

        // ---- ThingSpeak & Supabase telemetry ----
        if (now - last_send >= SEND_INTERVAL_MS) {
            last_send = now;

            // Read sensors ONCE
            read_dht11();
            float temperature = read_temperature();
            float humidity    = read_humidity();
            float current     = read_current();
            int   rfid        = get_latest_rfid();

            // Prepare ThingSpeak fields
            fmt_float(ts_fields[0].value, sizeof(ts_fields[0].value), temperature);
            fmt_float(ts_fields[1].value, sizeof(ts_fields[1].value), humidity);
            fmt_float(ts_fields[2].value, sizeof(ts_fields[2].value), current);
            snprintf(ts_fields[3].value, sizeof(ts_fields[3].value), "%d", rfid);

            // These run one after the other, not concurrently -- despite what
            // an older comment here used to claim. send_to_thingspeak()
            // finishes entirely (CIPCLOSE included) before the Supabase send
            // begins. Overlapping them would need per-connection buffers and
            // a "+IPD,<id>," demultiplexer, since both share one UART and one
            // g_tx/g_rx pair. AT+CIPMUX=1 makes it possible; nothing does it.
            bool ts_ok = send_to_thingspeak();                 // conn id 0
            bool sb_ok = send_sensor_telemetry_via_relay(temperature, humidity, current); // conn id 2

            // Handle results
            if (ts_ok && sb_ok) {
                consecutive_failures = 0;
            } else {
                printf("[WARN] One or more sends failed\n");
                consecutive_failures++;
            }

            // Debug output (optional - remove for max speed)
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

            thread_sleep_for(10);   // network task doesn't need a tight loop changed from 50 to 10
        }
    }
}



//  MAIN — owns RFID polling + LEDs only



static Thread networkThread(osPriorityNormal, 2048);


int main(void) //RMAIN
{
    mfrc522.PCD_Init();     //Initialisation for RFID
    motor_init();           //Move curtain servo to its home position

    // Fan servo (360° continuous) + Door lock initialization
    fanServo.period_ms(PERIOD_WIDTH);
    fanServo.pulsewidth_us(FAN_SERVO_NEUTRAL_US);  // Start at neutral (stopped)
    doorLock = DOOR_LOCK_LOCKED;  // Start locked (HIGH = locked)

    lcd_init();
    keypad_init();

    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    printf("\n=== STM32 + ESP-01 -> ThingSpeak ===\n");

    //LCD PRINT BASIC MESSAGES

    lcd_write_cmd(0x80);			// Move cursor to line 1 position 1
    for (int i = 0; i < (int)strlen(Message1); i++)		//for i amt of char LCD module
    {
        outChar = Message1[i];
        lcd_write_data(outChar); 	// write character data to LCD
    }

    lcd_write_cmd(0xC0);			// Move cursor to line 2 position 1

    for (int i = 0; i < (int)strlen(Message2); i++)		//for i amt char LCD module
    {
        outChar2 = Message2[i];
        lcd_write_data(outChar2); 	// write character data to LCD
    }

    // Kick off WiFi/ThingSpeak/Telegram on its own thread so it can
    // never block RFID polling below, even during multi-second AT waits.
    networkThread.start(network_task);

    int rfid = 0;

    while (1) {


        // ---- Keypad: '1' toggles Blind, '3' toggles Lighting -----
        if (key_pending) {
            key_pending = false;

            lcdmessage(Message1, 1); //Message 1
            lcdmessage(Message2, 2); //Message 2 on second line


            

            switch (last_key) {
                case '1':
                    printf("1 is pressed -- toggling Blind\n");
                    request_blind_toggle();
                    break;
                case '2':
                    printf("2 is pressed -- Window not wired up yet\n");
                    // TODO: needs a second servo pin, not yet wired
                    break;
                case '3':
                    printf("3 is pressed -- toggling Lighting\n");
                    request_lighting_toggle();
                    break;
                case '4':
                    printf("4 is pressed -- toggling Fan\n");
                    // Toggle fan power
                    {
                        fan_mutex.lock();
                        bool new_fan_power = !fan_power;
                        fan_mutex.unlock();
                        request_fan_actuate(fan_speed, new_fan_power);
                    }
                    break;
                case '5':
                    printf("5 is pressed -- Fan speed up\n");
                    // Increase fan speed by 25%
                    {
                        fan_mutex.lock();
                        uint8_t new_speed = fan_speed + 25;
                        if (new_speed > 100) new_speed = 100;
                        fan_mutex.unlock();
                        request_fan_actuate(new_speed, fan_power);
                    }
                    break;
                case '6':
                    printf("6 is pressed -- toggling Door Lock\n");
                    // Toggle door lock
                    request_door_actuate(!get_door_locked());
                    break;
                default:
                    for (int i = 0; i < (int)strlen(Message3); i++)		//for 20 char LCD module
                    {
                        outChar3 = Message3[i];
                        lcd_write_data(outChar3); 	// write character data to LCD
                    }
                    break;
            }
        }

        // ---- Consume pending blind actuation request from network thread ----
        bool blind_open_requested;
        if (consume_pending_blind_actuate(&blind_open_requested)) {
            apply_blind(blind_open_requested);
        }

        // ---- Consume pending fan actuation request from network thread ----
        uint8_t fan_speed_req;
        bool fan_power_req;
        if (consume_pending_fan_actuate(&fan_speed_req, &fan_power_req)) {
            apply_fan(fan_speed_req, fan_power_req);
        }

        // ---- Consume pending door lock actuation request from network thread ----
        bool door_locked_req;
        if (consume_pending_door_actuate(&door_locked_req)) {
            apply_door_lock(door_locked_req);
        }

        // ---- RFID read every loop iteration (every 10ms) --------
        rfid = read_RFID();
        set_latest_rfid(rfid);

        // ---- Blue LED follows RFID ------------------------------
        if (rfid == 1) {
            led_Blue = 1;
            led_Red  = 0;
            thread_sleep_for(16000);
        }
        if (rfid == 0) {
            led_Blue = 0;
            led_Red  = 1;
        }



        thread_sleep_for(10);   // 10ms yield
    }
}