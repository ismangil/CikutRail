// Pins and constants for the Waveshare ESP32-S3-Touch-LCD-3.49.
// Source: Waveshare's demo repo (Examples/Arduino/*/user_config.h).

#pragma once

#include <Arduino.h>

// QSPI display, AXS15231B, 172 x 640 in portrait.
constexpr int8_t  LCD_CS  = 9;
constexpr int8_t  LCD_SCK = 10;
constexpr int8_t  LCD_D0  = 11;
constexpr int8_t  LCD_D1  = 12;
constexpr int8_t  LCD_D2  = 13;
constexpr int8_t  LCD_D3  = 14;
constexpr int8_t  LCD_RST = 21;
constexpr int8_t  LCD_BL  = 8;    // backlight, PWM
constexpr int16_t LCD_W   = 172;
constexpr int16_t LCD_H   = 640;

// Touch: built into the AXS15231B, on its own I2C bus.
constexpr int8_t  TOUCH_SDA  = 17;
constexpr int8_t  TOUCH_SCL  = 18;
constexpr uint8_t TOUCH_ADDR = 0x3b;

// Board I2C bus: TCA9554 I/O expander, QMI8658 IMU, PCF85063 RTC, codecs.
constexpr int8_t  BOARD_SDA = 47;
constexpr int8_t  BOARD_SCL = 48;

// Battery: ADC1 channel 3 (GPIO 4) behind a 1:3 divider.
constexpr int8_t  BATT_ADC_PIN     = 4;
constexpr float   BATT_DIVIDER     = 3.0f;

// Power: the demo holds the battery power latch with TCA9554 pin 6 and
// reads the power button on GPIO 16. Not used by the bring-up sketch yet.
constexpr int8_t  PWR_BUTTON_PIN = 16;

// BOOT button: held at power-on it clears the Wi-Fi settings; while running
// it is the e-stop.
constexpr int8_t  BOOT_BUTTON_PIN = 0;
