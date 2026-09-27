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
| Topic | `<channel>track/turnout/{name}`, same topic for send and receive | Subscribe to `<channel>track/turnout/<name>`; channel configurable |
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
- **Button single-click reset:** the PM1 resets the ESP32 on a single
  click by default. A reset makes THROWN turnouts pulse, so the config page
  can disable single-click reset (double-click power off stays).

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
- GreenHat inputs: 10.2 kΩ pull-up to 5 V, 3.3 V drive works (as on the
  Pi) but is below the 74HC86's guaranteed 5 V HIGH threshold. Header
  pin 2 carries the GreenHat's 5 V and must not be connected to the node.

## Firmware design

PlatformIO + Arduino-ESP32, build target `[env:stamp-s3bat]`, plus
`[env:native]` for PC unit tests.

```
src/
  main.cpp          startup order, main loop
  config.*          settings in flash (Preferences/NVS), pin validation
  turnout_bank.*    11 channels: name ↔ GPIO, drive, save state, stagger,
                    minimum interval
  mqtt_link.*       ESP-IDF esp-mqtt client: QoS 1/2, auto-reconnect, last will
  jmri_protocol.*   topic/payload decoding; pure logic, unit-tested on PC
  portal.*          captive portal, web config, OTA upload
  health.*          M5Unified: battery, charging, LED, button, PM1 watchdog
test/               PC unit tests (protocol, config validation)
tools/mqtt_exercise.py   drives a broker the way JMRI does
```

### Startup order

1. Load config from flash.
2. Drive every enabled pin to its startup level (restore or low), as
   early in boot as possible to shorten the time the pins float, and
   latch it (`gpio_hold_en`) to try to keep outputs steady across
   software resets and OTA reboots. Phase 1 measures the float time and
   which reset types the latch survives (a PM1 button reset or power
   cycle is expected to clear it).
3. Start Wi-Fi (modem sleep off for low latency).
4. Connect MQTT with last will `cikutrail/<node>/status` = `offline`;
   publish `online`.
5. Subscribe to each turnout's command topic. Retained messages set each
   turnout to its last commanded state.

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
  firmware version, IP, RSSI, uptime, battery voltage, charging, 5 V
  input present.
- LED: blue blink = setup portal, yellow = connecting, green = subscribed,
  red blink = error.

## Setup and configuration

### First start

1. No saved Wi-Fi → the node starts access point `CikutRail-XXXX` (last 4
   characters of the MAC), with a default password printed on the serial
   console. LED blinks blue.
2. Joining it opens a captive setup page at `192.168.4.1`, with a list of
   scanned networks.
3. Save → reboot → join Wi-Fi → connect to MQTT → LED green.
4. If Wi-Fi can't be joined for about 3 minutes, the setup access point
   reopens. A long button press does the same at any time. Turnout pins
   keep their levels throughout.

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
| Behaviour | startup level (`restore` / `low`), JMRI offline (`hold` / `low`), stagger ms (0), minimum interval per turnout ms (0), button single-click reset (on), feedback topic (off) |
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
     74HC86 reads the 3.3 V HIGH);
   - how long pins float at power-up and reset, and which resets the pin
     latch survives (software restart, watchdog, PM1 button reset);
   - G3 behaves like the other pins through a reset.
2. **MQTT.** Wi-Fi + broker with a temporary compiled-in settings file.
   JMRI turnouts in DIRECT mode, checked against a JMRI panel.
   `tools/mqtt_exercise.py` for repeatable tests.
3. **Startup behaviour.** Saved state, restore/low policy, pin latching
   across restarts, JMRI-offline policy, stagger, minimum interval,
   single-click reset option.
4. **Setup and config.** Captive portal, config page, mDNS, OTA.
5. **Health.** Battery/charging info, LED states, long press, PM1 watchdog.
6. **Optional.** MONITORING feedback topic; per-pin sensor mode
   (emulating JMRI Pi sensors) if inputs are needed later.

## Open items

- Confirm the JMRI channel in use and pick each node's number block
  (MQTT_CONVENTIONS.md).
