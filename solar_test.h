#ifndef SOLAR_TEST_H
#define SOLAR_TEST_H

// Solar calibration test mode (SolarBugFixes branch).
// When SOLAR_TEST_MODE is 1, main() runs solar_test_run() at boot instead
// of the normal tracker: (1) blind servo angle-step test on PA_7
// (0->45->90->135->180->back), (2) main light brightness ramp on PB_7.
// See config.h for the tunable parameters.

void solar_test_run(void);

#endif // SOLAR_TEST_H
