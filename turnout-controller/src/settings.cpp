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
const uint8_t kApPasswordLength = 10;
// No 0/O, 1/l/i: easy to read off a console and type on a phone.
const char kApPasswordAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";

char g_apPassword[kApPasswordLength + 1];

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
  // ssid last: loadNet treats its presence as "settings saved".
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
    for (uint8_t i = 0; i < kApPasswordLength; ++i) {
      g_apPassword[i] = kApPasswordAlphabet[esp_random() % (sizeof(kApPasswordAlphabet) - 1)];
    }
    g_apPassword[kApPasswordLength] = '\0';
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
                  prefs.putUShort("interval", behaviour.minIntervalMs) > 0;
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

const char* const* turnoutNames() { return local_settings::kTurnoutNames; }

}  // namespace settings
