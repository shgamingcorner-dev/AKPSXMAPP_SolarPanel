# AKPS Solar Panel
Made by Song Heng, Sean, Quan Biao, Ashton, Louis

> 📌 **Pin map**: see [PIN_MAP.md](PIN_MAP.md) for the full NUCLEO-F103RB pin assignments.
> **Keypad**: 1=Blind, **2=Smart Mode**, 3=Lighting, 4=Fan speed menu.


## Mbed OS build tools

## Mbed Os Studio
The one our group uses is this you can download it from [here](https://github.com/ArmMbed). 
Just choose STM32F103RB as the device and run it

### Mbed CLI 2
Starting with version 6.5, Mbed OS uses Mbed CLI 2. It uses Ninja as a build system, and CMake to generate the build environment and manage the build process in a compiler-independent manner. If you are working with Mbed OS version prior to 6.5 then check the section [Mbed CLI 1](#mbed-cli-1).
1. [Install Mbed CLI 2](https://os.mbed.com/docs/mbed-os/latest/build-tools/install-or-upgrade.html).
1. From the command-line, import the example: `mbed-tools import mbed-os-example-blinky`
1. Change the current directory to where the project was imported.

### Mbed CLI 1
1. [Install Mbed CLI 1](https://os.mbed.com/docs/mbed-os/latest/quick-start/offline-with-mbed-cli.html).
1. From the command-line, import the example: `mbed import mbed-os-example-blinky`
1. Change the current directory to where the project was imported.

## Application functionality

The `main()` function is one of the two threads. It owns RFID polling, the keypad/LCD,
and the servo actuation consumers (blind, fan) that the network thread hands off
to it, plus the Smart Mode sensor loop.

The other thread is called `network_task()`. It owns everything network-related: joining WiFi
via the ESP-01, uploading sensor readings to ThingSpeak, and relaying Telegram alerts + Supabase
telemetry/alerts through the [AKPSRELAY](https://github.com/shgamingcorner-dev/AKPSRELAY) Flask
bridge (see "Architecture" below for why a relay exists at all).

## Architecture

```
STM32 (this repo) --HTTP (plain)--> AKPSRELAY (PythonAnywhere) --HTTPS--> Telegram / Supabase
                    \-HTTP (plain)-> ThingSpeak (accepts plain HTTP directly)
```

The ESP-01's AT firmware can only speak plain HTTP, not HTTPS/TLS. ThingSpeak accepts plain HTTP
so the board talks to it directly. Telegram and Supabase both require HTTPS, so a small Flask
relay ([AKPSRELAY](https://github.com/shgamingcorner-dev/AKPSRELAY), deployed on PythonAnywhere at
`shgam.pythonanywhere.com`) sits in the middle: the board POSTs plain HTTP to the relay, and the
relay forwards over HTTPS. The relay also holds the Telegram bot token and Supabase service key
server-side — the firmware only ever sees a shared `RELAY_SECRET`, never the real credentials.

Note: we tried hosting the relay on Replit first, but Replit's Autoscale deployments (Cloud
Run-backed) force HTTPS at the network edge and redirect any plain HTTP request — confirmed with
a direct `curl` test getting a 301 back. Since the ESP-01 can't follow a redirect to HTTPS, Replit
can't host this relay. PythonAnywhere's free tier serves plain HTTP with no forced redirect, which
is why it's the current host.

## What has been done

- **WiFi**: joins the configured AP (`WIFI_SSID`/`WIFI_PASSWORD` near the top of `main.cpp`).
  After 3 consecutive send failures the board re-runs the *entire* ESP init, not just
  `AT+CWJAP` — see the reconnect note under AT-command robustness for why a bare rejoin is
  not enough to recover.
- **RFID**: reads card/tag UID via the MFRC522, matches against `RFID_UID_CARD`/`RFID_UID_TAG`.
- **ThingSpeak**: uploads temperature, humidity, current, RFID state, and a simulated battery
  level every `SEND_INTERVAL_MS` (15s). The battery (field5, 0-100%) is a virtual demo: it
  charges +1%/tick when sunlight is detected and drains -1%/tick otherwise. Charge source is
  `BATTERY_SOURCE` in config.h: `0` = LDR (bright LDR = charging — works now), `1` = ACS712
  current (`|A| ≥ 0.5` = charging — for when the solar panel is wired in series). Display it
  with ThingSpeak's built-in Gauge widget (field5).
- **Current sensing**: `read_current()` reads a real ACS712 20A module (100mV/A) via `AnalogIn` on
  `PA_0`. The module is powered from 5V (zero output = VCC/2 ≈ 2.5V) and its OUT goes straight to
  PA_0 (ADC reads 0-3.3V, which comfortably covers the sensor's 2.5V zero point). 20 samples are
  averaged per read to smooth out noise from the shared power rail. **Calibrated 2026-08-09**:
  `ACS712_ZERO_V = 2.6V` (this module's measured zero with no load — the textbook 2.5V was off by
  0.1V and read as a false ~1A). If you re-zero it, read the sensor output with NO load and update
  `ACS712_ZERO_V` in `config.h`. Note: a single LED/resistor draws only a few mA — below the 20A
  module's ~0.1A resolution, so it reads ~0A; the sensor is meant for the solar panel output
  (several amps) wired in series with the panel.
- **Command Center**: `poll_device_state_via_relay()` polls the relay's `GET /device-state`
  route every `DEVICE_STATE_POLL_MS` (7s) and drives the actuators to match the
  dashboard's Supabase toggles, only actuating on an actual value change:
  - **Main lighting** on `MAIN_LIGHT_PIN` (`PB_1` = TIM3_CH4, *default* remap — NOT `PC_9`,
    whose full remap re-routes all of TIM3 and silently kills the PA_7 blind motor).
  - **Blind servo** on `PA_7` (TIM3_CH2, default remap), SG90 0°/180° = 600µs/2400µs.
  - **Fan** (`fan_power`/`fan_speed`), 360° continuous servo on `PA_1` (TIM2_CH2), neutral 1500µs.
  - **Smart Mode** (`smart_mode` boolean): when ON, the firmware drives lighting,
    fan, and blinds from its own LDR + DHT11 sensors (see "Smart Mode" below) and
    ignores remote writes for those three. Polls and pushes the `smart_mode` column
    end-to-end (the dashboard writes the same column via the relay's
    `POST /device-state` `field=smart_mode`), so keypad and dashboard are equal
    priority — last change wins.
  The keypad drives the same devices from the hardware side: **1**=Blind, **2**=Smart Mode,
  **3**=Lighting, **4**=Fan speed menu. Local changes get a 15s grace period so a stale
  remote read can't stomp a change whose relay push is still in flight.
- **Telegram alerts**: fires on RFID scan via the relay's `/telegram` route, rate-limited by
  `TG_COOLDOWN_MS`.
- **Supabase bridge**: sensor readings and RFID alerts are also pushed to Supabase
  (`sensor_telemetry` / `alert_logs` tables) via the relay's `/sensor-telemetry` and `/alert-log`
  routes, so the [FRONTENDAKPS](https://github.com/shgamingcorner-dev/FRONTENDAKPS) dashboard can
  display real hardware data instead of its mock simulation. Both routes dedupe on a `seq` counter
  (upsert with `on_conflict=seq` + `ignore-duplicates`) so a retried request after an AT-command
  timeout doesn't create a duplicate row.
- **Smart Mode** (automatic house automation): toggled by keypad **2** (or the dashboard's
  `smart_mode` toggle — equal priority, last change wins). While ON, the firmware drives:
  - **Lighting** from the LDR: darker outside → brighter inside (linear 20%→70% LDR maps
    to 100%→0% brightness).
  - **Fan** from the DHT11 temperature: 24°C→off, 32°C→100%, linear in between.
  - **Blinds** from the LDR: bright (≥60%) → up/open, dark (≤50%) → down/closed,
    with hysteresis in between. (Tuned 2026-08-09 from real finger tests: the bare
    LDR reads ~47% when covered, so DARK=50 closes on a finger-cover; BRIGHT=60
    re-opens on a flashlight.)
  A manual keypad press on blind/lighting/fan pauses Smart Mode for
  `SMART_MANUAL_OVERRIDE_MS` (60s) so a manual change sticks — last change wins.
  The `smart_mode` column is polled every 7s and pushed on keypad toggle, end-to-end
  through the relay's `POST /device-state` `field=smart_mode`.
- **Door lock was removed** (hardware no longer has the door servo). The old keypad **2**
  (Door Lock) is now the Smart Mode toggle; the door-lock code, the `door_locked`/
  `smart_lock` column aliasing, and the `POST /api/device/door` relay route are gone.
  If a `door_locked`/`smart_lock` reference survives anywhere, it is stale.
- **Solar tracker — two modes** (`sun_tracker.{h,cpp}`, select via `TRACKER_MODE` in config.h):
  - **`TRACKER_MODE 0` — LDR sweep-and-hold** (default, `tracker.cpp`): a 360° continuous
    motor on a pulley (`PB_0`, TIM3_CH3) sweeps through the panel arc while an LDR on
    `PA_4` is sampled every 100ms; the brightest angle is remembered, the motor returns
    there and holds `TRACKER_HOLD_BEST_MS`, then re-sweeps. No encoder — position is
    tracked as cumulative motor run-time (ms).
  - **`TRACKER_MODE 1` — SUN (astronomical)** (`sun_tracker.cpp`): computes the sun's
    azimuth/elevation for Singapore (1.35°N, 103.82°E, UTC+8) from the current time
    (NOAA solar equations, float, ~0.05° accuracy). Maps azimuth 90°→270° (east→west)
    linearly to motor position 0→5500ms and drives there, re-aiming every `SUN_UPDATE_MS`
    (5 min). At night (elevation < `SUN_MIN_ELEVATION` 3°) it parks at
    `SUN_NIGHT_PARK_POS` (0 = home/east). Time comes from the relay's new `/time`
    endpoint, fetched at boot + every `SUN_TIME_REFRESH_MS` (10 min); with no time it
    stays parked (safe default). **Calibration**: position 0 must physically face east
    at boot, and `SUN_POS_WEST` (5500ms) must equal the full east→west motor travel —
    adjust to your pulley rig if not.
- **Stability fixes**: RAM usage trimmed (smaller shared buffers, explicit thread stack size),
  and the Telegram response check now looks at the HTTP status line instead of searching for
  `"ok":true` in the JSON body, since the 256-byte read buffer can truncate the body before that
  substring appears.
- **Performance Optimizations**:
  - Reduced AT command timeouts throughout the network task (CIPSEND prompt: 2000ms→1000ms,
    data send: 5000ms→3000ms, close: 1000-2000ms→500-1000ms). TCP connect went the *other*
    way — 5000ms→2000-3000ms and then back up to 8000ms — because a failing DNS lookup takes
    far longer than a successful connect, and the short timeout was abandoning attempts while
    the module was still working on them.
  - Implemented polling-based `esp_read()` instead of blocking sleeps, eliminating unnecessary wait times
  - `esp_read()`/`at()` accept optional stop-token substrings (`",CONNECT"`/`"ERROR"` for
    `CIPSTART`, `">"` for the `CIPSEND` prompt, `"CLOSED"` for a finished HTTP response,
    `"GOT IP"`/`"FAIL"` for `CWJAP`) and return the moment that marker appears instead of
    waiting out the full timeout. Two earlier attempts at this were wrong, both instructive:
    - A blind "quiet gap" heuristic (return after 80ms of silence) rather than checking for
      the actual marker. It cannot tell "the response finished" from "the far end is slow",
      so it broke `CIPSTART` and `CWJAP`, whose real reply lags the command echo by more than
      the threshold.
    - The stop-token version itself, which returns *before consuming the rest of the
      response*. The leftover tail sat in the UART, the next command's read matched its own
      stop token against that stale text and returned early, and from then on every read
      answered the previous command — a permanent off-by-one. Fixed by `esp_drain()`.
  - Reduced network task idle polling from 50ms to 10ms for better responsiveness
  - **Note:** ThingSpeak and Supabase telemetry are *not* parallelised, despite a comment in
    `network_task()` claiming otherwise. `send_to_thingspeak()` runs to completion —
    `CIPCLOSE` included — before `send_sensor_telemetry_via_relay()` starts. Real concurrency
    would need per-connection buffers and a `+IPD,<id>,` demultiplexer, since both share one
    UART and one `g_tx`/`g_rx` pair. `AT+CIPMUX=1` makes it *possible*; nothing implements it.
- **DHT11: the sensor was on the wrong pin.** After a long hunt through the driver
  (critical sections, pull-ups, bit-loop timeouts — all of it reverted, see below), the actual
  cause was mundane: `DHT11_PIN` was `PA_1` while the sensor's DATA line is physically on
  `PC_4`. Every read timed out because nothing was ever connected to the pin being read.
  Changing the define fixed it outright, and `DHT11.cpp` is now back to the stock library.
  The lesson is the one already recorded further down this file, re-learned the hard way:
  when a subsystem fails *100%* of the time rather than intermittently, suspect wiring or
  configuration before logic — a real timing bug is almost never that consistent.
- **Keypad is interrupt-driven** (`keypad_utilities.cpp` / `keypad.h`): the 74C922's DA line
  is an `InterruptIn`, and its handler only reads the 4 data bits and sets a flag. Two traps
  worth knowing if this code is ever extended:
  - The data pins are four separate `DigitalIn`s, deliberately **not** a `BusIn`.
    `BusIn::read()` takes a `PlatformMutex` internally, and taking a mutex in ISR context is
    illegal under Mbed — it halts the board with `Mutex: Not allowed in ISR context` on the
    first keypress. `DigitalIn::read()` is a bare `gpio_read()` and is safe.
  - Mbed's `InterruptIn` callbacks run in **true ISR context**, not on a helper thread. No
    printf, no mutex, no blocking calls. The keypress is handed to `network_task()` through a
    mutex-protected flag, and the mutex is only ever taken on the main thread.
- **ESP-01 AT-command robustness** (`main.cpp`): several failures that all presented as
  "the network is broken" turned out to be protocol handling:
  - `esp_read()` returns as soon as its stop token appears, which left the tail of each
    response in the UART. The next command then consumed that stale text, matched its own
    stop token against it, and returned before its real reply arrived — after which *every*
    read was answering the previous command. `esp_drain()` now clears pending bytes before
    each transmit; anything buffered before we send is stale by definition.
  - A bare `OK` was accepted as a successful `AT+CIPSTART`. It proves nothing — after a
    reconnect the module replies `busy p...` then a stray `OK` with no socket open, and the
    code charged on into `CIPSEND` and got `link is not valid`. Success now requires
    `,CONNECT` (the leading comma matters: it excludes `ALREADY CONNECTED`).
  - Failed opens now always close their connection id. Without that, a socket that opened
    just after the timeout stayed open, the next attempt got `ALREADY CONNECTED`, was
    rejected, and returned without closing again — wedging that id permanently.
  - `wifi_reconnect()` re-runs the whole init instead of just `AT+CWJAP`. The ESP-01
    watchdog-resets on its own here (`rst cause:4`), and a reset silently drops `CIPMUX`
    back to 0, where every `AT+CIPSTART=<id>,...` is rejected with `Link type ERROR` and
    `AT+CIPCLOSE=<id>` answers `MUX=0`. A CWJAP-only reconnect rejoined the AP and still
    could not open a single socket until the board was power-cycled.
  - `esp_open_tcp()` retries once against `RELAY_IP` if the hostname fails. The ESP's DNS
    resolver is slow and unreliable, especially right after a reset clears its cache, and a
    failed lookup is indistinguishable from a dead server from the firmware's side. Requests
    still send `Host: RELAY_HOST` so virtual hosting is unaffected. **This is a fallback, not
    the default** — if the log shows the IP path being taken every time, the hostname is
    genuinely broken, and if PythonAnywhere renumbers, `RELAY_IP` needs re-resolving.
- **DHT11: one change kept, in `main.cpp` (not the driver).** `read_temperature()` and
  `read_humidity()` each used to trigger their own independent `readRawData()` transaction —
  two full sensor reads per cycle. A single combined `read_dht11()` (using
  `readTemperatureHumidity()`) now runs once per cycle and caches the result, halving bus
  traffic and, on a failed read, keeping the last good reading rather than falling back to the
  sentinel values. That matters because the sentinels (`2634`/`4001`) exceed the
  `NUMERIC(4,2)` limit on the Supabase columns, so every failed DHT11 read used to take the
  telemetry write down with it — one broken sensor silently 400'd an unrelated subsystem.
- **DHT11 driver changes that were tried and reverted** — `DHT11.cpp` is stock. Listed so they
  are not attempted again, since each looked plausible and each was wrong:
  - Wrapping the ACK wait and 40 data bits in a `CriticalSectionLock`, on the theory that
    `networkThread` was preempting the bit-bang. The failure rate did not move (100% before,
    100% after), which disproved it. Worse, it disabled interrupts for ~4-5ms, and the
    STM32F103's USART has a **1-byte** hardware buffer — so it was actively dropping ESP-01
    bytes and corrupting an unrelated subsystem to fix a non-problem.
  - Replacing the unbounded `while (pin == 0);` bit loops with a bounded helper. Defensible in
    isolation (they genuinely can hang), but the restructuring introduced an inverted flag: the
    ACK-wait loop only set `acked = true` *inside* its body, so when the sensor answered
    quickly — the normal, healthy case — the loop never executed and the read was reported as a
    timeout. Success was impossible. **The better the sensor, the more reliably it failed.**
  - Adding `pin_DHT11.mode(PullUp)`. Harmless and arguably correct, but it changed nothing,
    because the pin being biased was not the pin the sensor was attached to.
    

## What is still to be done

- **Other device-state fields.** `main_lighting`, `blind`, `fan_power`/`fan_speed`, and
  `smart_mode` are polled, actuated, and writable from the keypad. The `DeviceState` type in
  the frontend also has `hvacPower` and `securityArmState` — neither has a corresponding
  actuator or relay route on the hardware side yet.
- **Fan Supabase sync is racy.** The fan's actuate flag is consumed by *both* the network
  thread (which pushes to the relay) and the main loop (which moves the servo) — first
  consumer wins, so the fan can move locally while its dashboard push is skipped, or vice
  versa. The blind uses a two-flag pipeline that avoids this; the fan should be converted
  to the same pattern.
- **`gate_servo` was renamed to `blind`** across Supabase, the relay, the frontend, and the
  firmware — "gate" was never an accurate name for a window blind. If you find a `gate_servo`
  or `gateServo` reference anywhere, it is stale.
- **ACS712 calibrated (2026-08-09).** `ACS712_ZERO_V = 2.6V` is now the *measured* resting output
  of this specific 20A module (the old theoretical 1.5V/2.5V assumptions read a false ~1A at
  rest). It reads ~0.0A with no load. The remaining sub-0.1A wander is normal ADC noise — the 20A
  module can't resolve single-LED currents (a few mA); it's sized for the solar panel output.
- **WiFi instability under load.** Serial logs show `WIFI DISCONNECT` happening frequently, often
  right around an RFID scan — most likely the ESP-01 and MFRC522 briefly drawing current spikes at
  the same time and browning out a shared, under-rated power supply. The auto-reconnect logic
  papers over this but doesn't fix the root cause. Needs: a dedicated 5V supply (1-2A) feeding the
  ESP-01 adapter and RFID reader directly (not through the Nucleo's onboard 3.3V/5V pins), a bulk
  capacitor (100-470µF) near the ESP-01's power input, and a common ground across everything.
- **`TG_COOLDOWN_MS` is set to 5000 (5s) for testing** — comment in the code notes it should be
  raised back to a production value (e.g. 60000) before real deployment, so a card left near the
  reader doesn't spam Telegram/Supabase every 5 seconds.
- **`seq` counter resets to 0 on every reboot.** The dedup logic is only safe against retries
  within a single power-on session, not across reboots. Not currently a problem since it's a
  monotonic counter and Supabase's unique index just rejects the eventual re-collision, but worth
  knowing about.
- **Credentials are still committed in plaintext** (`WIFI_PASSWORD`, `RELAY_SECRET`) since this is
  a public repo shared with hardware that has no secret storage. Rotate `RELAY_SECRET` if it ever
  needs to change, and don't reuse a WiFi password here that matters elsewhere.
- **Performance Validation**: Test the optimized timeouts under various network conditions to ensure
  reliability is maintained while measuring actual performance gains in the field.

## Building and running

1. Connect a USB cable between the USB port on the board and the host computer.
2. Just use Keil Studio Cloud with the BIN file and build it on the board. Then just run it.
3. Remember to remove all previous data in telemetry tab on Supabase first.


Your PC may take a few minutes to compile your code.

Alternatively, you can manually copy the binary to the board, which you mount on the host computer over USB.

## Expected output
PB14 LED will be red when sending information through wifi
PB15 as well
The LED will be red if there is no RFID in range
Data will be uploaded to said thinkspeak
Data will be sent to supabase
Solar tracker:
- `TRACKER_MODE 0`: `[TRK] sweep done: best LDR xx.x% at pos xxx, DO=light` then
  `[TRK] reached best pos xxx` — the panel sweeps, aims at the brightest angle, holds.
- `TRACKER_MODE 1`: `[TIME] epoch=...` then `[SUN] az=xx.x ele=xx.x DAY -> target pos xxxx`
  (or `NIGHT -> target pos 0`). In daylight the motor drives to the sun's azimuth position
  and re-aims every 5 min; at night it parks at position 0 (face the panel east at boot so
  position 0 = east).

## Troubleshooting
Text me or its just skill issue idk

## Related Links

* [Mbed OS Stats API](https://os.mbed.com/docs/latest/apis/mbed-statistics.html).
* [Mbed OS Configuration](https://os.mbed.com/docs/latest/reference/configuration.html).
* [Mbed OS Serial Communication](https://os.mbed.com/docs/latest/tutorials/serial-communication.html).
* [Mbed OS bare metal](https://os.mbed.com/docs/mbed-os/latest/reference/mbed-os-bare-metal.html).
* [Mbed boards](https://os.mbed.com/platforms/).

### License and contributions

The software is provided under Apache-2.0 license. Contributions to this project are accepted under the same license. Please see [CONTRIBUTING.md](./CONTRIBUTING.md) for more info.

This project contains code from other projects. The original license text is included in those source files. They must comply with our license guide.