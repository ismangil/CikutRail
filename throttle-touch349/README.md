# Throttle: Waveshare ESP32-S3-Touch-LCD-3.49

A second handheld Wi-Fi throttle for JMRI's WiThrottle server, on the
[Waveshare ESP32-S3-Touch-LCD-3.49](https://www.waveshare.com/esp32-s3-touch-lcd-3.49.htm)
(172 x 640 touch screen, no encoder). It shares its non-hardware code with the
[M5Stick throttle](../throttle-m5stick/) through
[`../throttle-common/`](../throttle-common/). Plan and task list: issue #6.

**Status: wired to JMRI, not yet run on the layout.** The main screen drives
the loco, fires the first three JMRI routes and presses three of the loco's
functions through the WiThrottle server. It builds; it has not been tried
against JMRI yet. Display, touch and battery reading were checked on
2026-10-04.

First boot (or BOOT held at power-on) starts the Wi-Fi setup portal, the same
as the M5Stick throttle: join `WiThrottle-XXXX` and open `http://192.168.4.1/`.
The throttle then finds JMRI by mDNS, or uses the host and port you enter.

## Screen

172 x 640 portrait, top to bottom: status strip (loco name, green online / red struck out offline, battery), three
route buttons (the sidings), three function buttons, up / down arrows around
a signed speed readout, and an IDLE button.

- Each arrow press changes speed by 1 and repeats while held. Going down past
  0 goes into reverse. Forward is green, reverse is blue (shown with a minus).
- IDLE sets speed to 0 at once (no e-stop).
- The three function buttons come from the loco's own JMRI function list
  (`throttle-common/src/FunctionSlots.h`): slot 1 prefers a "light" function,
  slot 2 "beacon", slot 3 "uncouple", matched by label. Empty slots take the
  lowest-numbered unused functions. Functions labelled "uncouple" or "delayed"
  are momentary; the rest toggle.
- Function buttons send press on touch-down and release on touch-up; JMRI
  decides from the roster whether the function latches or is momentary, and
  the lit state follows what JMRI reports back.
- The routes are the first three in JMRI's route list; the lit one is the
  route JMRI reports active.
- BOOT is the e-stop while running.
- A long press (0.7 s) on the status strip opens the loco picker. Changing
  loco sets the old one to speed 0 before releasing it. The last loco is
  remembered and re-acquired at start-up; with none remembered, the picker
  opens. The picker shows the first 8 roster entries.

## Build

```
pio run -e touch349 -t upload     build and flash over USB-C
pio device monitor -e touch349    serial console
```

This project uses the [pioarduino](https://github.com/pioarduino/platform-espressif32)
platform (Arduino-ESP32 3.x), not the stock `espressif32@6.9.0` the other
projects use: Arduino_GFX 1.6.8 needs `esp32-hal-periman.h`, which Arduino-ESP32
2.0.17 does not have.

## Findings for "Check first" (#6)

From Waveshare's demo repo
([waveshareteam/ESP32-S3-Touch-LCD-3.49](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49))
and the Arduino_GFX source. Display, touch and the battery reading are confirmed on the board; the power button is not tried yet.

- **Display:** Arduino_GFX 1.6.8 has an `Arduino_AXS15231B` driver and a board
  entry for this exact pin set, so no custom driver is needed. The panel
  takes whole-frame writes, hence the `Arduino_Canvas` in `main.cpp`. Waveshare's
  own demo uses LVGL on the ESP-IDF `esp_lcd` API instead.
- **Touch:** I2C `0x3b` on SDA 17 / SCL 18, polled by writing an 11-byte command
  and reading 32 bytes. The reset and interrupt pins are not wired in the demo.
  Raw X runs along the long edge, so the sketch swaps and flips the axes.
- **Battery level:** ADC1 channel 3 (GPIO 4) behind a 1:3 divider; the demo
  multiplies the calibrated millivolts by 3. Case B (3.7 V LiPo) is the
  one in use, so the percentage will come from a LiPo voltage curve (4.2 V full).
- **Power button:** in the demo the TCA9554 expander pin 6 holds the battery
  power latch (drive it high to stay on, low to power off) and GPIO 16 reads
  the button. So firmware can read the button and can power the board off.
  Not tried yet. The demo's button code is under `Examples/Arduino/07_BATT_PWR_Test`.

Still open: what BOOT does in the throttle.

## Files

```
throttle-touch349/
├── platformio.ini
└── src/
    ├── main.cpp   app: link, loco, touch state machine
    ├── Ui.h / .cpp   drawing and hit-testing
    ├── Speed.h   speed step / clamp maths (native-tested)
    └── config.h   pins and I2C addresses
└── test/test_logic/   native tests: `pio test -e native`
```
