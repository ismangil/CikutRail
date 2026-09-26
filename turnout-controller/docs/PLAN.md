# Turnout controller: plan

## Goal

A JMRI MQTT turnout node on an M5Stack Stamp-S3Bat, controlling up to 11
IoTT turnout boards over Wi-Fi. The IoTT boards already work when wired to
a Raspberry Pi running JMRI's GPIO turnouts, so the node reproduces that
behaviour pin for pin, only with MQTT in between.

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

The IoTT boards in use are believed to be the 3-Channel Turnout Pulse
Driver, which turns a *level change* into a coil pulse. Any unexpected
edge fires a turnout, so:

- **Startup level:** `restore` (default) re-applies the last state saved
  in flash before Wi-Fi starts, so a power cycle causes no edges.
  `low` matches the Pi exactly (all pins LOW until JMRI commands them).
- **JMRI goes offline** (JMRI's last-will `<channel>track/state` =
  `OFFLINE`): `hold` (default) keeps every pin as it is. `low` matches the
  Pi's shutdown behaviour.
- **Stagger:** optional delay between pin changes (default 0 ms, like the
  Pi) to spread coil current when JMRI sends all turnouts at once.

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
  power off are its defaults and are left as they are. The firmware reads
  a long press through the PM1.

## Firmware design

PlatformIO + Arduino-ESP32, build target `[env:stamp-s3bat]`, plus
`[env:native]` for PC unit tests.

```
src/
  main.cpp          startup order, main loop
  config.*          settings in flash (Preferences/NVS), pin validation
  turnout_bank.*    11 channels: name ↔ GPIO, drive, save state, stagger
  mqtt_link.*       ESP-IDF esp-mqtt client: QoS 1/2, auto-reconnect, last will
  jmri_protocol.*   topic/payload decoding; pure logic, unit-tested on PC
  portal.*          captive portal, web config, OTA upload
  health.*          M5Unified: battery, charging, LED, button, PM1 watchdog
test/               PC unit tests (protocol, config validation)
tools/mqtt_exercise.py   drives a broker the way JMRI does
```

### Startup order

1. Load config from flash.
2. Drive every enabled pin to its startup level (restore or low) and
   latch it (`gpio_hold_en`) so software resets and OTA reboots don't
   glitch outputs.
3. Start Wi-Fi (modem sleep off for low latency).
4. Connect MQTT with last will `cikutrail/<node>/status` = `offline`;
   publish `online`.
5. Subscribe to each turnout's command topic. Retained messages set each
   turnout to its last commanded state.

### Command handling

- Only a real change touches the pin; a repeat of the current state does
  nothing.
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
| Behaviour | startup level (`restore` / `low`), JMRI offline (`hold` / `low`), stagger ms (0), feedback topic (off) |
| Maintenance | OTA firmware upload, reboot, factory reset |

Implementation: one small server on Arduino `WebServer` + `DNSServer`,
used for both first setup and later edits. WiFiManager is not used: its
custom fields handle an 11-row table poorly and it would mean two config
paths.

## Build phases

Each phase is tested on real hardware before the next starts.

1. **Pins.** PlatformIO project, turnout bank, serial commands to set pins.
   Verify an IoTT board fires correctly, and that power-up, reset and G3
   produce no stray edges (scope or LED on each line).
2. **MQTT.** Wi-Fi + broker with a temporary compiled-in settings file.
   JMRI turnouts in DIRECT mode, checked against a JMRI panel.
   `tools/mqtt_exercise.py` for repeatable tests.
3. **Startup behaviour.** Saved state, restore/low policy, pin latching
   across restarts, JMRI-offline policy, stagger.
4. **Setup and config.** Captive portal, config page, mDNS, OTA.
5. **Health.** Battery/charging info, LED states, long press, PM1 watchdog.
6. **Optional.** MONITORING feedback topic; per-pin sensor mode
   (emulating JMRI Pi sensors) if inputs are needed later.

## Open items

- Confirm the IoTT board model and its input's idle level and pull
  requirements (see WIRING.md).
- Confirm the JMRI channel in use and pick each node's number block
  (MQTT_CONVENTIONS.md).
