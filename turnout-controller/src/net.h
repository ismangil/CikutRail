// Wi-Fi and the MQTT link to JMRI (phase 2). esp-mqtt runs in its own
// task; turnout commands are queued and taken from the main loop, so only
// the main loop touches the pins.
#pragma once

#include <stdint.h>

#include "jmri_protocol.h"

namespace net {

struct TurnoutMessage {
  uint8_t channel;  // 1..tc::kChannelCount
  tc::JmriPayload payload;
  bool retained;
};

// Checks the compiled-in settings and starts Wi-Fi; MQTT follows once
// Wi-Fi is up. Does nothing but report if the settings are missing or bad.
void begin(const char* firmwareVersion);

// Logs connection changes and publishes cikutrail/<node>/info. Call often.
void loop();

// Takes the next turnout message received from the broker.
bool nextTurnoutMessage(TurnoutMessage* message);

const char* turnoutName(uint8_t channel);

// Counts what the main loop did with each message.
enum class Outcome : uint8_t { Applied, Unchanged, Ignored };
void noteOutcome(Outcome outcome);

void printStatus();

}  // namespace net
