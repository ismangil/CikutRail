// The S3Bat's PM1 power chip: 5VOUT (GreenHat logic supply) and voltage
// readings. 5VOUT's boost converter is enabled by PM1 pin G1.
#pragma once

#include <stdint.h>

namespace power {

// Starts I2C to the PM1 (SDA G48, SCL G47). Only reads registers, so it
// leaves 5VOUT as it was.
bool begin();
bool available();

bool fiveVoltOut(bool* on);
bool setFiveVoltOut(bool on);

struct Readings {
  uint16_t batteryMv;
  uint16_t inputMv;     // 5VIN / USB input
  uint16_t fiveVoltMv;  // 5 V rail, in or out
};
bool read(Readings* readings);

// Button settings, read only. pressedSinceLastRead clears the PM1's flag.
struct Buttons {
  bool singleClickResetDisabled;
  bool doubleClickOffDisabled;
  bool pressedSinceLastRead;
};
bool readButtons(Buttons* buttons);

// Status LED: the S3Bat's RGB LED, a NeoPixel on PM1 GPIO0.
bool ledBegin();
bool setLed(uint8_t r, uint8_t g, uint8_t b);

// Button held down right now.
bool buttonPressed(bool* pressed);

// Stops a double-click from powering the node off.
bool disableDoubleClickOff();

// PM1 watchdog: resets the node if not fed within the timeout. 0 = off.
bool watchdogSet(uint8_t seconds);
bool watchdogFeed();
bool watchdogCount(uint8_t* secondsLeft);

}  // namespace power
