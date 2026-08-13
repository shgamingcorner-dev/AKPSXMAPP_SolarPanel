#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker: (1) blind ON/OFF toggle like apply_blind(), then
// (2) a 360° duty scan on the same pin to check whether the blind is a
// positional SG90 or a continuous 360° servo. See config.h for params.

void solar_test_run(void);

#endif // SOLAR_TEST_H
