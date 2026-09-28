#include "health.h"

namespace tc {
namespace {

const uint32_t kBlinkMs = 500;       // setup, error: on 500 ms, off 500 ms
const uint32_t kSlowBlinkMs = 1000;  // JMRI offline: on 1 s, off 1 s

bool blinkOn(uint32_t nowMs, uint32_t halfPeriodMs) { return (nowMs / halfPeriodMs) % 2 == 0; }

}  // namespace

LedState ledStateFor(const HealthInputs& inputs) {
  if (inputs.portalOpen) return LedState::Setup;
  if (inputs.netOff) return LedState::Error;
  if (!inputs.wifiUp || !inputs.mqttUp) return LedState::Connecting;
  if (inputs.jmriOffline) return LedState::JmriOffline;
  return LedState::Ok;
}

const char* ledStateName(LedState state) {
  switch (state) {
    case LedState::Setup: return "blue blink (setup access point open)";
    case LedState::Error: return "red blink (network off: settings unusable)";
    case LedState::Connecting: return "yellow (connecting to Wi-Fi / MQTT)";
    case LedState::JmriOffline: return "slow green blink (JMRI offline)";
    case LedState::Ok: return "green (MQTT up, JMRI online)";
  }
  return "";
}

Rgb ledColour(LedState state, uint32_t nowMs) {
  const Rgb off = {0, 0, 0};
  switch (state) {
    case LedState::Setup: return blinkOn(nowMs, kBlinkMs) ? Rgb{0, 0, kLedLevel} : off;
    case LedState::Error: return blinkOn(nowMs, kBlinkMs) ? Rgb{kLedLevel, 0, 0} : off;
    case LedState::Connecting: return Rgb{kLedLevel, kLedLevel, 0};
    case LedState::JmriOffline: return blinkOn(nowMs, kSlowBlinkMs) ? Rgb{0, kLedLevel, 0} : off;
    case LedState::Ok: return Rgb{0, kLedLevel, 0};
  }
  return off;
}

ButtonTracker::ButtonTracker(uint32_t longPressMs)
    : m_longPressMs(longPressMs), m_pressed(false), m_longFired(false), m_pressedAtMs(0), m_lastHeldMs(0) {}

ButtonTracker::Event ButtonTracker::update(bool pressed, uint32_t nowMs) {
  if (pressed && !m_pressed) {
    m_pressed = true;
    m_longFired = false;
    m_pressedAtMs = nowMs;
    return Event::Pressed;
  }
  if (!pressed && m_pressed) {
    m_pressed = false;
    m_lastHeldMs = nowMs - m_pressedAtMs;
    return Event::Released;
  }
  if (pressed && !m_longFired && nowMs - m_pressedAtMs >= m_longPressMs) {
    m_longFired = true;
    return Event::LongPress;
  }
  return Event::None;
}

}  // namespace tc
