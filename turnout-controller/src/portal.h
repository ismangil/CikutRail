// Setup access point CikutRail-XXXX with a captive setup page at
// 192.168.4.1: Wi-Fi, MQTT, JMRI channel and node name. Saving stores the
// settings and hands them to net, which joins the network without a
// reboot. Nothing here touches the turnout pins.
#pragma once

#include <stdint.h>

namespace portal {

enum class Reason : uint8_t {
  FirstSetup,  // nothing saved
  WifiFailed,  // saved network not joined for a while; closes once it is
  Console,     // "portal on"
  Button,      // long press on the node's button
};

void open(Reason reason);
void close();
bool isOpen();

// Serves the page and closes the access point once the node has joined
// its network after a save (or after a Wi-Fi failure). Call often.
void loop();

void printStatus();

// Setup page handlers; web.cpp routes access-point requests to them.
void handleRoot();
void handleSave();
void handleStatus();
void handleScan();

}  // namespace portal
