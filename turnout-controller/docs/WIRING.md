# Wiring

Node: M5Stack Stamp-S3Bat. Pin names follow the M5Stack pinout (`G<n>` =
ESP32-S3 GPIO n).

Turnout boards: IoTT **GreenHat Coil Driver** rev 1.0
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

| Header pin | Signal | Connect to |
|---|---|---|
| 1 | GND | node **GND** |
| 2 | +5 V (GreenHat logic supply) | **nothing**. Never connect it to the node. |
| 3 | IN | node G*n* |

Connect GND on every header you use (or at least once per GreenHat), so
the node and all GreenHats share ground.

## How the GreenHat reacts to the pin

From the GreenHat schematic:

- Each input has a **10.2 kΩ pull-up to 5 V** and 100 nF to GND.
- The input drives one half of an H-bridge (DRV8313) directly. It also
  drives an RC delay (10.2 kΩ + 1 MΩ trimmer, 10 µF) through a 74HC86
  gate into the other half of the bridge. The coil sits between the two
  halves.
- Result: **every level change fires one pulse**, with its polarity set by
  the edge direction (LOW→HIGH = CLOSED pulse, HIGH→LOW = THROWN pulse).
  The trimmer sets the pulse length (about 0.1 s up to several seconds).
  A steady level, HIGH or LOW, leaves the coil off.
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
3. **3.3 V into 5 V logic.** The 74HC86 runs at 5 V, where its guaranteed
   HIGH threshold (3.5 V) is above the 3.3 V the node (or a Pi) drives. It
   works in practice (its typical threshold is about 2.5 V, and your boards
   already work on a Pi), but it is outside the datasheet. Symptom if a
   gate ever misreads: the coil stays energised after a CLOSED command.
   Phase 1 checks that the coil current stops after each pulse. If a board
   ever shows this, fitting a 74HCT86 in place of U3 fixes it.
4. **Pull-up into an idle pin.** An undriven node pin is pulled towards
   5 V through 10.2 kΩ and clamped by the ESP32's protection diode: about
   0.1–0.4 mA per pin, the same as on the Pi. Keep the node powered
   whenever the GreenHats are (the S3Bat battery helps here).

## Power

| Pad | Use |
|---|---|
| **5VIN** | Feed the node from the layout's 5 V supply here (or use USB-C). |
| **BAT** | 3.7 V Li-ion cell; rides through supply dips and keeps outputs driven. |
| **3V3** | ESP32 regulator output, 600 mA max, shared with the ESP. **Do not** power the GreenHats from it. |
| **5VOUT** | Boost output switched by the PM1; not used by this project. |
| **GND** | Common ground with all GreenHats. |

GreenHats keep their own supply and J6 jumper setting, exactly as they
were with the Pi. Don't power the node from a GreenHat's 5 V: its small
regulator isn't sized for the ESP32's Wi-Fi peaks.

## Onboard, via the PM1 (no ESP GPIO used)

- RGB LED (status)
- User button: single-click reset and double-click power off (PM1
  defaults), long press = open the setup portal. A single-click reset
  makes THROWN turnouts pulse (see above); the config page can disable it.
- Battery voltage, 5 V input voltage, charging status
- WAKE pad (PM1 G4); not used
