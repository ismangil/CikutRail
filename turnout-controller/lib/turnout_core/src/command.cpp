#include "command.h"

#include <ctype.h>
#include <string.h>

#include "channels.h"

namespace tc {
namespace {

const uint8_t kMaxTokens = 6;

bool equalsIgnoreCase(const char* a, const char* b) {
  while (*a != '\0' && *b != '\0') {
    if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == *b;
}

// Parses a whole decimal number; rejects signs, spaces and overflow past max.
bool parseNumber(const char* text, uint32_t max, uint32_t* value) {
  if (*text == '\0') return false;
  uint32_t result = 0;
  for (; *text != '\0'; ++text) {
    if (!isdigit(static_cast<unsigned char>(*text))) return false;
    const uint32_t digit = static_cast<uint32_t>(*text - '0');
    if (digit > max || result > (max - digit) / 10) return false;
    result = result * 10 + digit;
  }
  *value = result;
  return true;
}

bool parseChannel(const char* text, uint32_t* channel) {
  return parseNumber(text, kChannelCount, channel) && *channel >= 1;
}

// Splits line into whitespace-separated tokens inside buffer.
uint8_t tokenize(char* buffer, char** tokens) {
  uint8_t count = 0;
  char* p = buffer;
  while (*p != '\0') {
    while (isspace(static_cast<unsigned char>(*p))) ++p;
    if (*p == '\0') break;
    if (count == kMaxTokens) return kMaxTokens + 1;  // too many
    tokens[count++] = p;
    while (*p != '\0' && !isspace(static_cast<unsigned char>(*p))) ++p;
    if (*p != '\0') *p++ = '\0';
  }
  return count;
}

ParseResult fail(const char* message) {
  ParseResult result;
  result.error = message;
  return result;
}

ParseResult succeed(const Command& command) {
  ParseResult result;
  result.ok = true;
  result.command = command;
  return result;
}

ParseResult parseSwitch(CommandType type, char** tokens, uint8_t count) {
  Command command;
  command.type = type;
  if (count == 1) return succeed(command);
  if (count != 2) return fail("expected: on, off, or nothing");
  command.hasSwitch = true;
  if (equalsIgnoreCase(tokens[1], "on")) {
    command.switchOn = true;
  } else if (!equalsIgnoreCase(tokens[1], "off")) {
    return fail("expected: on, off, or nothing");
  }
  return succeed(command);
}

}  // namespace

bool parseChannelList(const char* text, uint16_t* mask) {
  if (equalsIgnoreCase(text, "all")) {
    *mask = kAllChannelsMask;
    return true;
  }
  char buffer[kMaxLineLength + 1];
  const size_t length = strlen(text);
  if (length == 0 || length > kMaxLineLength) return false;
  memcpy(buffer, text, length + 1);

  uint16_t result = 0;
  char* item = buffer;
  while (item != nullptr) {
    char* next = strchr(item, ',');
    if (next != nullptr) *next++ = '\0';

    uint32_t first = 0;
    uint32_t last = 0;
    char* dash = strchr(item, '-');
    if (dash != nullptr) {
      *dash = '\0';
      if (!parseChannel(item, &first) || !parseChannel(dash + 1, &last) || last < first) {
        return false;
      }
    } else {
      if (!parseChannel(item, &first)) return false;
      last = first;
    }
    for (uint32_t channel = first; channel <= last; ++channel) {
      result |= static_cast<uint16_t>(1u << (channel - 1));
    }
    item = next;
  }
  *mask = result;
  return true;
}

ParseResult parseCommand(const char* line) {
  const size_t length = strlen(line);
  if (length > kMaxLineLength) return fail("line too long");

  char buffer[kMaxLineLength + 1];
  memcpy(buffer, line, length + 1);
  char* tokens[kMaxTokens];
  const uint8_t count = tokenize(buffer, tokens);
  if (count == 0) return fail(nullptr);
  if (count > kMaxTokens) return fail("too many words");

  const char* verb = tokens[0];
  Command command;

  if (equalsIgnoreCase(verb, "help") || equalsIgnoreCase(verb, "?")) {
    if (count != 1) return fail("usage: help");
    command.type = CommandType::Help;
    return succeed(command);
  }
  if (equalsIgnoreCase(verb, "status") || equalsIgnoreCase(verb, "s")) {
    if (count != 1) return fail("usage: status");
    command.type = CommandType::Status;
    return succeed(command);
  }
  if (equalsIgnoreCase(verb, "pm1")) {
    if (count != 1) return fail("usage: pm1");
    command.type = CommandType::Pm1;
    return succeed(command);
  }

  const bool isClose = equalsIgnoreCase(verb, "close") || equalsIgnoreCase(verb, "c");
  const bool isThrow = equalsIgnoreCase(verb, "throw") || equalsIgnoreCase(verb, "t");
  const bool isToggle = equalsIgnoreCase(verb, "toggle");
  if (isClose || isThrow || isToggle) {
    if (count != 2) return fail("usage: close|throw|toggle <channels>, e.g. 3, 1-4,7 or all");
    if (!parseChannelList(tokens[1], &command.channels)) {
      return fail("channels must be 1-11, e.g. 3, 1-4,7 or all");
    }
    command.type = isClose ? CommandType::Close : isThrow ? CommandType::Throw : CommandType::Toggle;
    return succeed(command);
  }

  if (equalsIgnoreCase(verb, "cycle")) {
    if (count != 4) return fail("usage: cycle <channel> <count> <interval_ms>");
    uint32_t channel = 0;
    if (!parseChannel(tokens[1], &channel)) return fail("channel must be 1-11");
    if (!parseNumber(tokens[2], kCycleMaxCount, &command.count) || command.count == 0) {
      return fail("count must be 1-100");
    }
    if (!parseNumber(tokens[3], kCycleMaxIntervalMs, &command.intervalMs) ||
        command.intervalMs < kCycleMinIntervalMs) {
      return fail("interval must be 250-60000 ms");
    }
    command.type = CommandType::Cycle;
    command.channels = static_cast<uint16_t>(1u << (channel - 1));
    return succeed(command);
  }

  if (equalsIgnoreCase(verb, "5v")) return parseSwitch(CommandType::FiveVolt, tokens, count);
  if (equalsIgnoreCase(verb, "hold")) return parseSwitch(CommandType::Hold, tokens, count);

  if (equalsIgnoreCase(verb, "reset")) {
    if (count < 2 || count > 3) return fail("usage: reset soft|panic|wdt [5v-off]");
    command.type = CommandType::Reset;
    if (equalsIgnoreCase(tokens[1], "soft")) {
      command.resetKind = ResetKind::Soft;
    } else if (equalsIgnoreCase(tokens[1], "panic")) {
      command.resetKind = ResetKind::Panic;
    } else if (equalsIgnoreCase(tokens[1], "wdt")) {
      command.resetKind = ResetKind::Watchdog;
    } else {
      return fail("usage: reset soft|panic|wdt [5v-off]");
    }
    if (count == 3) {
      if (!equalsIgnoreCase(tokens[2], "5v-off")) return fail("usage: reset soft|panic|wdt [5v-off]");
      command.fiveVoltOffFirst = true;
    }
    return succeed(command);
  }

  return fail("unknown command; type help");
}

}  // namespace tc
