// JMRI MQTT turnout topics and payloads (jmri/jmrix/mqtt/MqttTurnout.java).
// JMRI publishes CLOSED / THROWN, retained, QoS 2, on
// <channel>track/turnout/<name>; see docs/MQTT_CONVENTIONS.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tc {

const uint8_t kMaxJmriChannelLength = 32;
const uint8_t kMaxTurnoutNameLength = 16;
const uint8_t kMaxNodeNameLength = 32;
const uint8_t kMaxTopicLength = 96;

enum class JmriPayload : uint8_t {
  Closed,
  Thrown,
  Unknown,       // JMRI's UNKNOWN: logged, pin untouched
  Inconsistent,  // JMRI's INCONSISTENT: logged, pin untouched
  Other,         // anything else, including an empty payload
};

// Exact, case-sensitive match, as JMRI compares them.
JmriPayload parseTurnoutPayload(const char* data, size_t length);
const char* payloadName(JmriPayload payload);

// JMRI channel: empty, or ending in '/' (older JMRI used "/trains/"),
// with no MQTT wildcards.
bool validJmriChannel(const char* channel);

// Turnout and node names: letters, digits, '_' and '-', so they are safe
// as one topic level.
bool validTurnoutName(const char* name);
bool validNodeName(const char* name);

// Checks one name per channel: each valid or empty (channel unused), and
// no name used twice. On failure, *error (if given) points to a static
// message.
bool validateTurnoutNames(const char* const* names, uint8_t count, const char** error);

// Writes "<channel>track/turnout/<name>". False if it doesn't fit.
bool turnoutTopic(const char* channel, const char* name, char* out, size_t size);

// Returns the channel (1..count) whose command topic equals
// topic[0..length), or 0. Sub-topics such as .../101/state never match.
uint8_t matchTurnoutTopic(const char* channel, const char* const* names, uint8_t count, const char* topic,
                          size_t length);

}  // namespace tc
