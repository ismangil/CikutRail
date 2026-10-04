// Drives the turnout GPIOs. Keeps the levels in RTC memory and, when the
// pin latch is on, holds each pad with gpio_hold_en so a reset doesn't
// release it (phase 1 measures which resets that survives).
#pragma once

#include <esp_system.h>
#include <stdint.h>

#include "behaviour.h"
#include "channel_config.h"
#include "turnout_state.h"

namespace bank {

// Where the startup levels came from.
enum class LevelSource : uint8_t {
  Rtc,           // after a reset: RTC memory, the levels just before it
  Flash,         // after a power cut: last levels saved in flash
  NothingSaved,  // power cut, nothing in flash: all LOW
  PolicyLow,     // startup policy "low": all LOW
};

struct BootReport {
  esp_reset_reason_t resetReason;
  uint32_t romResetReason;   // raw ROM code, e.g. 0x1 POWERON, 0x15 USB_UART_CHIP_RESET
  bool restored;             // RTC snapshot valid (a reset, not a power cut)
  uint8_t plannedReset;      // console reset that led to this boot: ResetKind + 1, 0 = none
  uint16_t padLevelsAtBoot;  // pads as read before driving (only meaningful for held pads)
  int64_t drivenAtUs;        // esp_timer time of the first drive (from chip reset, it appears);
                             // after a power cut, the pins wait for applyStartup()
  int64_t startupAtUs;       // ... when the startup policy was applied
  LevelSource source;
  uint16_t changedAtStartup; // channels the startup policy changed after the first drive
};

// After a reset, the first drive runs from a C++ constructor, before
// app_main() and so before Arduino's start-up code (PSRAM test, NVS), so
// the pins float as briefly as possible. It uses RTC memory: the levels
// and which channels are sensors (never driven). After a power cut RTC
// memory is empty, so no pin is driven until applyStartup(): 5VOUT is off
// then, so the GreenHats can't pulse, and a sensor pin is never driven.
//
// applyStartup() runs from initVariant() once flash is readable and before
// 5VOUT is switched on: it sets up every pin from the channel modes and
// the startup policy.
void applyStartup(tc::StartupLevel policy, bool haveSavedLevels, uint16_t savedLevels,
                  const tc::ChannelConfig& channels);
const BootReport& bootReport();

// Channels are numbered 1..tc::kChannelCount. Sensor channels ignore it.
void setState(uint8_t channel, tc::TurnoutState state);

// Changes channel modes in place (the config page). A channel that
// becomes a sensor stops being driven; one that becomes a turnout starts
// LOW (THROWN).
void setChannelConfig(const tc::ChannelConfig& channels);
const tc::ChannelConfig& channelConfig();
bool isSensorChannel(uint8_t channel);
tc::TurnoutState state(uint8_t channel);
uint16_t levels();  // bit i = channel i + 1 HIGH; turnout channels only
bool padLevel(uint8_t channel);

void setHold(bool enabled);
bool holdEnabled();

// Records a console reset about to happen, so the next boot can report it.
void notePlannedReset(uint8_t kindPlusOne);

}  // namespace bank
