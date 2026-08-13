#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

#include <stdint.h>   // uint8_t for main_light_test_set

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker: sweeps the blind servo (PA_7) pulse width
// 500->2500us to find the real 0°/180° range. See config.h for params.

void solar_test_run(void);

// Test hook implemented in main.cpp (only compiled when SOLAR_TEST_MODE 1):
// drives the REAL led_mainLighting_pwm (PB_1/TIM3) so the calibration test
// exercises the production light path, not a local copy.
void main_light_test_set(uint8_t brightness);

#endif // SOLAR_TEST_H
