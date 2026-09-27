#include "level_snapshot.h"

#include "channels.h"

namespace tc {
namespace {

// FNV-1a over the payload fields.
uint32_t checkValue(uint16_t levels, uint8_t holdEnabled, uint8_t plannedReset) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(levels & 0xFF),
      static_cast<uint8_t>(levels >> 8),
      holdEnabled,
      plannedReset,
  };
  uint32_t hash = 2166136261u;
  for (uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 16777619u;
  }
  return hash;
}

}  // namespace

void snapshotWrite(LevelSnapshot* snapshot, uint16_t levels, bool holdEnabled, uint8_t plannedReset) {
  snapshot->magic = kLevelSnapshotMagic;
  snapshot->levels = levels & kAllChannelsMask;
  snapshot->holdEnabled = holdEnabled ? 1 : 0;
  snapshot->plannedReset = plannedReset;
  snapshot->check = checkValue(snapshot->levels, snapshot->holdEnabled, snapshot->plannedReset);
}

bool snapshotValid(const LevelSnapshot& snapshot) {
  return snapshot.magic == kLevelSnapshotMagic &&
         (snapshot.levels & ~kAllChannelsMask) == 0 &&
         snapshot.holdEnabled <= 1 &&
         snapshot.check == checkValue(snapshot.levels, snapshot.holdEnabled, snapshot.plannedReset);
}

}  // namespace tc
