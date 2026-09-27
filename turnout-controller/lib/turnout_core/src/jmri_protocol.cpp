#include "jmri_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace tc {
namespace {

const char kTurnoutPrefix[] = "track/turnout/";

bool payloadIs(const char* data, size_t length, const char* word) {
  return length == strlen(word) && memcmp(data, word, length) == 0;
}

bool validTopicWord(const char* text, size_t maxLength) {
  if (text == nullptr) return false;
  const size_t length = strlen(text);
  if (length == 0 || length > maxLength) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (!isalnum(c) && c != '_' && c != '-') return false;
  }
  return true;
}

}  // namespace

JmriPayload parseTurnoutPayload(const char* data, size_t length) {
  if (data == nullptr) return JmriPayload::Other;
  if (payloadIs(data, length, "CLOSED")) return JmriPayload::Closed;
  if (payloadIs(data, length, "THROWN")) return JmriPayload::Thrown;
  if (payloadIs(data, length, "UNKNOWN")) return JmriPayload::Unknown;
  if (payloadIs(data, length, "INCONSISTENT")) return JmriPayload::Inconsistent;
  return JmriPayload::Other;
}

const char* payloadName(JmriPayload payload) {
  switch (payload) {
    case JmriPayload::Closed: return "CLOSED";
    case JmriPayload::Thrown: return "THROWN";
    case JmriPayload::Unknown: return "UNKNOWN";
    case JmriPayload::Inconsistent: return "INCONSISTENT";
    case JmriPayload::Other: break;
  }
  return "other";
}

bool validJmriChannel(const char* channel) {
  if (channel == nullptr) return false;
  const size_t length = strlen(channel);
  if (length == 0) return true;
  if (length > kMaxJmriChannelLength || channel[length - 1] != '/') return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(channel[i]);
    if (c == '+' || c == '#' || !isgraph(c)) return false;
  }
  return true;
}

bool validTurnoutName(const char* name) { return validTopicWord(name, kMaxTurnoutNameLength); }

bool validNodeName(const char* name) { return validTopicWord(name, kMaxNodeNameLength); }

bool validateTurnoutNames(const char* const* names, uint8_t count, const char** error) {
  const char* message = nullptr;
  for (uint8_t i = 0; i < count && message == nullptr; ++i) {
    if (names[i] == nullptr || names[i][0] == '\0') continue;
    if (!validTurnoutName(names[i])) {
      message = "turnout names must be 1-16 letters, digits, '_' or '-'";
      break;
    }
    for (uint8_t j = 0; j < i; ++j) {
      if (names[j] != nullptr && strcmp(names[i], names[j]) == 0) {
        message = "turnout name used twice";
        break;
      }
    }
  }
  if (error != nullptr) *error = message;
  return message == nullptr;
}

bool turnoutTopic(const char* channel, const char* name, char* out, size_t size) {
  const int written = snprintf(out, size, "%s%s%s", channel, kTurnoutPrefix, name);
  return written >= 0 && static_cast<size_t>(written) < size;
}

uint8_t matchTurnoutTopic(const char* channel, const char* const* names, uint8_t count, const char* topic,
                          size_t length) {
  const size_t channelLength = strlen(channel);
  const size_t prefixLength = sizeof(kTurnoutPrefix) - 1;
  if (topic == nullptr || length <= channelLength + prefixLength) return 0;
  if (memcmp(topic, channel, channelLength) != 0) return 0;
  if (memcmp(topic + channelLength, kTurnoutPrefix, prefixLength) != 0) return 0;

  const char* name = topic + channelLength + prefixLength;
  const size_t nameLength = length - channelLength - prefixLength;
  for (uint8_t i = 0; i < count; ++i) {
    if (names[i] == nullptr || names[i][0] == '\0') continue;
    if (strlen(names[i]) == nameLength && memcmp(names[i], name, nameLength) == 0) return i + 1;
  }
  return 0;
}

}  // namespace tc
