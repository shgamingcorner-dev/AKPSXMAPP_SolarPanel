#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

#include <stdint.h>   // uint8_t for main_light_test_set

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker: (1) positional-180 pulse sweep on PA_7,
// (2) main light ramp on PB_1, (3) 360 duty scan on PA_1. See config.h.

void solar_test_run(void);

// Test hook implemented in main.cpp (only compiled when SOLAR_TEST_MODE 1):
// drives the REAL led_mainLighting_pwm (PB_1/TIM3) so the calibration test
// exercises the production light path, not a local copy.
void main_light_test_set(uint8_t brightness);

#endif // SOLAR_TEST_H
