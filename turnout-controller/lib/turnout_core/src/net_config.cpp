#include "net_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace tc {
namespace {

const char* orEmpty(const char* text) { return text != nullptr ? text : ""; }

bool fieldFits(const char* text, size_t maxLength) { return strlen(text) <= maxLength; }

}  // namespace

void clearNetConfig(NetConfig* config) {
  memset(config, 0, sizeof(*config));
  config->mqttPort = kDefaultMqttPort;
  copyField(config->nodeName, sizeof(config->nodeName), "turnout1");
}

bool copyField(char* dest, size_t size, const char* src) {
  src = orEmpty(src);
  const size_t length = strlen(src);
  if (size == 0) return false;
  if (length >= size) {
    dest[0] = '\0';
    return false;
  }
  memcpy(dest, src, length + 1);
  return true;
}

bool parsePort(const char* text, uint16_t* port) {
  text = orEmpty(text);
  if (*text == '\0') return false;
  uint32_t value = 0;
  for (; *text != '\0'; ++text) {
    if (!isdigit(static_cast<unsigned char>(*text))) return false;
    value = value * 10 + static_cast<uint32_t>(*text - '0');
    if (value > 65535) return false;
  }
  if (value == 0) return false;
  *port = static_cast<uint16_t>(value);
  return true;
}

bool validHost(const char* host) {
  host = orEmpty(host);
  const size_t length = strlen(host);
  if (length == 0 || length > kMaxHostLength) return false;
  if (host[0] == '.' || host[0] == '-' || host[length - 1] == '.' || host[length - 1] == '-') return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(host[i]);
    if (!isalnum(c) && c != '.' && c != '-') return false;
    if (c == '.' && host[i + 1] == '.') return false;
  }
  return true;
}

bool validateNetConfig(const NetConfig& config, const char** error) {
  const char* message = nullptr;
  const size_t ssidLength = strnlen(config.wifiSsid, sizeof(config.wifiSsid));
  const size_t passwordLength = strnlen(config.wifiPassword, sizeof(config.wifiPassword));
  if (ssidLength == 0 || ssidLength > kMaxSsidLength) {
    message = "Wi-Fi network: 1-32 characters";
  } else if (passwordLength > kMaxWifiPasswordLength ||
             (passwordLength > 0 && passwordLength < kMinWifiPasswordLength)) {
    message = "Wi-Fi password: 8-63 characters, or empty for an open network";
  } else if (!validHost(config.mqttHost)) {
    message = "MQTT host: an IP address or host name (letters, digits, '.', '-')";
  } else if (config.mqttPort == 0) {
    message = "MQTT port: 1-65535";
  } else if (strnlen(config.mqttUser, sizeof(config.mqttUser)) > kMaxMqttUserLength) {
    message = "MQTT user: at most 32 characters";
  } else if (strnlen(config.mqttPassword, sizeof(config.mqttPassword)) > kMaxMqttPasswordLength) {
    message = "MQTT password: at most 64 characters";
  } else if (config.mqttUser[0] == '\0' && config.mqttPassword[0] != '\0') {
    message = "MQTT password needs a user";
  } else if (!validJmriChannel(config.jmriChannel)) {
    message = "JMRI channel: empty, or ending in '/' (e.g. /trains/), no + or #";
  } else if (!validNodeName(config.nodeName)) {
    message = "Node name: 1-32 letters, digits, '_' or '-'";
  }
  if (error != nullptr) *error = message;
  return message == nullptr;
}

bool applyNetForm(const NetConfig& saved, const NetForm& form, NetConfig* out, const char** error) {
  NetConfig config;
  clearNetConfig(&config);

  // Host without surrounding spaces (phone keyboards add them).
  char host[kMaxHostLength + 1];
  const char* hostText = orEmpty(form.mqttHost);
  while (isspace(static_cast<unsigned char>(*hostText))) ++hostText;
  size_t hostLength = strlen(hostText);
  while (hostLength > 0 && isspace(static_cast<unsigned char>(hostText[hostLength - 1]))) --hostLength;
  const bool hostFits = hostLength <= kMaxHostLength;
  if (hostFits) {
    memcpy(host, hostText, hostLength);
    host[hostLength] = '\0';
  }

  const char* message = nullptr;
  if (!copyField(config.wifiSsid, sizeof(config.wifiSsid), form.wifiSsid)) {
    message = "Wi-Fi network: 1-32 characters";
  } else if (!fieldFits(orEmpty(form.wifiPassword), kMaxWifiPasswordLength)) {
    message = "Wi-Fi password: 8-63 characters, or empty for an open network";
  } else if (!hostFits) {
    message = "MQTT host: at most 64 characters";
  } else if (!parsePort(form.mqttPort, &config.mqttPort)) {
    message = "MQTT port: 1-65535";
  } else if (!copyField(config.mqttUser, sizeof(config.mqttUser), form.mqttUser)) {
    message = "MQTT user: at most 32 characters";
  } else if (!fieldFits(orEmpty(form.mqttPassword), kMaxMqttPasswordLength)) {
    message = "MQTT password: at most 64 characters";
  } else if (!copyField(config.jmriChannel, sizeof(config.jmriChannel), form.jmriChannel)) {
    message = "JMRI channel: at most 32 characters";
  } else if (!copyField(config.nodeName, sizeof(config.nodeName), form.nodeName)) {
    message = "Node name: 1-32 letters, digits, '_' or '-'";
  }
  if (message != nullptr) {
    if (error != nullptr) *error = message;
    return false;
  }
  copyField(config.mqttHost, sizeof(config.mqttHost), host);

  const char* wifiPassword = orEmpty(form.wifiPassword);
  if (wifiPassword[0] == '\0' && strcmp(config.wifiSsid, saved.wifiSsid) == 0) {
    wifiPassword = saved.wifiPassword;
  }
  copyField(config.wifiPassword, sizeof(config.wifiPassword), wifiPassword);

  const char* mqttPassword = orEmpty(form.mqttPassword);
  if (config.mqttUser[0] == '\0') {
    mqttPassword = "";
  } else if (mqttPassword[0] == '\0' && strcmp(config.mqttUser, saved.mqttUser) == 0) {
    mqttPassword = saved.mqttPassword;
  }
  copyField(config.mqttPassword, sizeof(config.mqttPassword), mqttPassword);

  if (!validateNetConfig(config, error)) return false;
  *out = config;
  return true;
}

void defaultTurnoutNames(TurnoutNames* names) {
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    snprintf(names->name[i], sizeof(names->name[i]), "%u", 101 + i);
  }
}

bool parseTurnoutNames(const char* const* fields, TurnoutNames* out, const char** error) {
  TurnoutNames names;
  const char* pointers[kChannelCount];
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    const char* text = orEmpty(fields[i]);
    while (isspace(static_cast<unsigned char>(*text))) ++text;
    size_t length = strlen(text);
    while (length > 0 && isspace(static_cast<unsigned char>(text[length - 1]))) --length;
    if (length > kMaxTurnoutNameLength) {
      if (error != nullptr) *error = "turnout names must be 1-16 letters, digits, '_' or '-'";
      return false;
    }
    memcpy(names.name[i], text, length);
    names.name[i][length] = '\0';
    pointers[i] = names.name[i];
  }
  if (!validateTurnoutNames(pointers, kChannelCount, error)) return false;
  *out = names;
  return true;
}

bool parseBehaviourForm(const char* startup, const char* offline, const char* staggerMs, const char* intervalMs,
                        const char* feedback, Behaviour* out, const char** error) {
  Behaviour behaviour = defaultBehaviour();
  uint16_t stagger = 0;
  uint16_t interval = 0;
  const char* message = nullptr;
  startup = orEmpty(startup);
  offline = orEmpty(offline);
  if (strcmp(startup, "restore") == 0) {
    behaviour.startup = StartupLevel::Restore;
  } else if (strcmp(startup, "low") == 0) {
    behaviour.startup = StartupLevel::Low;
  } else {
    message = "startup: restore or low";
  }
  if (message == nullptr) {
    if (strcmp(offline, "hold") == 0) {
      behaviour.offline = OfflinePolicy::Hold;
    } else if (strcmp(offline, "low") == 0) {
      behaviour.offline = OfflinePolicy::Low;
    } else {
      message = "JMRI offline: hold or low";
    }
  }
  // parsePort accepts 1-65535; 0 is allowed here, so check it first.
  if (message == nullptr && strcmp(orEmpty(staggerMs), "0") != 0 &&
      (!parsePort(staggerMs, &stagger) || stagger > kMaxStaggerMs)) {
    message = "stagger: 0-5000 ms";
  }
  if (message == nullptr && strcmp(orEmpty(intervalMs), "0") != 0 &&
      (!parsePort(intervalMs, &interval) || interval > kMaxMinIntervalMs)) {
    message = "minimum interval: 0-10000 ms";
  }
  if (message != nullptr) {
    if (error != nullptr) *error = message;
    return false;
  }
  behaviour.staggerMs = stagger;
  behaviour.minIntervalMs = interval;
  behaviour.feedback = strcmp(orEmpty(feedback), "on") == 0;
  *out = behaviour;
  return true;
}

bool validAdminPassword(const char* password) {
  password = orEmpty(password);
  const size_t length = strlen(password);
  if (length < kMinAdminPasswordLength || length > kMaxAdminPasswordLength) return false;
  for (size_t i = 0; i < length; ++i) {
    if (!isgraph(static_cast<unsigned char>(password[i]))) return false;
  }
  return true;
}

bool htmlEscape(const char* in, char* out, size_t size) {
  in = orEmpty(in);
  if (size == 0) return false;
  size_t used = 0;
  for (; *in != '\0'; ++in) {
    const char* replacement = nullptr;
    switch (*in) {
      case '&': replacement = "&amp;"; break;
      case '<': replacement = "&lt;"; break;
      case '>': replacement = "&gt;"; break;
      case '"': replacement = "&quot;"; break;
      case '\'': replacement = "&#39;"; break;
      default: break;
    }
    const size_t length = replacement != nullptr ? strlen(replacement) : 1;
    if (used + length >= size) {
      out[0] = '\0';
      return false;
    }
    if (replacement != nullptr) {
      memcpy(out + used, replacement, length);
    } else {
      out[used] = *in;
    }
    used += length;
  }
  out[used] = '\0';
  return true;
}

}  // namespace tc
