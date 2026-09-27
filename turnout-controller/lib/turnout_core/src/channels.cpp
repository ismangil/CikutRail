#include "channels.h"

namespace tc {

const uint8_t kChannelGpio[kChannelCount] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

bool isForbiddenGpio(uint8_t gpio) {
  if (gpio == 0) return true;                  // boot strapping, driven by the PM1
  if (gpio == 19 || gpio == 20) return true;   // USB D-/D+
  if (gpio >= 22 && gpio <= 25) return true;   // not present on the ESP32-S3
  if (gpio >= 26 && gpio <= 37) return true;   // flash and octal PSRAM
  if (gpio == 43 || gpio == 44) return true;   // UART0 TX/RX
  if (gpio == 45 || gpio == 46) return true;   // strapping (VDD_SPI, boot mode)
  if (gpio == 47 || gpio == 48) return true;   // internal I2C to the PM1
  if (gpio > 48) return true;                  // not present
  return false;
}

bool validateChannelGpios(const uint8_t* gpios, uint8_t count, const char** error) {
  const char* message = nullptr;
  uint64_t seen = 0;
  for (uint8_t i = 0; i < count && message == nullptr; ++i) {
    const uint8_t gpio = gpios[i];
    if (isForbiddenGpio(gpio)) {
      message = "GPIO not allowed for turnouts";
    } else if (seen & (1ull << gpio)) {
      message = "GPIO used by more than one channel";
    } else {
      seen |= 1ull << gpio;
    }
  }
  if (error != nullptr) *error = message;
  return message == nullptr;
}

}  // namespace tc
