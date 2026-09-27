# Wiring

Node: M5Stack Stamp-S3Bat. Pin names follow the M5Stack pinout (`G<n>` =
ESP32-S3 GPIO n).

Turnout boards: IoTT **GreenHat Coil Driver** rev 1.0, sold as the
[3-Channel Turnout Pulse Driver](https://www.tindie.com/products/tanner87661/3-channel-turnout-pulse-driver/)
and shown in IoTT's [Video #78](https://www.youtube.com/watch?v=QB0OnHWNqEE)
([design files](https://github.com/tanner87661/IoTTStick/tree/master/Hat%20Devices/GreenHat%20Power%20Extension)).
Each GreenHat has 3 channels, so 11 turnouts need 4 GreenHats (one channel
spare).

## Turnout outputs

| Channel | GPIO | Pad | Default JMRI name | GreenHat |
|---|---|---|---|---|
| 1 | G1 | left edge | `MT101` | A, IN1 |
| 2 | G2 | left edge | `MT102` | A, IN2 |
| 3 | G3 | left edge | `MT103` | A, IN3 |
| 4 | G4 | left edge | `MT104` | B, IN1 |
| 5 | G5 | left edge | `MT105` | B, IN2 |
| 6 | G6 | left edge | `MT106` | B, IN3 |
| 7 | G7 | left edge | `MT107` | C, IN1 |
| 8 | G8 | right edge | `MT108` | C, IN2 |
| 9 | G9 | right edge | `MT109` | C, IN3 |
| 10 | G10 | right edge | `MT110` | D, IN1 |
| 11 | G11 | right edge | `MT111` | D, IN2 |

- 3.3 V push-pull outputs, the same as Raspberry Pi GPIO. `CLOSED` = HIGH,
  `THROWN` = LOW (JMRI's "Inverted" setting swaps them).
- **G3** is an ESP32-S3 strapping pin (JTAG source select). It is normally
  harmless; phase 1 checks it through a reset.

Pins not used for turnouts: G0 (boot, driven by the PM1), G19/G20 (USB),
G43/G44 (UART), G47/G48 (internal I2C to the PM1), G45/G46 (strapping).
The firmware refuses these.

## GreenHat input header (J1–J3, one per channel)

The pin order matches a PCA9685 servo header, so a standard servo cable
fits.

| Header pin | Signal | Connect to |
|---|---|---|
| 1 | GND | node **GND** |
| 2 | +5 V (GreenHat logic rail) | depends on J6, see below. **Never** the node's 3V3 or a GPIO. |
| 3 | IN | node G*n* |

Connect GND on every header you use (or at least once per GreenHat), so
the node and all GreenHats share ground.

**J6 bridges and header pin 2:**

- **J6 bridged** (standalone): the GreenHat's onboard regulator makes its
  5 V logic supply from the coil supply. Leave pin 2 unconnected.
- **J6 open**: the GreenHat expects its 5 V logic supply on pin 2 (from a
  PCA9685 board, or from the Pi's 5 V in your current setup). Feed pin 2
  from the layout's 5 V supply, the same one that feeds the node's
  **5VIN**. The board's 5 V draw is small (logic chips only).

## How the GreenHat reacts to the pin

From the GreenHat schematic:

From the GreenHat schematic and IoTT's
[Video #78](https://www.youtube.com/watch?v=QB0OnHWNqEE):

- Each input has a **10.2 kΩ pull-up to 5 V**, so it works with switches
  and open-collector outputs, and an unconnected input is HIGH. A 100 nF
  capacitor to GND filters out short spikes (switch bounce, µs glitches).
- The input drives one half of an H-bridge (DRV8313) directly. It also
  drives an RC delay (10.2 kΩ + 1 MΩ trimmer, 10 µF) into an XOR gate
  wired as a buffer, which drives the other half of the bridge. The coil
  sits between the two halves.
- Result: **every level change fires one pulse**, with its polarity set by
  the edge direction (LOW→HIGH = CLOSED pulse, HIGH→LOW = THROWN pulse).
  The trimmer sets the pulse length, from a few milliseconds to about 5 s;
  the two directions can differ by a few tenths of a second. A steady
  level, HIGH or LOW, leaves the coil off.
- Coil supply: about 10–30 V (typically 12–16 V), up to 2 A per output.
  Snap coils on DC draw around 3 A and heat quickly, so IoTT recommends
  the shortest pulse that still throws them.
- A reversal before the pulse has finished cuts that pulse short. The
  firmware has a per-turnout minimum-interval setting for this (see
  PLAN.md).

### Consequences

1. **Idle level is HIGH.** When the node isn't driving a pin (power-up,
   reset, or node unpowered), the GreenHat pull-up takes the line HIGH
   (CLOSED).
   - Turnouts that are CLOSED ride through a node reset with no pulse.
   - Turnouts that are THROWN get a CLOSED pulse when the node resets,
     then a THROWN pulse when the firmware restores their level.
   - Do **not** add a pull-down to fight this: 10 kΩ against the 10.2 kΩ
     pull-up gives ~2.5 V, an undefined level.
2. **The GreenHat fires on its own power-up.** Its delay capacitor starts
   empty while the input is pulled HIGH, so each channel gives a CLOSED
   pulse when the GreenHat is powered. The node then puts THROWN turnouts
   back. This is the GreenHat's own behaviour; it happened on the Pi too.
3. **3.3 V into 5 V logic.** The XOR gate (U3) must be the
   Schmitt-trigger type: its hysteresis is what makes the delayed side
   switch cleanly once. (The BOM lists a plain 74HC86 because JLCPCB
   doesn't stock the Schmitt version; self-built boards need it fitted by
   hand.) Don't swap in a 74HCT86: it has no hysteresis.
   The gate runs at 5 V, and its worst-case rising threshold at 5 V can be
   above the 3.3 V the node (or a Pi) drives. It works in practice, as your
   boards show on the Pi, but it isn't guaranteed. Symptom if a gate ever
   misses: the coil stays energised after a CLOSED command, and a snap
   coil at ~3 A overheats fast. Phase 1 checks that the coil current stops
   after each pulse. If a channel is marginal, use the open-collector
   stage below.
4. **Several coils at once.** Every turnout that changes at the same time
   draws coil current together (up to ~3 A each for snap coils). Set the
   node's stagger to at least the pulse length if the coil supply can't
   carry that.
5. **Pull-up into an idle pin.** An undriven node pin is pulled towards
   5 V through 10.2 kΩ and clamped by the ESP32's protection diode: about
   0.1–0.4 mA per pin, the same as on the Pi. Keep the node powered
   whenever the GreenHats are (the S3Bat battery helps here).

## Optional open-collector stage

The GreenHat is designed for open-collector inputs. A transistor per
channel, for example a ULN2803A (8 channels, so two for 11 turnouts), or
an N-MOSFET such as a 2N7002 with a gate pull-down, gives:

- a full 5 V HIGH from the GreenHat's own pull-up, removing the threshold
  question in item 3;
- no current from the 5 V pull-up into the ESP32 pins (item 5);
- the same idle level: while the ESP32 is in reset the transistor is off,
  so the line stays HIGH.

The transistor inverts the signal (ESP32 HIGH pulls the input LOW). The
firmware has an **output stage** setting, `direct` (default) or
`open-collector`, and inverts the pin for you, so JMRI's CLOSED/THROWN
meaning doesn't change.

Don't use the ESP32's own open-drain mode for this: the released pin
would be pulled towards 5 V, beyond what the ESP32 pins tolerate.

## Power

| Pad | Use |
|---|---|
| **5VIN** | Feed the node from the layout's 5 V supply here (or use USB-C). |
| **BAT** | 3.7 V Li-ion cell; rides through supply dips and keeps outputs driven. |
| **3V3** | ESP32 regulator output, 600 mA max, shared with the ESP. **Do not** power the GreenHats from it. |
| **5VOUT** | Boost output switched by the PM1; not used by this project. |
| **GND** | Common ground with all GreenHats. |

GreenHats keep their coil supply and J6 setting as they were with the Pi;
if J6 is open, feed header pin 2 from the layout's 5 V (see above). Don't
power the node from a GreenHat's onboard regulator: it isn't sized for
the ESP32's Wi-Fi peaks.

## Onboard, via the PM1 (no ESP GPIO used)

- RGB LED (status)
- User button: single-click reset and double-click power off (PM1
  defaults), long press = open the setup portal. A single-click reset
  makes THROWN turnouts pulse (see above); the config page can disable it.
- Battery voltage, 5 V input voltage, charging status
- WAKE pad (PM1 G4); not used
