#include "portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "net.h"
#include "net_config.h"
#include "settings.h"
#include "web.h"

namespace portal {
namespace {

const uint32_t kCloseAfterJoinMs = 30000;  // time to read the node's new IP on the page
const uint8_t kMaxNetworks = 20;
const byte kDnsPort = 53;

struct Network {
  String ssid;
  int32_t rssi;
  bool open;
};

bool g_open = false;
Reason g_reason = Reason::Console;
bool g_closeWhenJoined = false;
uint32_t g_joinedAtMs = 0;
bool g_joinedSeen = false;
char g_apName[20];
DNSServer g_dns;
Network g_networks[kMaxNetworks];
uint8_t g_networkCount = 0;
bool g_scanning = false;
const char* g_lastSaveError = nullptr;

const char* reasonName(Reason reason) {
  switch (reason) {
    case Reason::FirstSetup: return "no network settings saved";
    case Reason::WifiFailed: return "saved Wi-Fi network not reachable";
    case Reason::Console: return "opened from the console";
    case Reason::Button: return "opened with a long button press";
  }
  return "";
}

void startScan() {
  if (g_scanning) return;
  g_scanning = WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING;
}

void collectScan() {
  if (!g_scanning) return;
  const int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) return;
  g_scanning = false;
  if (found < 0) return;
  g_networkCount = 0;
  for (int16_t i = 0; i < found; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    bool duplicate = false;
    for (uint8_t j = 0; j < g_networkCount; ++j) {
      if (g_networks[j].ssid == ssid) {
        duplicate = true;
        if (WiFi.RSSI(i) > g_networks[j].rssi) g_networks[j].rssi = WiFi.RSSI(i);
      }
    }
    if (duplicate || g_networkCount == kMaxNetworks) continue;
    g_networks[g_networkCount++] = {ssid, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN};
  }
  WiFi.scanDelete();
  // Strongest first.
  for (uint8_t i = 1; i < g_networkCount; ++i) {
    for (uint8_t j = i; j > 0 && g_networks[j].rssi > g_networks[j - 1].rssi; --j) {
      const Network swap = g_networks[j];
      g_networks[j] = g_networks[j - 1];
      g_networks[j - 1] = swap;
    }
  }
}

}  // namespace

void handleRoot() {
  const tc::NetConfig& config = net::config();
  char port[6];
  snprintf(port, sizeof(port), "%u", config.mqttPort);

  String body;
  body.reserve(3500);
  if (g_lastSaveError != nullptr) {
    body += F("<p class=err>Not saved: ");
    body += web::esc(g_lastSaveError);
    body += F("</p>");
    g_lastSaveError = nullptr;
  }
  body += F("<p class=hint>Turnouts keep their positions while you change these. The node joins the network "
            "right after saving, without restarting.</p><form method=post action=/save><fieldset><legend>Wi-Fi"
            "</legend>");
  web::field(body, "Network", "ssid", "text", config.wifiSsid);
  if (g_networkCount > 0) {
    body += F("<p class=hint>Tap a network to use it:</p>");
    for (uint8_t i = 0; i < g_networkCount; ++i) {
      const String name = web::esc(g_networks[i].ssid.c_str());
      body += F("<button type=button class=net onclick=\"ssid.value=this.dataset.s\" data-s=\"");
      body += name;
      body += F("\">");
      body += name;
      body += F(" <span class=hint>");
      body += String(g_networks[i].rssi);
      body += g_networks[i].open ? F(" dBm, open") : F(" dBm");
      body += F("</span></button>");
    }
  }
  body += F("<p class=hint>");
  body += g_scanning ? F("Scanning&hellip; ") : F("");
  body += F("<a href=/scan>Scan again</a></p>");
  web::field(body, "Password", "wpass", "password", "",
             config.wifiPassword[0] != '\0' ? "(blank keeps the saved one)" : "(blank for an open network)");
  body += F("</fieldset><fieldset><legend>MQTT broker</legend>");
  web::field(body, "Host", "mhost", "text", config.mqttHost, "IP address, e.g. 192.168.0.198");
  web::field(body, "Port", "mport", "number", port);
  web::field(body, "User", "muser", "text", config.mqttUser, "(blank: no login)");
  web::field(body, "Password", "mpass", "password", "",
             config.mqttPassword[0] != '\0' ? "(blank keeps the saved one)" : nullptr);
  body += F("</fieldset><fieldset><legend>JMRI and node</legend>");
  web::field(body, "JMRI channel", "chan", "text", config.jmriChannel,
             "(blank in current JMRI; older JMRI used /trains/)");
  web::field(body, "Node name", "node", "text", config.nodeName, "(letters, digits, - and _)");
  body += F("</fieldset><button type=submit>Save and connect</button></form>");
  web::sendPage(body);
}

void handleSave() {
  // arg() returns temporaries; keep them alive until applyNetForm has copied them.
  WebServer& server = web::server();
  const String ssid = server.arg("ssid"), wpass = server.arg("wpass"), mhost = server.arg("mhost"),
               mport = server.arg("mport"), muser = server.arg("muser"), mpass = server.arg("mpass"),
               chan = server.arg("chan"), node = server.arg("node");
  const tc::NetForm form = {ssid.c_str(),  wpass.c_str(), mhost.c_str(), mport.c_str(),
                            muser.c_str(), mpass.c_str(), chan.c_str(),  node.c_str()};

  tc::NetConfig config;
  const char* error = nullptr;
  if (!tc::applyNetForm(net::config(), form, &config, &error)) {
    g_lastSaveError = error;
    web::redirect("/");
    return;
  }
  if (!settings::saveNet(config)) {
    g_lastSaveError = "could not write to flash";
    web::redirect("/");
    return;
  }
  Serial.printf("portal: settings saved, joining \"%s\"\n", config.wifiSsid);
  g_closeWhenJoined = true;
  g_joinedSeen = false;
  web::redirect("/status");
  // After the reply, so the phone gets it before the radio switches.
  net::applyConfig(config);
}

void handleStatus() {
  const tc::NetConfig& config = net::config();
  String body;
  body += F("<p>Wi-Fi <b>");
  body += web::esc(config.wifiSsid);
  body += F("</b>: ");
  if (WiFi.status() == WL_CONNECTED) {
    body += F("connected, node address <b>");
    body += WiFi.localIP().toString();
    body += F("</b></p><p>MQTT: ");
    body += net::mqttUp() ? F("connected") : F("connecting&hellip;");
    body += F("</p><p class=hint>This setup network closes about 30 s after the node joins. "
              "Reconnect your phone to your usual Wi-Fi.</p>");
  } else {
    body += F("joining&hellip;</p><p class=hint>If this doesn't change within a minute, check the network "
              "and password: <a href=/>back to settings</a>.</p>");
  }
  web::sendPage(body, 200, "3");
}

void handleScan() {
  startScan();
  web::redirect("/");
}

void open(Reason reason) {
  if (g_open) {
    // A console open doesn't cancel auto-close set by a failure or save.
    return;
  }
  g_reason = reason;
  g_closeWhenJoined = reason == Reason::WifiFailed;
  g_joinedSeen = false;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(g_apName, sizeof(g_apName), "CikutRail-%02X%02X", mac[4], mac[5]);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(g_apName, settings::apPassword());
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(kDnsPort, "*", WiFi.softAPIP());
  web::begin();
  g_open = true;
  startScan();
  net::onPortalOpened(reason == Reason::WifiFailed);
  Serial.printf("portal: open (%s): join Wi-Fi \"%s\", password %s, then http://%s\n", reasonName(reason),
                g_apName, settings::apPassword(), WiFi.softAPIP().toString().c_str());
}

void close() {
  if (!g_open) return;
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_open = false;
  g_scanning = false;
  Serial.println("portal: closed");
  net::onPortalClosed();
}

bool isOpen() { return g_open; }

void loop() {
  if (!g_open) return;
  g_dns.processNextRequest();
  collectScan();

  if (g_closeWhenJoined && WiFi.status() == WL_CONNECTED) {
    if (!g_joinedSeen) {
      g_joinedSeen = true;
      g_joinedAtMs = millis();
    } else if (millis() - g_joinedAtMs >= kCloseAfterJoinMs) {
      close();
    }
  } else {
    g_joinedSeen = false;
  }
}

void printStatus() {
  if (!g_open) {
    Serial.println("portal: closed (portal on opens it)");
    return;
  }
  Serial.printf("portal: open (%s): Wi-Fi \"%s\", password %s, http://%s, %u connected%s\n", reasonName(g_reason),
                g_apName, settings::apPassword(), WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum(),
                g_closeWhenJoined ? ", closes once the node has joined its network" : "");
}

}  // namespace portal
