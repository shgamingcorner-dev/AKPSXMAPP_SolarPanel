#undef __ARM_FP


#include "mbed.h"
#include "MFRC522.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "DHT11.h"

#define RST_PIN PA_2
#define SS_PIN  PB_2

MFRC522             mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;


//  api keys/wifi information


// ROTATE THESE BEFORE COMMITTING — the previous values were committed to a
// public repo in plaintext. Put your NEW WiFi password here after changing
// it on your router, and never reuse a value that was ever pushed to git.
#define WIFI_SSID        "SINGTEL-DFT4"
#define WIFI_PASSWORD    "3rx3cfm2hb"
#define TS_API_KEY       "WFQQ2K9I14E30IE3" //thinkspeak key
#define SEND_INTERVAL_MS 15000      // minimum 15s on free tier

//  Telegram-via-relay config
//  The bot token lives ONLY on the relay server (as a Replit Secret) now —
//  the firmware never sees it, so there's nothing Telegram-related left to
//  leak from this file. RELAY_SECRET is just a shared password between this
//  board and the relay so randoms on the internet can't POST messages
//  through it; it is NOT the bot token. Get a new bot token from @BotFather
//  if the old one leaked (/revoke), and set it as TELEGRAM_BOT_TOKEN on the
//  relay's Replit Secrets — not here.
#define RELAY_HOST    "https://akpsrelay--shgamingcorner.replit.app"  // no https://, no trailing slash
#define RELAY_PORT    80
#define RELAY_SECRET  "ab805d0429869cfc507b54bd1921a2ae"     // must match RELAY_SECRET on the relay

// ThingSpeak field assignment 
#define TS_FIELD_TEMPERATURE   1
#define TS_FIELD_HUMIDITY      2
#define TS_FIELD_CURRENT       3
#define TS_FIELD_RFIDQ         4
// #define TS_FIELD_XnXX      5   // uncomment to add more123

// RFID UIDs 
#define RFID_UID_CARD  "15828045"
#define RFID_UID_TAG   "E09F8E21"


//  HARDWARE PINS

#define ESP_TX  PC_10
#define ESP_RX  PC_11
#define DHT11_PIN PA_1

static DigitalOut led_tx(PB_14);
static DigitalOut led_rx(PB_15);
static DigitalOut led_Blue(PC_0);
static DigitalOut led_Red(PC_2);
static DigitalOut led_Green(PC_1);
static DigitalOut DHT11VCC(PA_7);


DHT11 dht11(DHT11_PIN);


//  INTERNALS — no need to edit below this line


#define TS_HOST  "api.thingspeak.com"
#define TS_PORT  80
#define BUF      256

static BufferedSerial esp(ESP_TX, ESP_RX, 115200);
static char g_tx[BUF];
static char g_rx[BUF];
static char tagID[9];

// ============================================================
//  SHARED STATE BETWEEN THREADS
//
//  main thread  : owns RFID scanning + LEDs, runs every ~10ms
//  network task : owns WiFi/ThingSpeak/Telegram, runs on its own loop
//
//  The only thing the network task needs from the main thread is
//  the latest RFID match result, so that's the only piece of
//  state that's shared, and it's protected by a mutex.
// ============================================================

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

static void fmt_float(char *out, int out_sz, float v)
{
    int whole = (int)v;
    int frac  = (int)((v - (float)whole) * 10.0f);
    if (frac < 0) frac = -frac;
    snprintf(out, out_sz, "%d.%d", whole, frac);
}

// ============================================================
//  RFID  (runs on the main thread)
// ============================================================

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


//  SENSOR FUNCTIONS
//  (called from the network task, right before each send)

static float read_temperature(void)
{
    DHT11VCC = 1;
    int temperature = 0;
    temperature = dht11.readTemperature();
    if (temperature != DHT11::ERROR_CHECKSUM && temperature != DHT11::ERROR_TIMEOUT)
    {
        printf("Temperature: %d C\n", temperature);
        return temperature;
    }
    else
    {
        printf("%s\n", dht11.getErrorString(temperature));
        return 2634;
        DHT11VCC=0;
    }
}

static float read_humidity(void)
{
    DHT11VCC = 1;
    int humidity = 0;
    humidity = dht11.readHumidity();
    if (humidity != DHT11::ERROR_CHECKSUM && humidity != DHT11::ERROR_TIMEOUT)
    {
        printf("humidity: %d %%\n", humidity);
        return humidity;
    }
    else
    {
        printf("%s\n", dht11.getErrorString(humidity));
        return 4001;
        DHT11VCC=0;
    }
    
}

static float read_current(void)
{
    return 226.0f;  // TODO: replace with real sensor
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

// ============================================================
//  ESP-01 LOW-LEVEL  (all called only from the network task)
// ============================================================

static void esp_send(const char *cmd)
{
    esp.write(cmd, strlen(cmd));
    led_tx = !led_tx;
}

static int esp_read(int wait_ms = 1000)
{
    thread_sleep_for(wait_ms);
    int n = 0;
    while (esp.readable()) {
        int chunk = esp.read(g_rx + n, sizeof(g_rx) - 1 - n);
        if (chunk <= 0) break;
        n += chunk;
        if (n >= (int)(sizeof(g_rx) - 1)) break;
        thread_sleep_for(20);
    }
    if (n > 0) {
        g_rx[n] = '\0';
        led_rx = !led_rx;
        printf("[ESP] %s\n", g_rx);
    }
    return n;
}

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



// ============================================================
//  THINGSPEAK SENDER
// ============================================================

static bool send_to_thingspeak(void)
{
    // 1. Open TCP
    snprintf(g_tx, sizeof(g_tx),
        "AT+CIPSTART=0,\"TCP\",\"%s\",%d\r\n", TS_HOST, TS_PORT);
    at(g_tx, 5000);
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
    at(g_tx, 2000);
    if (!strstr(g_rx, ">")) {
        esp_read(1000);
        if (!strstr(g_rx, ">")) {
            printf("[TS] No > prompt\n");
            at("AT+CIPCLOSE=0\r\n", 1000);
            return false;
        }
    }

    // 4. Send
    printf("[TS] Sending: %s\n", query);
    esp_send(query);
    esp_read(5000);

    if (strstr(g_rx, "SEND OK") || strstr(g_rx, "200 OK")) {
        printf("[TS] Upload OK\n");
    } else {
        printf("[TS] Unexpected response — check API key / rate limit\n");
    }

    // 5. Close
    at("AT+CIPCLOSE=0\r\n", 2000);
    return true;
}

// ============================================================
//  TELEGRAM SENDER (via the HTTPS relay, since the ESP-01's AT
//  firmware can only do plain HTTP and Telegram requires TLS)
// ============================================================

static bool send_telegram_via_relay(const char *message)
{
    // Connection id 1 — id 0 is used by send_to_thingspeak()
    snprintf(g_tx, sizeof(g_tx),
        "AT+CIPSTART=1,\"TCP\",\"%s\",%d\r\n", RELAY_HOST, RELAY_PORT);
    at(g_tx, 5000);
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
    at(g_tx, 2000);
    if (!strstr(g_rx, ">")) {
        esp_read(1000);
        if (!strstr(g_rx, ">")) {
            printf("[TG] No > prompt\n");
            at("AT+CIPCLOSE=1\r\n", 1000);
            return false;
        }
    }

    printf("[TG] Sending: %s\n", query);
    esp_send(query);
    esp_read(5000);

    if (strstr(g_rx, "\"ok\":true") || strstr(g_rx, "\"ok\": true")) {
        printf("[TG] Message sent OK\n");
    } else {
        printf("[TG] Unexpected response — check relay logs / RELAY_SECRET\n");
    }

    at("AT+CIPCLOSE=1\r\n", 2000);
    return true;
}

// ============================================================
//  ESP-01 INIT
// ============================================================

static bool wifi_connected = false;

static void esp_init(void)
{
    printf("=== ESP-01 init ===\n");
    at("AT+RST\r\n",      3000);
    at("AT\r\n",          1000);
    at("AT+CWMODE=1\r\n", 1000);

    printf(">> Joining WiFi...\n");
    snprintf(g_tx, sizeof(g_tx),
        "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    esp_send(g_tx);
    esp_read(12000);

    if      (strstr(g_rx, "GOT IP")) { wifi_connected = true;  printf("[WIFI] Connected!\n"); }
    else if (strstr(g_rx, "FAIL"))   { wifi_connected = false; printf("[WIFI] FAILED — check SSID/password\n"); }

    thread_sleep_for(2000);
    at("AT+CIFSR\r\n",    1000);
    at("AT+CIPMUX=1\r\n", 1000);
    printf("=== ESP-01 ready ===\n");
}

// ============================================================
//  NETWORK TASK  — runs entirely on its own Thread
//
//  Owns: ESP-01 init, WiFi join, sensor reads, ThingSpeak upload
//  cycle, and Telegram-on-scan. Every blocking AT-command wait
//  here only stalls this thread, never the RFID/LED loop in main().
// ============================================================

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

    while (1) {
        uint64_t now = Kernel::get_ms_count();

        // ---- Telegram alert on RFID scan, rate-limited by TG_COOLDOWN_MS ----
        int rfid_now = get_latest_rfid();
        if (rfid_now != 0 && now - last_tg_send >= TG_COOLDOWN_MS) {
            last_tg_send = now;
            const char *msg = (rfid_now == 1) ? "RFID card scanned!" : "RFID tag scanned!";
            if (!send_telegram_via_relay(msg)) {
                printf("[WARN] Telegram send failed\n");
            }
        }

        // ---- ThingSpeak: unchanged, runs every SEND_INTERVAL_MS ----
        if (now - last_send >= SEND_INTERVAL_MS) {
            last_send = now;

            float temperature = read_temperature();
            float humidity    = read_humidity();
            float current     = read_current();
            int   rfid        = get_latest_rfid();

            fmt_float(ts_fields[0].value, sizeof(ts_fields[0].value), temperature);
            fmt_float(ts_fields[1].value, sizeof(ts_fields[1].value), humidity);
            fmt_float(ts_fields[2].value, sizeof(ts_fields[2].value), current);
            snprintf (ts_fields[3].value, sizeof(ts_fields[3].value), "%d", rfid);

            printf("\n[DATA]\n");
            for (int i = 0; i < TS_NUM_FIELDS; i++) {
                if (ts_fields[i].field == 0) continue;
                printf("  field%d  %-15s = %s\n",
                       ts_fields[i].field, ts_fields[i].label, ts_fields[i].value);
            }

            if (!send_to_thingspeak()) {
                printf("[WARN] Send failed, retrying next cycle\n");
            }
        }

        thread_sleep_for(50);   // network task doesn't need a tight loop
    }
}


//  MAIN — owns RFID polling + LEDs only


// Explicit (smaller than default 4KB) stack — this board only has 20KB
// total RAM and was hitting 100% usage with the default thread stack size.
static Thread networkThread(osPriorityNormal, 2048);


int main(void)
{
    mfrc522.PCD_Init();     //Initialisation for RFID

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