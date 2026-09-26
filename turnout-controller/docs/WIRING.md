# Wiring

Board: M5Stack Stamp-S3Bat. Pin names follow the M5Stack pinout (`G<n>` =
ESP32-S3 GPIO n).

## Turnout outputs

| Channel | GPIO | Pad | Default JMRI name |
|---|---|---|---|
| 1 | G1 | left edge | `MT101` |
| 2 | G2 | left edge | `MT102` |
| 3 | G3 | left edge | `MT103` |
| 4 | G4 | left edge | `MT104` |
| 5 | G5 | left edge | `MT105` |
| 6 | G6 | left edge | `MT106` |
| 7 | G7 | left edge | `MT107` |
| 8 | G8 | right edge | `MT108` |
| 9 | G9 | right edge | `MT109` |
| 10 | G10 | right edge | `MT110` |
| 11 | G11 | right edge | `MT111` |

- 3.3 V push-pull outputs, the same logic level as Raspberry Pi GPIO.
  `CLOSED` = HIGH, `THROWN` = LOW (JMRI's "Inverted" setting swaps them).
- Connect each output to one IoTT board input, and connect **GND** between
  the node and every IoTT board.
- **G3** is an ESP32-S3 strapping pin (JTAG source select). It is normally
  harmless, but phase 1 checks it for glitches through a reset.

Pins not used for turnouts (on the 24-pin BTB connector or internal): G0
(boot, driven by the PM1), G19/G20 (USB), G43/G44 (UART), G47/G48
(internal I2C to the PM1), G45/G46 (strapping). The firmware refuses these.

## Idle level during reset

G1–G11 float for roughly 100 ms after power-up or reset, before the
firmware drives them. Raspberry Pi pins have built-in pulls at power-up
instead. A pulse driver may see a floating line as an edge and fire a coil.

Phase 1 tests this on the bench. If a stray pulse appears, fit a 10 kΩ
resistor on each affected line to match its idle level:

- to **GND** if the pin normally sits LOW (THROWN),
- to **3V3** if it normally sits HIGH (CLOSED).

## Power

| Pad | Use |
|---|---|
| **5VIN** | Feed the node from the layout's 5 V supply here (or use USB-C). |
| **BAT** | 3.7 V Li-ion cell; rides through supply dips. |
| **3V3** | ESP32 regulator output, 600 mA max, shared with the ESP. **Do not** power the IoTT boards from it. |
| **5VOUT** | Boost output switched by the PM1; not used by this project. |
| **GND** | Common ground with all IoTT boards. |

## Onboard, via the PM1 (no ESP GPIO used)

- RGB LED (status)
- User button: single-click reset and double-click power off (PM1
  defaults), long press = open the setup portal
- Battery voltage, 5 V input voltage, charging status
- WAKE pad (PM1 G4); not used
