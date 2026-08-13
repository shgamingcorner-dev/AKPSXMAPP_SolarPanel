# AKPS Solar Panel — Calibration Apply Prompt

Use this prompt with ANY AI assistant that has access to this codebase
(repo root: `C:\Users\shgam\AKPSOLARPANELC\AKPSXMAPP_SolarPanel`, branch
`SolarBugFixes`). It tells the AI exactly which constants to change after
the hardware calibration tests are run, and how.

---

## Your task

The user ran the calibration test firmware (SOLAR_TEST_MODE=1) and observed
the hardware. They will tell you the measured values below. Apply those
values to `config.h` and verify the changes compile.

## Context: the three calibrated devices

| Device | Pin | Constants in config.h |
|--------|-----|------------------------|
| Blind (90° positional servo) | PA_7 (MOTOR_PIN) | `PULSE_WIDTH_0_DEGREE`, `PULSE_WIDTH_90_DEGREE`, `PULSE_WIDTH_180_DEGREE` |
| Main light (LED, 20ms PWM) | PB_1 (MAIN_LIGHT_PIN) | none (driven by main.cpp `led_mainLighting_pwm`) |
| Tracker 360° motor | PB_0 (TRACKER_MOTOR_PIN) | `TRACKER_STOP_DUTY`, `TRACKER_FWD_DUTY`, `TRACKER_REV_DUTY`, `SUN_POS_EAST`, `SUN_POS_WEST`, `SUN_POS_TOTAL`, `TRACKER_MS_TO_FLAT`, `TRACKER_MS_FWD_MAX`, `TRACKER_MS_RETURN` |

## Rules you MUST follow (do not skip)

1. **Blind servo (positional 180°)**: The user measured the pulse widths
   where the horn first moves (real 0°) and stops moving (real 180°).
   - `PULSE_WIDTH_0_DEGREE = <measured start>` (us)
   - `PULSE_WIDTH_180_DEGREE = <measured end>` (us)
   - `PULSE_WIDTH_90_DEGREE = (0 + 180) / 2` (midpoint, us)
   - `PULSE_WIDTH_N_90_DEGREE` = same as `PULSE_WIDTH_0_DEGREE` (kept for compat)
   - ALL values are **microseconds** (us). Do NOT convert to duty.

2. **Tracker 360° motor**: The user measured which duties make it stop /
   move forward / move reverse.
   - `TRACKER_STOP_DUTY = <duty where it stops>` (0.0-1.0, typically ~0.075)
   - `TRACKER_FWD_DUTY = <duty for reliable forward>` (above stop duty,
     typically 0.10-0.125; DO NOT exceed 0.130 — servo saturation/heat)
   - `TRACKER_REV_DUTY = <duty for reliable reverse>` (below stop duty,
     typically 0.050-0.060; never below 0.040 — <0.8ms pulse out of range)
   - `TRACKER_MS_TO_FLAT`, `TRACKER_MS_FWD_MAX`, `TRACKER_MS_RETURN`:
     travel times in ms for the pulley rig. If the user measured a travel
     time T for east->west, set `SUN_POS_WEST = T` (ms) and keep
     `SUN_POS_EAST = 0`, `SUN_POS_TOTAL = SUN_POS_WEST`.

3. **Main light (PB_1)**: No constant changes. It stays on PB_1 at the
   servo-shared 20ms period. DO NOT move it to PB_7 (PB_7 is not in the
   F103 pinmap — it faults with error 0x80010130 at boot).

4. **Include order**: NEVER reorder `#include "mbed.h"` before `config.h`
   in any `.cpp` — config.h's `ADC_VREF` macro collides with Mbed's
   `PinNames.h` enum. `mbed.h` MUST come first, then `config.h`.

5. **After editing config.h**: set `SOLAR_TEST_MODE` back to 0 so the
   firmware runs normally (unless the user explicitly says to keep testing).

6. **Verification**: after applying, run the repo's host syntax check:
   ```
   cd C:\Users\shgam\AKPSOLARPANELC\AKPSXMAPP_SolarPanel
   unset PYTHONPATH
   python verify-mbed-host-syntax.py
   ```
   Report whether it passes ("ALL_ADHOC_CHECKS_PASS"). Note: the real ARM
   build requires Keil Studio Cloud — the host check only proves syntax.

7. **Pin summary (do not change these unless asked)**:
   - Blind = PA_7 (TIM3_CH2), tracker = PB_0 (TIM3_CH3), fan = PA_1 (TIM2_CH2)
   - Light = PB_1 (TIM3_CH4) — shares TIM3 period with servos (20ms)
   - Current sensor = PA_0 (ADC1_IN0), LDR = PA_4 (ADC1_IN4)

## Expected user inputs (fill in when given)

- Blind 0° pulse (us): ________
- Blind 180° pulse (us): ________
- Tracker stop duty: ________
- Tracker forward duty: ________
- Tracker reverse duty: ________
- East->west travel time (ms) or SUN_POS_WEST: ________

## Output format

Reply with:
1. A diff-style summary of every `config.h` line you changed (old -> new)
2. Confirmation that `SOLAR_TEST_MODE` is 0
3. Result of `verify-mbed-host-syntax.py` (PASS/FAIL)
4. One-line reminder: "Rebuild + flash in Keil Studio Cloud, branch
   SolarBugFixes."
