// Turnout state and its pin level, as JMRI's Raspberry Pi GPIO turnouts
// define it: CLOSED drives the pin HIGH, THROWN drives it LOW. JMRI applies
// its "Inverted" setting before the command reaches the node.
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
