#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker, so you can characterize the tracker motor's duty
// response: steps duty and prints [SCAN] duty=0.XXX so you can note which
// values STOP / FORWARD / REVERSE. See config.h for the tunable parameters.

void solar_test_run(void);

#endif // SOLAR_TEST_H
