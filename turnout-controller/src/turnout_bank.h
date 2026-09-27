// Drives the turnout GPIOs. Keeps the levels in RTC memory and, when the
// pin latch is on, holds each pad with gpio_hold_en so a reset doesn't
// release it (phase 1 measures which resets that survives).
#pragma once

#include <esp_system.h>
#include <stdint.h>

#include "turnout_state.h"

namespace bank {

struct BootReport {
  esp_reset_reason_t resetReason;
  uint32_t romResetReason;   // raw ROM code, e.g. 0x1 POWERON, 0x15 USB_UART_CHIP_RESET
  bool restored;             // levels came from the RTC snapshot
  uint8_t plannedReset;      // console reset that led to this boot: ResetKind + 1, 0 = none
  uint16_t padLevelsAtBoot;  // pads as read before driving (only meaningful for held pads)
  int64_t drivenAtUs;        // esp_timer time (since app start) when the pins were driven
};

// Drives every channel to its restored level, or LOW (THROWN) after a
// cold start, as JMRI's Pi GPIO turnouts do. Called from initVariant(),
// before setup().
void earlyInit();
const BootReport& bootReport();

// Channels are numbered 1..tc::kChannelCount.
void setState(uint8_t channel, tc::TurnoutState state);
tc::TurnoutState state(uint8_t channel);
bool padLevel(uint8_t channel);

void setHold(bool enabled);
bool holdEnabled();

// Records a console reset about to happen, so the next boot can report it.
void notePlannedReset(uint8_t kindPlusOne);

}  // namespace bank
