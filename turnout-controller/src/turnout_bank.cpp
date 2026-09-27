#include "turnout_bank.h"

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_rom_gpio.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>

#include "channels.h"
#include "level_snapshot.h"

namespace bank {
namespace {

// Survives software, panic and watchdog resets; random after power-on.
RTC_NOINIT_ATTR tc::LevelSnapshot g_snapshot;

const uint16_t kColdBootLevels = 0;  // all LOW = THROWN
const bool kColdBootHold = true;

uint16_t g_levels = 0;
bool g_hold = kColdBootHold;
BootReport g_boot = {};

gpio_num_t gpioFor(uint8_t channel) {
  return static_cast<gpio_num_t>(tc::kChannelGpio[channel - 1]);
}

uint16_t bitFor(uint8_t channel) { return static_cast<uint16_t>(1u << (channel - 1)); }

void saveSnapshot(uint8_t plannedReset) {
  tc::snapshotWrite(&g_snapshot, g_levels, g_hold, plannedReset);
}

// Runs as a C++ constructor, before app_main() and so before Arduino's
// start-up code. The order among constructors follows link order (the
// ESP-IDF linker script sorts priorities only within one file), so this
// uses only ROM, register and GPIO calls: no logging, and no
// esp_reset_reason(), which another constructor sets up.
void earlyInit() {
  g_boot.romResetReason = esp_rom_get_reset_reason(0);
  g_boot.restored = tc::snapshotValid(g_snapshot);
  g_boot.plannedReset = g_boot.restored ? g_snapshot.plannedReset : 0;
  g_levels = g_boot.restored ? g_snapshot.levels : kColdBootLevels;
  g_hold = g_boot.restored ? g_snapshot.holdEnabled != 0 : kColdBootHold;

  // Set each output latch before enabling the driver, so a pad that is
  // still held from before the reset sees the same level when released.
  // (gpio_config() logs, so the pads are set up call by call instead.)
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    const gpio_num_t gpio = gpioFor(channel);
    if (gpio_get_level(gpio)) g_boot.padLevelsAtBoot |= bitFor(channel);
    gpio_set_level(gpio, (g_levels & bitFor(channel)) ? 1 : 0);
    esp_rom_gpio_pad_select_gpio(gpio);
    gpio_set_pull_mode(gpio, GPIO_FLOATING);
    gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT);  // input path on, so padLevel() reads the pad
    gpio_hold_dis(gpio);
    if (g_hold) gpio_hold_en(gpio);
  }

  g_boot.drivenAtUs = esp_timer_get_time();
  saveSnapshot(0);
}

__attribute__((constructor)) void driveEarly() { earlyInit(); }

}  // namespace

void applyStartup(tc::StartupLevel policy, bool haveSavedLevels, uint16_t savedLevels) {
  g_boot.resetReason = esp_reset_reason();

  uint16_t target;
  if (policy == tc::StartupLevel::Low) {
    target = 0;
    g_boot.source = LevelSource::PolicyLow;
  } else if (g_boot.restored) {
    target = g_levels;  // newer than flash, which is written a little after each change
    g_boot.source = LevelSource::Rtc;
  } else if (haveSavedLevels) {
    target = savedLevels & tc::kAllChannelsMask;
    g_boot.source = LevelSource::Flash;
  } else {
    target = 0;
    g_boot.source = LevelSource::NothingSaved;
  }

  g_boot.changedAtStartup = static_cast<uint16_t>(target ^ g_levels);
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    if (g_boot.changedAtStartup & bitFor(channel)) {
      setState(channel, tc::stateForPinLevel((target & bitFor(channel)) != 0));
    }
  }
  g_boot.startupAtUs = esp_timer_get_time();
  saveSnapshot(0);
}

const BootReport& bootReport() { return g_boot; }

void setState(uint8_t channel, tc::TurnoutState state) {
  const bool level = tc::pinLevelFor(state);
  if (level) {
    g_levels |= bitFor(channel);
  } else {
    g_levels &= static_cast<uint16_t>(~bitFor(channel));
  }
  saveSnapshot(0);

  const gpio_num_t gpio = gpioFor(channel);
  if (g_hold) gpio_hold_dis(gpio);
  gpio_set_level(gpio, level ? 1 : 0);
  if (g_hold) gpio_hold_en(gpio);
}

uint16_t levels() { return g_levels; }

tc::TurnoutState state(uint8_t channel) {
  return tc::stateForPinLevel((g_levels & bitFor(channel)) != 0);
}

bool padLevel(uint8_t channel) { return gpio_get_level(gpioFor(channel)) != 0; }

void setHold(bool enabled) {
  g_hold = enabled;
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    if (enabled) {
      gpio_hold_en(gpioFor(channel));
    } else {
      gpio_hold_dis(gpioFor(channel));
    }
  }
  saveSnapshot(0);
}

bool holdEnabled() { return g_hold; }

void notePlannedReset(uint8_t kindPlusOne) { saveSnapshot(kindPlusOne); }

}  // namespace bank
