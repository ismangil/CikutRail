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
   button. Note movement and the reset reason.
10. **Power cycle.** Unplug USB-C (and the battery, if fitted) for a few
    seconds, reconnect. Expect a cold start with all pins LOW, and no
    turnout movement: the pins are LOW before 5VOUT comes on.
11. **G3.** Channel 3 is on G3, a strapping pin. In tests 6–9 it should
    behave exactly like channel 1.
12. **Float time (scope only).** With `hold off` and channel 1 THROWN,
    trigger on G1 rising during `reset soft`; measure how long it stays
    HIGH before the firmware drives it LOW. The boot report's `pins driven
    ... us after app start` gives the firmware's share.

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

Paste the filled table (and any odd boot reports) back into the
conversation; the answers decide the startup and restart design in
phase 3.
