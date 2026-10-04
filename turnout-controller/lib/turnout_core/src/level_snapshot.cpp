#include "level_snapshot.h"

#include "channels.h"

namespace tc {
namespace {

// FNV-1a over the payload fields.
uint32_t checkValue(const LevelSnapshot& s) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(s.levels & 0xFF),       static_cast<uint8_t>(s.levels >> 8),
      s.holdEnabled,                               s.plannedReset,
      static_cast<uint8_t>(s.sensorMask & 0xFF),   static_cast<uint8_t>(s.sensorMask >> 8),
      static_cast<uint8_t>(s.pullUpMask & 0xFF),   static_cast<uint8_t>(s.pullUpMask >> 8),
      static_cast<uint8_t>(s.pullDownMask & 0xFF), static_cast<uint8_t>(s.pullDownMask >> 8),
  };
  uint32_t hash = 2166136261u;
  for (uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 16777619u;
  }
  return hash;
}

}  // namespace

void snapshotWrite(LevelSnapshot* snapshot, uint16_t levels, bool holdEnabled, uint8_t plannedReset,
                   uint16_t sensorMask, uint16_t pullUpMask, uint16_t pullDownMask) {
  snapshot->magic = kLevelSnapshotMagic;
  snapshot->sensorMask = sensorMask & kAllChannelsMask;
  snapshot->levels = levels & kAllChannelsMask & static_cast<uint16_t>(~snapshot->sensorMask);
  snapshot->holdEnabled = holdEnabled ? 1 : 0;
  snapshot->plannedReset = plannedReset;
  snapshot->pullUpMask = pullUpMask & snapshot->sensorMask;
  snapshot->pullDownMask = pullDownMask & snapshot->sensorMask & static_cast<uint16_t>(~snapshot->pullUpMask);
  snapshot->check = checkValue(*snapshot);
}

bool snapshotValid(const LevelSnapshot& snapshot) {
  const uint16_t unused = static_cast<uint16_t>(~kAllChannelsMask);
  return snapshot.magic == kLevelSnapshotMagic && (snapshot.levels & unused) == 0 &&
         (snapshot.sensorMask & unused) == 0 && (snapshot.levels & snapshot.sensorMask) == 0 &&
         (snapshot.pullUpMask & ~snapshot.sensorMask) == 0 && (snapshot.pullDownMask & ~snapshot.sensorMask) == 0 &&
         snapshot.holdEnabled <= 1 && snapshot.check == checkValue(snapshot);
}

}  // namespace tc
