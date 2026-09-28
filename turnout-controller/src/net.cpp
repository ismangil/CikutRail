#include "net.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mqtt_client.h>

#include <atomic>

#include "channels.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "web.h"

namespace net {
namespace {

const uint32_t kInfoIntervalMs = 60000;
const uint32_t kJoinTimeoutMs = 180000;    // then the setup portal opens
const uint32_t kPortalRetryMs = 120000;    // Wi-Fi retries while the portal is open
const int kKeepaliveS = 15;  // the broker publishes the last will ~1.5x this after the node goes silent
const int kReconnectMs = 5000;
const UBaseType_t kQueueLength = 32;
const int kCommandQos = 2;  // as JMRI publishes
const char kOnline[] = "online";
const char kOffline[] = "offline";

const char* g_firmware = "";
tc::NetConfig g_config;
settings::Source g_source = settings::Source::None;
bool g_haveConfig = false;
char g_statusTopic[tc::kMaxTopicLength + 1];
char g_infoTopic[tc::kMaxTopicLength + 1];
char g_jmriStateTopic[tc::kMaxTopicLength + 1];
esp_mqtt_client_handle_t g_client = nullptr;
QueueHandle_t g_queue = nullptr;
bool g_mqttStarted = false;
bool g_lastWifiUp = false;
bool g_lastMqttUp = false;
uint32_t g_lastInfoMs = 0;
uint32_t g_wifiDownSinceMs = 0;
bool g_portalRetrying = false;  // portal open after a Wi-Fi failure
uint32_t g_lastPortalRetryMs = 0;
char g_mdnsName[tc::kMaxNodeNameLength + 1];  // name mDNS announces, "" = not started

// Written by the esp-mqtt task, read by the main loop.
std::atomic<bool> g_mqttUp(false);
std::atomic<bool> g_infoDue(false);
bool g_rereadDue = false;  // main loop only
std::atomic<uint32_t> g_connects(0);
std::atomic<uint32_t> g_received(0);
std::atomic<uint32_t> g_dropped(0);
std::atomic<uint32_t> g_fragmented(0);

// Written by the main loop only.
uint32_t g_applied = 0;
uint32_t g_unchanged = 0;
uint32_t g_ignored = 0;

const char* const* names() { return settings::turnoutNames(); }

uint8_t subscribedCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    if (names()[i][0] != '\0') ++count;
  }
  return count;
}

const char* sourceName(settings::Source source) {
  switch (source) {
    case settings::Source::Saved: return "saved";
    case settings::Source::CompiledIn: return "compiled in (local_settings.h)";
    case settings::Source::None: return "none";
  }
  return "";
}

void subscribeTurnouts(esp_mqtt_client_handle_t client) {
  char topic[tc::kMaxTopicLength + 1];
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    if (names()[i][0] == '\0') continue;
    if (tc::turnoutTopic(g_config.jmriChannel, names()[i], topic, sizeof(topic))) {
      esp_mqtt_client_subscribe(client, topic, kCommandQos);
    }
  }
}

void subscribeAll(esp_mqtt_client_handle_t client) {
  esp_mqtt_client_subscribe(client, g_jmriStateTopic, 1);
  subscribeTurnouts(client);
}

void onMqttEvent(void*, esp_event_base_t, int32_t id, void* data) {
  const esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(data);
  switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED:
      ++g_connects;
      esp_mqtt_client_publish(event->client, g_statusTopic, kOnline, 0, 1, 1);
      // Clean session, so the broker sends every retained command again.
      subscribeAll(event->client);
      g_mqttUp = true;
      g_infoDue = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      g_mqttUp = false;
      break;
    case MQTT_EVENT_DATA: {
      // Turnout payloads are a few bytes; a split message isn't one of ours.
      if (event->current_data_offset != 0 || event->data_len != event->total_data_len) {
        ++g_fragmented;
        break;
      }
      Message message = {};
      message.retained = event->retain;
      if (tc::isJmriStateTopic(g_config.jmriChannel, event->topic, static_cast<size_t>(event->topic_len))) {
        message.kind = Message::Kind::JmriState;
        message.jmriState = tc::parseJmriState(event->data, static_cast<size_t>(event->data_len));
        const size_t length = event->data_len < static_cast<int>(sizeof(message.text)) - 1
                                  ? static_cast<size_t>(event->data_len)
                                  : sizeof(message.text) - 1;
        memcpy(message.text, event->data, length);
        message.text[length] = '\0';
      } else {
        const uint8_t channel = tc::matchTurnoutTopic(g_config.jmriChannel, names(), tc::kChannelCount,
                                                      event->topic, static_cast<size_t>(event->topic_len));
        if (channel == 0) break;
        ++g_received;
        message.kind = Message::Kind::Turnout;
        message.channel = channel;
        message.payload = tc::parseTurnoutPayload(event->data, static_cast<size_t>(event->data_len));
      }
      if (xQueueSend(g_queue, &message, 0) != pdTRUE) ++g_dropped;
      break;
    }
    default:
      break;
  }
}

void publishInfo() {
  char info[192];
  power::Readings readings;
  const bool haveReadings = power::read(&readings);
  snprintf(info, sizeof(info), "{\"firmware\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"uptime_s\":%lu,\"vin_mv\":%s}",
           g_firmware, WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()),
           static_cast<unsigned long>(millis() / 1000), haveReadings ? String(readings.inputMv).c_str() : "null");
  esp_mqtt_client_publish(g_client, g_infoTopic, info, 0, 1, 1);
  g_lastInfoMs = millis();
}

// Leaves the broker cleanly: status offline first, as the last will would.
void stopMqtt() {
  if (g_client == nullptr) return;
  if (g_mqttUp) esp_mqtt_client_publish(g_client, g_statusTopic, kOffline, 0, 1, 1);
  esp_mqtt_client_destroy(g_client);
  g_client = nullptr;
  g_mqttStarted = false;
  g_mqttUp = false;
}

// The names are checked here as well as the network settings: they share
// the topic length limit with the channel.
const char* checkTopics() {
  const char* error = nullptr;
  if (!tc::validateTurnoutNames(names(), tc::kChannelCount, &error)) return error;
  if (!tc::jmriStateTopic(g_config.jmriChannel, g_jmriStateTopic, sizeof(g_jmriStateTopic))) {
    return "JMRI channel too long";
  }
  char topic[tc::kMaxTopicLength + 1];
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    if (names()[i][0] == '\0') continue;
    if (!tc::turnoutTopic(g_config.jmriChannel, names()[i], topic, sizeof(topic))) {
      return "JMRI channel plus turnout name too long";
    }
  }
  return nullptr;
}

// Creates the MQTT client for g_config; loop() starts it once Wi-Fi is up.
bool startMqtt() {
  const char* error = checkTopics();
  if (error != nullptr) {
    Serial.printf("net: off: %s\n", error);
    g_haveConfig = false;
    return false;
  }
  snprintf(g_statusTopic, sizeof(g_statusTopic), "cikutrail/%s/status", g_config.nodeName);
  snprintf(g_infoTopic, sizeof(g_infoTopic), "cikutrail/%s/info", g_config.nodeName);

  esp_mqtt_client_config_t mqtt = {};
  mqtt.host = g_config.mqttHost;
  mqtt.port = g_config.mqttPort;
  mqtt.transport = MQTT_TRANSPORT_OVER_TCP;
  mqtt.client_id = g_config.nodeName;
  mqtt.username = g_config.mqttUser[0] != '\0' ? g_config.mqttUser : nullptr;
  mqtt.password = g_config.mqttPassword[0] != '\0' ? g_config.mqttPassword : nullptr;
  mqtt.lwt_topic = g_statusTopic;
  mqtt.lwt_msg = kOffline;
  mqtt.lwt_qos = 1;
  mqtt.lwt_retain = 1;
  mqtt.keepalive = kKeepaliveS;
  mqtt.reconnect_timeout_ms = kReconnectMs;
  g_client = esp_mqtt_client_init(&mqtt);
  if (g_client == nullptr) {
    Serial.println("net: off: out of memory starting MQTT");
    g_haveConfig = false;
    return false;
  }
  esp_mqtt_client_register_event(g_client, MQTT_EVENT_ANY, onMqttEvent, nullptr);
  g_haveConfig = true;
  return true;
}

// http://<node>.local for the config page. Restarted when the name changes.
void startMdns() {
  if (strcmp(g_mdnsName, g_config.nodeName) == 0) return;
  if (g_mdnsName[0] != '\0') MDNS.end();
  g_mdnsName[0] = '\0';
  if (MDNS.begin(g_config.nodeName)) {
    MDNS.addService("http", "tcp", 80);
    tc::copyField(g_mdnsName, sizeof(g_mdnsName), g_config.nodeName);
    Serial.printf("net: config page at http://%s.local (user admin)\n", g_mdnsName);
  } else {
    Serial.println("net: mDNS didn't start; use the IP address");
  }
}

void joinWifi() {
  WiFi.setHostname(g_config.nodeName);
  WiFi.mode(portal::isOpen() ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds latency to every command
  WiFi.setAutoReconnect(!g_portalRetrying);
  WiFi.begin(g_config.wifiSsid, g_config.wifiPassword);
  g_wifiDownSinceMs = millis();
  Serial.printf("net: joining Wi-Fi \"%s\" (settings %s)\n", g_config.wifiSsid, sourceName(g_source));
}

void startNetwork() {
  if (startMqtt()) joinWifi();
}

}  // namespace

void begin(const char* firmwareVersion) {
  g_firmware = firmwareVersion;
  g_queue = xQueueCreate(kQueueLength, sizeof(Message));
  g_source = settings::loadNet(&g_config);
  if (g_source == settings::Source::None) {
    Serial.println("net: no network settings");
    portal::open(portal::Reason::FirstSetup);
    return;
  }
  startNetwork();
}

void applyConfig(const tc::NetConfig& config) {
  const bool sameWifi = g_haveConfig && strcmp(config.wifiSsid, g_config.wifiSsid) == 0 &&
                        strcmp(config.wifiPassword, g_config.wifiPassword) == 0;
  stopMqtt();
  g_config = config;
  g_source = settings::Source::Saved;
  if (sameWifi) {
    // MQTT, channel or node name only: stay on the network.
    Serial.println("net: MQTT settings changed, reconnecting");
    if (startMqtt() && WiFi.status() == WL_CONNECTED) startMdns();
    return;
  }
  WiFi.disconnect(false);
  g_portalRetrying = false;  // a fresh attempt with new settings
  g_lastWifiUp = false;
  startNetwork();
}

void applyTurnoutNames(const tc::TurnoutNames& names) {
  // The MQTT task reads the names, so they change only while it is stopped.
  stopMqtt();
  const bool saved = settings::saveTurnoutNames(names);
  Serial.printf("net: turnout names %s, resubscribing\n", saved ? "saved" : "changed (flash write failed)");
  if (g_haveConfig) startMqtt();
}

void forget() {
  stopMqtt();
  WiFi.disconnect(false);
  g_haveConfig = false;
  g_lastWifiUp = false;
  settings::forgetNet();
  settings::loadNet(&g_config);  // compiled-in defaults, to prefill the page
  g_source = settings::Source::None;
  Serial.println("net: saved settings erased");
  portal::open(portal::Reason::FirstSetup);
}

const tc::NetConfig& config() { return g_config; }

bool mqttUp() { return g_mqttUp; }

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

bool isOff() { return !g_haveConfig; }

void onPortalOpened(bool wifiFailed) {
  if (!wifiFailed) return;
  // Stop the radio hopping channels in the background; loop() retries
  // when nobody is on the setup page.
  g_portalRetrying = true;
  g_lastPortalRetryMs = millis();
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
}

void onPortalClosed() {
  g_portalRetrying = false;
  if (!g_haveConfig) return;
  WiFi.setAutoReconnect(true);
  if (WiFi.status() != WL_CONNECTED) WiFi.begin(g_config.wifiSsid, g_config.wifiPassword);
}

void loop() {
  if (!g_haveConfig) return;

  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp != g_lastWifiUp) {
    g_lastWifiUp = wifiUp;
    if (wifiUp) {
      Serial.printf("net: Wi-Fi up, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(),
                    static_cast<int>(WiFi.RSSI()));
      web::begin();
      startMdns();
      if (g_portalRetrying) {
        g_portalRetrying = false;
        WiFi.setAutoReconnect(true);
      }
    } else {
      Serial.println("net: Wi-Fi down");
      g_wifiDownSinceMs = millis();
    }
  }
  if (wifiUp && !g_mqttStarted && g_client != nullptr) {
    // esp-mqtt reconnects by itself from here on, through Wi-Fi drops too.
    esp_mqtt_client_start(g_client);
    g_mqttStarted = true;
    Serial.printf("net: connecting to MQTT %s:%u\n", g_config.mqttHost, g_config.mqttPort);
  }

  if (!wifiUp) {
    if (!portal::isOpen() && millis() - g_wifiDownSinceMs >= kJoinTimeoutMs) {
      Serial.printf("net: Wi-Fi \"%s\" not joined for %lu s\n", g_config.wifiSsid,
                    static_cast<unsigned long>(kJoinTimeoutMs / 1000));
      portal::open(portal::Reason::WifiFailed);
    } else if (g_portalRetrying && millis() - g_lastPortalRetryMs >= kPortalRetryMs) {
      g_lastPortalRetryMs = millis();
      if (WiFi.softAPgetStationNum() == 0) WiFi.begin(g_config.wifiSsid, g_config.wifiPassword);
    }
  }

  const bool mqttUp = g_mqttUp;
  if (mqttUp != g_lastMqttUp) {
    g_lastMqttUp = mqttUp;
    if (mqttUp) {
      Serial.printf("net: MQTT up, subscribed to %u turnouts\n", subscribedCount());
    } else {
      Serial.println("net: MQTT down");
    }
  }
  if (mqttUp && (g_infoDue.exchange(false) || millis() - g_lastInfoMs >= kInfoIntervalMs)) publishInfo();
  if (mqttUp && g_rereadDue) {
    g_rereadDue = false;
    subscribeTurnouts(g_client);
  }
}

void rereadRetained() { g_rereadDue = true; }

bool nextMessage(Message* message) {
  return g_queue != nullptr && xQueueReceive(g_queue, message, 0) == pdTRUE;
}

const char* turnoutName(uint8_t channel) { return names()[channel - 1]; }

void noteOutcome(Outcome outcome) {
  switch (outcome) {
    case Outcome::Applied: ++g_applied; break;
    case Outcome::Unchanged: ++g_unchanged; break;
    case Outcome::Ignored: ++g_ignored; break;
  }
}

void printStatus() {
  Serial.printf("settings: %s\n", sourceName(g_source));
  if (!g_haveConfig) {
    Serial.println("net: off (no usable settings; see portal)");
    portal::printStatus();
    return;
  }
  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  Serial.printf("Wi-Fi: %s \"%s\"", wifiUp ? "up" : "down", g_config.wifiSsid);
  if (wifiUp) {
    Serial.printf(", IP %s, RSSI %d dBm", WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()));
  }
  Serial.println();
  Serial.printf("MQTT: %s, %s:%u as \"%s\"%s, %lu connects\n", g_mqttUp ? "up" : "down", g_config.mqttHost,
                g_config.mqttPort, g_config.nodeName, g_config.mqttUser[0] != '\0' ? " with login" : "",
                static_cast<unsigned long>(g_connects));
  Serial.printf("status topic: %s, JMRI state topic: %s\n", g_statusTopic, g_jmriStateTopic);
  if (wifiUp) {
    Serial.printf("config page: http://%s.local or http://%s, user admin, password %s\n",
                  g_mdnsName[0] != '\0' ? g_mdnsName : g_config.nodeName, WiFi.localIP().toString().c_str(),
                  settings::adminPassword());
  }
  Serial.printf("JMRI channel: \"%s\"\n", g_config.jmriChannel);
  char topic[tc::kMaxTopicLength + 1];
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    if (names()[i][0] == '\0') {
      Serial.printf("%2u  (unused)\n", i + 1);
    } else if (tc::turnoutTopic(g_config.jmriChannel, names()[i], topic, sizeof(topic))) {
      Serial.printf("%2u  %s\n", i + 1, topic);
    }
  }
  Serial.printf("messages: %lu received, %lu applied, %lu unchanged, %lu ignored, %lu dropped, %lu split\n",
                static_cast<unsigned long>(g_received), static_cast<unsigned long>(g_applied),
                static_cast<unsigned long>(g_unchanged), static_cast<unsigned long>(g_ignored),
                static_cast<unsigned long>(g_dropped), static_cast<unsigned long>(g_fragmented));
  portal::printStatus();
}

}  // namespace net
