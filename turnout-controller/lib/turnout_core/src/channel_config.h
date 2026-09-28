// Channel modes (phase 6): each channel is a turnout output or a sensor
// input, as JMRI's Raspberry Pi GPIO pins can be. Hardware-free.
#pragma once

#include <stdint.h>

#include "channels.h"

namespace tc {

enum class ChannelMode : uint8_t { Turnout, Sensor };
enum class SensorPull : uint8_t { None, Up, Down };

// Bit i = channel i + 1.
struct ChannelConfig {
  uint16_t sensorMask;     // channels in sensor mode (the rest are turnouts)
  uint16_t pullUpMask;     // sensor channels with the internal pull-up
  uint16_t pullDownMask;   // sensor channels with the internal pull-down
  uint16_t activeLowMask;  // sensor channels reporting ACTIVE when the pin is LOW
};

// All turnouts.
ChannelConfig defaultChannelConfig();

ChannelMode channelMode(const ChannelConfig& config, uint8_t channel);
SensorPull sensorPull(const ChannelConfig& config, uint8_t channel);
bool sensorActiveLow(const ChannelConfig& config, uint8_t channel);

// Keeps only meaningful bits: pulls and active-low on sensor channels,
// never both pulls on one channel (pull-up wins).
ChannelConfig normalised(const ChannelConfig& config);

inline bool sensorActive(bool pinHigh, bool activeLow) { return pinHigh != activeLow; }

const char* channelModeName(ChannelMode mode);
const char* sensorPullName(SensorPull pull);

// Builds a config from the config page's fields, one per channel:
// mode "turnout"/"sensor", pull "up"/"down"/"none", active LOW "on" or
// empty. Pull and active LOW are ignored for turnouts.
bool parseChannelForm(const char* const* modes, const char* const* pulls, const char* const* activeLow,
                      ChannelConfig* out, const char** error);

// Debounces sensor inputs: a level counts once it has been steady for
// stableMs.
class SensorDebouncer {
 public:
  explicit SensorDebouncer(uint32_t stableMs = 50);

  // Starts a channel at a known level (no change reported).
  void reset(uint8_t channel, bool level);

  // Returns true when the channel's debounced level changes.
  bool update(uint8_t channel, bool rawLevel, uint32_t nowMs);

  bool level(uint8_t channel) const;

 private:
  uint32_t m_stableMs;
  bool m_level[kChannelCount];
  bool m_candidate[kChannelCount];
  uint32_t m_candidateSinceMs[kChannelCount];
};

}  // namespace tc
