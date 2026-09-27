// Network settings in flash (Preferences / NVS). They survive power cuts
// and reflashing; the setup page writes them.
#pragma once

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

// Setup access point password: generated on first use, then kept.
const char* apPassword();

// Turnout names, one per channel ("" = unused). Compiled in until the
// phase 4 config page.
const char* const* turnoutNames();

}  // namespace settings
