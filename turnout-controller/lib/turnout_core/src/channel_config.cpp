#include "channel_config.h"

#include <string.h>

namespace tc {
namespace {

uint16_t bitFor(uint8_t channel) { return static_cast<uint16_t>(1u << (channel - 1)); }

bool validChannel(uint8_t channel) { return channel >= 1 && channel <= kChannelCount; }

const char* orEmpty(const char* text) { return text != nullptr ? text : ""; }

}  // namespace

ChannelConfig defaultChannelConfig() {
  ChannelConfig config = {0, 0, 0, 0};
  return config;
}

ChannelMode channelMode(const ChannelConfig& config, uint8_t channel) {
  return validChannel(channel) && (config.sensorMask & bitFor(channel)) ? ChannelMode::Sensor : ChannelMode::Turnout;
}

SensorPull sensorPull(const ChannelConfig& config, uint8_t channel) {
  if (channelMode(config, channel) != ChannelMode::Sensor) return SensorPull::None;
  if (config.pullUpMask & bitFor(channel)) return SensorPull::Up;
  if (config.pullDownMask & bitFor(channel)) return SensorPull::Down;
  return SensorPull::None;
}

bool sensorActiveLow(const ChannelConfig& config, uint8_t channel) {
  return channelMode(config, channel) == ChannelMode::Sensor && (config.activeLowMask & bitFor(channel)) != 0;
}

ChannelConfig normalised(const ChannelConfig& config) {
  ChannelConfig out;
  out.sensorMask = config.sensorMask & kAllChannelsMask;
  out.pullUpMask = config.pullUpMask & out.sensorMask;
  out.pullDownMask = config.pullDownMask & out.sensorMask & static_cast<uint16_t>(~out.pullUpMask);
  out.activeLowMask = config.activeLowMask & out.sensorMask;
  return out;
}

const char* channelModeName(ChannelMode mode) { return mode == ChannelMode::Sensor ? "sensor" : "turnout"; }

const char* sensorPullName(SensorPull pull) {
  switch (pull) {
    case SensorPull::Up: return "pull-up";
    case SensorPull::Down: return "pull-down";
    case SensorPull::None: break;
  }
  return "no pull";
}

bool parseChannelForm(const char* const* modes, const char* const* pulls, const char* const* activeLow,
                      ChannelConfig* out, const char** error) {
  ChannelConfig config = defaultChannelConfig();
  for (uint8_t channel = 1; channel <= kChannelCount; ++channel) {
    const uint8_t i = channel - 1;
    const char* mode = orEmpty(modes[i]);
    if (strcmp(mode, "sensor") == 0) {
      config.sensorMask |= bitFor(channel);
    } else if (strcmp(mode, "turnout") != 0) {
      if (error != nullptr) *error = "channel mode: turnout or sensor";
      return false;
    }
    const char* pull = orEmpty(pulls[i]);
    if (strcmp(pull, "up") == 0) {
      config.pullUpMask |= bitFor(channel);
    } else if (strcmp(pull, "down") == 0) {
      config.pullDownMask |= bitFor(channel);
    } else if (strcmp(pull, "none") != 0 && pull[0] != '\0') {
      if (error != nullptr) *error = "sensor pull: up, down or none";
      return false;
    }
    if (strcmp(orEmpty(activeLow[i]), "on") == 0) config.activeLowMask |= bitFor(channel);
  }
  *out = normalised(config);
  return true;
}

SensorDebouncer::SensorDebouncer(uint32_t stableMs) : m_stableMs(stableMs) {
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    m_level[i] = false;
    m_candidate[i] = false;
    m_candidateSinceMs[i] = 0;
  }
}

void SensorDebouncer::reset(uint8_t channel, bool level) {
  if (!validChannel(channel)) return;
  m_level[channel - 1] = level;
  m_candidate[channel - 1] = level;
}

bool SensorDebouncer::update(uint8_t channel, bool rawLevel, uint32_t nowMs) {
  if (!validChannel(channel)) return false;
  const uint8_t i = channel - 1;
  if (rawLevel != m_candidate[i]) {
    m_candidate[i] = rawLevel;
    m_candidateSinceMs[i] = nowMs;
    return false;
  }
  if (m_candidate[i] != m_level[i] && nowMs - m_candidateSinceMs[i] >= m_stableMs) {
    m_level[i] = m_candidate[i];
    return true;
  }
  return false;
}

bool SensorDebouncer::level(uint8_t channel) const { return validChannel(channel) && m_level[channel - 1]; }

}  // namespace tc
