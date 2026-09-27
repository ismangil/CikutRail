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

bool readButtons(Buttons* buttons) {
  if (!g_available) return false;
  return g_pm1.getSingleResetDisable(&buttons->singleClickResetDisabled) == M5PM1_OK &&
         g_pm1.getDoubleOffDisable(&buttons->doubleClickOffDisabled) == M5PM1_OK &&
         g_pm1.btnGetFlag(&buttons->pressedSinceLastRead) == M5PM1_OK;
}

}  // namespace power
