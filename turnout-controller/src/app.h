// What main.cpp offers the web pages: local turnout changes, behaviour
// settings and factory reset, applied the same way as from the console.
#pragma once

#include <stdint.h>

#include "behaviour.h"
#include "turnout_state.h"

namespace app {

// A local change (config page test button), like the console's close /
// throw: at once, counted towards stagger and interval. JMRI isn't told.
void setTurnoutLocal(uint8_t channel, tc::TurnoutState state);
bool isPending(uint8_t channel);

const tc::Behaviour& behaviour();
bool setBehaviour(const tc::Behaviour& behaviour);  // applies and saves

// Erases every saved setting, applies the defaults and opens the setup
// access point. Pins keep their levels; nothing restarts.
void factoryReset();

}  // namespace app
