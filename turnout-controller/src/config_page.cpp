#include "config_page.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_random.h>

#include "app.h"
#include "channels.h"
#include "net.h"
#include "net_config.h"
#include "settings.h"
#include "turnout_bank.h"
#include "web.h"

namespace configpage {
namespace {

const char kRealm[] = "CikutRail turnout node";

char g_token[17];
String g_notice;
bool g_noticeIsError = false;

const char* token() {
  if (g_token[0] == '\0') snprintf(g_token, sizeof(g_token), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
                                   static_cast<unsigned long>(esp_random()));
  return g_token;
}

void notice(const String& text, bool isError) {
  g_notice = text;
  g_noticeIsError = isError;
}

bool loggedIn() {
  WebServer& server = web::server();
  if (server.authenticate("admin", settings::adminPassword())) return true;
  server.requestAuthentication(BASIC_AUTH, kRealm, "login required: user admin, password on the USB console (net)");
  return false;
}

// Logged in and the form's token matches this boot's.
bool formAccepted() {
  if (!loggedIn()) return false;
  if (web::server().arg("t") != token()) {
    notice("The page was out of date (the node has restarted since it was loaded); nothing changed.", true);
    web::redirect("/");
    return false;
  }
  return true;
}

void formStart(String& body, const char* action) {
  body += F("<form method=post action=");
  body += action;
  body += F("><input type=hidden name=t value=");
  body += token();
  body += F(">");
}

void option(String& body, const char* value, const char* label, bool selected) {
  body += F("<option value=");
  body += value;
  if (selected) body += F(" selected");
  body += F(">");
  body += label;
  body += F("</option>");
}

void turnoutsSection(String& body) {
  const char* const* names = settings::turnoutNames();
  body += F("<fieldset><legend>Turnouts</legend>");
  formStart(body, "/names");
  // Enter in a name field submits the form with its first submit button;
  // make that "save names", never a test button that moves a turnout.
  body += F("<button type=submit tabindex=-1 aria-hidden=true "
            "style='position:absolute;left:-9999px;width:1px;height:1px'>Save names</button>");
  body += F("<table><tr><th>Ch</th><th>Pin</th><th>JMRI name <span class=hint>(MT&hellip;)</span></th>"
            "<th>State</th><th>Test</th></tr>");
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    const bool closed = bank::state(channel) == tc::TurnoutState::Closed;
    char fieldName[4];
    snprintf(fieldName, sizeof(fieldName), "n%u", channel);
    body += F("<tr><td>");
    body += channel;
    body += F("</td><td>G");
    body += tc::kChannelGpio[channel - 1];
    body += F("</td><td><input name=");
    body += fieldName;
    body += F(" value=\"");
    body += web::esc(names[channel - 1]);
    body += F("\" maxlength=16 autocapitalize=off></td><td class=");
    body += closed ? F("closed>CLOSED") : F("thrown>THROWN");
    if (app::isPending(channel)) body += F(" <span class=hint>(change waiting)</span>");
    body += F("</td><td><button name=set value=");
    body += channel;
    body += F("c formaction=/turnout>Close</button> <button name=set value=");
    body += channel;
    body += F("t formaction=/turnout>Throw</button></td></tr>");
  }
  body += F("</table><p class=hint>A blank name leaves the channel unsubscribed (its pin keeps its level). "
            "The test buttons change the pin here only: JMRI isn't told, and its next command wins.</p>"
            "<button type=submit>Save names</button></form></fieldset>");
}

void behaviourSection(String& body) {
  const tc::Behaviour& behaviour = app::behaviour();
  char number[8];
  body += F("<fieldset><legend>Behaviour</legend>");
  formStart(body, "/behaviour");
  body += F("<label>At power-up <select name=startup>");
  option(body, "restore", "restore the last levels", behaviour.startup == tc::StartupLevel::Restore);
  option(body, "low", "all LOW (THROWN), as the Pi", behaviour.startup == tc::StartupLevel::Low);
  body += F("</select></label><label>When JMRI goes offline <select name=offline>");
  option(body, "hold", "hold every turnout", behaviour.offline == tc::OfflinePolicy::Hold);
  option(body, "low", "all LOW (THROWN), as the Pi", behaviour.offline == tc::OfflinePolicy::Low);
  body += F("</select></label>");
  snprintf(number, sizeof(number), "%u", behaviour.staggerMs);
  web::field(body, "Stagger", "stagger", "number", number, "(ms between any two changes, 0-5000)");
  snprintf(number, sizeof(number), "%u", behaviour.minIntervalMs);
  web::field(body, "Minimum interval", "interval", "number", number, "(ms between two changes of one turnout, 0-10000)");
  body += F("<button type=submit>Save behaviour</button></form></fieldset>");
}

void networkSection(String& body) {
  const tc::NetConfig& config = net::config();
  char port[6];
  snprintf(port, sizeof(port), "%u", config.mqttPort);
  body += F("<fieldset><legend>Network</legend>");
  formStart(body, "/network");
  web::field(body, "Wi-Fi network", "ssid", "text", config.wifiSsid);
  web::field(body, "Wi-Fi password", "wpass", "password", "", "(blank keeps the saved one)");
  web::field(body, "MQTT host", "mhost", "text", config.mqttHost);
  web::field(body, "MQTT port", "mport", "number", port);
  web::field(body, "MQTT user", "muser", "text", config.mqttUser, "(blank: no login)");
  web::field(body, "MQTT password", "mpass", "password", "",
             config.mqttPassword[0] != '\0' ? "(blank keeps the saved one)" : nullptr);
  web::field(body, "JMRI channel", "chan", "text", config.jmriChannel, "(blank in current JMRI)");
  web::field(body, "Node name", "node", "text", config.nodeName, "(also the .local name)");
  body += F("<p class=hint>MQTT, channel or name changes reconnect MQTT only. A Wi-Fi change leaves this "
            "network; if the node can't join the new one, its setup access point opens after 3 minutes.</p>"
            "<button type=submit>Save network</button></form></fieldset>");
}

void adminSection(String& body) {
  body += F("<fieldset><legend>Admin password</legend>");
  formStart(body, "/admin");
  web::field(body, "New password", "p1", "password", "", "(8-64 characters, no spaces)");
  web::field(body, "Repeat", "p2", "password", "");
  body += F("<button type=submit>Change password</button></form></fieldset>");
}

void factorySection(String& body) {
  body += F("<fieldset><legend>Factory reset</legend>");
  formStart(body, "/factory");
  body += F("<p class=hint>Erases the network, turnout names, behaviour, saved levels and this admin password, "
            "then opens the setup access point (its password stays, shown by <code>portal</code> on the USB "
            "console). Pins keep their levels; nothing restarts.</p>"
            "<label><input type=checkbox name=confirm value=yes style='width:auto'> Yes, erase everything</label>"
            "<button type=submit>Factory reset</button></form></fieldset>");
}

}  // namespace

void handleRoot() {
  if (!loggedIn()) return;
  const tc::NetConfig& config = net::config();
  String body;
  body.reserve(9000);
  body += F("<p class=hint>");
  body += web::esc(config.nodeName);
  body += F(" &middot; ");
  body += WiFi.localIP().toString();
  body += F(" &middot; Wi-Fi ");
  body += String(static_cast<int>(WiFi.RSSI()));
  body += F(" dBm &middot; MQTT ");
  body += net::mqttUp() ? F("connected") : F("not connected");
  body += F(" &middot; <a href=/>refresh</a></p>");
  if (g_notice.length() > 0) {
    body += g_noticeIsError ? F("<p class=err>") : F("<p class=ok>");
    body += web::esc(g_notice.c_str());
    body += F("</p>");
    g_notice = "";
  }
  turnoutsSection(body);
  behaviourSection(body);
  networkSection(body);
  adminSection(body);
  factorySection(body);
  web::sendPage(body);
}

void handleNames() {
  if (!formAccepted()) return;
  WebServer& server = web::server();
  String values[tc::kChannelCount];
  const char* fields[tc::kChannelCount];
  for (uint8_t i = 0; i < tc::kChannelCount; ++i) {
    values[i] = server.arg(String("n") + (i + 1));
    fields[i] = values[i].c_str();
  }
  tc::TurnoutNames names;
  const char* error = nullptr;
  if (!tc::parseTurnoutNames(fields, &names, &error)) {
    notice(String("Names not saved: ") + error, true);
  } else {
    net::applyTurnoutNames(names);
    notice("Turnout names saved; MQTT resubscribed.", false);
  }
  web::redirect("/");
}

void handleTurnout() {
  if (!formAccepted()) return;
  const String set = web::server().arg("set");  // e.g. "3c" or "11t"
  const long channel = set.toInt();
  const char last = set.length() > 0 ? set[set.length() - 1] : '\0';
  if (channel < 1 || channel > tc::kChannelCount || (last != 'c' && last != 't')) {
    notice("Unknown test button.", true);
  } else {
    const tc::TurnoutState state = last == 'c' ? tc::TurnoutState::Closed : tc::TurnoutState::Thrown;
    app::setTurnoutLocal(static_cast<uint8_t>(channel), state);
    notice(String("Channel ") + channel + " set " + tc::stateName(state) + " here (JMRI not told).", false);
  }
  web::redirect("/");
}

void handleBehaviour() {
  if (!formAccepted()) return;
  WebServer& server = web::server();
  const String startup = server.arg("startup"), offline = server.arg("offline"), stagger = server.arg("stagger"),
               interval = server.arg("interval");
  tc::Behaviour behaviour;
  const char* error = nullptr;
  if (!tc::parseBehaviourForm(startup.c_str(), offline.c_str(), stagger.c_str(), interval.c_str(), &behaviour,
                              &error)) {
    notice(String("Behaviour not saved: ") + error, true);
  } else if (!app::setBehaviour(behaviour)) {
    notice("Behaviour applied, but not saved to flash: it lasts until power-off.", true);
  } else {
    notice("Behaviour saved.", false);
  }
  web::redirect("/");
}

void handleNetwork() {
  if (!formAccepted()) return;
  WebServer& server = web::server();
  const String ssid = server.arg("ssid"), wpass = server.arg("wpass"), mhost = server.arg("mhost"),
               mport = server.arg("mport"), muser = server.arg("muser"), mpass = server.arg("mpass"),
               chan = server.arg("chan"), node = server.arg("node");
  const tc::NetForm form = {ssid.c_str(),  wpass.c_str(), mhost.c_str(), mport.c_str(),
                            muser.c_str(), mpass.c_str(), chan.c_str(),  node.c_str()};
  const tc::NetConfig& current = net::config();
  tc::NetConfig config;
  const char* error = nullptr;
  if (!tc::applyNetForm(current, form, &config, &error)) {
    notice(String("Network not saved: ") + error, true);
    web::redirect("/");
    return;
  }
  if (!settings::saveNet(config)) {
    notice("Network not saved: could not write to flash.", true);
    web::redirect("/");
    return;
  }
  const bool wifiChanged =
      strcmp(config.wifiSsid, current.wifiSsid) != 0 || strcmp(config.wifiPassword, current.wifiPassword) != 0;
  if (wifiChanged) {
    String body = F("<p class=ok>Saved. The node is leaving this network to join <b>");
    body += web::esc(config.wifiSsid);
    body += F("</b>. Find it there at http://");
    body += web::esc(config.nodeName);
    body += F(".local. If it can't join, its setup access point opens after about 3 minutes.</p>");
    web::sendPage(body);
  } else {
    notice("Network saved; MQTT reconnecting.", false);
    web::redirect("/");
  }
  // After the reply, so the browser gets it before the connection drops.
  net::applyConfig(config);
}

void handleAdmin() {
  if (!formAccepted()) return;
  WebServer& server = web::server();
  const String p1 = server.arg("p1"), p2 = server.arg("p2");
  if (p1 != p2) {
    notice("Password not changed: the two entries differ.", true);
  } else if (!tc::validAdminPassword(p1.c_str())) {
    notice("Password not changed: 8-64 characters, no spaces.", true);
  } else if (!settings::setAdminPassword(p1.c_str())) {
    notice("Password not changed: could not write to flash.", true);
  } else {
    Serial.println("web: admin password changed");
    web::sendPage(F("<p class=ok>Password changed. Your browser will ask for the new one: "
                    "<a href=/>back to the config page</a>.</p>"));
    return;
  }
  web::redirect("/");
}

void handleFactory() {
  if (!formAccepted()) return;
  if (web::server().arg("confirm") != "yes") {
    notice("Nothing erased: tick the box to confirm.", true);
    web::redirect("/");
    return;
  }
  web::sendPage(F("<p class=ok>Settings erased. The node leaves this network and opens its setup access point "
                  "(CikutRail-&hellip;, password shown by <code>portal</code> on the USB console).</p>"));
  app::factoryReset();
}

}  // namespace configpage
