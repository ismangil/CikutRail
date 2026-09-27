// Network settings (Wi-Fi, MQTT, JMRI channel, node name): limits,
// validation, and the setup page's form rules. Hardware-free.
#pragma once

#include <stddef.h>
#include <stdint.h>

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

// Escapes & < > " ' for HTML text and attribute values. False if the
// result (NUL included) doesn't fit in size bytes; out is then empty.
bool htmlEscape(const char* in, char* out, size_t size);

}  // namespace tc
