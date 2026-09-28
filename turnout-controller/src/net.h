// Wi-Fi and the MQTT link to JMRI. esp-mqtt runs in its own task; turnout
// commands are queued and taken from the main loop, so only the main loop
// touches the pins. Settings come from flash (the setup page), or from
// src/local_settings.h until something is saved.
#pragma once

#include <stdint.h>

#include "jmri_protocol.h"
#include "net_config.h"

namespace net {

struct Message {
  enum class Kind : uint8_t { Turnout, JmriState };
  Kind kind;
  uint8_t channel;              // Turnout: 1..tc::kChannelCount
  tc::JmriPayload payload;      // Turnout
  tc::JmriState jmriState;      // JmriState: <channel>track/state
  char text[16];                // JmriState: the payload, cut short, for the log
  bool retained;
};

// Loads the settings and joins Wi-Fi, or opens the setup portal if there
// are none.
void begin(const char* firmwareVersion);

// Logs connection changes, publishes cikutrail/<node>/info, and opens the
// setup portal if the saved network can't be joined for a while.
void loop();

// Switches to new settings (the setup or config page, after saving)
// without a reboot. Same Wi-Fi: only MQTT reconnects. Otherwise it leaves
// Wi-Fi and joins again with these.
void applyConfig(const tc::NetConfig& config);

// New turnout names (the config page): stops MQTT, saves them, and
// subscribes again. Channels that lose their name keep their pin level.
void applyTurnoutNames(const tc::TurnoutNames& names);

// Erases the saved settings, leaves the network and opens the portal.
void forget();

const tc::NetConfig& config();
bool mqttUp();
bool wifiUp();
// No usable settings: Wi-Fi and MQTT aren't running.
bool isOff();

// Called by the portal. While it is open after a Wi-Fi failure, Wi-Fi
// retries only when nobody is on the setup page, since each attempt
// changes the radio's channel.
void onPortalOpened(bool wifiFailed);
void onPortalClosed();

// Subscribes to the turnout topics again, so the broker re-sends every
// retained command (MQTT 3.1.1 re-sends retained messages when a
// subscription is replaced). Done from loop() once MQTT is up.
void rereadRetained();

// Takes the next turnout or JMRI state message received from the broker.
bool nextMessage(Message* message);

const char* turnoutName(uint8_t channel);

// Counts what the main loop did with each message.
enum class Outcome : uint8_t { Applied, Unchanged, Ignored };
void noteOutcome(Outcome outcome);

void printStatus();

}  // namespace net
