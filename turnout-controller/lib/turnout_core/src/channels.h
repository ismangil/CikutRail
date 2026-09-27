// Turnout channel to GPIO mapping for the Stamp-S3Bat.
// Hardware-free, so it builds for the board and for PC unit tests.
#pragma once

#include <stdint.h>

namespace tc {

const uint8_t kChannelCount = 11;

// Bit i of a channel mask is channel i + 1.
const uint16_t kAllChannelsMask = (1u << kChannelCount) - 1;

// GPIO for each channel, index 0 = channel 1. G1-G11 on the edge pads.
extern const uint8_t kChannelGpio[kChannelCount];

// True for GPIOs that must never drive a turnout: pins that don't exist on
// the ESP32-S3, flash/PSRAM, USB, UART0, strapping pins that set the boot
// mode, and the S3Bat's internal I2C bus to the PM1.
bool isForbiddenGpio(uint8_t gpio);

// Checks a channel table: every GPIO allowed and used once. On failure,
// *error (if given) points to a static message.
bool validateChannelGpios(const uint8_t* gpios, uint8_t count, const char** error);

}  // namespace tc
