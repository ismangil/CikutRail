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

uint16_t g_levels = 0;  // turnout channels only; sensor bits stay 0
bool g_hold = kColdBootHold;
tc::ChannelConfig g_channels = {0, 0, 0, 0};
BootReport g_boot = {};

gpio_num_t gpioFor(uint8_t channel) {
  return static_cast<gpio_num_t>(tc::kChannelGpio[channel - 1]);
}

uint16_t bitFor(uint8_t channel) { return static_cast<uint16_t>(1u << (channel - 1)); }

bool isSensor(uint8_t channel) { return (g_channels.sensorMask & bitFor(channel)) != 0; }

void saveSnapshot(uint8_t plannedReset) {
  tc::snapshotWrite(&g_snapshot, g_levels, g_hold, plannedReset, g_channels.sensorMask, g_channels.pullUpMask,
                    g_channels.pullDownMask);
}

// Only register and GPIO calls (no logging), so these also run from the
// constructor below. gpio_config() logs, so the pad is set up call by call.

// A turnout output. The output latch is set before the driver is enabled,
// so a pad still held from before a reset sees the same level when released.
void setUpTurnoutPin(uint8_t channel, bool level) {
  const gpio_num_t gpio = gpioFor(channel);
  gpio_set_level(gpio, level ? 1 : 0);
  esp_rom_gpio_pad_select_gpio(gpio);
  gpio_set_pull_mode(gpio, GPIO_FLOATING);
  gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT);  // input path on, so padLevel() reads the pad
  gpio_hold_dis(gpio);
  if (g_hold) gpio_hold_en(gpio);
}

// A sensor input: never driven, never held.
void setUpSensorPin(uint8_t channel, tc::SensorPull pull) {
  const gpio_num_t gpio = gpioFor(channel);
  gpio_hold_dis(gpio);
  esp_rom_gpio_pad_select_gpio(gpio);
  gpio_set_direction(gpio, GPIO_MODE_INPUT);
  switch (pull) {
    case tc::SensorPull::Up: gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY); break;
    case tc::SensorPull::Down: gpio_set_pull_mode(gpio, GPIO_PULLDOWN_ONLY); break;
    case tc::SensorPull::None: gpio_set_pull_mode(gpio, GPIO_FLOATING); break;
  }
}

void setUpChannel(uint8_t channel) {
  if (isSensor(channel)) {
    setUpSensorPin(channel, tc::sensorPull(g_channels, channel));
  } else {
    setUpTurnoutPin(channel, (g_levels & bitFor(channel)) != 0);
  }
}

// Runs as a C++ constructor, before app_main() and so before Arduino's
// start-up code. The order among constructors follows link order (the
// ESP-IDF linker script sorts priorities only within one file), so this
// uses only ROM, register and GPIO calls: no logging, and no
// esp_reset_reason(), which another constructor sets up.
//
// After a reset, RTC memory has the levels and which channels are
// sensors: drive the turnouts, set up the sensors. After a power cut it
// is empty and nothing is known about sensors, so no pin is driven yet:
// 5VOUT is off at power-up, so undriven GreenHat inputs can't pulse.
void earlyInit() {
  g_boot.romResetReason = esp_rom_get_reset_reason(0);
  g_boot.restored = tc::snapshotValid(g_snapshot);
  g_boot.plannedReset = g_boot.restored ? g_snapshot.plannedReset : 0;
  g_levels = g_boot.restored ? g_snapshot.levels : kColdBootLevels;
  g_hold = g_boot.restored ? g_snapshot.holdEnabled != 0 : kColdBootHold;
  if (g_boot.restored) {
    g_channels.sensorMask = g_snapshot.sensorMask;
    g_channels.pullUpMask = g_snapshot.pullUpMask;
    g_channels.pullDownMask = g_snapshot.pullDownMask;
  }

  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    if (gpio_get_level(gpioFor(channel))) g_boot.padLevelsAtBoot |= bitFor(channel);
  }
  if (!g_boot.restored) return;

  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) setUpChannel(channel);
  g_boot.drivenAtUs = esp_timer_get_time();
  saveSnapshot(0);
}

__attribute__((constructor)) void driveEarly() { earlyInit(); }

}  // namespace

void applyStartup(tc::StartupLevel policy, bool haveSavedLevels, uint16_t savedLevels,
                  const tc::ChannelConfig& channels) {
  g_boot.resetReason = esp_reset_reason();
  const bool firstDrive = !g_boot.restored;
  const uint16_t levelsBefore = g_levels;
  g_channels = tc::normalised(channels);
  const uint16_t turnouts = tc::kAllChannelsMask & static_cast<uint16_t>(~g_channels.sensorMask);

  uint16_t target;
  if (policy == tc::StartupLevel::Low) {
    target = 0;
    g_boot.source = LevelSource::PolicyLow;
  } else if (g_boot.restored) {
    target = g_levels;  // newer than flash, which is written a little after each change
    g_boot.source = LevelSource::Rtc;
  } else if (haveSavedLevels) {
    target = savedLevels;
    g_boot.source = LevelSource::Flash;
  } else {
    target = 0;
    g_boot.source = LevelSource::NothingSaved;
  }
  g_levels = target & turnouts;

  // Every pin is set up here: after a power cut none was driven yet, and
  // after a reset the modes in flash are the ones to trust.
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) setUpChannel(channel);
  g_boot.changedAtStartup = firstDrive ? g_levels : static_cast<uint16_t>((g_levels ^ levelsBefore) & turnouts);
  g_boot.startupAtUs = esp_timer_get_time();
  if (firstDrive) g_boot.drivenAtUs = g_boot.startupAtUs;
  saveSnapshot(0);
}

const BootReport& bootReport() { return g_boot; }

void setState(uint8_t channel, tc::TurnoutState state) {
  if (channel < 1 || channel > tc::kChannelCount || isSensor(channel)) return;
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

void setChannelConfig(const tc::ChannelConfig& channels) {
  const tc::ChannelConfig next = tc::normalised(channels);
  const uint16_t becomingTurnouts = g_channels.sensorMask & static_cast<uint16_t>(~next.sensorMask);
  g_channels = next;
  // A channel that becomes a turnout starts LOW (THROWN).
  g_levels &= static_cast<uint16_t>(~(g_channels.sensorMask | becomingTurnouts));
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) setUpChannel(channel);
  saveSnapshot(0);
}

const tc::ChannelConfig& channelConfig() { return g_channels; }

bool isSensorChannel(uint8_t channel) { return channel >= 1 && channel <= tc::kChannelCount && isSensor(channel); }

uint16_t levels() { return g_levels; }

tc::TurnoutState state(uint8_t channel) {
  return tc::stateForPinLevel((g_levels & bitFor(channel)) != 0);
}

bool padLevel(uint8_t channel) { return gpio_get_level(gpioFor(channel)) != 0; }

void setHold(bool enabled) {
  g_hold = enabled;
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    if (enabled && !isSensor(channel)) {
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
