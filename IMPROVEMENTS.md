# AKPSXMAPP_SolarPanel — Consolidated Improvement Roadmap

> Combined view of (A) code-quality improvements from the code review and
> (B) remaining phases from the solar-panel tracker plan
> (`.hermes/plans/2026-08-03_021829-solar-panel-tracker.md`).
> Last updated: 2026-08-03

---

## A. Code-Quality Improvements (from review)

| # | Improvement | Risk | Est. Time | Confidence |
|---|-------------|------|-----------|------------|
| A1 | Robust response parsing — replace `strstr(g_rx, "\"door_locked\":true")` with `sscanf`/tokenized parse | Medium | 30–60 min | High |
| A2 | Unify `send_device_state_via_relay()` + `send_device_state_int_via_relay()` into one overloaded sender | Low | 20–30 min | High |
| A3 | Encapsulate ~20 globals + 14 mutexes in main.cpp into a `SystemState` struct/module | Low–Med | 1–2 hrs | Medium |
| A4 | Static buffers — replace `new (nothrow) char[BUF/RX_BUF]` with `static char` | Very Low | 5 min | Very High |
| A5 | Fan duty constants — move `0.075f`, `0.0005f`, `0.05f`, `0.10f` to config.h | Very Low | 10 min | Very High |
| A6 | TIM3 pin static asserts — fail build if pins move to PC_8/PC_9 | Low | 15 min | High |
| A7 | IWDG watchdog — kick in main loop; recover from ESP-01 AT hangs | Medium | 30–45 min | Medium |
| A8 | Servo pulse bounds checks — clamp/assert sane `PULSE_WIDTH_*` ranges | Low | 10 min | High |
| A9 | Split `secrets.h` (SSID, password, API keys) gitignored | Low | 10 min | High |
| A10 | Document mutex lock ordering (fan_timestamp ↔ fan_actuate) | Low | 15 min | Very High |
| A11 | Gate debug `printf` spam behind `#ifdef DEBUG` | Low | 20 min | High |
| A12 | Delete dead code — `test_door_servo()`, unused globals (`passWord[]`, `key2`, `outChar3`) | Very Low | 15 min | Very High |
| A13 | Fix `esp_read()` indentation; use `PRIu64` instead of `%llu` | Very Low | 10 min | Very High |
| A14 | `const` correctness — message strings → `const char[]` (flash not RAM) | Very Low | 15 min | Very High |

## B. Tracker Plan Remaining Work (from .hermes/plans)

| # | Task | Phase | Risk | Est. Time | Confidence |
|---|------|-------|------|-----------|------------|
| B1 | Create `hermes-verify-tracker-sim.py` — Python behavioral oracle (convergence, cloud dip, local max re-sweep, night park) | 2 | Low | 1–2 hrs | High |
| B2 | relay.py: NOAA-style `solar_target_angle()` (lat/lon = Singapore placeholder → real location), add `tracker_target` to `GET /device-state`, deploy + Reload on PythonAnywhere | 3 | Medium | 1–2 hrs | Medium |
| B3 | main.cpp poll: parse `tracker_target`, call `tracker_set_sun_target()` (0–180, or -1 at night) | 3 | Low | 20–30 min | High |
| B4 | Night parking — if `tracker_target < 0 && fb < TRACKER_CLOUD` → park at `TRACKER_MIN_ANGLE`, skip stepping until dawn | 4 | Low | 30 min | High |
| B5 | Startup behavior — first tick after 3s: move to sun target if known, else hill-climb from 90° | 4 | Low | 20 min | High |
| B6 | Current-feedback upgrade — wire panel through ACS712, calibrate `ACS712_ZERO_V`, flip `TRACKER_FEEDBACK_CURRENT 1`, re-verify | 0.4 + 4 | Medium | 1–2 hrs (hardware) | Medium |
| B7 | Telemetry — push `tracker_angle` on change (≥5°), relay field support, ThingSpeak field5, frontend display | 5 | Low | 1–2 hrs | Medium |

## C. Suggested Execution Order

1. **Quick wins first:** A4, A5, A8, A12, A13, A14 (~65 min, zero regression risk)
2. **Parser hardening:** A1 (before any relay format change)
3. **Tracker finish (do B before A3 — tracker is self-contained, main.cpp refactor is not):**
   - B3 (firmware poll) → B2 (relay solar math) → B4 (night park) → B5 (startup)
   - B1 (sim) is the test oracle — do before B2/B3 if you want TDD
   - B6 only when hardware is wired
   - B7 last (dashboard polish)
4. **Architecture:** A3, A2, A7 after the demo, with a full hardware test cycle

## Notes / Constraints from the Plan

- **Do NOT** split main.cpp's door/blind/fan pipelines into modules before the demo. It works; it was hard-won. (Plan's explicit scope decision)
- **Do NOT** move `read_current()`/ACS712 out of main.cpp yet (blast radius).
- **TIM3 trap:** never use PC_8/PC_9 for any new servo/PWM — full remap silently kills door (PA_6), blind (PA_7), light (PB_1).
- **ACS712 is wired but measures nothing** until Phase 0.4 — keep LDR feedback until calibrated.
- External 5V supply for servos (never Nucleo rail); clamp 10–170°.
