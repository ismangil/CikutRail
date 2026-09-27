// Settings the web pages edit: network (Wi-Fi, MQTT, JMRI channel, node
// name), turnout names, behaviour and the admin password. Limits,
// validation and the form rules. Hardware-free.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "behaviour.h"
#include "channels.h"
#include "jmri_protocol.h"

namespace tc {

const uint8_t kMaxSsidLength = 32;
const uint8_t kMinWifiPasswordLength = 8;  // WPA2; empty means an open network
const uint8_t kMaxWifiPasswordLength = 63;
const uint8_t kMaxHostLength = 64;
const uint8_t kMaxMqttUserLength = 32;
const uint8_t kMaxMqttPasswordLength = 64;
const uint16_t kDefaultMqttPort = 1883;

struct NetConfig {
  char wifiSsid[kMaxSsidLength + 1];
  char wifiPassword[kMaxWifiPasswordLength + 1];
  char mqttHost[kMaxHostLength + 1];
  uint16_t mqttPort;
  char mqttUser[kMaxMqttUserLength + 1];
  char mqttPassword[kMaxMqttPasswordLength + 1];
  char jmriChannel[kMaxJmriChannelLength + 1];
  char nodeName[kMaxNodeNameLength + 1];
};

// Empty strings, port 1883, node name "turnout1".
void clearNetConfig(NetConfig* config);

// Copies src into dest (size bytes, NUL included). False, and dest left
// empty, if it doesn't fit.
bool copyField(char* dest, size_t size, const char* src);

// Whole decimal 1-65535, no sign or spaces.
bool parsePort(const char* text, uint16_t* port);

// An IPv4 address or host name: letters, digits, '.' and '-'.
bool validHost(const char* host);

// Checks every field. On failure, *error (if given) points to a static
// message naming the field.
bool validateNetConfig(const NetConfig& config, const char** error);

// What the setup page submits. A null field counts as empty.
struct NetForm {
  const char* wifiSsid;
  const char* wifiPassword;
  const char* mqttHost;
  const char* mqttPort;
  const char* mqttUser;
  const char* mqttPassword;
  const char* jmriChannel;
  const char* nodeName;
};

// Builds *out from a submitted form, starting from the saved settings, and
// validates it. The page never shows saved passwords, so:
// - a blank Wi-Fi password keeps the saved one if the network is unchanged;
//   for a different network, blank means an open network;
// - a blank MQTT password keeps the saved one if the user is unchanged; an
//   empty user always clears the password.
// Spaces around the host are dropped.
bool applyNetForm(const NetConfig& saved, const NetForm& form, NetConfig* out, const char** error);

// JMRI turnout names, one per channel; "" = channel unused.
struct TurnoutNames {
  char name[kChannelCount][kMaxTurnoutNameLength + 1];
};

// "101".."111": node 1's block (docs/MQTT_CONVENTIONS.md).
void defaultTurnoutNames(TurnoutNames* names);

// Builds names from the config page's 11 fields (spaces around each are
// dropped) and checks them: valid or empty, none used twice.
bool parseTurnoutNames(const char* const* fields, TurnoutNames* out, const char** error);

// Builds behaviour settings from the config page's fields: startup
// "restore"/"low", offline "hold"/"low", stagger and interval in ms.
bool parseBehaviourForm(const char* startup, const char* offline, const char* staggerMs, const char* intervalMs,
                        Behaviour* out, const char** error);

// Config page admin password: 8-64 printable ASCII characters, no spaces.
const uint8_t kMinAdminPasswordLength = 8;
const uint8_t kMaxAdminPasswordLength = 64;
bool validAdminPassword(const char* password);

// Escapes & < > " ' for HTML text and attribute values. False if the
// result (NUL included) doesn't fit in size bytes; out is then empty.
bool htmlEscape(const char* in, char* out, size_t size);

}  // namespace tc
