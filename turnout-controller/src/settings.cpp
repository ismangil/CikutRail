#include "settings.h"

#include <Preferences.h>
#include <esp_random.h>

#if __has_include("local_settings.h")
#include "local_settings.h"
#else
#include "local_settings.example.h"
#endif

namespace settings {
namespace {

const char kNetNamespace[] = "net";
const char kPortalNamespace[] = "portal";
const char kBehaviourNamespace[] = "behave";
const char kLevelsNamespace[] = "turnouts";
const char kNamesNamespace[] = "names";
const char kChannelsNamespace[] = "channels";
const uint8_t kApPasswordLength = 10;
// No 0/O, 1/l/i: easy to read off a console and type on a phone.
const char kApPasswordAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";

char g_apPassword[kApPasswordLength + 1];
char g_adminPassword[tc::kMaxAdminPasswordLength + 1];
char g_names[tc::kChannelCount][tc::kMaxTurnoutNameLength + 1];
const char* g_namePointers[tc::kChannelCount];
bool g_namesLoaded = false;

void randomPassword(char* out) {
  for (uint8_t i = 0; i < kApPasswordLength; ++i) {
    out[i] = kApPasswordAlphabet[esp_random() % (sizeof(kApPasswordAlphabet) - 1)];
  }
  out[kApPasswordLength] = '\0';
}

void nameKey(uint8_t index, char* key, size_t size) { snprintf(key, size, "n%u", index + 1); }

void loadNames() {
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    tc::copyField(g_names[i], sizeof(g_names[i]), local_settings::kTurnoutNames[i]);
    g_namePointers[i] = g_names[i];
  }
  Preferences prefs;
  if (prefs.begin(kNamesNamespace, true)) {
    if (prefs.isKey("saved")) {
      char key[8];
      for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
        nameKey(i, key, sizeof(key));
        g_names[i][0] = '\0';
        if (prefs.isKey(key)) prefs.getString(key, g_names[i], sizeof(g_names[i]));
      }
    }
    prefs.end();
  }
  // Something unusable in flash: fall back to the defaults.
  if (!tc::validateTurnoutNames(g_namePointers, tc::kChannelCount, nullptr)) {
    for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
      tc::copyField(g_names[i], sizeof(g_names[i]), local_settings::kTurnoutNames[i]);
    }
  }
  g_namesLoaded = true;
}

void compiledDefaults(tc::NetConfig* config) {
  tc::clearNetConfig(config);
  tc::copyField(config->wifiSsid, sizeof(config->wifiSsid), local_settings::kWifiSsid);
  tc::copyField(config->wifiPassword, sizeof(config->wifiPassword), local_settings::kWifiPassword);
  tc::copyField(config->mqttHost, sizeof(config->mqttHost), local_settings::kMqttHost);
  config->mqttPort = local_settings::kMqttPort;
  tc::copyField(config->mqttUser, sizeof(config->mqttUser), local_settings::kMqttUser);
  tc::copyField(config->mqttPassword, sizeof(config->mqttPassword), local_settings::kMqttPassword);
  tc::copyField(config->jmriChannel, sizeof(config->jmriChannel), local_settings::kJmriChannel);
  tc::copyField(config->nodeName, sizeof(config->nodeName), local_settings::kNodeName);
}

void readString(Preferences& prefs, const char* key, char* out, size_t size) {
  out[0] = '\0';
  if (prefs.isKey(key)) prefs.getString(key, out, size);
  out[size - 1] = '\0';
}

}  // namespace

Source loadNet(tc::NetConfig* config) {
  compiledDefaults(config);

  Preferences prefs;
  if (prefs.begin(kNetNamespace, true)) {
    const bool saved = prefs.isKey("ssid");
    if (saved) {
      tc::NetConfig stored;
      tc::clearNetConfig(&stored);
      readString(prefs, "ssid", stored.wifiSsid, sizeof(stored.wifiSsid));
      readString(prefs, "wpass", stored.wifiPassword, sizeof(stored.wifiPassword));
      readString(prefs, "mhost", stored.mqttHost, sizeof(stored.mqttHost));
      stored.mqttPort = prefs.getUShort("mport", tc::kDefaultMqttPort);
      readString(prefs, "muser", stored.mqttUser, sizeof(stored.mqttUser));
      readString(prefs, "mpass", stored.mqttPassword, sizeof(stored.mqttPassword));
      readString(prefs, "chan", stored.jmriChannel, sizeof(stored.jmriChannel));
      readString(prefs, "node", stored.nodeName, sizeof(stored.nodeName));
      prefs.end();
      *config = stored;
      // Something unusable in flash: show it on the setup page to fix.
      return tc::validateNetConfig(stored, nullptr) ? Source::Saved : Source::None;
    }
    prefs.end();
  }
  return tc::validateNetConfig(*config, nullptr) ? Source::CompiledIn : Source::None;
}

bool saveNet(const tc::NetConfig& config) {
  Preferences prefs;
  if (!prefs.begin(kNetNamespace, false)) return false;
  // loadNet treats the presence of "ssid" as "settings saved". Remove it
  // first and write it last, so a failed or interrupted save leaves no
  // half-updated record that looks committed (the node then falls back to
  // the compiled-in settings or the setup page).
  if (prefs.isKey("ssid") && !prefs.remove("ssid")) {
    prefs.end();
    return false;
  }
  const bool ok = prefs.putString("wpass", config.wifiPassword) == strlen(config.wifiPassword) &&
                  prefs.putString("mhost", config.mqttHost) > 0 && prefs.putUShort("mport", config.mqttPort) > 0 &&
                  prefs.putString("muser", config.mqttUser) == strlen(config.mqttUser) &&
                  prefs.putString("mpass", config.mqttPassword) == strlen(config.mqttPassword) &&
                  prefs.putString("chan", config.jmriChannel) == strlen(config.jmriChannel) &&
                  prefs.putString("node", config.nodeName) > 0 && prefs.putString("ssid", config.wifiSsid) > 0;
  prefs.end();
  return ok;
}

bool forgetNet() {
  Preferences prefs;
  if (!prefs.begin(kNetNamespace, false)) return false;
  const bool ok = prefs.clear();
  prefs.end();
  return ok;
}

const char* apPassword() {
  if (g_apPassword[0] != '\0') return g_apPassword;
  Preferences prefs;
  const bool open = prefs.begin(kPortalNamespace, false);
  if (open && prefs.isKey("appass")) prefs.getString("appass", g_apPassword, sizeof(g_apPassword));
  if (strlen(g_apPassword) != kApPasswordLength) {
    // Never empty: an empty password would start an open access point.
    randomPassword(g_apPassword);
    if (open) prefs.putString("appass", g_apPassword);
  }
  if (open) prefs.end();
  return g_apPassword;
}

tc::Behaviour loadBehaviour() {
  tc::Behaviour behaviour = tc::defaultBehaviour();
  Preferences prefs;
  if (!prefs.begin(kBehaviourNamespace, true)) return behaviour;
  if (prefs.getUChar("startup", 0) == static_cast<uint8_t>(tc::StartupLevel::Low)) {
    behaviour.startup = tc::StartupLevel::Low;
  }
  if (prefs.getUChar("offline", 0) == static_cast<uint8_t>(tc::OfflinePolicy::Low)) {
    behaviour.offline = tc::OfflinePolicy::Low;
  }
  behaviour.feedback = prefs.getBool("feedback", false);
  const uint16_t stagger = prefs.getUShort("stagger", 0);
  const uint16_t interval = prefs.getUShort("interval", 0);
  prefs.end();
  behaviour.staggerMs = stagger <= tc::kMaxStaggerMs ? stagger : 0;
  behaviour.minIntervalMs = interval <= tc::kMaxMinIntervalMs ? interval : 0;
  return behaviour;
}

bool saveBehaviour(const tc::Behaviour& behaviour) {
  Preferences prefs;
  if (!prefs.begin(kBehaviourNamespace, false)) return false;
  const bool ok = prefs.putUChar("startup", static_cast<uint8_t>(behaviour.startup)) > 0 &&
                  prefs.putUChar("offline", static_cast<uint8_t>(behaviour.offline)) > 0 &&
                  prefs.putUShort("stagger", behaviour.staggerMs) > 0 &&
                  prefs.putUShort("interval", behaviour.minIntervalMs) > 0 &&
                  prefs.putBool("feedback", behaviour.feedback) > 0;
  prefs.end();
  return ok;
}

tc::ChannelConfig loadChannelConfig() {
  tc::ChannelConfig channels = tc::defaultChannelConfig();
  Preferences prefs;
  if (!prefs.begin(kChannelsNamespace, true)) return channels;
  channels.sensorMask = prefs.getUShort("sensors", 0);
  channels.pullUpMask = prefs.getUShort("pullup", 0);
  channels.pullDownMask = prefs.getUShort("pulldown", 0);
  channels.activeLowMask = prefs.getUShort("actlow", 0);
  prefs.end();
  return tc::normalised(channels);
}

bool saveChannelConfig(const tc::ChannelConfig& channels) {
  const tc::ChannelConfig c = tc::normalised(channels);
  Preferences prefs;
  if (!prefs.begin(kChannelsNamespace, false)) return false;
  const bool ok = prefs.putUShort("sensors", c.sensorMask) > 0 && prefs.putUShort("pullup", c.pullUpMask) > 0 &&
                  prefs.putUShort("pulldown", c.pullDownMask) > 0 && prefs.putUShort("actlow", c.activeLowMask) > 0;
  prefs.end();
  return ok;
}

bool loadLevels(uint16_t* levels) {
  Preferences prefs;
  if (!prefs.begin(kLevelsNamespace, true)) return false;
  const bool saved = prefs.isKey("levels");
  if (saved) *levels = static_cast<uint16_t>(prefs.getUShort("levels", 0) & tc::kAllChannelsMask);
  prefs.end();
  return saved;
}

bool saveLevels(uint16_t levels) {
  Preferences prefs;
  if (!prefs.begin(kLevelsNamespace, false)) return false;
  const bool ok = prefs.putUShort("levels", levels) > 0;
  prefs.end();
  return ok;
}

const char* adminPassword() {
  if (g_adminPassword[0] != '\0') return g_adminPassword;
  Preferences prefs;
  const bool open = prefs.begin(kPortalNamespace, false);
  if (open && prefs.isKey("admin")) prefs.getString("admin", g_adminPassword, sizeof(g_adminPassword));
  if (!tc::validAdminPassword(g_adminPassword)) {
    // Never empty: that would leave the config page open to anyone.
    randomPassword(g_adminPassword);
    if (open) prefs.putString("admin", g_adminPassword);
  }
  if (open) prefs.end();
  return g_adminPassword;
}

bool setAdminPassword(const char* password) {
  if (!tc::validAdminPassword(password)) return false;
  Preferences prefs;
  if (!prefs.begin(kPortalNamespace, false)) return false;
  const bool ok = prefs.putString("admin", password) == strlen(password);
  prefs.end();
  if (ok) tc::copyField(g_adminPassword, sizeof(g_adminPassword), password);
  return ok;
}

const char* const* turnoutNames() {
  if (!g_namesLoaded) loadNames();
  return g_namePointers;
}

bool saveTurnoutNames(const tc::TurnoutNames& names) {
  Preferences prefs;
  if (!prefs.begin(kNamesNamespace, false)) return false;
  bool ok = true;
  char key[8];
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    nameKey(i, key, sizeof(key));
    ok = ok && prefs.putString(key, names.name[i]) == strlen(names.name[i]);
  }
  ok = ok && prefs.putBool("saved", true) > 0;
  prefs.end();
  if (!g_namesLoaded) loadNames();
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    tc::copyField(g_names[i], sizeof(g_names[i]), names.name[i]);
  }
  return ok;
}

bool factoryReset() {
  bool ok = true;
  const char* const namespaces[] = {kNetNamespace, kBehaviourNamespace, kLevelsNamespace, kNamesNamespace};
  for (const char* name : namespaces) {
    Preferences prefs;
    if (!prefs.begin(name, false)) {
      ok = false;
      continue;
    }
    ok = prefs.clear() && ok;
    prefs.end();
  }
  Preferences portalPrefs;
  if (portalPrefs.begin(kPortalNamespace, false)) {
    if (portalPrefs.isKey("admin")) ok = portalPrefs.remove("admin") && ok;
    portalPrefs.end();
  }
  g_adminPassword[0] = '\0';
  g_namesLoaded = false;
  return ok;
}

}  // namespace settings
