#include "power.h"

#include <M5PM1.h>
#include <Wire.h>

namespace power {
namespace {

const int8_t kSdaPin = 48;
const int8_t kSclPin = 47;
const m5pm1_gpio_num_t kFiveVoltEnablePin = M5PM1_GPIO_NUM_1;

M5PM1 g_pm1;
bool g_available = false;

}  // namespace

bool begin() {
  // Warnings and errors only: at info level the library logs every LED
  // colour change, twice a second while the LED blinks.
  M5PM1::setLogLevel(M5PM1_LOG_LEVEL_WARN);
  g_available = g_pm1.begin(&Wire, M5PM1_DEFAULT_ADDR, kSdaPin, kSclPin, M5PM1_I2C_FREQ_100K) == M5PM1_OK;
  return g_available;
}

bool available() { return g_available; }

bool fiveVoltOut(bool* on) {
  if (!g_available || g_pm1.updateSnapshot() != M5PM1_OK) return false;
  m5pm1_pin_status_t status;
  if (g_pm1.getPinStatus(kFiveVoltEnablePin, &status) != M5PM1_OK) return false;
  *on = status.func == M5PM1_GPIO_FUNC_GPIO && status.mode == M5PM1_GPIO_MODE_OUTPUT && status.output != 0;
  return true;
}

bool setFiveVoltOut(bool on) {
  if (!g_available) return false;
  // Output latch first, so switching the pin to output can't flash the
  // wrong level.
  return g_pm1.gpioSetOutput(kFiveVoltEnablePin, on ? 1 : 0) == M5PM1_OK &&
         g_pm1.gpioSetFunc(kFiveVoltEnablePin, M5PM1_GPIO_FUNC_GPIO) == M5PM1_OK &&
         g_pm1.gpioSetDrive(kFiveVoltEnablePin, M5PM1_GPIO_DRIVE_PUSHPULL) == M5PM1_OK &&
         g_pm1.gpioSetMode(kFiveVoltEnablePin, M5PM1_GPIO_MODE_OUTPUT) == M5PM1_OK;
}

bool read(Readings* readings) {
  if (!g_available) return false;
  return g_pm1.readVbat(&readings->batteryMv) == M5PM1_OK &&
         g_pm1.readVin(&readings->inputMv) == M5PM1_OK &&
         g_pm1.read5VInOut(&readings->fiveVoltMv) == M5PM1_OK;
}

bool ledBegin() {
  if (!g_available) return false;
  // As M5PM1's NeoPixel example: GPIO0 in its special function, driven
  // high, and LED_EN high, which on the S3Bat also powers the RGB LED.
  return g_pm1.gpioSetFunc(M5PM1_GPIO_NUM_0, M5PM1_GPIO_FUNC_OTHER) == M5PM1_OK &&
         g_pm1.gpioSetDrive(M5PM1_GPIO_NUM_0, M5PM1_GPIO_DRIVE_PUSHPULL) == M5PM1_OK &&
         g_pm1.gpioSetOutput(M5PM1_GPIO_NUM_0, true) == M5PM1_OK && g_pm1.setLedEnLevel(true) == M5PM1_OK &&
         g_pm1.setLedCount(1) == M5PM1_OK;
}

bool setLed(uint8_t r, uint8_t g, uint8_t b) {
  if (!g_available) return false;
  return g_pm1.setLedColor(0, r, g, b) == M5PM1_OK && g_pm1.refreshLeds() == M5PM1_OK;
}

bool buttonPressed(bool* pressed) { return g_available && g_pm1.btnGetState(pressed) == M5PM1_OK; }

bool disableButtonActions() {
  return g_available && g_pm1.setSingleResetDisable(true) == M5PM1_OK &&
         g_pm1.setDoubleOffDisable(true) == M5PM1_OK && g_pm1.setDownloadLock(true) == M5PM1_OK;
}

bool watchdogSet(uint8_t seconds) { return g_available && g_pm1.wdtSet(seconds) == M5PM1_OK; }

bool watchdogFeed() { return g_available && g_pm1.wdtFeed() == M5PM1_OK; }

bool watchdogCount(uint8_t* secondsLeft) { return g_available && g_pm1.wdtGetCount(secondsLeft) == M5PM1_OK; }

bool readButtons(Buttons* buttons) {
  if (!g_available) return false;
  return g_pm1.getSingleResetDisable(&buttons->singleClickResetDisabled) == M5PM1_OK &&
         g_pm1.getDoubleOffDisable(&buttons->doubleClickOffDisabled) == M5PM1_OK &&
         g_pm1.btnGetFlag(&buttons->pressedSinceLastRead) == M5PM1_OK &&
         g_pm1.getDownloadLock(&buttons->downloadLocked) == M5PM1_OK;
}

}  // namespace power
