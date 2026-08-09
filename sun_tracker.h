/*
 * sun_tracker.h — astronomical sun tracking for the solar panel (cake branch).
 *
 * Instead of sweeping with the LDR to find the brightest angle, SUN mode
 * computes where the sun actually is in the sky (azimuth + elevation) from
 * the current time and the Singapore location, then drives the 360°
 * continuous pulley motor to the position matching that azimuth.
 *
 * Position mapping (same 0..1900ms space as the LDR sweep):
 *   sun azimuth 90°  (east,  morning) -> SUN_POS_EAST  (0)
 *   sun azimuth 270° (west,  afternoon) -> SUN_POS_WEST (1900)
 *   azimuth outside 90..270 (sun north of the site / night) -> clamp.
 *
 * Time source: the network thread fetches Unix epoch from the relay /time
 * endpoint and calls sun_tracker_set_epoch(). If no time is available the
 * module stays parked (safe default — never random motion).
 *
 * Non-blocking: sun_tracker_tick() is called from the main loop and only
 * acts when SUN_UPDATE_MS elapses. Motor travel uses the same signed
 * cumulative run-time position model as the LDR tracker (no encoder).
 */
#ifndef SUN_TRACKER_H
#define SUN_TRACKER_H

#include <cstdint>

// Lifetime / wiring (called from main):
void    sun_tracker_init(void);            // init motor + park
void    sun_tracker_tick(void);            // non-blocking, call from main loop
void    sun_tracker_set_epoch(uint64_t epoch_sec);  // from relay /time (UTC)
bool    sun_tracker_has_time(void);        // true once a valid epoch is set

// Debug / status:
float   sun_tracker_get_azimuth(void);     // current computed azimuth (deg)
float   sun_tracker_get_elevation(void);   // current computed elevation (deg)
int32_t sun_tracker_get_target_pos(void);  // current target motor position (ms)
bool    sun_tracker_is_moving(void);       // true while driving to target
bool    sun_tracker_is_day(void);          // elevation >= SUN_MIN_ELEVATION

#endif // SUN_TRACKER_H
