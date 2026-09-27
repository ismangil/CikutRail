// Wi-Fi and the MQTT link to JMRI. esp-mqtt runs in its own task; turnout
// commands are queued and taken from the main loop, so only the main loop
// touches the pins. Settings come from flash (the setup page), or from
// src/local_settings.h until something is saved.
#pragma once

#include <stdint.h>

#include "jmri_protocol.h"
#include "net_config.h"

namespace net {

struct TurnoutMessage {
  uint8_t channel;  // 1..tc::kChannelCount
  tc::JmriPayload payload;
  bool retained;
};

// Loads the settings and joins Wi-Fi, or opens the setup portal if there
// are none.
void begin(const char* firmwareVersion);

// Logs connection changes, publishes cikutrail/<node>/info, and opens the
// setup portal if the saved network can't be joined for a while.
void loop();

// Switches to new settings (the setup page, after saving) without a
// reboot: leaves MQTT and Wi-Fi, then joins again with these.
void applyConfig(const tc::NetConfig& config);

// Erases the saved settings, leaves the network and opens the portal.
void forget();

const tc::NetConfig& config();
bool mqttUp();

// Called by the portal. While it is open after a Wi-Fi failure, Wi-Fi
// retries only when nobody is on the setup page, since each attempt
// changes the radio's channel.
void onPortalOpened(bool wifiFailed);
void onPortalClosed();

// Takes the next turnout message received from the broker.
bool nextTurnoutMessage(TurnoutMessage* message);

const char* turnoutName(uint8_t channel);

// Counts what the main loop did with each message.
enum class Outcome : uint8_t { Applied, Unchanged, Ignored };
void noteOutcome(Outcome outcome);

void printStatus();

}  // namespace net
