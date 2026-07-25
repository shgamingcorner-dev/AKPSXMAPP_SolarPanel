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
#define WIFI_SSID        "SINGTEL-2TKY"
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
#define RELAY_HOST    "shgam.pythonanywhere.com"  // no https://, no trailing slash
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
