// Behaviour settings (startup level, JMRI-offline policy, stagger, minimum
// interval) and the scheduler that applies turnout changes with them.
// Hardware-free: time is passed in.
#pragma once

#include <stdint.h>

#include "channels.h"
#include "turnout_state.h"

namespace tc {

enum class StartupLevel : uint8_t {
  Restore,  // last saved levels (default)
  Low,      // all LOW, as JMRI's Pi GPIO turnouts start
};

enum class OfflinePolicy : uint8_t {
  Hold,  // keep every pin as it is (default)
  Low,   // all LOW, as JMRI's Pi GPIO turnouts do on shutdown
};

const uint16_t kMaxStaggerMs = 5000;
const uint16_t kMaxMinIntervalMs = 10000;

struct Behaviour {
  StartupLevel startup;
  OfflinePolicy offline;
  uint16_t staggerMs;      // between any two changes; 0 = none (as the Pi)
  uint16_t minIntervalMs;  // between two changes of one turnout; 0 = none
  bool feedback;           // publish each turnout's pin state on .../state (JMRI MONITORING)
};

Behaviour defaultBehaviour();
const char* startupLevelName(StartupLevel level);
const char* offlinePolicyName(OfflinePolicy policy);

// Holds the wanted state per channel and releases one change at a time:
// at least staggerMs after the previous change of any channel, and at
// least minIntervalMs after the previous change of the same channel. A
// newer request replaces an older one for the same channel; a request
// that matches the channel's state by the time it is due is dropped.
// Waiting requests are released oldest first.
class TurnoutScheduler {
 public:
  TurnoutScheduler();

  void configure(uint16_t staggerMs, uint16_t minIntervalMs);

  void request(uint8_t channel, TurnoutState state);

  // Records a change made outside the scheduler (the console), so the
  // stagger and minimum interval count from it.
  void noteApplied(uint8_t channel, uint32_t nowMs);

  // If a change is due at nowMs, returns it and counts it as applied.
  // current[i] is channel i + 1's present state.
  bool next(uint32_t nowMs, const TurnoutState* current, uint8_t* channel, TurnoutState* state);

  bool isPending(uint8_t channel) const;
  uint8_t pendingCount() const;

 private:
  uint16_t m_staggerMs;
  uint16_t m_minIntervalMs;
  bool m_wanted[kChannelCount];
  TurnoutState m_target[kChannelCount];
  uint32_t m_order[kChannelCount];
  uint32_t m_nextOrder;
  bool m_changedOnce[kChannelCount];
  uint32_t m_lastChangeMs[kChannelCount];
  bool m_anyChanged;
  uint32_t m_lastAnyChangeMs;
};

}  // namespace tc
