// Serial console commands for the phase 1 bench firmware.
// Parsing only; the firmware carries them out.
#pragma once

#include <stdint.h>

namespace tc {

enum class CommandType : uint8_t {
  Help,
  Status,
  Boot,       // boot
  Close,      // close <channels>
  Throw,      // throw <channels>
  Toggle,     // toggle <channels>
  Cycle,      // cycle <channel> <count> <interval_ms>
  FiveVolt,   // 5v [on|off]
  Hold,       // hold [on|off]
  Reset,      // reset soft|panic|wdt [5v-off]
  Pm1,        // pm1
  Pm1Buttons, // pm1 btn
};

enum class ResetKind : uint8_t { Soft, Panic, Watchdog };

// Bench limits for "cycle", so a mistyped command can't cook a snap coil.
const uint32_t kCycleMaxCount = 100;
const uint32_t kCycleMinIntervalMs = 250;
const uint32_t kCycleMaxIntervalMs = 60000;

const uint8_t kMaxLineLength = 96;

struct Command {
  CommandType type = CommandType::Help;
  uint16_t channels = 0;          // Close/Throw/Toggle/Cycle: channel mask
  bool hasSwitch = false;         // FiveVolt/Hold: false = just report
  bool switchOn = false;          // FiveVolt/Hold: requested state
  uint32_t count = 0;             // Cycle: number of toggles
  uint32_t intervalMs = 0;        // Cycle: time between toggles
  ResetKind resetKind = ResetKind::Soft;
  bool fiveVoltOffFirst = false;  // Reset: turn 5VOUT off before resetting
};

struct ParseResult {
  bool ok = false;
  Command command;
  const char* error = nullptr;  // static message when !ok
};

// Parses one console line. Keywords are case-insensitive. An empty line
// is an error with a null message, so the console can ignore it quietly.
ParseResult parseCommand(const char* line);

// Parses "3", "1,4,7", "2-5", "1-3,9" or "all" into a channel mask.
bool parseChannelList(const char* text, uint16_t* mask);

}  // namespace tc
