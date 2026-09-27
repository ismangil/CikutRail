// Settings in flash (Preferences / NVS): network (from the setup page),
// behaviour (console "config") and the turnout levels. They survive power
// cuts and reflashing.
#pragma once

#include "behaviour.h"
#include "net_config.h"

namespace settings {

enum class Source : uint8_t {
  Saved,       // from flash
  CompiledIn,  // from src/local_settings.h, nothing saved yet
  None,        // nothing usable: *config holds defaults for the setup page
};

Source loadNet(tc::NetConfig* config);
bool saveNet(const tc::NetConfig& config);
bool forgetNet();

// Behaviour settings (console "config"); defaults if nothing is saved.
tc::Behaviour loadBehaviour();
bool saveBehaviour(const tc::Behaviour& behaviour);

// Turnout pin levels (bit i = channel i + 1 HIGH), for the startup
// "restore" policy after a power cut. False if none are saved.
bool loadLevels(uint16_t* levels);
bool saveLevels(uint16_t levels);

// Setup access point password: generated on first use, then kept.
const char* apPassword();

// Config page password (user "admin"): generated on first use, then kept.
const char* adminPassword();
bool setAdminPassword(const char* password);

// Turnout names, one per channel ("" = unused): saved ones, else the
// compiled-in defaults (101-111). Change them only while MQTT is stopped:
// the MQTT task reads them (net::applyTurnoutNames does this).
const char* const* turnoutNames();
bool saveTurnoutNames(const tc::TurnoutNames& names);

// Erases every saved setting except the setup access point password (so
// it can still be joined): network, behaviour, levels, names, admin
// password. Nothing is restarted; callers apply the defaults.
bool factoryReset();

}  // namespace settings
