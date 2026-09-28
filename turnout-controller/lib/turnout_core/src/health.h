// Status LED and button logic (phase 5). Hardware-free: time is passed in.
#pragma once

#include <stdint.h>

namespace tc {

// In priority order: the first that applies is shown.
enum class LedState : uint8_t {
  Setup,        // setup access point open: blue blink
  Error,        // network off (unusable settings): red blink
  Connecting,   // Wi-Fi or MQTT not up: yellow
  JmriOffline,  // JMRI's OFFLINE seen, not back yet: slow green blink
  Ok,           // MQTT up, JMRI online: green
};

struct HealthInputs {
  bool portalOpen;
  bool netOff;
  bool wifiUp;
  bool mqttUp;
  bool jmriOffline;
};

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

inline bool operator==(const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
inline bool operator!=(const Rgb& a, const Rgb& b) { return !(a == b); }

// Dim: the LED sits next to the layout, not in a control room.
const uint8_t kLedLevel = 24;

LedState ledStateFor(const HealthInputs& inputs);
const char* ledStateName(LedState state);

// The colour to show at nowMs; blinking states alternate with off.
Rgb ledColour(LedState state, uint32_t nowMs);

// Turns button samples into events. A long press fires once, while the
// button is still held; the release after it is still reported.
class ButtonTracker {
 public:
  enum class Event : uint8_t { None, Pressed, LongPress, Released };

  explicit ButtonTracker(uint32_t longPressMs = 3000);

  Event update(bool pressed, uint32_t nowMs);

  // How long the last release was held, for the log.
  uint32_t lastHeldMs() const { return m_lastHeldMs; }

 private:
  uint32_t m_longPressMs;
  bool m_pressed;
  bool m_longFired;
  uint32_t m_pressedAtMs;
  uint32_t m_lastHeldMs;
};

}  // namespace tc
