#undef __ARM_FP

//Libraries
#include "mbed.h"
#include "MFRC522.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "DHT11.h"

//api keys and wifi credentials — change these to your own before compiling
#define WIFI_SSID        "bye"
#define WIFI_PASSWORD    "goodbye1"
#define TS_API_KEY       "WFQQ2K9I14E30IE3" //thinkspeak key
#define SEND_INTERVAL_MS 15000      // minimum 15s on free tier

//  relay server for HTTPS Bridging requests
#define RELAY_HOST    "shgam.pythonanywhere.com"  // no https://, no trailing slash
#define RELAY_PORT    80
#define RELAY_SECRET  "ab805d0429869cfc507b54bd1921a2ae"     // must match RELAY_SECRET on the relay

// thinkspeak field assignment to sensor data
#define TS_FIELD_TEMPERATURE   1
#define TS_FIELD_HUMIDITY      2
#define TS_FIELD_CURRENT       3
#define TS_FIELD_RFIDQ         4
// #define TS_FIELD_XnXX      5   // uncomment to add more123

// RFID UIDs (change to own before compiling for each card/tag)
#define RFID_UID_CARD  "15828045"
#define RFID_UID_TAG   "E09F8E21"

// motor timings
#define WAIT_TIME_MS_0 2000 //sleep enough time to allow motor to turn to the preferred position
#define PERIOD_WIDTH 20 //period in ms according to the servo motor datasheet
#define PULSE_WIDTH_90_DEGREE 2400 //pulse width in us to move to 90 degree position
#define PULSE_WIDTH_0_DEGREE 1500 //pulse width in us to move to 0 position
#define PULSE_WIDTH_N_90_DEGREE 600 //pulse width in us to move to -90 degree position

//  HARDWARE PINS
#define RST_PIN PA_2
#define SS_PIN  PB_2
#define ESP_TX  PC_10
#define ESP_RX  PC_11
#define DHT11_PIN PA_1

// ACS712 20A current sensor -- OUT goes through a 10k/15k divider (0.6 ratio)
// before this pin, since the sensor runs on 5V but the ADC only tolerates 3.3V.
#define CURRENT_SENSOR_PIN PA_0

MFRC522             mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

PwmOut motor(PA_7);
static DigitalOut led_tx(PB_14);
static DigitalOut led_rx(PB_15);
static DigitalOut led_Blue(PC_0);
static DigitalOut led_Red(PB_6); //PC_2         
static DigitalOut led_Green(PC_1);
static DigitalOut DHT11VCC(PB_0);
static AnalogIn   current_sensor(CURRENT_SENSOR_PIN);


// Command Center == mainLighting toggle from the dashboard
#define MAIN_LIGHT_PIN PC_2   //Main lighting pin REMEMBER::::CHANGE TO PB_6
static DigitalOut led_mainLighting(MAIN_LIGHT_PIN);


DHT11 dht11(DHT11_PIN);


//  DONOTEDIT — no need to edit below this line unless programming your own stuff

#define TS_HOST  "api.thingspeak.com"
#define TS_PORT  80
#define BUF      256
// g_rx gets its own, larger size -- BUF is also used for stack-local query/body
// buffers in the send_* functions (all running on networkThread's 2048-byte
// stack), so bumping BUF itself would blow the stack. g_rx is a static/global
// buffer, so growing only it costs static RAM, not stack.
// This fixes a real overflow: the /device-state response (preamble + IPD
// header + ~230-byte HTTP response) totals ~274 bytes, over BUF's 255 usable
// bytes, silently truncating the tail ("main_lighting":true) every time.
#define RX_BUF   512

static BufferedSerial esp(ESP_TX, ESP_RX, 115200);
static char g_tx[BUF];
static char g_rx[RX_BUF];
static char tagID[9];


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

static void fmt_float(char *out, int out_sz, float v)
{
    int whole = (int)v;
    int frac  = (int)((v - (float)whole) * 10.0f);
    if (frac < 0) frac = -frac;
    snprintf(out, out_sz, "%d.%d", whole, frac);
}



//  RFID  (runs on the main thread)


static bool rfid_readID(void)
{
    char HexString[3];
    for (uint8_t i = 0; i < 4; i++) {
        sprintf(HexString, "%02X", mfrc522.uid.uidByte[i]);
        tagID[2*i]   = HexString[0];
        tagID[2*i+1] = HexString[1];
    }
    tagID[8] = '\0';
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
#define ACS712_SENSITIVITY_V_PER_A 0.060f
#define ACS712_ZERO_V              1.5f
#define ADC_VREF                   3.3f

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


static void esp_send(const char *cmd)
{
    esp.write(cmd, strlen(cmd));
    led_tx = !led_tx;
}

static int esp_read(int wait_ms = 1000) { //NEW
      uint32_t start = Kernel::get_ms_count();
      uint32_t last_data_ms = start;
      int n = 0;
      // Poll until timeout OR buffer full -- do NOT bail out just because a
      // single poll found nothing readable. The response can arrive in more
      // than one chunk with a brief gap between them (e.g. a multi-segment
      // TCP delivery), and breaking early there truncates the buffer mid-body
      // -- this is exactly what caused main_lighting to be misread as OFF
      // right after gate_servo (which sorts first in the JSON and so always
      // landed before any premature cutoff).
      //
      // Once data HAS started arriving though, waiting out the full wait_ms
      // regardless is wasted time -- a sustained quiet gap (much longer than
      // the momentary single-poll gap that caused the bug above) is a safe
      // signal the response is complete, and cuts several seconds of dead
      // waiting off every ThingSpeak/Supabase send.
      const uint32_t IDLE_GAP_MS = 80;
      while (Kernel::get_ms_count() - start < (uint32_t)wait_ms) {
          if (esp.readable()) {
              int chunk = esp.read(g_rx + n, sizeof(g_rx) - 1 - n);
              if (chunk > 0) {
                  n += chunk;
                  last_data_ms = Kernel::get_ms_count();
                  if (n >= (int)(sizeof(g_rx) - 1)) break;
              }
          } else if (n > 0 && (Kernel::get_ms_count() - last_data_ms) >= IDLE_GAP_MS) {
              break;
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

static void at(const char *cmd, int wait_ms = 1000)
{
    printf(">> %s", cmd);
    esp_send(cmd);
    esp_read(wait_ms);
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
    // 1. Open TCP
    snprintf(g_tx, sizeof(g_tx),
        "AT+CIPSTART=0,\"TCP\",\"%s\",%d\r\n", TS_HOST, TS_PORT);
    at(g_tx, 2000); // Reduced from 5000
    if (!strstr(g_rx, "OK") && !strstr(g_rx, "CONNECT")) {
        printf("[TS] TCP open failed\n");
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
    at(g_tx, 1000); // Reduced from 2000
    if (!strstr(g_rx, ">")) {
        esp_read(1000);
        if (!strstr(g_rx, ">")) {
            printf("[TS] No > prompt\n");
            at("AT+CIPCLOSE=0\r\n", 1000); // Reduced from 2000
            return false;
        }
    }

    // 4. Send
    printf("[TS] Sending: %s\n", query);
    esp_send(query);
    esp_read(3000); // Reduced from 5000

    if (strstr(g_rx, "SEND OK") || strstr(g_rx, "200 OK")) {
        printf("[TS] Upload OK\n");
    } else {
        printf("[TS] Unexpected response — check API key / rate limit\n");
    }

    // 5. Close
    at("AT+CIPCLOSE=0\r\n", 1000); // Reduced from 2000
    return true;
}



//  TELEGRAM SENDER (via the HTTPS relay, since the ESP-01's AT firmware can only do plain HTTP and Telegram requires HTTPS)


static bool send_telegram_via_relay(const char *message)
{
    // Connection id 1 — id 0 is used by send_to_thingspeak()
    snprintf(g_tx, sizeof(g_tx),
        "AT+CIPSTART=1,\"TCP\",\"%s\",%d\r\n", RELAY_HOST, RELAY_PORT);
    at(g_tx, 3000); // Reduced from 5000
    if (!strstr(g_rx, "OK") && !strstr(g_rx, "CONNECT")) {
        printf("[TG] TCP open failed\n");
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
    at(g_tx, 1000); // Reduced from 2000
    if (!strstr(g_rx, ">")) {
        esp_read(1000);
        if (!strstr(g_rx, ">")) {
            printf("[TG] No > prompt\n");
            at("AT+CIPCLOSE=1\r\n", 500); // Reduced from 1000
            return false;
        }
    }

    printf("[TG] Sending: %s\n", query);
    esp_send(query);
    esp_read(3000); // Reduced from 5000

    // Check the HTTP status line, not the JSON body
    if (strstr(g_rx, "200 OK")) {
        printf("[TG] Message sent OK\n");
    } else {
        printf("[TG] Unexpected response — check relay logs / RELAY_SECRET\n");
    }

    at("AT+CIPCLOSE=1\r\n", 500); // Reduced from 2000
    return true;
}



//  SUPABASE BRIDGE (via the same relay, POST with a form body)


static uint32_t g_seq = 0; // 'seq' is a increasing counter that is shared across both calls of the function it just that so the retries is just no op instead of dupe rows


//SUPABASE
static bool send_sensor_telemetry_via_relay(float temperature, float humidity, float power)
{
    char temp_s[16], hum_s[16], pow_s[16];
    fmt_float(temp_s, sizeof(temp_s), temperature);
    fmt_float(hum_s,  sizeof(hum_s),  humidity);
    fmt_float(pow_s,  sizeof(pow_s),  power);

    char body[BUF];
    snprintf(body, sizeof(body),
        "secret=%s&temperature=%s&humidity=%s&power=%s&seq=%lu",
        RELAY_SECRET, temp_s, hum_s, pow_s, (unsigned long)g_seq++);
    int body_len = strlen(body);

    // Connection id 2 -- id 0 is ThingSpeak, id 1 is Telegram
    snprintf(g_tx, sizeof(g_tx),
        "AT+CIPSTART=2,\"TCP\",\"%s\",%d\r\n", RELAY_HOST, RELAY_PORT);
    at(g_tx, 3000); // Reduced from 5000
    if (!strstr(g_rx, "OK") && !strstr(g_rx, "CONNECT")) {
        printf("[SB] TCP open failed\n");
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
    at(g_tx, 1000); // Reduced from 2000
    if (!strstr(g_rx, ">")) {
        esp_read(1000);
        if (!strstr(g_rx, ">")) {
            printf("[SB] No > prompt\n");
            at("AT+CIPCLOSE=2\r\n", 500); // Reduced from 1000
            return false;
        }
    }

    printf("[SB] Sending telemetry: %s\n", body);
    esp_send(query);
    esp_read(3000); // Reduced from 5000

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[SB] Telemetry logged OK\n" : "[SB] Unexpected response — check relay logs\n"); //ERROR CHECK

    at("AT+CIPCLOSE=2\r\n", 500); // Reduced from 2000
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
    snprintf(g_tx, sizeof(g_tx),
        "AT+CIPSTART=3,\"TCP\",\"%s\",%d\r\n", RELAY_HOST, RELAY_PORT);
    at(g_tx, 3000); // Reduced from 5000
    if (!strstr(g_rx, "OK") && !strstr(g_rx, "CONNECT")) {
        printf("[SB] TCP open failed\n");
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
    at(g_tx, 1000); // Reduced from 2000
    if (!strstr(g_rx, ">")) {
        esp_read(1000);
        if (!strstr(g_rx, ">")) {
            printf("[SB] No > prompt\n");
            at("AT+CIPCLOSE=3\r\n", 500); // Reduced from 1000
            return false;
        }
    }

    printf("[SB] Sending alert: %s\n", body);
    esp_send(query);
    esp_read(3000); // Reduced from 5000

    bool ok = strstr(g_rx, "200 OK") != NULL;
    printf(ok ? "[SB] Alert logged OK\n" : "[SB] Unexpected response — check relay logs\n"); //ERROR CHECK

    at("AT+CIPCLOSE=3\r\n", 500); // Reduced from 2000
    return ok;
}



//  DEVICE STATE POLL (Command Center Phase 1 -- mainLighting only)
//
//  Polls the relay's /device-state route (reads device_states.main_lighting
//  from Supabase) and drives led_mainLighting to match. Only writes the pin
//  when the value actually changes, so we're not toggling it every cycle.


static bool last_main_lighting = false;
static bool main_lighting_known = false;

static bool last_gate_servo = false;
static bool gate_servo_known = false;

static bool poll_device_state_via_relay(void)
{
    // Connection id 4 -- ids 0-3 are ThingSpeak/Telegram/telemetry/alert-log
    snprintf(g_tx, sizeof(g_tx),
        "AT+CIPSTART=4,\"TCP\",\"%s\",%d\r\n", RELAY_HOST, RELAY_PORT);
    at(g_tx, 3000);
    if (!strstr(g_rx, "OK") && !strstr(g_rx, "CONNECT")) {
        printf("[DS] TCP open failed\n");
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
    at(g_tx, 1000);
    if (!strstr(g_rx, ">")) {
        esp_read(1000);
        if (!strstr(g_rx, ">")) {
            printf("[DS] No > prompt\n");
            at("AT+CIPCLOSE=4\r\n", 500);
            return false;
        }
    }

    esp_send(query);
    esp_read(3000);

    bool ok = strstr(g_rx, "200 OK") != NULL;
    if (ok) {
        bool main_lighting = strstr(g_rx, "\"main_lighting\":true") != NULL
                           || strstr(g_rx, "\"main_lighting\": true") != NULL;

        if (!main_lighting_known || main_lighting != last_main_lighting) {
            led_mainLighting = main_lighting;
            last_main_lighting = main_lighting;
            main_lighting_known = true;
            printf("[DS] mainLighting -> %s\n", main_lighting ? "ON" : "OFF");
        }

        bool gate_servo = strstr(g_rx, "\"gate_servo\":true") != NULL
                        || strstr(g_rx, "\"gate_servo\": true") != NULL;

        if (!gate_servo_known || gate_servo != last_gate_servo) {
            // Blocks this poll cycle for WAIT_TIME_MS_0 while the curtain moves --
            // acceptable since gate_servo only changes rarely (dashboard toggle),
            // unlike the RFID/telemetry sends which need to stay snappy.
            motor_position_to_angle(gate_servo ? PULSE_WIDTH_90_DEGREE : PULSE_WIDTH_0_DEGREE);
            last_gate_servo = gate_servo;
            gate_servo_known = true;
            printf("[DS] gateServo -> %s\n", gate_servo ? "OPEN" : "CLOSED");
        }
    } else {
        printf("[DS] Unexpected response — check relay logs\n");
    }

    at("AT+CIPCLOSE=4\r\n", 500);
    return ok;
}



//  ESP-01 INIT


static bool wifi_connected = false;

static void esp_init(void)
{
    printf("=== ESP-01 init ===\n");
    at("AT+RST\r\n",      2000); // Reduced from 3000
    at("AT\r\n",          500);  // Reduced from 1000
    at("AT+CWMODE=1\r\n", 500);  // Reduced from 1000

    printf(">> Joining WiFi...\n");
    snprintf(g_tx, sizeof(g_tx),
        "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    esp_send(g_tx);
    esp_read(8000); // Reduced from 12000

    if      (strstr(g_rx, "GOT IP")) { wifi_connected = true;  printf("[WIFI] Connected!\n"); }
    else if (strstr(g_rx, "FAIL"))   { wifi_connected = false; printf("[WIFI] FAILED — check SSID/password\n"); }

    thread_sleep_for(2000);
    at("AT+CIFSR\r\n",    500);  // Reduced from 1000
    at("AT+CIPMUX=1\r\n",  500); // Reduced from 1000
    printf("=== ESP-01 ready ===\n");
}

// rejoins wifi after the AP drops the connection mid-session
static void wifi_reconnect(void)
{
    printf("[WIFI] Reconnecting...\n");
    snprintf(g_tx, sizeof(g_tx),
        "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    esp_send(g_tx);
    esp_read(8000); // Reduced from 12000

    if (strstr(g_rx, "GOT IP")) {
        wifi_connected = true;
        printf("[WIFI] Reconnected!\n");
    } else {
        wifi_connected = false;
        printf("[WIFI] Reconnect failed, will retry\n");
    }
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
    const uint64_t TG_COOLDOWN_MS = 5000; // 5 sec for testing — raise back to 60000 later

    uint64_t last_device_state_poll = 0;
    const uint64_t DEVICE_STATE_POLL_MS = 7000; // Command Center Phase 1 poll interval

    // If wifi looks dead 3 times rejoin
    int consecutive_failures = 0;
    const int MAX_CONSECUTIVE_FAILURES = 3;

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

        // ---- ThingSpeak & Supabase telemetry (RUN IN PARALLEL) ----
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

            // Launch both transmissions CONCURRENTLY
            bool ts_ok = false;
            bool sb_ok = false;

            // ThingSpeak (conn ID 0)
            ts_ok = send_to_thingspeak();

            // Supabase telemetry (conn ID 2) - runs WHILE ThingSpeak is sending
            sb_ok = send_sensor_telemetry_via_relay(temperature, humidity, current);

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


int main(void)
{
    mfrc522.PCD_Init();     //Initialisation for RFID
    motor_init();           //Move curtain servo to its home position

    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    printf("\n=== STM32 + ESP-01 -> ThingSpeak ===\n");


    // Kick off WiFi/ThingSpeak/Telegram on its own thread so it can
    // never block RFID polling below, even during multi-second AT waits.
    networkThread.start(network_task);

    int rfid = 0;

    while (1) {
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