# Phase 1 bench test

The phase 1 firmware has no Wi-Fi or MQTT yet. It drives the 11 turnout
pins from USB serial commands and reports what happens to the pins and
5VOUT across resets. These tests answer the open hardware questions in
[PLAN.md](PLAN.md) before the MQTT work starts.

## Setup

- Stamp-S3Bat on USB-C (battery optional).
- One GreenHat, J6 open, coil supply connected as on the layout.
- Wiring (see [WIRING.md](WIRING.md)):

  | S3Bat | GreenHat |
  |---|---|
  | G1, G2, G3 | IN1, IN2, IN3 (header pin 3) |
  | 5VOUT | header pin 2 (one header) |
  | GND | header pin 1 |

- A turnout drive on channel 1. A Tortoise-style motor drive makes pulse
  length easy to see; with a snap coil, set the trimmer fully left
  (shortest pulse) first.
- Optional: a multimeter or scope on the coil terminals and on G1.

## Flash and connect

```
cd turnout-controller
pio run -e stamp-s3bat -t upload
pio device monitor -e stamp-s3bat
```

**On a Pi that also runs JMRI with a Pi-SPROG** (or any other serial
device), name the S3Bat's port instead of letting PlatformIO pick one, so
nothing ever opens the SPROG's port (`/dev/ttyAMA0` / `/dev/serial0`).
The S3Bat appears over USB as `/dev/ttyACM*`; its stable name is under
`/dev/serial/by-id/`:

```
ls /dev/serial/by-id/                  # find the Espressif USB entry
PORT=/dev/serial/by-id/usb-Espressif...   # the full name from the list
pio run -e stamp-s3bat -t upload --upload-port "$PORT"
pio device monitor -e stamp-s3bat --port "$PORT"
```

Upload resets the board into download mode over USB by itself. If it
can't find the board, check M5Stack's Stamp-S3Bat page for the
download-mode button sequence. Type `help` for the command list.

The boot report is printed once, about 1.5 s after boot, and is lost if
no monitor is connected by then (after a power cycle, for example). Type
`boot` to print it again; it includes the raw ROM reset code, since
ESP-IDF 4.4 reports USB resets as `unknown`.

Open the port without toggling DTR/RTS separately: passing through
RTS asserted / DTR low resets the ESP32-S3 through its USB serial.
`pio device monitor` and a plain pyserial open (both lines left at their
defaults) are safe. On a Pi, ModemManager may probe the port when it
appears and leave stray bytes in the console's line buffer (the next
command then fails as unknown; just repeat it). A udev rule stops it:

```
echo 'ATTRS{idVendor}=="303a", ENV{ID_MM_DEVICE_IGNORE}="1"' | sudo tee /etc/udev/rules.d/99-esp32-no-modemmanager.rules
sudo udevadm control --reload-rules
```

## Tests

Record the results in the table at the end.

1. **Boot report.** Reset the board and check the report: reset reason,
   `cold start, all LOW (THROWN)` on first power-up, `5VOUT at boot: off,
   turned on after pins were driven`, and `status` showing all channels
   THROWN / LOW.
2. **One pulse per command.** `close 1`, wait, `throw 1`, wait. Expect
   exactly one movement per command, in the right direction, and the pad
   column in `status` matching the state.
3. **Coil off after the pulse.** After `close 1`, wait longer than the
   pulse and measure across the coil terminals: 0 V. Repeat after
   `throw 1`. A voltage that stays on after `close 1` means the XOR gate
   isn't reading the 3.3 V HIGH: **stop, switch off the coil supply** and
   report it.
4. **Repeated changes.** `cycle 1 10 2000` (interval longer than the
   pulse). Expect 10 clean movements.
5. **5VOUT.** `5v` shows `on`; measure about 5 V on header pin 2. `pm1`
   prints battery, input and 5 V readings. `5v off`, measure 0 V, `5v on`.
   (The `pm1` "5V rail" reading doesn't follow the 5VOUT pad: it stayed at
   about 5.05 V with 5VOUT off on USB power. Use the meter.)
6. **Resets with the pin latch on.** Set a mix: `throw 1`, `close 2`,
   `throw 3`. Then run each of these, and after each one note whether any
   turnout moved and what the boot report says (`reset reason`, `pin
   levels: restored`, and `pads read at boot`):
   - `reset soft`
   - `reset panic`
   - `reset wdt`
7. **Same resets with the latch off.** `hold off`, set the same mix, and
   repeat test 6. Expected: channels 1 and 3 (THROWN) pulse CLOSED and
   then THROWN again, because the GreenHat pull-up takes the floating
   pins HIGH.
8. **Resets with 5VOUT off first.** `hold off`, same mix, then
   `reset soft 5v-off`. Note any movement. (This checks whether cutting
   the GreenHat logic supply first helps; the driven pins partly power
   the GreenHat through its pull-ups, so the outcome isn't certain.)
9. **Button reset.** `hold on`, same mix, then single-click the S3Bat
   button. Note movement and the reset reason. `pm1 btn` shows whether
   single-click reset is enabled and whether the PM1 saw a press since
   the last read; `boot` shows whether the ESP32 actually reset (a new
   `pins driven` time).
10. **Power cycle.** Unplug USB-C (and the battery, if fitted) for a few
    seconds, reconnect. Expect a cold start with all pins LOW, and no
    turnout movement: the pins are LOW before 5VOUT comes on.
11. **G3.** Channel 3 is on G3, a strapping pin. In tests 6–9 it should
    behave exactly like channel 1.
12. **Float time (scope only).** With `hold off` and channel 1 THROWN,
    trigger on G1 rising during `reset soft`; measure how long it stays
    HIGH before the firmware drives it LOW. The boot report's `pins driven
    ... us after app start` gives the firmware's share.

13. **Battery ride-through.** With the battery fitted and USB-C connected,
    unplug USB-C for 10 s without touching the button, reconnect, and
    type `boot`. An unchanged boot report means the battery kept the node
    running; a new `power-on` report means it didn't.

## Results

| Test | Result | Notes |
|---|---|---|
| 1 Boot report | | |
| 2 One pulse per command | | |
| 3 Coil off after pulse (CLOSED / THROWN) | | |
| 4 Repeated changes | | |
| 5 5VOUT on / off, voltage | | |
| 6 Latch on: soft / panic / wdt | | |
| 7 Latch off: soft / panic / wdt | | |
| 8 5VOUT off first | | |
| 9 Button reset | | |
| 10 Power cycle | | |
| 11 G3 same as G1 | | |
| 12 Float time | | |
| 13 Battery ride-through | | |

Paste the filled table (and any odd boot reports) back into the
conversation; the answers decide the startup and restart design in
phase 3.

### Run 1, 2026-09-27

One GreenHat (J6 open), a Kato snap coil on channel 1, Stamp-S3Bat on
USB-C with a battery fitted (4.2 V), Pi with a Pi-SPROG (port named by id).

| Test | Result | Notes |
|---|---|---|
| 1 Boot report | Pass | ROM 0x01 power-on, cold start all LOW, 5VOUT off at boot and turned on after the pins were driven. Pins driven about 103 ms after app start. |
| 2 One pulse per command | Pass | Once a loose S3Bat → GreenHat wire was refitted. With it off, the GreenHat pull-up held IN1 HIGH and nothing moved. |
| 3 Coil off after pulse (CLOSED / THROWN) | Pass / Pass | 0 V both ways: the XOR gate reads the 3.3 V HIGH. |
| 4 Repeated changes | Pass | 10 clean movements, trimmer at the shortest reliable pulse. |
| 5 5VOUT on / off, voltage | Pass | About 5 V / 0 V at header pin 2, no movement. `pm1` "5V rail" doesn't track the pad. |
| 6 Latch on: soft / panic / wdt | Skipped | |
| 7 Latch off: soft / panic / wdt | Not run | |
| 8 5VOUT off first | Not run | |
| 9 Button reset | No reset | On USB power, 3 single clicks: `pm1 btn` showed single-click reset enabled and the press registered, but the ESP32 didn't reset. No movement. Not tested on battery (see 13). |
| 10 Power cycle | Pass | No movement, 3 times. |
| 11 G3 same as G1 | Not run | |
| 12 Float time | Not run | |
| 13 Battery ride-through | **Fail** | Unplugging USB-C for 10 s gave a new power-on boot with the battery at 4.2 V. Cause not known yet (PM1 may need a button press to run from battery, or a battery connection fault). |

Other findings:

- The pins are driven about 100 ms after app start: Arduino's
  `initArduino()` runs `psramInit()` and `nvs_flash_init()` before
  `initVariant()`. With the latch off, a pin floats at least that long.
- RTC memory survives a reflash (the flash reset restores the old levels)
  but not a power cut, which starts cold.
