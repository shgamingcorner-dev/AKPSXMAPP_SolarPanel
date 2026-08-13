#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker, so you can verify the BLIND servo (PA_7): toggles
// OPEN (2400us) / CLOSED (600us) like apply_blind() in main.cpp. See
// config.h for the tunable parameters.

void solar_test_run(void);

#endif // SOLAR_TEST_H
