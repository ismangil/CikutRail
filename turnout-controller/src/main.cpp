// Phase 1 bench firmware: drive the 11 turnout pins from USB serial
// commands and report how pins and 5VOUT behave across resets.
// See docs/PHASE1_BENCH.md for the test procedure.
#include <Arduino.h>
#include <esp_system.h>

#include "channels.h"
#include "command.h"
#include "power.h"
#include "turnout_bank.h"

namespace {

const uint32_t kSerialWaitMs = 1500;
const uint32_t kFiveVoltOffSettleMs = 500;
const char* const kFirmwareName = "CikutRail turnout-controller, phase 1 bench firmware";

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
  if (g_cycle.active) {
    Serial.printf("cycle: channel %u, %lu of %lu toggles\n", g_cycle.channel,
                  static_cast<unsigned long>(g_cycle.done), static_cast<unsigned long>(g_cycle.count));
  }
}

void printBootReport() {
  const bank::BootReport& boot = bank::bootReport();
  Serial.printf("\n%s\n", kFirmwareName);
  Serial.printf("reset reason: %s (ROM code 0x%02lx)\n", resetReasonName(boot.resetReason),
                static_cast<unsigned long>(boot.romResetReason));
  Serial.printf("console reset before this boot: %s\n", resetKindName(boot.plannedReset));
  Serial.printf("pin levels: %s\n", boot.restored ? "restored from RTC memory" : "cold start, all LOW (THROWN)");
  printLevels("pads read at boot, before driving:", boot.padLevelsAtBoot);
  Serial.printf("pins driven %lld us after app start\n", static_cast<long long>(boot.drivenAtUs));
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

// Arduino-ESP32 calls initVariant() from initArduino(), before setup().
// Driving the pins here keeps the time they float after a reset short.
extern "C" void initVariant() { bank::earlyInit(); }

void setup() {
  Serial.begin(115200);

  // Pins are already at their levels, so turning 5VOUT on now can't make
  // a THROWN turnout pulse.
  if (power::begin()) {
    g_fiveVoltKnown = power::fiveVoltOut(&g_fiveVoltWasOn);
    if (g_fiveVoltKnown && !g_fiveVoltWasOn) power::setFiveVoltOut(true);
  }

  const uint32_t start = millis();
  while (!Serial && millis() - start < kSerialWaitMs) delay(10);
  printBootReport();
  Serial.println("type help for commands");
}

void loop() {
  readConsole();
  runCycle();
  delay(1);
}
