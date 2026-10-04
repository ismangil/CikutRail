# Throttle: Waveshare ESP32-S3-Touch-LCD-3.49

A second handheld Wi-Fi throttle for JMRI's WiThrottle server, on the
[Waveshare ESP32-S3-Touch-LCD-3.49](https://www.waveshare.com/esp32-s3-touch-lcd-3.49.htm)
(172 x 640 touch screen, no encoder). It shares its non-hardware code with the
[M5Stick throttle](../throttle-m5stick/) through
[`../throttle-common/`](../throttle-common/). Plan and task list: issue #6.

**Status: bring-up only.** `src/main.cpp` draws a crosshair under the finger
and logs the battery voltage. It builds, but has not been run on the board
yet. There is no throttle UI yet.

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
and the Arduino_GFX source. Nothing here is confirmed on the board yet.

- **Display:** Arduino_GFX 1.6.8 has an `Arduino_AXS15231B` driver and a board
  entry for this exact pin set, so no custom driver is needed. The panel
  takes whole-frame writes, hence the `Arduino_Canvas` in `main.cpp`. Waveshare's
  own demo uses LVGL on the ESP-IDF `esp_lcd` API instead.
- **Touch:** I2C `0x3b` on SDA 17 / SCL 18, polled by writing an 11-byte command
  and reading 32 bytes. The reset and interrupt pins are not wired in the demo.
  Raw X runs along the long edge, so the sketch swaps and flips the axes.
- **Battery level:** ADC1 channel 3 (GPIO 4) behind a 1:3 divider; the demo
  multiplies the calibrated millivolts by 3. Turning volts into a percentage
  depends on case A (18650) or B (LiPo), which is still to be chosen.
- **Power button:** in the demo the TCA9554 expander pin 6 holds the battery
  power latch (drive it high to stay on, low to power off) and GPIO 16 reads
  the button. So firmware can read the button and can power the board off.
  Not tried yet. The demo's button code is under `Examples/Arduino/07_BATT_PWR_Test`.

Still open: what BOOT does in the throttle, and the case A or B choice.

## Files

```
throttle-touch349/
├── platformio.ini
└── src/
    ├── main.cpp   bring-up sketch: display, touch, battery
    └── config.h   pins and I2C addresses
```
