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

}  // namespace power
