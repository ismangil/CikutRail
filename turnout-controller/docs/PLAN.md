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
- **JMRI goes offline** (JMRI's last-will `<channel>track/state` =
  `OFFLINE`): `hold` (default) keeps every pin as it is. `low` matches the
  Pi's shutdown behaviour.
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
- **Button single-click reset:** the PM1 resets the ESP32 on a single
  click by default. A reset makes THROWN turnouts pulse, so the config page
  can disable single-click reset (double-click power off stays).
  (Phase 1 bench: a single click on USB power did not reset the ESP32; see
  PHASE1_BENCH.md, test 9.)

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
  power off are its defaults. The firmware reads a long press through the
  PM1, and can disable single-click reset (see above).
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
  command.*         serial console commands                           (phase 1)
  level_snapshot.*  pin levels kept in RTC memory across resets       (phase 1)
  jmri_protocol.*   topic/payload decoding                            (phase 2)
  net_config.*      network settings validation, setup page form      (phase 2)
src/
  main.cpp          startup order, main loop, console                 (phase 1)
  turnout_bank.*    drive pins, pin latch, restore; later stagger and
                    minimum interval                                  (phase 1)
  power.*           PM1: 5VOUT, voltages; later LED, button, watchdog (phase 1)
  net.*             Wi-Fi + ESP-IDF esp-mqtt client: QoS 2, auto-reconnect,
                    last will                                         (phase 2)
  settings.*        network settings in flash (Preferences/NVS)       (phase 2)
  portal.*          setup access point and captive page; later the
                    config page and OTA                               (phase 2, 4)
test/               PC unit tests for lib/turnout_core
tools/mqtt_exercise.py   drives a broker the way JMRI does              (phase 2)
```

The PM1 is driven with M5Stack's lightweight
[M5PM1](https://github.com/m5stack/M5PM1) library rather than M5Unified.
M5Unified detects the board by probing pins and displays used on other
M5Stack products, and some of those pins are turnout outputs here.

### Startup order

1. Load config from flash.
2. Drive every enabled pin to its startup level (restore or low), as
   early in boot as possible to shorten the time the pins float (from
   Arduino's `initVariant()`, before `setup()`), and
   latch it (`gpio_hold_en`) to try to keep outputs steady across
   software resets and OTA reboots. The float time and which reset types
   the latch survives are still unmeasured (the phase 1 reset tests were
   skipped). The pins are driven about 100 ms after app start in the
   phase 1 firmware and about 210 ms in phase 2, on every power-on (the
   cause is still to be found); phase 3 should drive them before
   Arduino's start-up code runs. A power cycle starts cold with no
   movement.
3. Turn on 5VOUT (GreenHat logic power, PM1 G1) only after the pins are
   at their levels, so THROWN turnouts don't pulse at startup.
4. Start Wi-Fi (modem sleep off for low latency).
5. Connect MQTT with last will `cikutrail/<node>/status` = `offline`;
   publish `online`.
6. Subscribe to each turnout's command topic. Retained messages set each
   turnout to its last commanded state.

Planned restarts (OTA, config save, reboot from the web page) rely on the
pin latch holding every pin through the restart. Whether turning 5VOUT
off first helps as well is a phase 1 test: the driven pins partly power
the GreenHat's 5 V rail through its pull-ups, so it isn't certain.
Details in WIRING.md, "Power sequencing through 5VOUT".

### Command handling

- Only a real change touches the pin; a repeat of the current state does
  nothing.
- Stagger and minimum interval (if set) are applied here; a held-back
  change keeps only the newest command.
- Each change is saved to flash (write only on change, to limit wear).
- Optional feedback: publish the new state on
  `<channel>track/turnout/<name>/state`, never on the command topic.

### Status

- `cikutrail/<node>/status`: `online` / `offline`, retained.
- `cikutrail/<node>/info`: retained JSON, refreshed periodically:
  firmware version, IP, RSSI, uptime, 5 V input present.
- LED: blue blink = setup portal, yellow = connecting, green = subscribed,
  red blink = error.

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

Also served afterwards at `http://<node>.local` (mDNS), behind an admin
password.

| Section | Settings (defaults) |
|---|---|
| Wi-Fi | SSID, password |
| MQTT | host, port (1883), username, password, client ID (node name) |
| JMRI | channel (empty; older JMRI uses `/trains/`) |
| Node | node name, admin password |
| Turnouts (G1–G11) | enabled, JMRI name (`101`–`111` → `MT101`–`MT111`), test button |
| Behaviour | output stage (`direct` / `open-collector`), startup level (`restore` / `low`), JMRI offline (`hold` / `low`), stagger ms (0), minimum interval per turnout ms (0), button single-click reset (on), feedback topic (off) |
| Maintenance | OTA firmware upload, reboot, factory reset |

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
3. **Startup behaviour.** Saved state, restore/low policy, pin latching
   across restarts, JMRI-offline policy, stagger, minimum interval,
   single-click reset option.
4. **Setup and config.** Captive portal, config page, mDNS, OTA.
5. **Health.** LED states, long press, PM1 watchdog.
6. **Optional.** MONITORING feedback topic; per-pin sensor mode
   (emulating JMRI Pi sensors) if inputs are needed later.

## Open items

- Losing node power with the coil supply still on makes CLOSED turnouts
  pulse THROWN (WIRING.md, power sequencing). Decide whether the layout's
  supplies need arranging so this can't happen, or whether the retained
  restore is enough.
- Phase 2 bench: ch2's pad read HIGH at a power-on, before the firmware
  drove it, with 5VOUT off. Not seen in phase 1; cause unknown.
- The USB console occasionally loses a line of output, and stray input
  arrives when the port reappears (likely ModemManager; see
  PHASE1_BENCH.md for the udev rule).
