// Compile-time defaults and pin/I2C constants for the
// M5StickC Plus 2 portable throttle.

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// HAT I2C bus (M5StickC Plus 2 8-pin top connector)
// ---------------------------------------------------------------------------
// On M5StickC / M5StickC Plus 2 the HAT pins expose I2C on:
//   SDA = GPIO 0
//   SCL = GPIO 26
// Speed kept at standard 100 kHz; the Encoder HAT's MCU does not need fast mode.
constexpr int HAT_I2C_SDA = 0;
constexpr int HAT_I2C_SCL = 26;
constexpr uint32_t HAT_I2C_HZ = 100000UL;

// ---------------------------------------------------------------------------
// M5Stack MiniEncoderC HAT (SKU U157) register map
// ---------------------------------------------------------------------------
// The HAT exposes the rotary encoder + push button over I2C @ 0x42. Register
// map mirrors the upstream M5Unit-MiniEncoderC Arduino driver:
//   0x00          absolute encoder counter, signed int32 little-endian
//   0x10          incremental encoder counter, signed int32 little-endian
//                 (cleared on every read)
//   0x20          button state, 1 byte (0 = pressed, 1 = released)
//   0x30          RGB LED, 3 bytes, RGB order on the wire (write only)
//   0x40          reset counter (write 0x01 to zero the absolute counter)
//   0xFE          firmware version (read only)
//   0xFF          I2C address (read/write)
constexpr uint8_t ENCODER_HAT_ADDR        = 0x42;
constexpr uint8_t ENCODER_REG_ABS_COUNT   = 0x00;
constexpr uint8_t ENCODER_REG_INC_COUNT   = 0x10;
constexpr uint8_t ENCODER_REG_BUTTON      = 0x20;
constexpr uint8_t ENCODER_REG_LED         = 0x30;
constexpr uint8_t ENCODER_REG_RESET       = 0x40;

// ---------------------------------------------------------------------------
// Throttle behaviour
// ---------------------------------------------------------------------------
// Centre-zero bipolar slider: position in [-MAX_SPEED .. +MAX_SPEED].
// Positive = Forward, negative = Reverse, zero = stop.
// WiThrottle speeds are 0..126 in 128-step mode; we mirror that.
constexpr int16_t THROTTLE_MAX_SPEED = 126;

// Detent multipliers. The HAT returns one count per detent; the sketch
// optionally accelerates if the user rotates quickly.
constexpr int16_t THROTTLE_STEP_SLOW = 1;
constexpr int16_t THROTTLE_STEP_FAST = 4;
constexpr uint32_t THROTTLE_FAST_WINDOW_MS = 60; // detents arriving within
                                                  // this window count as fast

// Button long-press threshold (ms). Same for encoder push and BtnA/BtnB.
constexpr uint32_t LONG_PRESS_MS = 1000;

// Wi-Fi, server and NVS constants live in throttle-common/CommonConfig.h.
#include "CommonConfig.h"

// Per-device NVS key (the shared keys are in CommonConfig.h).
#define NVS_KEY_POLARITY "polarity" // 0 = normal, 1 = flipped (per-device)

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
// M5StickC Plus 2 TFT is 135 wide x 240 tall in portrait.
constexpr int16_t TFT_W = 135;
constexpr int16_t TFT_H = 240;

// Display auto-dim after this much idle time. Set to 0 to disable.
constexpr uint32_t DISPLAY_DIM_AFTER_MS = 30000;
constexpr uint8_t DISPLAY_BRIGHT = 200;
constexpr uint8_t DISPLAY_DIM    = 40;
