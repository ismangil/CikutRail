// The node's one web server (port 80), shared by two pages chosen by the
// interface a request arrives on:
// - the setup access point (192.168.4.1): the setup page (portal.*), no
//   login, captive (every other URL redirects to it);
// - the home network (http://<node>.local or the node's IP): the config
//   page (config_page.*), behind a login.
#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace web {

// Starts the server once an interface is up; later calls do nothing.
void begin();
void loop();

WebServer& server();

// True if the current request arrived on the setup access point.
bool fromAccessPoint();

// Page helpers shared by both pages.
String esc(const char* text);
void sendPage(const String& body, int code = 200, const char* refresh = nullptr);
void field(String& body, const char* label, const char* name, const char* type, const char* value,
           const char* hint = nullptr, const char* placeholder = nullptr);
void redirect(const char* location);

}  // namespace web
