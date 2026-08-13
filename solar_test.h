#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker, so you can verify the BLIND servo (PA_7): steps
// through 0 -> 45 -> 90 -> 0 -> -45 -> -90 degrees. See config.h for the
// tunable parameters.

void solar_test_run(void);

#endif // SOLAR_TEST_H
