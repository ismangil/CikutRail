// Unit tests for the hardware-free turnout core. Run with: pio test -e native
#include <string.h>
#include <unity.h>

#include "channels.h"
#include "command.h"
#include "jmri_protocol.h"
#include "level_snapshot.h"
#include "turnout_state.h"

using namespace tc;

void setUp() {}
void tearDown() {}

// --- channels ---

void test_default_channel_table_is_valid() {
  const char* error = "unset";
  TEST_ASSERT_TRUE(validateChannelGpios(kChannelGpio, kChannelCount, &error));
  TEST_ASSERT_NULL(error);
  for (uint8_t i = 0; i < kChannelCount; ++i) TEST_ASSERT_EQUAL_UINT8(i + 1, kChannelGpio[i]);
}

void test_forbidden_gpios() {
  const uint8_t forbidden[] = {0, 19, 20, 22, 26, 33, 37, 43, 44, 45, 46, 47, 48, 49, 255};
  for (uint8_t gpio : forbidden) TEST_ASSERT_TRUE_MESSAGE(isForbiddenGpio(gpio), "should be forbidden");
  const uint8_t allowed[] = {1, 3, 11, 12, 18, 21, 38, 42};
  for (uint8_t gpio : allowed) TEST_ASSERT_FALSE_MESSAGE(isForbiddenGpio(gpio), "should be allowed");
}

void test_channel_table_rejects_forbidden_and_duplicates() {
  const char* error = nullptr;
  const uint8_t withI2c[] = {1, 2, 47};
  TEST_ASSERT_FALSE(validateChannelGpios(withI2c, 3, &error));
  TEST_ASSERT_NOT_NULL(error);
  const uint8_t duplicate[] = {1, 2, 1};
  TEST_ASSERT_FALSE(validateChannelGpios(duplicate, 3, &error));
  TEST_ASSERT_NOT_NULL(error);
}

// --- turnout state ---

void test_levels_match_jmri_pi_gpio() {
  TEST_ASSERT_TRUE(pinLevelFor(TurnoutState::Closed));
  TEST_ASSERT_FALSE(pinLevelFor(TurnoutState::Thrown));
  TEST_ASSERT_TRUE(stateForPinLevel(true) == TurnoutState::Closed);
  TEST_ASSERT_TRUE(stateForPinLevel(false) == TurnoutState::Thrown);
  TEST_ASSERT_EQUAL_STRING("CLOSED", stateName(TurnoutState::Closed));
  TEST_ASSERT_EQUAL_STRING("THROWN", stateName(TurnoutState::Thrown));
}

// --- channel lists ---

void test_channel_list_forms() {
  uint16_t mask = 0;
  TEST_ASSERT_TRUE(parseChannelList("3", &mask));
  TEST_ASSERT_EQUAL_HEX16(0x0004, mask);
  TEST_ASSERT_TRUE(parseChannelList("1,4,7", &mask));
  TEST_ASSERT_EQUAL_HEX16(0x0049, mask);
  TEST_ASSERT_TRUE(parseChannelList("2-5", &mask));
  TEST_ASSERT_EQUAL_HEX16(0x001E, mask);
  TEST_ASSERT_TRUE(parseChannelList("1-3,11", &mask));
  TEST_ASSERT_EQUAL_HEX16(0x0407, mask);
  TEST_ASSERT_TRUE(parseChannelList("ALL", &mask));
  TEST_ASSERT_EQUAL_HEX16(kAllChannelsMask, mask);
}

void test_channel_list_rejects_bad_input() {
  uint16_t mask = 0xBEEF;
  const char* bad[] = {"", "0", "12", "5-3", "1,", ",1", "1--2", "a", "-1", "1-", "99999999999", "3 4"};
  for (const char* text : bad) {
    TEST_ASSERT_FALSE_MESSAGE(parseChannelList(text, &mask), text);
  }
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, mask);  // untouched on failure
}

// --- commands ---

void test_simple_commands() {
  TEST_ASSERT_TRUE(parseCommand("help").command.type == CommandType::Help);
  TEST_ASSERT_TRUE(parseCommand("?").command.type == CommandType::Help);
  TEST_ASSERT_TRUE(parseCommand("  Status ").command.type == CommandType::Status);
  TEST_ASSERT_TRUE(parseCommand("s").command.type == CommandType::Status);
  TEST_ASSERT_TRUE(parseCommand("PM1").command.type == CommandType::Pm1);
  TEST_ASSERT_TRUE(parseCommand("Boot").command.type == CommandType::Boot);
  TEST_ASSERT_TRUE(parseCommand("pm1 BTN").command.type == CommandType::Pm1Buttons);
  TEST_ASSERT_FALSE(parseCommand("pm1 btn now").ok);
  TEST_ASSERT_FALSE(parseCommand("pm1 led").ok);
  TEST_ASSERT_TRUE(parseCommand("NET").command.type == CommandType::Net);
  TEST_ASSERT_FALSE(parseCommand("net up").ok);
  TEST_ASSERT_FALSE(parseCommand("boot again").ok);
  TEST_ASSERT_FALSE(parseCommand("status now").ok);
}

void test_empty_line_is_silent_error() {
  ParseResult result = parseCommand("   ");
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_NULL(result.error);
}

void test_close_throw_toggle() {
  ParseResult result = parseCommand("close 1-3");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.type == CommandType::Close);
  TEST_ASSERT_EQUAL_HEX16(0x0007, result.command.channels);

  result = parseCommand("t all");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.type == CommandType::Throw);
  TEST_ASSERT_EQUAL_HEX16(kAllChannelsMask, result.command.channels);

  result = parseCommand("toggle 11");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.type == CommandType::Toggle);
  TEST_ASSERT_EQUAL_HEX16(0x0400, result.command.channels);

  TEST_ASSERT_FALSE(parseCommand("close").ok);
  TEST_ASSERT_FALSE(parseCommand("close 12").ok);
  TEST_ASSERT_FALSE(parseCommand("close 1 2").ok);
}

void test_cycle_limits() {
  ParseResult result = parseCommand("cycle 4 10 500");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.type == CommandType::Cycle);
  TEST_ASSERT_EQUAL_HEX16(0x0008, result.command.channels);
  TEST_ASSERT_EQUAL_UINT32(10, result.command.count);
  TEST_ASSERT_EQUAL_UINT32(500, result.command.intervalMs);

  TEST_ASSERT_TRUE(parseCommand("cycle 1 100 250").ok);
  TEST_ASSERT_TRUE(parseCommand("cycle 1 1 60000").ok);
  TEST_ASSERT_FALSE(parseCommand("cycle 1 0 500").ok);
  TEST_ASSERT_FALSE(parseCommand("cycle 1 101 500").ok);
  TEST_ASSERT_FALSE(parseCommand("cycle 1 10 249").ok);
  TEST_ASSERT_FALSE(parseCommand("cycle 1 10 60001").ok);
  TEST_ASSERT_FALSE(parseCommand("cycle 1-2 10 500").ok);
  TEST_ASSERT_FALSE(parseCommand("cycle 1 10").ok);
}

void test_switch_commands() {
  ParseResult result = parseCommand("5v");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.type == CommandType::FiveVolt);
  TEST_ASSERT_FALSE(result.command.hasSwitch);

  result = parseCommand("5V On");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.hasSwitch);
  TEST_ASSERT_TRUE(result.command.switchOn);

  result = parseCommand("hold off");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.type == CommandType::Hold);
  TEST_ASSERT_TRUE(result.command.hasSwitch);
  TEST_ASSERT_FALSE(result.command.switchOn);

  TEST_ASSERT_FALSE(parseCommand("hold maybe").ok);
  TEST_ASSERT_FALSE(parseCommand("5v on now").ok);
}

void test_reset_commands() {
  ParseResult result = parseCommand("reset soft");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.type == CommandType::Reset);
  TEST_ASSERT_TRUE(result.command.resetKind == ResetKind::Soft);
  TEST_ASSERT_FALSE(result.command.fiveVoltOffFirst);

  result = parseCommand("reset wdt 5v-off");
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(result.command.resetKind == ResetKind::Watchdog);
  TEST_ASSERT_TRUE(result.command.fiveVoltOffFirst);

  TEST_ASSERT_TRUE(parseCommand("reset panic").command.resetKind == ResetKind::Panic);
  TEST_ASSERT_FALSE(parseCommand("reset").ok);
  TEST_ASSERT_FALSE(parseCommand("reset hard").ok);
  TEST_ASSERT_FALSE(parseCommand("reset soft now").ok);
}

void test_unknown_and_oversized_lines() {
  ParseResult result = parseCommand("fly");
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_NOT_NULL(result.error);

  char longLine[kMaxLineLength + 2];
  memset(longLine, 'a', sizeof(longLine) - 1);
  longLine[sizeof(longLine) - 1] = '\0';
  TEST_ASSERT_FALSE(parseCommand(longLine).ok);

  TEST_ASSERT_FALSE(parseCommand("close 1 2 3 4 5 6").ok);  // too many words
}

// --- level snapshot ---

void test_snapshot_round_trip() {
  LevelSnapshot snapshot;
  snapshotWrite(&snapshot, 0x0555, true, 2);
  TEST_ASSERT_TRUE(snapshotValid(snapshot));
  TEST_ASSERT_EQUAL_HEX16(0x0555, snapshot.levels);
  TEST_ASSERT_EQUAL_UINT8(1, snapshot.holdEnabled);
  TEST_ASSERT_EQUAL_UINT8(2, snapshot.plannedReset);
}

void test_snapshot_masks_unused_bits() {
  LevelSnapshot snapshot;
  snapshotWrite(&snapshot, 0xFFFF, false, 0);
  TEST_ASSERT_EQUAL_HEX16(kAllChannelsMask, snapshot.levels);
  TEST_ASSERT_TRUE(snapshotValid(snapshot));
}

void test_snapshot_detects_corruption() {
  LevelSnapshot snapshot;
  snapshotWrite(&snapshot, 0x0123, true, 0);

  LevelSnapshot bad = snapshot;
  bad.levels ^= 0x0001;
  TEST_ASSERT_FALSE(snapshotValid(bad));

  bad = snapshot;
  bad.magic = 0;
  TEST_ASSERT_FALSE(snapshotValid(bad));

  bad = snapshot;
  bad.holdEnabled = 0;
  TEST_ASSERT_FALSE(snapshotValid(bad));

  LevelSnapshot garbage;
  memset(&garbage, 0xA5, sizeof(garbage));
  TEST_ASSERT_FALSE(snapshotValid(garbage));
}


// --- JMRI protocol ---

const char* const kNames[kChannelCount] = {"101", "102", "103", "104", "105", "106",
                                           "107", "108", "109", "110", ""};

uint8_t match(const char* channel, const char* topic) {
  return matchTurnoutTopic(channel, kNames, kChannelCount, topic, strlen(topic));
}

void test_payloads_match_jmri_exactly() {
  TEST_ASSERT_TRUE(parseTurnoutPayload("CLOSED", 6) == JmriPayload::Closed);
  TEST_ASSERT_TRUE(parseTurnoutPayload("THROWN", 6) == JmriPayload::Thrown);
  TEST_ASSERT_TRUE(parseTurnoutPayload("UNKNOWN", 7) == JmriPayload::Unknown);
  TEST_ASSERT_TRUE(parseTurnoutPayload("INCONSISTENT", 12) == JmriPayload::Inconsistent);
  // Not NUL-terminated: esp-mqtt passes a length.
  TEST_ASSERT_TRUE(parseTurnoutPayload("THROWNxyz", 6) == JmriPayload::Thrown);
  TEST_ASSERT_TRUE(parseTurnoutPayload("closed", 6) == JmriPayload::Other);
  TEST_ASSERT_TRUE(parseTurnoutPayload("CLOSED ", 7) == JmriPayload::Other);
  TEST_ASSERT_TRUE(parseTurnoutPayload("", 0) == JmriPayload::Other);
  TEST_ASSERT_TRUE(parseTurnoutPayload(nullptr, 0) == JmriPayload::Other);
  TEST_ASSERT_EQUAL_STRING("INCONSISTENT", payloadName(JmriPayload::Inconsistent));
}

void test_channel_and_name_rules() {
  TEST_ASSERT_TRUE(validJmriChannel(""));
  TEST_ASSERT_TRUE(validJmriChannel("/trains/"));
  TEST_ASSERT_TRUE(validJmriChannel("layout/"));
  TEST_ASSERT_FALSE(validJmriChannel("/trains"));
  TEST_ASSERT_FALSE(validJmriChannel("a+/"));
  TEST_ASSERT_FALSE(validJmriChannel("a #/"));
  TEST_ASSERT_FALSE(validJmriChannel(nullptr));

  TEST_ASSERT_TRUE(validTurnoutName("101"));
  TEST_ASSERT_TRUE(validTurnoutName("yard_2-a"));
  TEST_ASSERT_FALSE(validTurnoutName(""));
  TEST_ASSERT_FALSE(validTurnoutName("1/2"));
  TEST_ASSERT_FALSE(validTurnoutName("+"));
  TEST_ASSERT_FALSE(validTurnoutName("12345678901234567"));
  TEST_ASSERT_TRUE(validNodeName("turnout1"));
  TEST_ASSERT_FALSE(validNodeName("turnout 1"));
}

void test_turnout_name_table() {
  const char* error = "unset";
  TEST_ASSERT_TRUE(validateTurnoutNames(kNames, kChannelCount, &error));
  TEST_ASSERT_NULL(error);
  const char* const duplicate[] = {"101", "", "101"};
  TEST_ASSERT_FALSE(validateTurnoutNames(duplicate, 3, &error));
  TEST_ASSERT_NOT_NULL(error);
  const char* const bad[] = {"101", "1#"};
  TEST_ASSERT_FALSE(validateTurnoutNames(bad, 2, &error));
  const char* const unused[] = {"", nullptr, ""};
  TEST_ASSERT_TRUE(validateTurnoutNames(unused, 3, nullptr));
}

void test_turnout_topic() {
  char topic[kMaxTopicLength + 1];
  TEST_ASSERT_TRUE(turnoutTopic("", "101", topic, sizeof(topic)));
  TEST_ASSERT_EQUAL_STRING("track/turnout/101", topic);
  TEST_ASSERT_TRUE(turnoutTopic("/trains/", "101", topic, sizeof(topic)));
  TEST_ASSERT_EQUAL_STRING("/trains/track/turnout/101", topic);
  char small[17];
  TEST_ASSERT_FALSE(turnoutTopic("", "101", small, sizeof(small)));
}

void test_topic_matching() {
  TEST_ASSERT_EQUAL_UINT8(1, match("", "track/turnout/101"));
  TEST_ASSERT_EQUAL_UINT8(10, match("", "track/turnout/110"));
  TEST_ASSERT_EQUAL_UINT8(3, match("/trains/", "/trains/track/turnout/103"));
  TEST_ASSERT_EQUAL_UINT8(0, match("", "/trains/track/turnout/103"));
  TEST_ASSERT_EQUAL_UINT8(0, match("/trains/", "track/turnout/103"));
  TEST_ASSERT_EQUAL_UINT8(0, match("", "track/turnout/101/state"));
  TEST_ASSERT_EQUAL_UINT8(0, match("", "track/turnout/10"));
  TEST_ASSERT_EQUAL_UINT8(0, match("", "track/turnout/1011"));
  TEST_ASSERT_EQUAL_UINT8(0, match("", "track/turnout/"));
  TEST_ASSERT_EQUAL_UINT8(0, match("", "track/sensor/101"));
  // Channel 11 has no name, so nothing maps to it.
  TEST_ASSERT_EQUAL_UINT8(0, match("", "track/turnout/"));
  // Topic given by length, not NUL-terminated.
  const char buffer[] = "track/turnout/102junk";
  TEST_ASSERT_EQUAL_UINT8(2, matchTurnoutTopic("", kNames, kChannelCount, buffer, 17));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_channel_table_is_valid);
  RUN_TEST(test_forbidden_gpios);
  RUN_TEST(test_channel_table_rejects_forbidden_and_duplicates);
  RUN_TEST(test_levels_match_jmri_pi_gpio);
  RUN_TEST(test_channel_list_forms);
  RUN_TEST(test_channel_list_rejects_bad_input);
  RUN_TEST(test_simple_commands);
  RUN_TEST(test_empty_line_is_silent_error);
  RUN_TEST(test_close_throw_toggle);
  RUN_TEST(test_cycle_limits);
  RUN_TEST(test_switch_commands);
  RUN_TEST(test_reset_commands);
  RUN_TEST(test_unknown_and_oversized_lines);
  RUN_TEST(test_snapshot_round_trip);
  RUN_TEST(test_snapshot_masks_unused_bits);
  RUN_TEST(test_snapshot_detects_corruption);
  RUN_TEST(test_payloads_match_jmri_exactly);
  RUN_TEST(test_channel_and_name_rules);
  RUN_TEST(test_turnout_name_table);
  RUN_TEST(test_turnout_topic);
  RUN_TEST(test_topic_matching);
  return UNITY_END();
}
