// Turnout state and its pin level: CLOSED drives the pin HIGH, THROWN
// drives it LOW. HIGH is the GreenHat's idle level (its input pull-up), so
// a CLOSED turnout doesn't move while the node isn't driving its pin. Each
// coil is wired so that HIGH closes it (see WIRING.md). JMRI's "Inverted"
// setting, if used, is applied before the command reaches the node.
#pragma once

#include <stdint.h>

namespace tc {

enum class TurnoutState : uint8_t { Closed, Thrown };

inline bool pinLevelFor(TurnoutState state) { return state == TurnoutState::Closed; }

inline TurnoutState stateForPinLevel(bool level) {
  return level ? TurnoutState::Closed : TurnoutState::Thrown;
}

inline const char* stateName(TurnoutState state) {
  return state == TurnoutState::Closed ? "CLOSED" : "THROWN";
}

}  // namespace tc
