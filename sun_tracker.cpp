/*
 * sun_tracker.cpp — astronomical sun tracking (see sun_tracker.h).
 *
 * Pin: TRACKER_MOTOR_PIN = PB_0 = TIM3_CH3 DEFAULT remap (same as LDR mode).
 * Motor: 360° continuous servo, TRACKER_FWD_DUTY / TRACKER_REV_DUTY /
 * TRACKER_STOP_DUTY from config.h. Position model: signed cumulative
 * run-time in ms (FWD += dt, REV -= dt) — identical to tracker.cpp.
 *
 * Solar equations (float, sufficient for motor aiming; ~0.5° accuracy):
 *   - day-of-year from epoch
 *   - solar declination (Cooper/NOAA approx)
 *   - equation of time + hour angle from UTC + longitude
 *   - azimuth/elevation from lat/dec/hour angle
 * Ref: NOAA Solar Calculator equations (public domain).
 *
 * Timezone: SUN_TIMEZONE_UTC_OFFSET (8 for Singapore). No DST.
 *
 * TRACKER_MODE == 1 selects this module in main.cpp; TRACKER_MODE == 0
 * keeps the LDR sweep-and-hold (tracker.cpp) unchanged.
 */
#include "mbed.h"
#include "config.h"
#include "sun_tracker.h"

#include <math.h>

static PwmOut trackerMotor(TRACKER_MOTOR_PIN);

static uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Time state
// ---------------------------------------------------------------------------
static uint64_t g_epoch_sec = 0;      // Unix epoch (UTC seconds)
static bool     g_has_time  = false;
static uint64_t g_last_aim  = 0;      // ms since boot of last sun-position recompute

void sun_tracker_set_epoch(uint64_t epoch_sec)
{
    g_epoch_sec = epoch_sec;
    g_has_time  = (epoch_sec > 1000000000ULL);   // sane: after 2001
    // A late time arrival (or a re-fetch) should trigger an IMMEDIATE aim,
    // not wait for the next 5-min cadence tick. g_last_aim is in ms-since-boot;
    // backdate it so the next tick's (now - g_last_aim) >= SUN_UPDATE_MS.
    g_last_aim = now_ms() - SUN_UPDATE_MS;
    if (g_has_time) {
        printf("[SUN] epoch time set: %llu\n", (unsigned long long)g_epoch_sec);
    }
}

bool sun_tracker_has_time(void) { return g_has_time; }

// ---------------------------------------------------------------------------
// Solar position math (NOAA-style, float)
// ---------------------------------------------------------------------------
static float deg2rad(float d) { return d * 3.14159265f / 180.0f; }
static float rad2deg(float r) { return r * 180.0f / 3.14159265f; }

// Normalize angle to [0, 360)
static float norm360(float a)
{
    while (a < 0.0f)   a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a;
}

// Compute solar azimuth (0=N, 90=E, 180=S, 270=W) and elevation (deg)
// for the given UTC epoch and location. Returns true if sun is above
// SUN_MIN_ELEVATION (i.e. "day" for tracking purposes).
// NOAA solar calculator equations, float precision (sufficient for motor aim).
static bool compute_sun(float lat_deg, float lon_deg, uint64_t epoch_sec,
                        float *azimuth_out, float *elevation_out)
{
    const float PI  = 3.14159265f;
    const float DEG = PI / 180.0f;

    // Julian date from Unix epoch. Split into days + fraction so float
    // precision isn't lost (epoch ~1.78e9 exceeds float's exact int range;
    // casting the whole thing to float loses ~128s -> ~0.5° hour-angle error).
    uint64_t epoch_days   = epoch_sec / 86400;
    uint32_t epoch_rem    = (uint32_t)(epoch_sec % 86400);
    float jd = (float)epoch_days + (float)epoch_rem / 86400.0f + 2440587.5f;
    // Julian century
    float T = (jd - 2451545.0f) / 36525.0f;

    // Geometric mean longitude, anomaly (deg)
    float L0 = norm360(280.46646f + T * (36000.76983f + T * 0.0003032f));
    float M  = 357.52911f + T * (35999.05029f - 0.0001537f * T);
    // Equation of center
    float Mr = M * DEG;
    float C = (1.914602f - T * (0.004817f + 0.000014f * T)) * sinf(Mr)
            + (0.019993f - 0.000101f * T) * sinf(2.0f * Mr)
            + 0.000289f * sinf(3.0f * Mr);
    // True + apparent longitude (aberration/nutation correction)
    float O = L0 + C;
    float Omega = 125.04f - 1934.136f * T;
    float lam = O - 0.00569f - 0.00478f * sinf(Omega * DEG);
    // Obliquity
    float eps0 = 23.43929111f - T * (0.0130042f + T * (0.00000016f + T * 0.000000504f));
    // Declination
    float decl = asinf(sinf(eps0 * DEG) * sinf(lam * DEG)) / DEG;

    // Equation of time (minutes) — NOAA EoT
    float y = tanf(eps0 / 2.0f * DEG);
    y = y * y;
    float ecc = 0.016708634f - T * (0.000042037f + 0.0000001267f * T);
    float L0r = L0 * DEG;
    float eot = 4.0f * rad2deg(
        y * sinf(2.0f * L0r)
        - 2.0f * ecc * sinf(Mr)
        + 4.0f * ecc * y * sinf(Mr) * cosf(2.0f * L0r)
        - 0.5f * y * y * sinf(4.0f * L0r)
        - 1.25f * ecc * ecc * sinf(2.0f * Mr));

    // True solar time (minutes); 4 min per degree east of UTC meridian
    float utc_frac = (float)(epoch_sec % 86400) / 86400.0f * 1440.0f;
    float tst = fmodf(utc_frac + eot + 4.0f * lon_deg, 1440.0f);
    // Hour angle (deg)
    float ha = tst / 4.0f - 180.0f;
    if (ha < -180.0f) ha += 360.0f;
    if (ha >  180.0f) ha -= 360.0f;

    // Elevation
    float lat_r = lat_deg * DEG, decl_r = decl * DEG, ha_r = ha * DEG;
    float elev = asinf(sinf(lat_r) * sinf(decl_r)
                     + cosf(lat_r) * cosf(decl_r) * cosf(ha_r)) / DEG;

    // Azimuth (from north, clockwise)
    float denom = cosf(lat_r) * cosf(elev * DEG);
    float cos_az = 0.0f;
    if (denom != 0.0f) {
        cos_az = (sinf(decl_r) - sinf(lat_r) * sinf(elev * DEG)) / denom;
    }
    if (cos_az > 1.0f) cos_az = 1.0f;
    if (cos_az < -1.0f) cos_az = -1.0f;
    float az = acosf(cos_az) / DEG;
    if (ha > 0) az = 360.0f - az;

    if (azimuth_out)   *azimuth_out = az;
    if (elevation_out) *elevation_out = elev;
    return (elev >= SUN_MIN_ELEVATION);
}

// ---------------------------------------------------------------------------
// Motor state (same position model as tracker.cpp)
// ---------------------------------------------------------------------------
static int32_t  g_pos = 0;              // signed cumulative run time (ms)
static int32_t  g_target = 0;           // target position (ms)
static bool     g_moving = false;
static uint64_t g_last_pos_update = 0;
static float    g_azimuth = 0.0f;
static float    g_elevation = 0.0f;
static bool     g_is_day = false;
static bool     g_has_target = false;   // true once we've computed a target

static void motor_stop(void)
{
    trackerMotor.write(TRACKER_STOP_DUTY);
    g_moving = false;
}

static void motor_run(float duty)
{
    trackerMotor.write(duty);
    g_moving = true;
}

// Integrate travel into g_pos while the motor runs (clamped like tracker.cpp).
static void update_position(void)
{
    uint64_t now = now_ms();
    uint64_t dt = now - g_last_pos_update;
    if (dt > 100) dt = 100;
    if (g_target > g_pos) {
        g_pos += (int32_t)dt;
    } else if (g_target < g_pos) {
        g_pos -= (int32_t)dt;
    }
    g_last_pos_update = now;
}

// Map sun azimuth (deg) to a motor position in [SUN_POS_EAST, SUN_POS_WEST].
// East (90) -> SUN_POS_EAST, West (270) -> SUN_POS_WEST, linear between.
static int32_t azimuth_to_pos(float az)
{
    // Clamp to the tracked arc [90, 270] (sun is south of SG all year;
    // north azimuths ~0-90/270-360 only occur in extreme cases/night).
    float a = az;
    if (a < 90.0f)  a = 90.0f;
    if (a > 270.0f) a = 270.0f;
    float frac = (a - 90.0f) / 180.0f;                       // 0..1
    return SUN_POS_EAST + (int32_t)(frac * SUN_POS_TOTAL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void sun_tracker_init(void)
{
    g_pos = 0;
    g_target = SUN_NIGHT_PARK_POS;
    g_has_target = false;
    g_last_aim = now_ms() - SUN_UPDATE_MS;   // first tick aims immediately
    trackerMotor.period_ms(PERIOD_WIDTH);
    motor_stop();
    printf("[SUN] sun tracker init (mode=%d), lat=%.2f lon=%.2f tz=%+d\n",
           TRACKER_MODE, SUN_LATITUDE, SUN_LONGITUDE, SUN_TIMEZONE_UTC_OFFSET);
}

void sun_tracker_tick(void)
{
    uint64_t now = now_ms();

    // If we have no time yet, stay parked (safe default).
    if (!g_has_time) {
        if (g_moving) motor_stop();
        return;
    }

    // Recompute the sun position and re-aim every SUN_UPDATE_MS.
    if (now - g_last_aim >= SUN_UPDATE_MS) {
        g_last_aim = now;
        g_is_day = compute_sun(SUN_LATITUDE, SUN_LONGITUDE,
                               g_epoch_sec, &g_azimuth, &g_elevation);
        if (g_is_day) {
            g_target = azimuth_to_pos(g_azimuth);
        } else {
            // Night: park at home. Sun azimuth is meaningless below horizon.
            g_target = SUN_NIGHT_PARK_POS;
        }
        g_has_target = true;
        printf("[SUN] az=%.1f ele=%.1f %s -> target pos %ld\n",
               g_azimuth, g_elevation, g_is_day ? "DAY" : "NIGHT",
               (long)g_target);
    }

    if (!g_has_target) return;

    // Drive toward target (position model, tolerance like tracker.cpp).
    update_position();
    if (abs(g_target - g_pos) <= 150) {
        motor_stop();
        return;
    }
    if (g_target > g_pos) {
        motor_run(TRACKER_FWD_DUTY);
    } else {
        motor_run(TRACKER_REV_DUTY);
    }
}

float   sun_tracker_get_azimuth(void)     { return g_azimuth; }
float   sun_tracker_get_elevation(void)   { return g_elevation; }
int32_t sun_tracker_get_target_pos(void)  { return g_target; }
bool    sun_tracker_is_moving(void)       { return g_moving; }
bool    sun_tracker_is_day(void)          { return g_is_day; }
