# Turnout controller: plan

## Goal

A JMRI MQTT turnout node on an M5Stack Stamp-S3Bat, controlling up to 11
turnouts over Wi-Fi through IoTT GreenHat Coil Driver boards, sold as the
3-Channel Turnout Pulse Driver (3 channels each, so 4 boards). The GreenHats already work when wired to a Raspberry
Pi running JMRI's GPIO turnouts, so the node reproduces that behaviour pin
for pin, only with MQTT in between.

## JMRI behaviour to reproduce

Taken from JMRI's source (`jmri/jmrix/pi/RaspberryPiTurnout.java`,
`jmri/jmrix/mqtt/MqttTurnout.java`, `MqttAdapter.java`, MQTT
`Bundle.properties`, JMRI master branch).

| Behaviour | JMRI | Node |
|---|---|---|
| Pin level | Pi: CLOSED = HIGH, THROWN = LOW; "Inverted" swaps them | `CLOSED` → HIGH, `THROWN` → LOW, steady (not pulsed) |
| Inversion | MQTT turnouts apply "Inverted" in JMRI before publishing | The node never inverts; JMRI's checkbox works as on the Pi |
| Topic | `<channel>track/turnout/{name}`, same topic for send and receive | Subscribe to `<channel>track/turnout/<name>`; channel configurable (empty on this layout) |
| Delivery | Retained, QoS 2 | Retained commands restore every turnout on reconnect |
| Other payloads | `UNKNOWN`, `INCONSISTENT` | Logged, pin untouched |
| Startup | Pi pin created LOW | Configurable (see below) |
| JMRI shutdown | Pi pin set LOW | Configurable (see below) |
| Feedback | Pi supports DIRECT only | DIRECT by default; optional `/state` topic for MONITORING |

Topic and naming rules shared with other projects are in
[../../docs/MQTT_CONVENTIONS.md](../../docs/MQTT_CONVENTIONS.md).

### Deliberate, configurable differences from the Pi

The GreenHat turns every *level change* into one coil pulse, with the
pulse length set by a trimmer on the board and the polarity set by the
edge direction. Its inputs have 10.2 kΩ pull-ups to 5 V, so an undriven
pin reads HIGH (CLOSED). Any unexpected edge moves a turnout, so (details
in [WIRING.md](WIRING.md)):

- **Startup level:** `restore` (default) re-applies the last state saved
  in flash as early in boot as possible, so turnouts end where JMRI left
  them. `low` matches the Pi exactly (all pins LOW until JMRI commands
  them, so every turnout pulses THROWN at startup). Either way, while the
  ESP32 is in reset its pins float and the GreenHat pull-ups take them
  HIGH, so THROWN turnouts give a CLOSED pulse and then a THROWN pulse
  across a node reset. CLOSED turnouts give none.
- **JMRI goes offline** (`<channel>track/state` = `OFFLINE`, which JMRI
  publishes retained as its last will and on a clean quit): `hold`
  (default) keeps every pin as it is. `low` matches the Pi's shutdown
  behaviour; when JMRI connects again (it clears `track/state`), the node
  re-reads the retained commands so the pins match JMRI's table.
- **Stagger:** optional delay between pin changes (default 0 ms, like the
  Pi) to spread coil current on the GreenHats' coil supply when JMRI sends
  all turnouts at once.
- **Minimum interval per turnout:** optional (default 0 ms, like the Pi).
  A reversal that arrives while the GreenHat's pulse is still running cuts
  that pulse short and can leave the turnout half-thrown. When set (to at
  least the trimmer's pulse length), a reversal is held back until the
  interval has passed. Only the latest command is kept.
- **Output stage:** `direct` (default, pin level as on the Pi) or
  `open-collector` (pin inverted, for a transistor stage between the node
  and the GreenHat; see WIRING.md).
- **Button single-click reset:** disabled at startup, with double-click
  power off. On the phase 1 bench a single click did nothing, but once
  phase 5 disabled double-click power off, a single click power-cycled
  the node (turnouts pulsed, then were restored). So the firmware turns
  both off, and locks the PM1's download mode (a held button otherwise
  stops the firmware); the button's only action is the 3 s long press.

## Hardware summary

Details in [WIRING.md](WIRING.md).

- ESP32-S3-PICO-1-N8R8 (8 MB flash, 8 MB PSRAM; PSRAM not used).
- Turnout outputs: **G1–G11** on the castellated edge pads. All free of
  internal functions. G3 is an ESP32-S3 strapping pin (JTAG source select,
  only active if a fuse is set); to be checked for glitches in phase 1.
- PM1 power-management chip on internal I2C (SDA G48, SCL G47, address
  0x6E) owns the RGB LED, the user button, battery and 5 V sensing,
  charging and the wake pin. None of these use an ESP GPIO.
- The button goes to the PM1: single-click = reset and double-click =
  power off are its defaults; the firmware disables both at startup and
  reads a long press through the PM1.
- GreenHat inputs: 10.2 kΩ pull-up to 5 V, Schmitt-trigger XOR delay
  stage at 5 V. Direct 3.3 V drive works (as on the Pi) but the gate's
  worst-case threshold isn't guaranteed below 3.3 V. An optional
  open-collector transistor stage per channel removes that doubt.
  Header pin 2 is the GreenHat's 5 V logic rail. This layout's GreenHats
  have J6 open, so pin 2 is fed from the node's **5VOUT** (PM1-switched
  boost), which lets the firmware sequence GreenHat logic power.

## Firmware design

PlatformIO + Arduino-ESP32, build target `[env:stamp-s3bat]`, plus
`[env:native]` for PC unit tests.

```
lib/turnout_core/   hardware-free logic, built for the board and for PC tests
  channels.*        channel ↔ GPIO table, forbidden-pin checks        (phase 1)
  turnout_state.h   CLOSED = HIGH, THROWN = LOW                       (phase 1)
  behaviour.*       startup/offline policies, stagger + interval
                    scheduler                                         (phase 3)
  command.*         serial console commands                           (phase 1)
  level_snapshot.*  pin levels kept in RTC memory across resets       (phase 1)
  jmri_protocol.*   topic/payload decoding                            (phase 2)
  net_config.*      network settings validation, setup page form      (phase 2)
  health.*          status LED states, button long press              (phase 5)
src/
  main.cpp          startup order, main loop, console                 (phase 1)
  turnout_bank.*    drive pins, pin latch, restore; later stagger and
                    minimum interval                                  (phase 1)
  power.*           PM1: 5VOUT, voltages (phase 1); LED, button,
                    watchdog                                          (phase 5)
  net.*             Wi-Fi + ESP-IDF esp-mqtt client: QoS 2, auto-reconnect,
                    last will                                         (phase 2)
  settings.*        network settings in flash (Preferences/NVS)       (phase 2)
  portal.*          setup access point and captive page               (phase 2)
  web.*             one web server for the setup and config pages     (phase 4)
  config_page.*     config page on the home network, admin login      (phase 4)
test/               PC unit tests for lib/turnout_core
tools/mqtt_exercise.py   drives a broker the way JMRI does              (phase 2)
```

The PM1 is driven with M5Stack's lightweight
[M5PM1](https://github.com/m5stack/M5PM1) library rather than M5Unified.
M5Unified detects the board by probing pins and displays used on other
M5Stack products, and some of those pins are turnout outputs here.

### Startup order

1. Load config from flash.
2. Drive every pin as early as possible, from a C++ constructor before
   `app_main()`: to the levels in RTC memory after a reset, or all LOW
   after a power cut (5VOUT is off then, so nothing can pulse). Then, from
   `initVariant()` once flash is readable, apply the startup policy
   (restore: RTC levels after a reset, flash levels after a power cut;
   low: all LOW). Latch every pin (`gpio_hold_en`), which may keep outputs
   steady across software resets. The float time and which reset types
   the latch survives are still unmeasured (the phase 1 reset tests were
   skipped). Even from a constructor, the pins are first driven about
   190 ms after reset: most of that appears to be the bootloader checking
   the firmware image (PHASE3_BENCH.md, run 1), which firmware can't
   shorten. A power cycle starts cold with no movement.
3. Turn on 5VOUT (GreenHat logic power, PM1 G1) only after the pins are
   at their levels, so THROWN turnouts don't pulse at startup.
4. Start Wi-Fi (modem sleep off for low latency).
5. Connect MQTT with last will `cikutrail/<node>/status` = `offline`;
   publish `online`.
6. Subscribe to each turnout's command topic. Retained messages set each
   turnout to its last commanded state.

Restarts are treated as unsafe: whether the pin latch holds through a
reset was never measured (the phase 1 reset tests were skipped), and a
reset can make THROWN turnouts pulse. So the firmware never restarts
itself: settings apply in place (the setup page, the config page,
`config`), and phase 4 leaves out OTA and a Reboot button.
Details in WIRING.md, "Power sequencing through 5VOUT".

### Command handling

- Only a real change touches the pin; a repeat of the current state does
  nothing.
- Stagger and minimum interval (if set) are applied here; a held-back
  change keeps only the newest command.
- The levels are saved to flash 2 s after the last change, so a burst of
  changes costs one write.
- Optional feedback: publish the new state on
  `<channel>track/turnout/<name>/state`, never on the command topic.

### Status

- `cikutrail/<node>/status`: `online` / `offline`, retained.
- `cikutrail/<node>/info`: retained JSON, refreshed periodically:
  firmware version, IP, RSSI, uptime, 5 V input present.
- LED: blue blink = setup portal, red blink = error (network off),
  yellow = connecting, slow green blink = JMRI offline, green = subscribed.

## Setup and configuration

### First start

1. No saved Wi-Fi → the node starts access point `CikutRail-XXXX` (last 4
   characters of the MAC). Its password is random, made on first use,
   kept in flash and printed on the serial console. LED blinks blue.
2. Joining it opens a captive setup page at `192.168.4.1`, with a list of
   scanned networks.
3. Save → join Wi-Fi → connect to MQTT → LED green. No reboot: a reset
   can make THROWN turnouts pulse (WIRING.md), so new settings are
   applied in place. The access point closes 30 s after the node joins.
   (Built in phase 2; the LED comes in phase 5.)
4. If Wi-Fi can't be joined for about 3 minutes, the setup access point
   reopens, and the node retries the saved network every 2 minutes while
   nobody is on the page. A long button press (phase 5) or the console's
   `portal on` opens it at any time. Turnout pins keep their levels
   throughout.

### Config page

The config page is served at `http://<node>.local` (mDNS) on the home
network, behind an admin password (built in phase 4, below).

| Section | Settings (defaults) |
|---|---|
| Wi-Fi | SSID, password |
| MQTT | host, port (1883), username, password, client ID (node name) |
| JMRI | channel (empty; older JMRI uses `/trains/`) |
| Node | node name, admin password (random, shown on the USB console) |
| Turnouts (G1–G11) | enabled, JMRI name (`101`–`111` → `MT101`–`MT111`), test button |
| Behaviour | startup level (`restore` / `low`), JMRI offline (`hold` / `low`), stagger ms (0), minimum interval per turnout ms (0); output stage and feedback topic not built |
| Maintenance | factory reset (OTA and reboot left out: they need a restart) |

Implementation: one small server on Arduino `WebServer` + `DNSServer`,
used for both first setup and later edits. WiFiManager is not used: its
custom fields handle an 11-row table poorly and it would mean two config
paths.

## Build phases

Each phase is tested on real hardware before the next starts.

1. **Pins.** PlatformIO project, turnout bank, serial commands to set pins.
   On a GreenHat, check:
   - each command fires exactly one pulse in the right direction;
   - the coil current stops after the pulse at both levels (confirms the
     Schmitt XOR reads the 3.3 V HIGH);
   - how long pins float at power-up and reset, and which resets the pin
     latch survives (software restart, watchdog, PM1 button reset);
   - G3 behaves like the other pins through a reset;
   - 5VOUT feeds the GreenHats' logic, and whether the PM1 keeps 5VOUT on
     through each kind of ESP32 reset;
   - the 5VOUT power sequence: no THROWN turnout pulses at cold start or
     on a planned restart.

   Bench run 1 ([PHASE1_BENCH.md](PHASE1_BENCH.md)) covered the pulse,
   coil-off, 5VOUT and cold-start checks. The reset checks (float time,
   the pin latch, G3, 5VOUT through a reset, planned restart) were
   skipped and stay open.
2. **MQTT.** Wi-Fi + broker. The first-setup part of phase 4 is brought
   forward: the setup access point and page for Wi-Fi, MQTT, channel and
   node name, saved in flash, applied without a reboot. Turnout names stay
   compiled in. JMRI turnouts in DIRECT mode, checked against a JMRI panel.
   `tools/mqtt_exercise.py` for repeatable tests. Procedure in
   [PHASE2_BENCH.md](PHASE2_BENCH.md).
3. **Startup behaviour.** Saved state, restore/low policy, earlier pin
   drive, JMRI-offline policy, stagger, minimum interval, set from the
   console (`config`) and saved in flash. No firmware-initiated restarts.
   Procedure in [PHASE3_BENCH.md](PHASE3_BENCH.md).
4. **Setup and config.** Config page on the home network (mDNS, admin
   login) for network, turnout names, behaviour, admin password and
   factory reset; turnout test buttons. No OTA and no Reboot button, by
   decision: firmware stays on USB, and the node never restarts itself.
   Procedure in [PHASE4_BENCH.md](PHASE4_BENCH.md).
5. **Health.** Status LED states (blue blink setup, red blink error,
   yellow connecting, slow green blink JMRI offline, green OK), long press
   (3 s) opens the setup access point, double-click power-off disabled,
   PM1 watchdog off by default with a console test (`wdt hang`) to see
   what its reset does before deciding. Procedure in
   [PHASE5_BENCH.md](PHASE5_BENCH.md).
6. **Optional.** MONITORING feedback topic; per-pin sensor mode
   (emulating JMRI Pi sensors) if inputs are needed later.

## Open items

- Losing node power with the coil supply still on makes CLOSED turnouts
  pulse THROWN (WIRING.md, power sequencing). Decide whether the layout's
  supplies need arranging so this can't happen, or whether the retained
  restore is enough.
- After a reset the pins float for about 190 ms, mostly in the
  bootloader's image check. A smaller image, or building the bootloader
  to skip the check on reset, would shorten it; the pin latch might cover
  it, if it holds (unmeasured).
- Stray input arrives on the USB console when the port reappears (likely
  ModemManager; see PHASE1_BENCH.md for the udev rule). The lost output
  lines seen in phases 2-3 were at least partly two bench programs
  reading the console at once (PHASE4_BENCH.md, run 1).
- The config page stalls for up to 5 s when a browser holds an idle
  connection (Arduino `WebServer` serves one at a time). Switch to
  `esp_http_server` if it matters in use.
