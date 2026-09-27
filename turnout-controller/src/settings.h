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

// Turnout names, one per channel ("" = unused). Compiled in until the
// phase 4 config page.
const char* const* turnoutNames();

}  // namespace settings
