# AKPS Solar Panel — Pin Config Map

Target: **NUCLEO-F103RB** (STM32F103RB, Mbed OS 6)
Updated: 2026-08-08 — **door lock servo (PA_6) removed; keypad '2' is now Smart Mode toggle**

> **TIM3 remap rule (critical):** every PWM user must stay on the **DEFAULT remap**
> (`PA_6`/`PA_7`/`PB_0`/`PB_1`). NEVER use `PC_8`/`PC_9` (TIM3 full remap) — it
> re-routes every TIM3 channel and silently kills the blind motor / light / tracker.

## Pin assignments

| Pin | Function | Type | Peripheral | Notes |
|-----|----------|------|------------|-------|
| **PA_0** | Current sensor (ACS712) | AnalogIn | ADC1_IN0 | 20A sensor via 10k/15k divider, 0.6 ratio |
| **PA_1** | Fan servo | PwmOut | TIM2_CH2 | 360° continuous servo, neutral 1500µs (0.075 duty) |
| **PA_2** | MFRC522 RST | DigitalOut | — | Reset/power-down for RFID |
| **PA_4** | LDR analog (bare LDR + divider) | AnalogIn | ADC1_IN4 | 3.3V→LDR→PA_4→10k→GND; bright = high % |
| **PA_7** | Blind/curtain motor | PwmOut | TIM3_CH2 (default) | SG90, 600µs closed / 2400µs open |
| **PB_0** | Solar tracker motor | PwmOut | TIM3_CH3 (default) | 360° continuous motor, pulley |
| **PB_1** | ~~Main light~~ **FREE** | — | TIM3_CH4 (default) | Moved to PB_7/TIM4 — see below |
| **PB_2** | MFRC522 SS (NSS) | DigitalOut | — | SPI chip select (active low) |
| **PB_3** | SPI SCK (MFRC522) | SPI | SPI1 | 4MHz |
| **PB_4** | SPI MISO (MFRC522) | SPI | SPI1 | |
| **PB_5** | SPI MOSI (MFRC522) | SPI | SPI1 | |
| **PB_6** | Red LED | DigitalOut | — | RFID no-match indicator |
| **PB_7** | **Main light** | PwmOut | **TIM4_CH2** | **Moved here from PB_1/TIM3!** 100Hz PWM, brightness 0-100%. TIM4 keeps the light's 10ms period SEPARATE from TIM3 (servos need 20ms — sharing TIM3 with the light's period_ms(10) broke the blind/tracker PWM). |
| **PB_8** | Keypad D0 | DigitalIn | — | 74C922 data bit 0 |
| **PB_9** | Keypad D1 | DigitalIn | — | 74C922 data bit 1 |
| **PB_10** | Keypad D2 | DigitalIn | — | 74C922 data bit 2 |
| **PB_11** | Keypad D3 | DigitalIn | — | 74C922 data bit 3 |
| **PB_12** | DHT11 VCC (power) | DigitalOut | — | Repointed here to free PB_0 for tracker |
| **PB_13** | Keypad DA (Data Available) | InterruptIn | — | 74C922 rising edge → ISR |
| **PB_14** | Buzzer **and** TX LED | DigitalOut | — | Shared — see note below |
| **PB_15** | RX LED | DigitalOut | — | |
| **PC_0** | Blue LED | DigitalOut | — | RFID match indicator |
| **PC_1** | Green LED | DigitalOut | — | WiFi/network OK |
| **PC_4** | DHT11 data | DigitalIn/Out | — | Temperature/humidity |
| **PC_6** | LCD WR (write) | DigitalOut | GPIO | Keep GPIO — no TIM3 AF conflict |
| **PC_7** | LCD RS (register select) | DigitalOut | GPIO | |
| **PC_10** | ESP-01 TX (to ESP RX) | UART TX | USART3 | |
| **PC_11** | ESP-01 RX (from ESP TX) | UART RX | USART3 | |
| **PA_8–PA_11** | LCD data D4–D7 | PortOut | PortA mask 0xF00 | 4-bit mode |
| **PA_12** | LCD EN (enable) | DigitalOut | GPIO | |
| **PD_2** | ~~LDR DO (module)~~ | **REMOVED** | — | Bare LDR has no DO; dark derived from analog |
| **PA_6** | ~~Door lock servo~~ | **REMOVED** | — | Hardware no longer has the door servo |

## Removed (door lock)

- `PA_6` — door lock SG90 servo: **removed** from config.h and main.cpp.
- `DOOR_LOCK_PIN`, `DOOR_LOCK_LOCKED/UNLOCKED` defines — **removed**.
- All door code (`doorLock`, `door_mutex`, `door_locked`, `apply_door_lock`,
  `request_door_*`, `consume_pending_door_*`, `get_door_locked`, `test_door_servo`,
  `set_door_lock`) — **removed**.
- Keypad **2** is now the **Smart Mode toggle** (was "Door Lock").
- Relay `POST /api/device/door` route — **removed**; `door_locked`/`smart_lock`
  fields dropped from the firmware-facing GET/POST payloads.

## Bare LDR wiring (replaces the LDR module)

The module's **AO** (analog out) and **DO** (digital comparator out) are replaced
by a single bare LDR + one resistor divider on **PA_4**:

```
3.3V ── LDR ── PA_4 ── 10kΩ ── GND
```

- Brighter light → LDR resistance drops → voltage at PA_4 rises → higher %.
- Matches `SMART_LDR_INVERT 0` (AO reads HIGH in bright). If the serial log shows
  the opposite, flip `SMART_LDR_INVERT` to 1.
- **No DO pin**: the old module comparator (`PD_2`) is gone. "Dark" is derived
  from the analog reading: `tracker_is_dark()` returns true when LDR% <
  `TRACKER_LDR_FLOOR` (5.0). The threshold is adjustable in config.h.
- `PD_2` is now free (was `LDR_DO_PIN`).

## Keypad map (after change)

| Key | Action |
|-----|--------|
| **1** | Toggle blind/curtain (push `blind` to Supabase) |
| **2** | Toggle **Smart Mode** (push `smart_mode` to Supabase) |
| **3** | Toggle main lighting (push `main_lighting` + brightness) |
| **4** | Fan speed menu (1=Low 33%, 2=Med 66%, 3=High 100%, 4=Off) |

## Shared-pin note (PB_14)

`BUZZER_PIN` and the TX activity LED (`led_tx`) both live on **PB_14**. The buzzer
is a passive beeper driven by the music utilities; the LED is a status indicator.
They share the pin by design — both are DigitalOut and only one drives at a time
(buzzer during melodies, LED for UART activity). If you ever see the buzzer and
LED fighting, move one to a free pin (e.g. `PC_5`).

## Supabase `device_states` columns in use

| Column | Firmware poll | Firmware push | Keypad |
|--------|---------------|---------------|--------|
| `blind` | ✅ | ✅ | 1 |
| `main_lighting` | ✅ | ✅ | 3 |
| `lighting_brightness` | ✅ | ❌ (read only) | — |
| `fan_power` | ✅ | ✅ | 4 |
| `fan_speed` | ✅ | ✅ | 4 |
| `smart_mode` | ✅ | ✅ | **2** |

## Smart Mode config knobs (config.h)

| Define | Value | Meaning |
|--------|-------|---------|
| `SMART_MODE_DEFAULT` | 0 | Boot state (0=off) |
| `SMART_LDR_INVERT` | 0 | Flip if LDR AO reads HIGH in dark |
| `SMART_LIGHT_DARK_LDR` | 20% | LDR ≤ → 100% brightness |
| `SMART_LIGHT_BRIGHT_LDR` | 70% | LDR ≥ → 0% (off) |
| `SMART_FAN_TEMP_OFF` | 24°C | Temp ≤ → fan off |
| `SMART_FAN_TEMP_MAX` | 32°C | Temp ≥ → fan 100% |
| `SMART_BLIND_BRIGHT_LDR` | 60% | LDR ≥ → blinds up |
| `SMART_BLIND_DARK_LDR` | 15% | LDR ≤ → blinds down |
| `SMART_UPDATE_MS` | 5000 | Re-evaluate cadence |
| `SMART_MANUAL_OVERRIDE_MS` | 60000 | Manual keypad pauses Smart Mode 60s |
