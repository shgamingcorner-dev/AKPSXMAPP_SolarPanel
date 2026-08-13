#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker, so you can calibrate the real east->west travel
// time on your pulley rig (prints a [TRAVEL] t= tick every 1000ms).
// See config.h for the tunable parameters.

void solar_test_run(void);

#endif // SOLAR_TEST_H
