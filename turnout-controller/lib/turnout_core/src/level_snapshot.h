// Turnout pin levels kept in RTC memory, so the firmware can put every pin
// back after a software, panic or watchdog reset. RTC memory holds random
// data after power-on, so the record carries a magic number and a check
// value and is ignored unless both match.
#pragma once

#include <stdint.h>

namespace tc {

struct LevelSnapshot {
  uint32_t magic;
  uint16_t levels;       // bit i = pin level of channel i + 1 (1 = HIGH)
  uint8_t holdEnabled;   // pin latch (gpio hold) setting
  uint8_t plannedReset;  // console reset in progress: ResetKind + 1, 0 = none
  uint32_t check;
};

const uint32_t kLevelSnapshotMagic = 0x43524C31;  // "CRL1"

void snapshotWrite(LevelSnapshot* snapshot, uint16_t levels, bool holdEnabled, uint8_t plannedReset);
bool snapshotValid(const LevelSnapshot& snapshot);

}  // namespace tc
