#include "web.h"

#include <WiFi.h>

#include "config_page.h"
#include "net_config.h"
#include "portal.h"

namespace web {
namespace {

WebServer g_server(80);
bool g_started = false;

void notFound() {
  if (fromAccessPoint()) {
    // Captive portal: every other URL (the phone's connectivity checks
    // included) goes to the setup page.
    g_server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    g_server.send(302);
  } else {
    g_server.send(404, "text/plain", "not found");
  }
}

// Setup page routes only answer on the access point, config page routes
// only on the home network.
void onAccessPoint(void (*handler)()) {
  if (fromAccessPoint()) {
    handler();
  } else {
    notFound();
  }
}

void onHomeNetwork(void (*handler)()) {
  if (!fromAccessPoint()) {
    handler();
  } else {
    notFound();
  }
}

}  // namespace

void begin() {
  if (g_started) return;
  g_server.on("/", HTTP_GET, [] {
    if (fromAccessPoint()) {
      portal::handleRoot();
    } else {
      configpage::handleRoot();
    }
  });
  g_server.on("/save", HTTP_POST, [] { onAccessPoint(portal::handleSave); });
  g_server.on("/status", HTTP_GET, [] { onAccessPoint(portal::handleStatus); });
  g_server.on("/scan", HTTP_GET, [] { onAccessPoint(portal::handleScan); });
  g_server.on("/network", HTTP_POST, [] { onHomeNetwork(configpage::handleNetwork); });
  g_server.on("/names", HTTP_POST, [] { onHomeNetwork(configpage::handleNames); });
  g_server.on("/turnout", HTTP_POST, [] { onHomeNetwork(configpage::handleTurnout); });
  g_server.on("/behaviour", HTTP_POST, [] { onHomeNetwork(configpage::handleBehaviour); });
  g_server.on("/admin", HTTP_POST, [] { onHomeNetwork(configpage::handleAdmin); });
  g_server.on("/factory", HTTP_POST, [] { onHomeNetwork(configpage::handleFactory); });
  g_server.onNotFound(notFound);
  g_server.begin();
  g_started = true;
}

void loop() {
  if (g_started) g_server.handleClient();
}

WebServer& server() { return g_server; }

bool fromAccessPoint() {
  const IPAddress ap = WiFi.softAPIP();
  return ap != IPAddress(0, 0, 0, 0) && g_server.client().localIP() == ap;
}

String esc(const char* text) {
  char out[6 * tc::kMaxHostLength + 1];
  return tc::htmlEscape(text, out, sizeof(out)) ? String(out) : String();
}

void sendPage(const String& body, int code, const char* refresh) {
  String page;
  page.reserve(body.length() + 1100);
  page += F("<!doctype html><html><head><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'>");
  if (refresh != nullptr) {
    page += F("<meta http-equiv=refresh content='");
    page += refresh;
    page += F("'>");
  }
  page += F("<title>CikutRail turnout node</title><style>"
            "body{font-family:system-ui,sans-serif;margin:0 auto;padding:1em;max-width:40em;line-height:1.4}"
            "label{display:block;margin-top:.8em;font-weight:600}"
            "input,select{box-sizing:border-box;width:100%;padding:.5em;font-size:1em}"
            "button{margin-top:1.2em;padding:.6em 1.2em;font-size:1em}"
            ".hint{color:#555;font-size:.9em;font-weight:normal}"
            ".err{background:#fdd;padding:.6em;border-radius:4px}"
            ".ok{background:#dfd;padding:.6em;border-radius:4px}"
            ".net{display:block;width:100%;text-align:left;margin:.2em 0;padding:.4em;font-size:.95em}"
            "fieldset{margin-top:1em;border:1px solid #ccc;border-radius:4px}"
            "table{border-collapse:collapse;width:100%}td,th{padding:.25em .3em;text-align:left}"
            "td input{padding:.3em}td button{margin:0;padding:.3em .6em;font-size:.9em}"
            ".closed{color:#060;font-weight:600}.thrown{color:#036;font-weight:600}"
            "</style></head><body><h1>Turnout node</h1>");
  page += body;
  page += F("</body></html>");
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(code, "text/html", page);
}

void field(String& body, const char* label, const char* name, const char* type, const char* value,
           const char* hint, const char* placeholder) {
  body += F("<label>");
  body += label;
  if (hint != nullptr) {
    body += F(" <span class=hint>");
    body += hint;
    body += F("</span>");
  }
  body += F("<input name=");
  body += name;
  body += F(" id=");
  body += name;
  body += F(" type=");
  body += type;
  body += F(" value=\"");
  body += esc(value);
  body += F("\"");
  if (placeholder != nullptr) {
    body += F(" placeholder=\"");
    body += esc(placeholder);
    body += F("\"");
  }
  body += F(" autocapitalize=off autocorrect=off spellcheck=false></label>");
}

void redirect(const char* location) {
  g_server.sendHeader("Location", location);
  g_server.send(303);
}

}  // namespace web
