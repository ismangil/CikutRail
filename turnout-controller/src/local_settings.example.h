// Compiled-in settings. The network settings are normally entered on the
// setup page (access point CikutRail-XXXX) and saved in flash; these only
// prefill that page, or are used while nothing is saved. Copy this file to
// local_settings.h (git-ignored) to change them. The turnout names are
// compiled in until the phase 4 config page.
#pragma once

#include <stdint.h>

#include "channels.h"

namespace local_settings {

const char* const kWifiSsid = "";
const char* const kWifiPassword = "";

const char* const kMqttHost = "";  // broker IP address or host name
const uint16_t kMqttPort = 1883;
const char* const kMqttUser = "";  // empty: connect without a login
const char* const kMqttPassword = "";

// JMRI's MQTT channel: empty in current JMRI, "/trains/" in older versions.
const char* const kJmriChannel = "";

// Used for the client ID, the Wi-Fi host name and cikutrail/<node>/...
const char* const kNodeName = "turnout1";

// JMRI turnout names, one per channel (MT101 is "101"). Empty: the channel
// isn't subscribed. Node 1 owns 101-199 (docs/MQTT_CONVENTIONS.md).
const char* const kTurnoutNames[tc::kChannelCount] = {
    "101", "102", "103", "104", "105", "106", "107", "108", "109", "110", "111",
};

}  // namespace local_settings
