#include "behaviour.h"

namespace tc {

Behaviour defaultBehaviour() {
  Behaviour behaviour;
  behaviour.startup = StartupLevel::Restore;
  behaviour.offline = OfflinePolicy::Hold;
  behaviour.staggerMs = 0;
  behaviour.minIntervalMs = 0;
  behaviour.feedback = false;
  return behaviour;
}

const char* startupLevelName(StartupLevel level) { return level == StartupLevel::Restore ? "restore" : "low"; }

const char* offlinePolicyName(OfflinePolicy policy) { return policy == OfflinePolicy::Hold ? "hold" : "low"; }

TurnoutScheduler::TurnoutScheduler()
    : m_staggerMs(0), m_minIntervalMs(0), m_nextOrder(0), m_anyChanged(false), m_lastAnyChangeMs(0) {
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    m_wanted[i] = false;
    m_target[i] = TurnoutState::Thrown;
    m_order[i] = 0;
    m_changedOnce[i] = false;
    m_lastChangeMs[i] = 0;
  }
}

void TurnoutScheduler::configure(uint16_t staggerMs, uint16_t minIntervalMs) {
  m_staggerMs = staggerMs;
  m_minIntervalMs = minIntervalMs;
}

void TurnoutScheduler::request(uint8_t channel, TurnoutState state) {
  if (channel < 1 || channel > kChannelCount) return;
  const uint8_t i = channel - 1;
  // A replaced request keeps its place in the queue.
  if (!m_wanted[i]) m_order[i] = m_nextOrder++;
  m_wanted[i] = true;
  m_target[i] = state;
}

void TurnoutScheduler::noteApplied(uint8_t channel, uint32_t nowMs) {
  if (channel < 1 || channel > kChannelCount) return;
  m_changedOnce[channel - 1] = true;
  m_lastChangeMs[channel - 1] = nowMs;
  m_anyChanged = true;
  m_lastAnyChangeMs = nowMs;
}

bool TurnoutScheduler::next(uint32_t nowMs, const TurnoutState* current, uint8_t* channel, TurnoutState* state) {
  // Requests that already match need no change and no time slot.
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    if (m_wanted[i] && m_target[i] == current[i]) m_wanted[i] = false;
  }
  if (m_anyChanged && nowMs - m_lastAnyChangeMs < m_staggerMs) return false;

  int8_t best = -1;
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    if (!m_wanted[i]) continue;
    if (m_changedOnce[i] && nowMs - m_lastChangeMs[i] < m_minIntervalMs) continue;
    // Oldest request first; the difference stays right when the counter wraps.
    if (best < 0 || static_cast<int32_t>(m_order[i] - m_order[best]) < 0) best = static_cast<int8_t>(i);
  }
  if (best < 0) return false;

  m_wanted[best] = false;
  *channel = static_cast<uint8_t>(best + 1);
  *state = m_target[best];
  noteApplied(*channel, nowMs);
  return true;
}

bool TurnoutScheduler::isPending(uint8_t channel) const {
  return channel >= 1 && channel <= kChannelCount && m_wanted[channel - 1];
}

uint8_t TurnoutScheduler::pendingCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    if (m_wanted[i]) ++count;
  }
  return count;
}

}  // namespace tc
