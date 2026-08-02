# AKPS Solar Panel
Made by Song Heng, Sean, Quan Biao, Ashton, Louis


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
and the servo actuation consumers (blind, door, fan) that the network thread hands off
to it.

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
- **ThingSpeak**: uploads temperature, humidity, current, and RFID state every `SEND_INTERVAL_MS`
  (15s).
- **Current sensing**: `read_current()` reads a real ACS712 (20A, 100mV/A) via `AnalogIn` on
  `PA_0`. The sensor runs on 5V but the STM32 ADC only tolerates 3.3V, so its output goes through a
  10kΩ/15kΩ divider first (0.6 ratio) — the firmware undoes that scaling when converting the pin
  voltage back to amps. 20 samples are averaged per read to smooth out noise from the shared power
  rail (see the WiFi instability note below). If a reading looks off-zero with nothing connected,
  recalibrate `ACS712_ZERO_V` in `main.cpp` to the value you actually measure — sensor offset and
  divider resistor tolerance both shift this slightly from the ideal 1.5V.
- **Command Center**: `poll_device_state_via_relay()` polls the relay's `GET /device-state`
  route every `DEVICE_STATE_POLL_MS` (7s) and drives the actuators to match the
  dashboard's Supabase toggles, only actuating on an actual value change:
  - **Main lighting** on `MAIN_LIGHT_PIN` (`PB_1` = TIM3_CH4, *default* remap — NOT `PC_9`,
    whose full remap re-routes all of TIM3 and silently kills the PA_7 blind motor).
  - **Blind servo** on `PA_7` (TIM3_CH2, default remap), SG90 0°/180° = 600µs/2400µs.
  - **Fan** (`fan_power`/`fan_speed`), 360° continuous servo on `PA_1` (TIM2_CH2), neutral 1500µs.
  - **Door lock** on `PA_6` (TIM3_CH1, default remap), SG90 LOCKED 600µs / UNLOCKED 2400µs.
    Polls and pushes the `door_locked` column — the same field the dashboard writes via the
    relay's `POST /api/device/door` — and the relay keeps the legacy `smart_lock` column
    aliased to it, so the two can never drift apart.
  The keypad drives the same devices from the hardware side: **1**=Blind, **2**=Door Lock,
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
- **Door lock + state-sync fixes** (the "door unlocks itself" saga): the door lock is an SG90
  servo on `PA_6` (TIM3_CH1, default remap — same remap mode as the PA_7 blind and PB_1 light,
  so no timer-remap fight), toggled by keypad **2**, and follows the dashboard's `door_locked`
  column every 7s poll. Three bugs had to be killed to make it hold:
  - **Telemetry overwrote the state.** The firmware used to include `door_locked`/`fan_power`/
    `fan_speed` in the 15s telemetry body; the relay then PATCHed them into `device_states`.
    Worse, once telemetry went sensors-only, the relay's `request.form.get('door_locked', 'false')`
    default wrote `door_locked=false` *every 15s* anyway — the door "unlocked itself" ~15s after
    any remote change. Fixed on both sides: firmware telemetry is sensors-only, and the relay
    only writes fields actually present in the body (`if 'door_locked' in request.form:`).
  - **Two DB columns, one door.** The dashboard writes `door_locked` (via `POST /api/device/door`);
    a legacy `smart_lock` column existed too, and the two drifted apart. The firmware now polls
    and pushes `door_locked` end-to-end (the field the dashboard actually writes), and the relay
    aliases `smart_lock` ↔ `door_locked` on every write path so they can never disagree.
  - **Local vs remote race.** The door uses the same two-flag pipeline as the blind (keypad
    `toggle` flag → network thread pushes + queues an `actuate` flag → main loop moves the servo),
    with a 15s grace period after a keypad press so a stale remote read can't re-apply while the
    push is still in flight.
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
  `door_locked` are polled, actuated, and writable from the keypad. The `DeviceState` type in
  the frontend also has `hvacPower` and `securityArmState` — neither has a corresponding
  actuator or relay route on the hardware side yet.
- **Fan Supabase sync is racy.** The fan's actuate flag is consumed by *both* the network
  thread (which pushes to the relay) and the main loop (which moves the servo) — first
  consumer wins, so the fan can move locally while its dashboard push is skipped, or vice
  versa. The blind/door use a two-flag pipeline that avoids this; the fan should be converted
  to the same pattern.
- **`gate_servo` was renamed to `blind`** across Supabase, the relay, the frontend, and the
  firmware — "gate" was never an accurate name for a window blind. If you find a `gate_servo`
  or `gateServo` reference anywhere, it is stale.
- **ACS712 still needs calibrating.** It reads roughly 0.5-5 A at rest and the pin voltage
  wanders between ~1.5 V and ~1.8 V, so the noise alone is worth several amps. `ACS712_ZERO_V`
  is still the *theoretical* 1.5 V rather than a measured one. Measure the resting `pin=X.XV`
  from the debug line with genuinely zero current flowing and set the constant to that. The
  wander itself is worth investigating separately — it points at the same shared-rail
  instability as the ESP-01 resets.
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