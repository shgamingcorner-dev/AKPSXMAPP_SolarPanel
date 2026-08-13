#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

#include <stdint.h>   // uint8_t for main_light_test_set

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker: (1) blind servo angle-step test on PA_7
// (0->45->90->135->180->back), (2) main light brightness ramp on PB_1.
// See config.h for the tunable parameters.

void solar_test_run(void);

// Test hook implemented in main.cpp (only compiled when SOLAR_TEST_MODE 1):
// drives the REAL led_mainLighting_pwm (PB_1/TIM3) so the calibration test
// exercises the production light path, not a local copy.
void main_light_test_set(uint8_t brightness);

#endif // SOLAR_TEST_H
