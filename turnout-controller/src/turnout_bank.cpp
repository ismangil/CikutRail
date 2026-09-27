#include "turnout_bank.h"

#include <driver/gpio.h>
#include <esp_attr.h>
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

}  // namespace

void earlyInit() {
  g_boot.resetReason = esp_reset_reason();
  g_boot.restored = tc::snapshotValid(g_snapshot);
  g_boot.plannedReset = g_boot.restored ? g_snapshot.plannedReset : 0;
  g_levels = g_boot.restored ? g_snapshot.levels : kColdBootLevels;
  g_hold = g_boot.restored ? g_snapshot.holdEnabled != 0 : kColdBootHold;

  // Set each output latch before enabling the driver, so a pad that is
  // still held from before the reset sees the same level when released.
  uint64_t mask = 0;
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    const gpio_num_t gpio = gpioFor(channel);
    if (gpio_get_level(gpio)) g_boot.padLevelsAtBoot |= bitFor(channel);
    gpio_set_level(gpio, (g_levels & bitFor(channel)) ? 1 : 0);
    mask |= 1ull << gpio;
  }

  gpio_config_t config = {};
  config.pin_bit_mask = mask;
  config.mode = GPIO_MODE_INPUT_OUTPUT;  // input path on, so padLevel() reads the pad
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);

  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    const gpio_num_t gpio = gpioFor(channel);
    gpio_hold_dis(gpio);
    if (g_hold) gpio_hold_en(gpio);
  }

  g_boot.drivenAtUs = esp_timer_get_time();
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
