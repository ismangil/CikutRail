# Phase 6 bench test

Phase 6 adds two optional features, both off until set up:

- **Feedback** (Behaviour → Feedback, or `config feedback on`): the node
  publishes each turnout's actual pin state, retained, on
  `<channel>track/turnout/<name>/state` = `CLOSED` / `THROWN`, whatever
  changed it (JMRI, a test button, the console, the offline policy, the
  restore at power-up), and all of them on every MQTT connect. It never
  publishes on the command topic. Turning feedback off, renaming a
  turnout or making it a sensor clears its retained state. It reports the
  pin level, not the points: a jammed turnout still reports as moved.
- **Sensor mode** per channel (config page → Channels → Mode): the pin
  becomes an input with a pull-up, pull-down or none, optionally active
  LOW, debounced 50 ms, published retained on `<channel>track/sensor/
  <name>` = `ACTIVE` / `INACTIVE` (JMRI `MS<name>`), on change and on
  every connect. A sensor pin is never driven, and its turnout topic isn't
  subscribed.

**Sensor mode is only for pins wired to a switch or detector, never to a
GreenHat**: an undriven GreenHat input floats HIGH, so a THROWN turnout
pulses CLOSED. See WIRING.md, "Sensor inputs".

With sensors, the startup changes: after a **power cut** no pin is driven
until the settings are read from flash (5VOUT is off then, so GreenHats
can't pulse), so a sensor pin is never driven even briefly; after a
**reset**, RTC memory says which channels are sensors and the first drive
skips them. Channel modes survive a factory reset (they follow the
wiring).

## JMRI setup

- **Feedback**: in the MQTT connection's settings, set *Turnout receive
  topic* to `track/turnout/{0}/state` (connection-wide: every MQTT turnout
  then needs feedback on), and set each turnout's feedback mode to
  **MONITORING**.
- **Sensors**: add sensors in *Tools → Tables → Sensors* on the MQTT
  connection, hardware address = the channel's name (e.g. `105` →
  `MS105`). The default receive topic `track/sensor/{0}` matches. JMRI's
  sensor *send* topic is the same, so setting a sensor by hand in JMRI
  overwrites the node's retained value until the input next changes.

## Setup

As in phase 5. A switch or jumper wire for a sensor test on a **spare
channel not wired to the GreenHat** (channels 4-11 on this bench), from the
pin to GND, ideally through 1 kΩ.

## Tests

1. **Boot after a power cut.** Unplug and reconnect USB-C. The boot report
   says `pins not driven until the startup policy`, then restores the
   levels; turnouts behave as in phase 3.
2. **Feedback on.** `config feedback on`. `tools/mqtt_exercise.py watch`
   shows `track/turnout/101/state` … `111/state`, retained, matching the
   pins.
3. **Feedback follows the pin.** Click MT101 in JMRI, a test button, and
   `offline low` if wanted: each change appears on `101/state` within a
   moment.
4. **JMRI MONITORING.** Set JMRI up as above: MT101 in JMRI follows the
   node, including a test-button change.
5. **Feedback off clears.** `config feedback off`: the `/state` topics are
   removed (`watch` shows empty retained messages).
6. **Sensor mode.** On the page, set channel 5 to *sensor*, pull-up,
   active LOW, name `105`; save. `status` shows it as a sensor;
   `track/sensor/105` = `INACTIVE` (retained). Connect the pin to GND:
   `sensor 105 (ch5) ACTIVE` and the topic follows; release: `INACTIVE`.
   Bounce from a switch gives one change each way.
7. **Sensor after a reset or power cut.** Power-cycle: the sensor comes
   back as a sensor (never driven) and republishes its state.
8. **Sensor in JMRI.** Add `MS105`: it follows the input.
9. **Back to turnout.** Set channel 5 back to *turnout*: the sensor topic
   is cleared and the pin is driven LOW (THROWN).

## Results

| Test | Result | Notes |
|---|---|---|
| 1 Boot after a power cut | | |
| 2 Feedback on | | |
| 3 Feedback follows the pin | | |
| 4 JMRI MONITORING | | |
| 5 Feedback off clears | | |
| 6 Sensor mode | | |
| 7 Sensor after a reset or power cut | | |
| 8 Sensor in JMRI | | |
| 9 Back to turnout | | |
