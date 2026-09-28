// Turnout node firmware: JMRI MQTT turnouts over Wi-Fi, set up through a
// setup page (docs/PHASE2_BENCH.md), with startup and restart behaviour
// (docs/PHASE3_BENCH.md), plus the phase 1 bench console on USB serial
// (docs/PHASE1_BENCH.md).
#include <Arduino.h>
#include <esp_system.h>

#include "app.h"
#include "behaviour.h"
#include "channels.h"
#include "command.h"
#include "health.h"
#include "jmri_protocol.h"
#include "net.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "turnout_bank.h"
#include "web.h"

namespace {

const uint32_t kSerialWaitMs = 1500;
const uint32_t kFiveVoltOffSettleMs = 500;
const char* const kFirmwareName = "CikutRail turnout-controller";
const char* const kFirmwareVersion = "0.5.0-phase5";
const uint32_t kButtonPollMs = 50;
const uint32_t kWatchdogFeedMs = 1000;
// Levels go to flash this long after the last change, so a burst of
// changes costs one write.
const uint32_t kSaveLevelsAfterMs = 2000;

struct CycleJob {
  bool active = false;
  uint8_t channel = 0;
  uint32_t done = 0;
  uint32_t count = 0;
  uint32_t intervalMs = 0;
  uint32_t lastMs = 0;
};

CycleJob g_cycle;
char g_line[tc::kMaxLineLength + 1];
size_t g_lineLength = 0;
bool g_lineOverflow = false;
bool g_fiveVoltWasOn = false;  // 5VOUT as found at boot, for the boot report
bool g_fiveVoltKnown = false;

tc::Behaviour g_behaviour = tc::defaultBehaviour();
tc::TurnoutScheduler g_scheduler;
bool g_haveSavedLevels = false;
uint16_t g_savedLevels = 0;     // as last written to flash
uint16_t g_lastLevels = 0;      // as last seen, to time the flash write
uint32_t g_levelsChangedMs = 0;
bool g_jmriOffline = false;     // a live OFFLINE seen, JMRI not back yet

// Status LED and button.
bool g_ledReady = false;
tc::LedState g_ledState = tc::LedState::Connecting;
bool g_ledStateKnown = false;
tc::Rgb g_ledShown = {0, 0, 0};
bool g_ledShownKnown = false;
tc::ButtonTracker g_button;
uint32_t g_lastButtonPollMs = 0;

// PM1 watchdog: off by default. "hang" stops feeding it, for the bench
// test of what its reset does.
enum class WatchdogMode : uint8_t { Off, On, Hang };
WatchdogMode g_watchdogMode = WatchdogMode::Off;
uint8_t g_watchdogS = 0;
uint32_t g_lastFeedMs = 0;
uint32_t g_appliedCount = 0;    // changes made by the scheduler

void configure(const tc::Command& command);  // console "config", below
void watchdogCommand(const tc::Command& command);  // console "wdt", below

// "[12.345] " seconds since boot, for timing stagger and interval.
void stamp() {
  const uint32_t now = millis();
  Serial.printf("[%lu.%03lu] ", static_cast<unsigned long>(now / 1000), static_cast<unsigned long>(now % 1000));
}

const char* levelSourceName(bank::LevelSource source) {
  switch (source) {
    case bank::LevelSource::Rtc: return "restore: levels from RTC memory (a reset)";
    case bank::LevelSource::Flash: return "restore: levels saved in flash (a power cut)";
    case bank::LevelSource::NothingSaved: return "restore: nothing saved, all LOW (THROWN)";
    case bank::LevelSource::PolicyLow: return "low: all LOW (THROWN)";
  }
  return "";
}

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external reset pin";
    case ESP_RST_SW: return "software (esp_restart)";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown";
  }
}

const char* resetKindName(uint8_t kindPlusOne) {
  switch (kindPlusOne) {
    case 1: return "reset soft";
    case 2: return "reset panic";
    case 3: return "reset wdt";
    default: return "none";
  }
}

const char* onOff(bool on) { return on ? "on" : "off"; }

void printLevels(const char* label, uint16_t levels) {
  Serial.printf("%s", label);
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    Serial.printf(" %u:%c", channel, (levels & (1u << (channel - 1))) ? 'H' : 'L');
  }
  Serial.println();
}

void printFiveVolt() {
  bool on = false;
  if (power::fiveVoltOut(&on)) {
    Serial.printf("5VOUT: %s\n", onOff(on));
  } else {
    Serial.println("5VOUT: unknown (PM1 not responding)");
  }
}

void printStatus() {
  Serial.println("ch  gpio  state   pad");
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    Serial.printf("%2u  G%-3u %-7s %s\n", channel, tc::kChannelGpio[channel - 1],
                  tc::stateName(bank::state(channel)), bank::padLevel(channel) ? "HIGH" : "LOW");
  }
  Serial.printf("pin latch (hold): %s\n", onOff(bank::holdEnabled()));
  printFiveVolt();
  if (g_ledStateKnown) Serial.printf("LED: %s\n", tc::ledStateName(g_ledState));
  if (g_scheduler.pendingCount() > 0) {
    Serial.printf("waiting changes (stagger/interval): %u\n", g_scheduler.pendingCount());
  }
  if (g_cycle.active) {
    Serial.printf("cycle: channel %u, %lu of %lu toggles\n", g_cycle.channel,
                  static_cast<unsigned long>(g_cycle.done), static_cast<unsigned long>(g_cycle.count));
  }
}

void printBootReport() {
  const bank::BootReport& boot = bank::bootReport();
  Serial.printf("\n%s %s\n", kFirmwareName, kFirmwareVersion);
  Serial.printf("reset reason: %s (ROM code 0x%02lx)\n", resetReasonName(boot.resetReason),
                static_cast<unsigned long>(boot.romResetReason));
  Serial.printf("console reset before this boot: %s\n", resetKindName(boot.plannedReset));
  printLevels("pads read at boot, before driving:", boot.padLevelsAtBoot);
  // esp_timer appears to count from chip reset (these times grow with the
  // firmware image size), so they include the bootloader, which checks the
  // whole image before starting it.
  Serial.printf("pins first driven %lld us after chip reset, %s\n", static_cast<long long>(boot.drivenAtUs),
                boot.restored ? "to the levels in RTC memory" : "all LOW (power cut: RTC memory empty)");
  Serial.printf("startup %s, at %lld us after reset", levelSourceName(boot.source),
                static_cast<long long>(boot.startupAtUs));
  if (boot.changedAtStartup != 0) {
    Serial.print(", changed channels");
    for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
      if (boot.changedAtStartup & (1u << (channel - 1))) Serial.printf(" %u", channel);
    }
  }
  Serial.println();
  if (!power::available()) {
    Serial.println("PM1: not found; 5VOUT not controlled");
  } else if (!g_fiveVoltKnown) {
    Serial.println("5VOUT at boot: unknown (PM1 read failed)");
  } else {
    Serial.printf("5VOUT at boot: %s%s\n", onOff(g_fiveVoltWasOn),
                  g_fiveVoltWasOn ? "" : ", turned on after pins were driven");
  }
  Serial.println();
  printStatus();
}

void printHelp() {
  Serial.println(
      "Commands (channels: 3, 1-4,7 or all):\n"
      "  status | s                show channels, pin latch, 5VOUT\n"
      "  boot                      print this boot's report again\n"
      "  close | c <channels>      drive HIGH (CLOSED)\n"
      "  throw | t <channels>      drive LOW (THROWN)\n"
      "  toggle <channels>         flip each channel\n"
      "  cycle <ch> <n> <ms>       toggle one channel n times (n <= 100, 250-60000 ms apart)\n"
      "                            keep ms above the GreenHat pulse length or the coil\n"
      "                            stays energised\n"
      "  5v [on|off]               show or switch 5VOUT (GreenHat logic supply)\n"
      "  hold [on|off]             show or switch the pin latch (gpio hold)\n"
      "  reset soft|panic|wdt [5v-off]\n"
      "                            reset the ESP32: esp_restart, abort, or interrupt\n"
      "                            watchdog; 5v-off turns 5VOUT off first\n"
      "  pm1                       battery, input and 5 V readings\n"
      "  pm1 btn                   button settings, and whether it was pressed\n"
      "  net                       Wi-Fi, MQTT, turnout topics and message counts\n"
      "  net forget                erase the saved network settings, open the setup portal\n"
      "  portal [on|off]           show or switch the setup access point and page\n"
      "  config                    show behaviour settings\n"
      "  config startup restore|low       levels at power-up: last saved, or all LOW\n"
      "  config offline hold|low          when JMRI goes offline: keep, or all LOW\n"
      "  config stagger <ms>              0-5000 between any two changes (MQTT)\n"
      "  config interval <ms>             0-10000 between two changes of one turnout\n"
      "  wdt [off | <5-255> | hang]       PM1 watchdog: show, off, on with a timeout (until\n"
      "                                   power-off), or stop feeding it (bench test: resets)\n"
      "  help | ?                  this list\n"
      "Any command stops a running cycle.");
}

void applyToChannels(uint16_t mask, tc::CommandType type) {
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    if (!(mask & (1u << (channel - 1)))) continue;
    tc::TurnoutState target;
    if (type == tc::CommandType::Close) {
      target = tc::TurnoutState::Closed;
    } else if (type == tc::CommandType::Throw) {
      target = tc::TurnoutState::Thrown;
    } else {
      target = bank::state(channel) == tc::TurnoutState::Closed ? tc::TurnoutState::Thrown : tc::TurnoutState::Closed;
    }
    bank::setState(channel, target);
    g_scheduler.noteApplied(channel, millis());
    Serial.printf("%u %s\n", channel, tc::stateName(target));
  }
}

void printReadings() {
  power::Readings readings;
  if (!power::read(&readings)) {
    Serial.println("PM1 readings unavailable");
    return;
  }
  Serial.printf("battery %u mV, input %u mV, 5V rail %u mV\n", readings.batteryMv, readings.inputMv,
                readings.fiveVoltMv);
  printFiveVolt();
}

void printButtons() {
  power::Buttons buttons;
  if (!power::readButtons(&buttons)) {
    Serial.println("PM1 button settings unavailable");
    return;
  }
  Serial.printf("single-click reset: %s\n", buttons.singleClickResetDisabled ? "disabled" : "enabled");
  Serial.printf("double-click power off: %s\n", buttons.doubleClickOffDisabled ? "disabled" : "enabled");
  Serial.printf("pressed since last read: %s\n", buttons.pressedSinceLastRead ? "yes" : "no");
}

[[noreturn]] void resetNow(const tc::Command& command) {
  if (command.fiveVoltOffFirst) {
    if (power::setFiveVoltOut(false)) {
      Serial.printf("5VOUT off, waiting %lu ms\n", static_cast<unsigned long>(kFiveVoltOffSettleMs));
      delay(kFiveVoltOffSettleMs);
    } else {
      Serial.println("could not turn 5VOUT off; resetting anyway");
    }
  }
  const uint8_t kindPlusOne = static_cast<uint8_t>(command.resetKind) + 1;
  bank::notePlannedReset(kindPlusOne);
  Serial.printf("%s now\n", resetKindName(kindPlusOne));
  Serial.flush();
  delay(50);

  switch (command.resetKind) {
    case tc::ResetKind::Soft:
      esp_restart();
    case tc::ResetKind::Panic:
      abort();
    case tc::ResetKind::Watchdog:
      break;
  }
  // Spin with interrupts off until the interrupt watchdog resets the chip.
  portDISABLE_INTERRUPTS();
  for (;;) {
  }
}

void execute(const tc::Command& command) {
  if (g_cycle.active) {
    g_cycle.active = false;
    Serial.println("cycle stopped");
  }

  switch (command.type) {
    case tc::CommandType::Help:
      printHelp();
      break;
    case tc::CommandType::Status:
      printStatus();
      break;
    case tc::CommandType::Boot:
      printBootReport();
      break;
    case tc::CommandType::Close:
    case tc::CommandType::Throw:
    case tc::CommandType::Toggle:
      applyToChannels(command.channels, command.type);
      break;
    case tc::CommandType::Cycle:
      for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
        if (command.channels & (1u << (channel - 1))) g_cycle.channel = channel;
      }
      g_cycle.active = true;
      g_cycle.done = 0;
      g_cycle.count = command.count;
      g_cycle.intervalMs = command.intervalMs;
      g_cycle.lastMs = millis() - command.intervalMs;  // first toggle right away
      break;
    case tc::CommandType::FiveVolt:
      if (command.hasSwitch && !power::setFiveVoltOut(command.switchOn)) {
        Serial.println("PM1 did not accept the 5VOUT change");
      }
      printFiveVolt();
      break;
    case tc::CommandType::Hold:
      if (command.hasSwitch) bank::setHold(command.switchOn);
      Serial.printf("pin latch (hold): %s\n", onOff(bank::holdEnabled()));
      break;
    case tc::CommandType::Reset:
      resetNow(command);
    case tc::CommandType::Pm1:
      printReadings();
      break;
    case tc::CommandType::Pm1Buttons:
      printButtons();
      break;
    case tc::CommandType::Net:
      net::printStatus();
      break;
    case tc::CommandType::NetForget:
      net::forget();
      break;
    case tc::CommandType::Config:
      configure(command);
      break;
    case tc::CommandType::Watchdog:
      watchdogCommand(command);
      break;
    case tc::CommandType::Portal:
      if (command.hasSwitch) {
        if (command.switchOn) {
          portal::open(portal::Reason::Console);
        } else {
          portal::close();
        }
      }
      portal::printStatus();
      break;
  }
}

void runCycle() {
  if (!g_cycle.active || millis() - g_cycle.lastMs < g_cycle.intervalMs) return;
  g_cycle.lastMs = millis();
  applyToChannels(static_cast<uint16_t>(1u << (g_cycle.channel - 1)), tc::CommandType::Toggle);
  if (++g_cycle.done >= g_cycle.count) {
    g_cycle.active = false;
    Serial.println("cycle done");
  }
}

// Applies due changes, within the stagger and minimum interval.
void runScheduler() {
  tc::TurnoutState current[tc::kChannelCount];
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) current[channel - 1] = bank::state(channel);
  uint8_t channel;
  tc::TurnoutState target;
  while (g_scheduler.next(millis(), current, &channel, &target)) {
    bank::setState(channel, target);
    current[channel - 1] = target;
    ++g_appliedCount;
    stamp();
    Serial.printf("ch%u -> %s (%s)\n", channel, tc::stateName(target), net::turnoutName(channel));
    net::noteOutcome(net::Outcome::Applied);
  }
}

void handleJmriState(const net::Message& message) {
  stamp();
  if (message.jmriState != tc::JmriState::Offline) {
    Serial.printf("JMRI state \"%s\"%s\n", message.text, message.retained ? " (retained)" : "");
    // JMRI clears its OFFLINE (an empty message) when it connects again.
    // Re-reading the retained commands puts back anything the offline
    // policy changed, so the pins match JMRI's table again.
    if (!message.retained && g_jmriOffline) {
      g_jmriOffline = false;
      stamp();
      Serial.println("JMRI back: re-reading the retained turnout commands");
      net::rereadRetained();
    }
    return;
  }
  // A retained OFFLINE may be old news from before this connection.
  if (message.retained) {
    Serial.println("JMRI OFFLINE (retained): ignored");
    return;
  }
  g_jmriOffline = true;
  if (g_behaviour.offline == tc::OfflinePolicy::Hold) {
    Serial.println("JMRI OFFLINE: holding every turnout");
    return;
  }
  Serial.println("JMRI OFFLINE: all turnouts LOW (THROWN)");
  for (uint8_t channel = 1; channel <= tc::kChannelCount; ++channel) {
    g_scheduler.request(channel, tc::TurnoutState::Thrown);
  }
}

// Takes commands from JMRI. A repeat of the current state does nothing;
// UNKNOWN, INCONSISTENT and anything else are logged and ignored.
void handleMessages() {
  net::Message message;
  while (net::nextMessage(&message)) {
    if (message.kind == net::Message::Kind::JmriState) {
      handleJmriState(message);
      continue;
    }
    const char* name = net::turnoutName(message.channel);
    const char* retained = message.retained ? " (retained)" : "";
    if (message.payload != tc::JmriPayload::Closed && message.payload != tc::JmriPayload::Thrown) {
      stamp();
      Serial.printf("mqtt %s %s%s: ignored\n", name, tc::payloadName(message.payload), retained);
      net::noteOutcome(net::Outcome::Ignored);
      continue;
    }
    const tc::TurnoutState target =
        message.payload == tc::JmriPayload::Closed ? tc::TurnoutState::Closed : tc::TurnoutState::Thrown;
    stamp();
    if (!g_scheduler.isPending(message.channel) && bank::state(message.channel) == target) {
      Serial.printf("mqtt %s %s%s: ch%u unchanged\n", name, tc::stateName(target), retained, message.channel);
      net::noteOutcome(net::Outcome::Unchanged);
      continue;
    }
    Serial.printf("mqtt %s %s%s\n", name, tc::stateName(target), retained);
    const bool wasPending = g_scheduler.isPending(message.channel);
    const uint32_t appliedBefore = g_appliedCount;
    g_scheduler.request(message.channel, target);
    runScheduler();
    if (g_scheduler.isPending(message.channel)) {
      stamp();
      Serial.printf("ch%u waiting (stagger %u ms, interval %u ms)\n", message.channel, g_behaviour.staggerMs,
                    g_behaviour.minIntervalMs);
    } else if (wasPending && g_appliedCount == appliedBefore) {
      stamp();
      Serial.printf("ch%u waiting change dropped: already %s\n", message.channel, tc::stateName(target));
    }
  }
}

// Writes the levels to flash once they have been steady for a moment.
void saveLevelsWhenSteady() {
  const uint16_t levels = bank::levels();
  if (levels != g_lastLevels) {
    g_lastLevels = levels;
    g_levelsChangedMs = millis();
  }
  if ((!g_haveSavedLevels || g_lastLevels != g_savedLevels) && millis() - g_levelsChangedMs >= kSaveLevelsAfterMs) {
    if (settings::saveLevels(g_lastLevels)) {
      g_savedLevels = g_lastLevels;
      g_haveSavedLevels = true;
    } else {
      g_levelsChangedMs = millis();  // try again later
    }
  }
}

void printBehaviour() {
  Serial.printf("startup: %s\n", tc::startupLevelName(g_behaviour.startup));
  Serial.printf("offline: %s\n", tc::offlinePolicyName(g_behaviour.offline));
  Serial.printf("stagger: %u ms\n", g_behaviour.staggerMs);
  Serial.printf("interval: %u ms\n", g_behaviour.minIntervalMs);
  if (g_haveSavedLevels) {
    printLevels("levels saved in flash:", g_savedLevels);
  } else {
    Serial.println("levels saved in flash: none yet");
  }
}

void configure(const tc::Command& command) {
  switch (command.configKey) {
    case tc::ConfigKey::Show:
      printBehaviour();
      return;
    case tc::ConfigKey::Startup:
      g_behaviour.startup = static_cast<tc::StartupLevel>(command.configValue);
      break;
    case tc::ConfigKey::Offline:
      g_behaviour.offline = static_cast<tc::OfflinePolicy>(command.configValue);
      break;
    case tc::ConfigKey::Stagger:
      g_behaviour.staggerMs = static_cast<uint16_t>(command.configValue);
      break;
    case tc::ConfigKey::Interval:
      g_behaviour.minIntervalMs = static_cast<uint16_t>(command.configValue);
      break;
  }
  g_scheduler.configure(g_behaviour.staggerMs, g_behaviour.minIntervalMs);
  if (!settings::saveBehaviour(g_behaviour)) Serial.println("could not write to flash; setting applies until power-off");
  printBehaviour();
}

const char* watchdogModeName() {
  switch (g_watchdogMode) {
    case WatchdogMode::Off: return "off";
    case WatchdogMode::On: return "on, fed every second";
    case WatchdogMode::Hang: return "on, NOT fed (hang test)";
  }
  return "";
}

void watchdogCommand(const tc::Command& command) {
  switch (command.watchdog) {
    case tc::WatchdogAction::Show:
      break;
    case tc::WatchdogAction::Off:
      if (power::watchdogSet(0)) g_watchdogMode = WatchdogMode::Off;
      break;
    case tc::WatchdogAction::Set:
      g_watchdogS = static_cast<uint8_t>(command.watchdogS);
      if (power::watchdogSet(g_watchdogS)) {
        g_watchdogMode = WatchdogMode::On;
        g_lastFeedMs = millis();
      }
      break;
    case tc::WatchdogAction::Hang:
      if (g_watchdogMode == WatchdogMode::Off) {
        Serial.println("wdt hang: turn the watchdog on first (e.g. wdt 10)");
        return;
      }
      g_watchdogMode = WatchdogMode::Hang;
      stamp();
      Serial.printf("wdt: no longer fed; the PM1 should reset the node within %u s\n", g_watchdogS);
      return;
  }
  uint8_t left = 0;
  const bool haveCount = power::watchdogCount(&left);
  Serial.printf("watchdog: %s", watchdogModeName());
  if (g_watchdogMode != WatchdogMode::Off) Serial.printf(", timeout %u s", g_watchdogS);
  if (haveCount) Serial.printf(", PM1 count %u s", left);
  Serial.println();
}

void feedWatchdog() {
  if (g_watchdogMode != WatchdogMode::On || millis() - g_lastFeedMs < kWatchdogFeedMs) return;
  g_lastFeedMs = millis();
  power::watchdogFeed();
}

void updateLed() {
  if (!g_ledReady) return;
  tc::HealthInputs inputs;
  inputs.portalOpen = portal::isOpen();
  inputs.netOff = net::isOff();
  inputs.wifiUp = net::wifiUp();
  inputs.mqttUp = net::mqttUp();
  inputs.jmriOffline = g_jmriOffline;
  const tc::LedState state = tc::ledStateFor(inputs);
  if (!g_ledStateKnown || state != g_ledState) {
    g_ledState = state;
    g_ledStateKnown = true;
    stamp();
    Serial.printf("LED: %s\n", tc::ledStateName(state));
  }
  const tc::Rgb colour = tc::ledColour(state, millis());
  if (!g_ledShownKnown || colour != g_ledShown) {
    if (power::setLed(colour.r, colour.g, colour.b)) {
      g_ledShown = colour;
      g_ledShownKnown = true;
    }
  }
}

void pollButton() {
  if (millis() - g_lastButtonPollMs < kButtonPollMs) return;
  g_lastButtonPollMs = millis();
  bool pressed = false;
  if (!power::buttonPressed(&pressed)) return;
  switch (g_button.update(pressed, millis())) {
    case tc::ButtonTracker::Event::None:
      break;
    case tc::ButtonTracker::Event::Pressed:
      stamp();
      Serial.println("button pressed");
      break;
    case tc::ButtonTracker::Event::LongPress:
      stamp();
      if (portal::isOpen()) {
        Serial.println("button long press: setup access point already open");
      } else {
        Serial.println("button long press: opening the setup access point");
        portal::open(portal::Reason::Button);
      }
      break;
    case tc::ButtonTracker::Event::Released:
      stamp();
      Serial.printf("button released after %lu ms\n", static_cast<unsigned long>(g_button.lastHeldMs()));
      break;
  }
}

}  // namespace

namespace app {

void setTurnoutLocal(uint8_t channel, tc::TurnoutState state) {
  if (channel < 1 || channel > tc::kChannelCount) return;
  bank::setState(channel, state);
  g_scheduler.noteApplied(channel, millis());
  stamp();
  Serial.printf("web ch%u -> %s (local)\n", channel, tc::stateName(state));
}

bool isPending(uint8_t channel) { return g_scheduler.isPending(channel); }

const tc::Behaviour& behaviour() { return g_behaviour; }

bool setBehaviour(const tc::Behaviour& behaviour) {
  g_behaviour = behaviour;
  g_scheduler.configure(g_behaviour.staggerMs, g_behaviour.minIntervalMs);
  stamp();
  Serial.printf("web: behaviour startup %s, offline %s, stagger %u ms, interval %u ms\n",
                tc::startupLevelName(g_behaviour.startup), tc::offlinePolicyName(g_behaviour.offline),
                g_behaviour.staggerMs, g_behaviour.minIntervalMs);
  return settings::saveBehaviour(g_behaviour);
}

void factoryReset() {
  stamp();
  Serial.println("factory reset: erasing saved settings; pins keep their levels");
  // net::forget() stops MQTT first, so the turnout names can be reset.
  net::forget();
  settings::factoryReset();
  g_behaviour = tc::defaultBehaviour();
  g_scheduler.configure(g_behaviour.staggerMs, g_behaviour.minIntervalMs);
  g_haveSavedLevels = false;  // the current levels are saved again shortly
}

}  // namespace app

namespace {

void readConsole() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (g_lineOverflow) {
        Serial.println("error: line too long");
      } else if (g_lineLength > 0) {
        g_line[g_lineLength] = '\0';
        const tc::ParseResult result = tc::parseCommand(g_line);
        if (result.ok) {
          execute(result.command);
        } else if (result.error != nullptr) {
          Serial.printf("error: %s\n", result.error);
        }
      }
      g_lineLength = 0;
      g_lineOverflow = false;
    } else if (g_lineLength < tc::kMaxLineLength) {
      g_line[g_lineLength++] = c;
    } else {
      g_lineOverflow = true;
    }
  }
}

}  // namespace

// Arduino-ESP32 calls initVariant() from initArduino(), before setup():
// flash is readable, 5VOUT not yet on. The pins were already driven once
// from a constructor (turnout_bank.cpp); the startup policy sets them now.
extern "C" void initVariant() {
  g_behaviour = settings::loadBehaviour();
  g_haveSavedLevels = settings::loadLevels(&g_savedLevels);
  bank::applyStartup(g_behaviour.startup, g_haveSavedLevels, g_savedLevels);
}

void setup() {
  Serial.begin(115200);
  g_scheduler.configure(g_behaviour.staggerMs, g_behaviour.minIntervalMs);
  g_lastLevels = bank::levels();
  g_levelsChangedMs = millis();

  // Pins are already at their levels, so turning 5VOUT on now can't make
  // a THROWN turnout pulse.
  uint8_t watchdogLeft = 0;
  bool watchdogWasOn = false;
  if (power::begin()) {
    g_fiveVoltKnown = power::fiveVoltOut(&g_fiveVoltWasOn);
    if (g_fiveVoltKnown && !g_fiveVoltWasOn) power::setFiveVoltOut(true);
    // The PM1 keeps running through an ESP32 reset: a watchdog left on by
    // a "wdt" before it would reset this firmware too, over and over.
    watchdogWasOn = power::watchdogCount(&watchdogLeft) && watchdogLeft > 0;
    power::watchdogSet(0);
    power::disableDoubleClickOff();
    g_ledReady = power::ledBegin();
  }

  const uint32_t start = millis();
  while (!Serial && millis() - start < kSerialWaitMs) delay(10);
  printBootReport();
  if (watchdogWasOn) Serial.printf("PM1 watchdog was running (%u s left); turned off\n", watchdogLeft);
  if (!g_ledReady) Serial.println("status LED: not available (PM1)");
  Serial.println("type help for commands");
  net::begin(kFirmwareVersion);
}

void loop() {
  readConsole();
  runCycle();
  handleMessages();
  runScheduler();
  saveLevelsWhenSteady();
  net::loop();
  portal::loop();
  web::loop();
  updateLed();
  pollButton();
  feedWatchdog();
  delay(1);
}
