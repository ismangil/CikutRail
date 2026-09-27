#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mqtt_client.h>

#include <atomic>

#include "channels.h"
#include "power.h"

#if __has_include("local_settings.h")
#include "local_settings.h"
#define NET_HAVE_LOCAL_SETTINGS 1
#else
#include "local_settings.example.h"
#define NET_HAVE_LOCAL_SETTINGS 0
#endif

namespace net {
namespace {

using namespace local_settings;

const uint32_t kInfoIntervalMs = 60000;
const int kKeepaliveS = 15;  // the broker publishes the last will ~1.5x this after the node goes silent
const int kReconnectMs = 5000;
const UBaseType_t kQueueLength = 32;
const int kCommandQos = 2;  // as JMRI publishes
const char kOnline[] = "online";
const char kOffline[] = "offline";

bool g_enabled = false;
const char* g_disabledReason = nullptr;
const char* g_firmware = "";
char g_statusTopic[tc::kMaxTopicLength + 1];
char g_infoTopic[tc::kMaxTopicLength + 1];
esp_mqtt_client_handle_t g_client = nullptr;
QueueHandle_t g_queue = nullptr;
bool g_mqttStarted = false;
bool g_lastWifiUp = false;
bool g_lastMqttUp = false;
uint32_t g_lastInfoMs = 0;

// Written by the esp-mqtt task, read by the main loop.
std::atomic<bool> g_mqttUp(false);
std::atomic<bool> g_infoDue(false);
std::atomic<uint32_t> g_connects(0);
std::atomic<uint32_t> g_received(0);
std::atomic<uint32_t> g_dropped(0);
std::atomic<uint32_t> g_fragmented(0);

// Written by the main loop only.
uint32_t g_applied = 0;
uint32_t g_unchanged = 0;
uint32_t g_ignored = 0;

uint8_t subscribedCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    if (kTurnoutNames[i][0] != '\0') ++count;
  }
  return count;
}

void subscribeAll(esp_mqtt_client_handle_t client) {
  char topic[tc::kMaxTopicLength + 1];
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    if (kTurnoutNames[i][0] == '\0') continue;
    if (tc::turnoutTopic(kJmriChannel, kTurnoutNames[i], topic, sizeof(topic))) {
      esp_mqtt_client_subscribe(client, topic, kCommandQos);
    }
  }
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
      const uint8_t channel =
          tc::matchTurnoutTopic(kJmriChannel, kTurnoutNames, tc::kChannelCount, event->topic, event->topic_len);
      if (channel == 0) break;
      ++g_received;
      TurnoutMessage message;
      message.channel = channel;
      message.payload = tc::parseTurnoutPayload(event->data, static_cast<size_t>(event->data_len));
      message.retained = event->retain;
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

bool checkSettings() {
  if (!NET_HAVE_LOCAL_SETTINGS) {
    g_disabledReason = "no src/local_settings.h (copy local_settings.example.h)";
  } else if (kWifiSsid[0] == '\0' || kMqttHost[0] == '\0') {
    g_disabledReason = "Wi-Fi SSID or MQTT host not set in local_settings.h";
  } else if (!tc::validNodeName(kNodeName)) {
    g_disabledReason = "node name must be 1-32 letters, digits, '_' or '-'";
  } else if (!tc::validJmriChannel(kJmriChannel)) {
    g_disabledReason = "JMRI channel must be empty or end in '/', with no + or #";
  } else if (!tc::validateTurnoutNames(kTurnoutNames, tc::kChannelCount, &g_disabledReason)) {
    // g_disabledReason set
  } else {
    char topic[tc::kMaxTopicLength + 1];
    for (uint8_t i = 0; i < tc::kChannelCount && g_disabledReason == nullptr; ++i) {
      if (kTurnoutNames[i][0] == '\0') continue;
      if (!tc::turnoutTopic(kJmriChannel, kTurnoutNames[i], topic, sizeof(topic))) {
        g_disabledReason = "JMRI channel plus turnout name too long";
      }
    }
  }
  return g_disabledReason == nullptr;
}

}  // namespace

void begin(const char* firmwareVersion) {
  g_firmware = firmwareVersion;
  if (!checkSettings()) {
    Serial.printf("net: off: %s\n", g_disabledReason);
    return;
  }
  snprintf(g_statusTopic, sizeof(g_statusTopic), "cikutrail/%s/status", kNodeName);
  snprintf(g_infoTopic, sizeof(g_infoTopic), "cikutrail/%s/info", kNodeName);
  g_queue = xQueueCreate(kQueueLength, sizeof(TurnoutMessage));

  esp_mqtt_client_config_t config = {};
  config.host = kMqttHost;
  config.port = kMqttPort;
  config.transport = MQTT_TRANSPORT_OVER_TCP;
  config.client_id = kNodeName;
  config.username = kMqttUser[0] != '\0' ? kMqttUser : nullptr;
  config.password = kMqttPassword[0] != '\0' ? kMqttPassword : nullptr;
  config.lwt_topic = g_statusTopic;
  config.lwt_msg = kOffline;
  config.lwt_qos = 1;
  config.lwt_retain = 1;
  config.keepalive = kKeepaliveS;
  config.reconnect_timeout_ms = kReconnectMs;
  g_client = esp_mqtt_client_init(&config);
  if (g_queue == nullptr || g_client == nullptr) {
    g_disabledReason = "out of memory starting MQTT";
    Serial.printf("net: off: %s\n", g_disabledReason);
    return;
  }
  esp_mqtt_client_register_event(g_client, MQTT_EVENT_ANY, onMqttEvent, nullptr);

  WiFi.setHostname(kNodeName);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds latency to every command
  WiFi.setAutoReconnect(true);
  WiFi.begin(kWifiSsid, kWifiPassword);
  g_enabled = true;
  Serial.printf("net: joining Wi-Fi \"%s\"\n", kWifiSsid);
}

void loop() {
  if (!g_enabled) return;

  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp != g_lastWifiUp) {
    g_lastWifiUp = wifiUp;
    if (wifiUp) {
      Serial.printf("net: Wi-Fi up, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(),
                    static_cast<int>(WiFi.RSSI()));
    } else {
      Serial.println("net: Wi-Fi down");
    }
  }
  if (wifiUp && !g_mqttStarted) {
    // esp-mqtt reconnects by itself from here on, through Wi-Fi drops too.
    esp_mqtt_client_start(g_client);
    g_mqttStarted = true;
    Serial.printf("net: connecting to MQTT %s:%u\n", kMqttHost, kMqttPort);
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
}

bool nextTurnoutMessage(TurnoutMessage* message) {
  return g_queue != nullptr && xQueueReceive(g_queue, message, 0) == pdTRUE;
}

const char* turnoutName(uint8_t channel) { return kTurnoutNames[channel - 1]; }

void noteOutcome(Outcome outcome) {
  switch (outcome) {
    case Outcome::Applied: ++g_applied; break;
    case Outcome::Unchanged: ++g_unchanged; break;
    case Outcome::Ignored: ++g_ignored; break;
  }
}

void printStatus() {
  if (!g_enabled) {
    Serial.printf("net: off: %s\n", g_disabledReason != nullptr ? g_disabledReason : "not started");
    return;
  }
  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  Serial.printf("Wi-Fi: %s \"%s\"", wifiUp ? "up" : "down", kWifiSsid);
  if (wifiUp) {
    Serial.printf(", IP %s, RSSI %d dBm", WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()));
  }
  Serial.println();
  Serial.printf("MQTT: %s, %s:%u as \"%s\"%s, %lu connects\n", g_mqttUp ? "up" : "down", kMqttHost, kMqttPort,
                kNodeName, kMqttUser[0] != '\0' ? " with login" : "", static_cast<unsigned long>(g_connects));
  Serial.printf("status topic: %s\n", g_statusTopic);
  Serial.printf("JMRI channel: \"%s\"\n", kJmriChannel);
  char topic[tc::kMaxTopicLength + 1];
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    if (kTurnoutNames[i][0] == '\0') {
      Serial.printf("%2u  (unused)\n", i + 1);
    } else if (tc::turnoutTopic(kJmriChannel, kTurnoutNames[i], topic, sizeof(topic))) {
      Serial.printf("%2u  %s\n", i + 1, topic);
    }
  }
  Serial.printf("messages: %lu received, %lu applied, %lu unchanged, %lu ignored, %lu dropped, %lu split\n",
                static_cast<unsigned long>(g_received), static_cast<unsigned long>(g_applied),
                static_cast<unsigned long>(g_unchanged), static_cast<unsigned long>(g_ignored),
                static_cast<unsigned long>(g_dropped), static_cast<unsigned long>(g_fragmented));
}

}  // namespace net
