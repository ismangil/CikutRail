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
  harmless, but not yet checked through a reset (the phase 1 reset tests
  were skipped).

Pins not used for turnouts: G0 (boot, driven by the PM1), G19/G20 (USB),
G43/G44 (UART), G47/G48 (internal I2C to the PM1), G45/G46 (strapping).
The firmware refuses these.

## GreenHat input header (J1–J3, one per channel)

The pin order matches a PCA9685 servo header, so a standard servo cable
fits.

| Header pin | Signal | Connect to |
|---|---|---|
| 1 | GND | node **GND** |
| 2 | +5 V (GreenHat logic rail) | node **5VOUT** (with J6 open, see below). **Never** the node's 3V3 or a GPIO. |
| 3 | IN | node G*n* |

Connect GND on every header you use (or at least once per GreenHat), so
the node and all GreenHats share ground.

**J6 bridges and header pin 2:**

- **J6 bridged** (standalone): the GreenHat's onboard regulator makes its
  5 V logic supply from the coil supply. Leave pin 2 unconnected.
- **J6 open** (this layout): the GreenHat takes its 5 V logic supply
  from pin 2, as it did from the Pi. Feed it from the node's **5VOUT**
  pad. The three headers on one GreenHat share the same 5 V rail, so one
  pin 2 per GreenHat is enough.

**Why 5VOUT:** it is a boost converter on the S3Bat, switched on and off by
the PM1 power chip, so the firmware controls the GreenHats' logic power.
The GreenHats' 5 V draw is small: the
XOR gate plus the input pull-ups, about 0.5 mA per LOW input, so roughly
10 mA for 11 channels. (M5Stack's 5VOUT current rating couldn't be
checked from here, but a boost like this delivers far more than that.)

The alternative is to feed pin 2 straight from the layout 5 V supply that
feeds the node's **5VIN** pad. That works like the Pi, but the firmware
can't switch it. The 5VIN pad carries 5 V only when that supply is
connected, not when the node runs from USB-C.

## How the GreenHat reacts to the pin

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

A useful rule falls out of the circuit: the delayed side is always a late
(or, when unpowered, LOW) copy of the input. So **a channel can only pulse
towards the level on its input**. A turnout moves the wrong way only if
its input takes the wrong level.

1. **Idle level is HIGH while the GreenHat logic is powered.** When the
   node isn't driving a pin (reset, or node unpowered) and the GreenHat
   has 5 V, the pull-up takes the line HIGH (CLOSED).
   - Turnouts that are CLOSED ride through a node reset with no movement.
   - Turnouts that are THROWN get a CLOSED pulse when the node resets,
     then a THROWN pulse when the firmware restores their level.
   - Do **not** add a pull-down to fight this: 10 kΩ against the 10.2 kΩ
     pull-up gives ~2.5 V, an undefined level.
   - With the GreenHat logic unpowered (5VOUT off), there is no pull-up
     and no delayed side, so a floating input causes no pulse at all.
2. **Power sequencing through 5VOUT.** Because the node switches the
   GreenHats' 5 V:
   - **Cold start:** the node drives its pins to the restored levels
     first, then turns 5VOUT on. THROWN turnouts don't pulse; CLOSED
     turnouts get one CLOSED pulse and don't move.
   - **Planned restarts** (firmware update, config save): the node
     relies on the pin latch to hold every pin through the restart.
     Turning 5VOUT off first might add a margin, but it isn't certain:
     pins driven HIGH keep partly powering the GreenHat's 5 V rail
     through its pull-ups, and the delay capacitors can keep the XOR gate
     alive while the pins float. Neither has been bench-tested yet (the
     phase 1 reset tests were skipped).
   - **Unplanned resets** (crash, watchdog, PM1 button reset) happen
     with 5VOUT still on, so item 1 applies. Whether the PM1 keeps 5VOUT
     on through each kind of ESP32 reset is still open.
   - With the Pi's always-on 5 V, every channel pulsed CLOSED at GreenHat
     power-up and THROWN turnouts were then put back.
   - **Node power loss** (seen on the phase 2 bench): when the node loses
     power while the coil supply stays on, every CLOSED (HIGH) channel
     fires one THROWN pulse. The pin falls to 0 V faster than the
     GreenHat's delayed side, which is an ordinary HIGH→LOW edge. THROWN
     channels don't move. Firmware can't prevent it, since the node is
     unpowered. On restart the retained MQTT commands put those turnouts
     back with one CLOSED pulse, so they end where JMRI left them.
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
   0.1–0.4 mA per pin, the same as on the Pi. With the GreenHats' 5 V
   coming from the node's own 5VOUT, this only happens during a node
   reset.

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
| **BAT** | Not used: the node runs from 5VIN or USB-C only, with no battery ride-through. |
| **3V3** | ESP32 regulator output, 600 mA max, shared with the ESP. **Do not** power the GreenHats from it. |
| **5VOUT** | Boost output switched by the PM1: GreenHat logic 5 V (header pin 2). |
| **GND** | Common ground with all GreenHats. |

GreenHats keep their coil supply and their J6 setting (open) as they
were with the Pi; header pin 2 now comes from the node's 5VOUT. Don't
power the node from a GreenHat's onboard regulator: it isn't sized for
the ESP32's Wi-Fi peaks.

## Onboard, via the PM1 (no ESP GPIO used)

- RGB LED (status; PM1 GPIO0 as NeoPixel, see PHASE5_BENCH.md for the
  colours)
- User button: single-click reset and double-click power off (PM1
  defaults; the firmware disables double-click power off), long press
  (3 s) = open the setup access point. A single-click reset
  makes THROWN turnouts pulse (see above); the config page can disable it.
  **On the phase 1 bench, a single click on USB power did not reset the
  ESP32**, although the PM1 reported single-click reset enabled and saw
  the press (PHASE1_BENCH.md, test 9).
- 5 V input voltage (the PM1's battery and charging features are unused)
- WAKE pad (PM1 G4); not used
