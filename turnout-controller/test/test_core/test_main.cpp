// Unit tests for the hardware-free turnout core. Run with: pio test -e native
#include <string.h>
#include <unity.h>

#include "behaviour.h"
#include "channels.h"
#include "command.h"
#include "jmri_protocol.h"
#include "level_snapshot.h"
#include "net_config.h"
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
  TEST_ASSERT_TRUE(parseCommand("net Forget").command.type == CommandType::NetForget);
  ParseResult portal = parseCommand("portal on");
  TEST_ASSERT_TRUE(portal.ok && portal.command.type == CommandType::Portal && portal.command.hasSwitch &&
                   portal.command.switchOn);
  TEST_ASSERT_FALSE(parseCommand("portal").command.hasSwitch);
  TEST_ASSERT_FALSE(parseCommand("portal maybe").ok);
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


// --- network settings ---

NetConfig savedConfig() {
  NetConfig config;
  clearNetConfig(&config);
  copyField(config.wifiSsid, sizeof(config.wifiSsid), "home");
  copyField(config.wifiPassword, sizeof(config.wifiPassword), "wifisecret");
  copyField(config.mqttHost, sizeof(config.mqttHost), "192.168.0.198");
  copyField(config.mqttUser, sizeof(config.mqttUser), "jmri");
  copyField(config.mqttPassword, sizeof(config.mqttPassword), "mqttsecret");
  return config;
}

NetForm formFrom(const NetConfig& c) {
  NetForm form = {c.wifiSsid, "", c.mqttHost, "1883", c.mqttUser, "", c.jmriChannel, c.nodeName};
  return form;
}

void test_net_config_defaults_and_fields() {
  NetConfig config;
  clearNetConfig(&config);
  TEST_ASSERT_EQUAL_UINT16(1883, config.mqttPort);
  TEST_ASSERT_EQUAL_STRING("turnout1", config.nodeName);
  TEST_ASSERT_EQUAL_STRING("", config.wifiSsid);
  const char* error = nullptr;
  TEST_ASSERT_FALSE(validateNetConfig(config, &error));  // no network yet
  TEST_ASSERT_NOT_NULL(error);

  char small[4];
  TEST_ASSERT_TRUE(copyField(small, sizeof(small), "abc"));
  TEST_ASSERT_FALSE(copyField(small, sizeof(small), "abcd"));
  TEST_ASSERT_EQUAL_STRING("", small);
  TEST_ASSERT_TRUE(copyField(small, sizeof(small), nullptr));
  TEST_ASSERT_EQUAL_STRING("", small);
}

void test_port_and_host_rules() {
  uint16_t port = 0;
  TEST_ASSERT_TRUE(parsePort("1883", &port));
  TEST_ASSERT_EQUAL_UINT16(1883, port);
  TEST_ASSERT_TRUE(parsePort("65535", &port));
  TEST_ASSERT_FALSE(parsePort("0", &port));
  TEST_ASSERT_FALSE(parsePort("65536", &port));
  TEST_ASSERT_FALSE(parsePort("-1", &port));
  TEST_ASSERT_FALSE(parsePort("18 83", &port));
  TEST_ASSERT_FALSE(parsePort("", &port));
  TEST_ASSERT_FALSE(parsePort(nullptr, &port));

  TEST_ASSERT_TRUE(validHost("192.168.0.198"));
  TEST_ASSERT_TRUE(validHost("broker-1.lan"));
  TEST_ASSERT_FALSE(validHost(""));
  TEST_ASSERT_FALSE(validHost("mqtt://host"));
  TEST_ASSERT_FALSE(validHost("two words"));
  TEST_ASSERT_FALSE(validHost(".lan"));
  TEST_ASSERT_FALSE(validHost("host."));
  TEST_ASSERT_FALSE(validHost("a..b"));
}

void test_net_config_validation() {
  NetConfig config = savedConfig();
  const char* error = "unset";
  TEST_ASSERT_TRUE(validateNetConfig(config, &error));
  TEST_ASSERT_NULL(error);

  copyField(config.wifiPassword, sizeof(config.wifiPassword), "short");
  TEST_ASSERT_FALSE(validateNetConfig(config, &error));
  copyField(config.wifiPassword, sizeof(config.wifiPassword), "");  // open network
  TEST_ASSERT_TRUE(validateNetConfig(config, nullptr));

  config = savedConfig();
  copyField(config.mqttUser, sizeof(config.mqttUser), "");
  TEST_ASSERT_FALSE(validateNetConfig(config, &error));  // password without user
  copyField(config.mqttPassword, sizeof(config.mqttPassword), "");
  TEST_ASSERT_TRUE(validateNetConfig(config, nullptr));  // anonymous broker

  config = savedConfig();
  copyField(config.jmriChannel, sizeof(config.jmriChannel), "/trains");
  TEST_ASSERT_FALSE(validateNetConfig(config, &error));
  config = savedConfig();
  copyField(config.nodeName, sizeof(config.nodeName), "turnout 1");
  TEST_ASSERT_FALSE(validateNetConfig(config, &error));
}

void test_form_keeps_saved_passwords() {
  const NetConfig saved = savedConfig();
  NetConfig out;
  const char* error = nullptr;
  NetForm form = formFrom(saved);
  TEST_ASSERT_TRUE_MESSAGE(applyNetForm(saved, form, &out, &error), error);
  TEST_ASSERT_EQUAL_STRING("wifisecret", out.wifiPassword);
  TEST_ASSERT_EQUAL_STRING("mqttsecret", out.mqttPassword);

  form.wifiPassword = "newsecret1";
  form.mqttPassword = "newmqtt";
  TEST_ASSERT_TRUE(applyNetForm(saved, form, &out, &error));
  TEST_ASSERT_EQUAL_STRING("newsecret1", out.wifiPassword);
  TEST_ASSERT_EQUAL_STRING("newmqtt", out.mqttPassword);
}

void test_form_changed_network_or_user() {
  const NetConfig saved = savedConfig();
  NetConfig out;
  const char* error = nullptr;

  // A different network with a blank password is an open network.
  NetForm form = formFrom(saved);
  form.wifiSsid = "cafe";
  TEST_ASSERT_TRUE(applyNetForm(saved, form, &out, &error));
  TEST_ASSERT_EQUAL_STRING("cafe", out.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("", out.wifiPassword);

  // A different MQTT user doesn't inherit the saved password.
  form = formFrom(saved);
  form.mqttUser = "other";
  TEST_ASSERT_TRUE(applyNetForm(saved, form, &out, &error));
  TEST_ASSERT_EQUAL_STRING("", out.mqttPassword);

  // No user: anonymous, even if a password was typed.
  form = formFrom(saved);
  form.mqttUser = "";
  form.mqttPassword = "typed";
  TEST_ASSERT_TRUE(applyNetForm(saved, form, &out, &error));
  TEST_ASSERT_EQUAL_STRING("", out.mqttUser);
  TEST_ASSERT_EQUAL_STRING("", out.mqttPassword);
}

void test_form_first_setup_and_errors() {
  NetConfig empty;
  clearNetConfig(&empty);
  NetConfig out;
  const char* error = nullptr;
  NetForm form = {"home", "wifisecret", "  192.168.0.198 ", "1883", "", "", "", "turnout1"};
  TEST_ASSERT_TRUE_MESSAGE(applyNetForm(empty, form, &out, &error), error);
  TEST_ASSERT_EQUAL_STRING("192.168.0.198", out.mqttHost);
  TEST_ASSERT_EQUAL_UINT16(1883, out.mqttPort);

  NetForm bad = form;
  bad.mqttPort = "abc";
  TEST_ASSERT_FALSE(applyNetForm(empty, bad, &out, &error));
  TEST_ASSERT_NOT_NULL(error);
  bad = form;
  bad.wifiSsid = "";
  TEST_ASSERT_FALSE(applyNetForm(empty, bad, &out, &error));
  bad = form;
  bad.wifiSsid = "123456789012345678901234567890123";  // 33
  TEST_ASSERT_FALSE(applyNetForm(empty, bad, &out, &error));
  bad = form;
  bad.mqttHost = nullptr;
  TEST_ASSERT_FALSE(applyNetForm(empty, bad, &out, &error));
  // A failed form leaves *out alone.
  NetConfig untouched = savedConfig();
  TEST_ASSERT_FALSE(applyNetForm(empty, bad, &untouched, &error));
  TEST_ASSERT_EQUAL_STRING("home", untouched.wifiSsid);
}

void test_html_escape() {
  char out[32];
  TEST_ASSERT_TRUE(htmlEscape("a<b>&\"c'", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("a&lt;b&gt;&amp;&quot;c&#39;", out);
  TEST_ASSERT_TRUE(htmlEscape("", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  char small[5];
  TEST_ASSERT_FALSE(htmlEscape("&", small, sizeof(small)));  // "&amp;" + NUL needs 6
  TEST_ASSERT_EQUAL_STRING("", small);
  TEST_ASSERT_TRUE(htmlEscape("abcd", small, sizeof(small)));
}


// --- JMRI state ---

void test_jmri_state_topic() {
  char topic[kMaxTopicLength + 1];
  TEST_ASSERT_TRUE(jmriStateTopic("", topic, sizeof(topic)));
  TEST_ASSERT_EQUAL_STRING("track/state", topic);
  TEST_ASSERT_TRUE(jmriStateTopic("/trains/", topic, sizeof(topic)));
  TEST_ASSERT_EQUAL_STRING("/trains/track/state", topic);
  TEST_ASSERT_TRUE(isJmriStateTopic("", "track/state", 11));
  TEST_ASSERT_TRUE(isJmriStateTopic("/trains/", "/trains/track/state", 19));
  TEST_ASSERT_FALSE(isJmriStateTopic("", "/trains/track/state", 19));
  TEST_ASSERT_FALSE(isJmriStateTopic("", "track/state/x", 13));
  TEST_ASSERT_FALSE(isJmriStateTopic("", "track/turnout/101", 17));
  TEST_ASSERT_TRUE(parseJmriState("OFFLINE", 7) == JmriState::Offline);
  TEST_ASSERT_TRUE(parseJmriState("ONLINE", 6) == JmriState::Other);
  TEST_ASSERT_TRUE(parseJmriState("offline", 7) == JmriState::Other);
  TEST_ASSERT_TRUE(parseJmriState(nullptr, 0) == JmriState::Other);
}

// --- behaviour and scheduler ---

struct Bench {
  TurnoutScheduler scheduler;
  TurnoutState state[kChannelCount];
  Bench() {
    for (uint8_t i = 0; i < kChannelCount; ++i) state[i] = TurnoutState::Thrown;
  }
  // Runs the scheduler at nowMs; returns the channel changed, or 0.
  uint8_t step(uint32_t nowMs) {
    uint8_t channel = 0;
    TurnoutState target;
    if (!scheduler.next(nowMs, state, &channel, &target)) return 0;
    state[channel - 1] = target;
    return channel;
  }
};

void test_behaviour_defaults_match_the_pi() {
  const Behaviour b = defaultBehaviour();
  TEST_ASSERT_TRUE(b.startup == StartupLevel::Restore);
  TEST_ASSERT_TRUE(b.offline == OfflinePolicy::Hold);
  TEST_ASSERT_EQUAL_UINT16(0, b.staggerMs);
  TEST_ASSERT_EQUAL_UINT16(0, b.minIntervalMs);
  TEST_ASSERT_EQUAL_STRING("restore", startupLevelName(StartupLevel::Restore));
  TEST_ASSERT_EQUAL_STRING("low", offlinePolicyName(OfflinePolicy::Low));
}

void test_scheduler_without_limits_applies_everything_at_once() {
  Bench bench;
  bench.scheduler.request(1, TurnoutState::Closed);
  bench.scheduler.request(2, TurnoutState::Closed);
  TEST_ASSERT_EQUAL_UINT8(1, bench.step(100));
  TEST_ASSERT_EQUAL_UINT8(2, bench.step(100));
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(100));
  TEST_ASSERT_EQUAL_UINT8(0, bench.scheduler.pendingCount());
}

void test_scheduler_drops_requests_that_already_match() {
  Bench bench;
  bench.scheduler.request(1, TurnoutState::Thrown);  // already THROWN
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(0));
  TEST_ASSERT_FALSE(bench.scheduler.isPending(1));
}

void test_scheduler_stagger_spaces_changes_oldest_first() {
  Bench bench;
  bench.scheduler.configure(500, 0);
  bench.scheduler.request(3, TurnoutState::Closed);
  bench.scheduler.request(1, TurnoutState::Closed);
  bench.scheduler.request(2, TurnoutState::Closed);
  TEST_ASSERT_EQUAL_UINT8(3, bench.step(1000));
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(1499));
  TEST_ASSERT_EQUAL_UINT8(1, bench.step(1500));
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(1999));
  TEST_ASSERT_EQUAL_UINT8(2, bench.step(2000));
  TEST_ASSERT_EQUAL_UINT8(0, bench.scheduler.pendingCount());
}

void test_scheduler_minimum_interval_keeps_latest() {
  Bench bench;
  bench.scheduler.configure(0, 3000);
  bench.scheduler.request(1, TurnoutState::Closed);
  TEST_ASSERT_EQUAL_UINT8(1, bench.step(0));
  // A reversal during the interval waits; newer requests replace it.
  bench.scheduler.request(1, TurnoutState::Thrown);
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(1000));
  bench.scheduler.request(1, TurnoutState::Closed);  // back to the current state
  bench.scheduler.request(1, TurnoutState::Thrown);  // latest wins
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(2999));
  TEST_ASSERT_EQUAL_UINT8(1, bench.step(3000));
  TEST_ASSERT_TRUE(bench.state[0] == TurnoutState::Thrown);
  // Other channels aren't held back by channel 1's interval.
  bench.scheduler.request(1, TurnoutState::Closed);
  bench.scheduler.request(2, TurnoutState::Closed);
  TEST_ASSERT_EQUAL_UINT8(2, bench.step(3001));
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(3002));
  TEST_ASSERT_EQUAL_UINT8(1, bench.step(6000));
}

void test_scheduler_reversal_cancelled_in_interval_does_nothing() {
  Bench bench;
  bench.scheduler.configure(0, 3000);
  bench.scheduler.request(1, TurnoutState::Closed);
  TEST_ASSERT_EQUAL_UINT8(1, bench.step(0));
  bench.scheduler.request(1, TurnoutState::Thrown);
  bench.scheduler.request(1, TurnoutState::Closed);  // cancelled before it was due
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(5000));
  TEST_ASSERT_EQUAL_UINT8(0, bench.scheduler.pendingCount());
}

void test_scheduler_counts_console_changes() {
  Bench bench;
  bench.scheduler.configure(1000, 5000);
  bench.scheduler.noteApplied(1, 10000);
  bench.state[0] = TurnoutState::Closed;
  bench.scheduler.request(2, TurnoutState::Closed);
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(10500));  // stagger from the console change
  TEST_ASSERT_EQUAL_UINT8(2, bench.step(11000));
  bench.scheduler.request(1, TurnoutState::Thrown);
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(14999));  // channel 1's interval
  TEST_ASSERT_EQUAL_UINT8(1, bench.step(15000));
}

void test_scheduler_survives_millis_wrap() {
  Bench bench;
  bench.scheduler.configure(500, 0);
  bench.scheduler.request(1, TurnoutState::Closed);
  bench.scheduler.request(2, TurnoutState::Closed);
  TEST_ASSERT_EQUAL_UINT8(1, bench.step(0xFFFFFF00u));
  TEST_ASSERT_EQUAL_UINT8(0, bench.step(0x00000010u));  // 0x110 ms later
  TEST_ASSERT_EQUAL_UINT8(2, bench.step(0x00000200u));  // 0x300 ms later
}

void test_scheduler_ignores_bad_channels() {
  Bench bench;
  bench.scheduler.request(0, TurnoutState::Closed);
  bench.scheduler.request(kChannelCount + 1, TurnoutState::Closed);
  TEST_ASSERT_EQUAL_UINT8(0, bench.scheduler.pendingCount());
  TEST_ASSERT_FALSE(bench.scheduler.isPending(0));
}

void test_config_commands() {
  TEST_ASSERT_TRUE(parseCommand("config").command.type == CommandType::Config);
  TEST_ASSERT_TRUE(parseCommand("config").command.configKey == ConfigKey::Show);
  ParseResult r = parseCommand("config startup LOW");
  TEST_ASSERT_TRUE(r.ok && r.command.configKey == ConfigKey::Startup &&
                   r.command.configValue == static_cast<uint32_t>(StartupLevel::Low));
  r = parseCommand("config offline hold");
  TEST_ASSERT_TRUE(r.ok && r.command.configKey == ConfigKey::Offline &&
                   r.command.configValue == static_cast<uint32_t>(OfflinePolicy::Hold));
  r = parseCommand("config stagger 5000");
  TEST_ASSERT_TRUE(r.ok && r.command.configKey == ConfigKey::Stagger && r.command.configValue == 5000);
  r = parseCommand("config interval 0");
  TEST_ASSERT_TRUE(r.ok && r.command.configKey == ConfigKey::Interval && r.command.configValue == 0);
  TEST_ASSERT_FALSE(parseCommand("config stagger 5001").ok);
  TEST_ASSERT_FALSE(parseCommand("config interval 10001").ok);
  TEST_ASSERT_FALSE(parseCommand("config stagger -1").ok);
  TEST_ASSERT_FALSE(parseCommand("config startup maybe").ok);
  TEST_ASSERT_FALSE(parseCommand("config offline").ok);
  TEST_ASSERT_FALSE(parseCommand("config colour red").ok);
}


// --- config page forms ---

void test_default_turnout_names() {
  TurnoutNames names;
  defaultTurnoutNames(&names);
  TEST_ASSERT_EQUAL_STRING("101", names.name[0]);
  TEST_ASSERT_EQUAL_STRING("111", names.name[kChannelCount - 1]);
}

void test_parse_turnout_names() {
  const char* fields[kChannelCount] = {" 201 ", "202", "", "yard-a", nullptr, "", "", "", "", "", "211"};
  TurnoutNames names;
  const char* error = nullptr;
  TEST_ASSERT_TRUE_MESSAGE(parseTurnoutNames(fields, &names, &error), error);
  TEST_ASSERT_EQUAL_STRING("201", names.name[0]);
  TEST_ASSERT_EQUAL_STRING("", names.name[2]);
  TEST_ASSERT_EQUAL_STRING("yard-a", names.name[3]);
  TEST_ASSERT_EQUAL_STRING("", names.name[4]);

  const char* duplicate[kChannelCount] = {"201", "201", "", "", "", "", "", "", "", "", ""};
  TurnoutNames kept = names;
  TEST_ASSERT_FALSE(parseTurnoutNames(duplicate, &kept, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_EQUAL_STRING("201", kept.name[0]);  // unchanged on failure
  const char* bad[kChannelCount] = {"2 01", "", "", "", "", "", "", "", "", "", ""};
  TEST_ASSERT_FALSE(parseTurnoutNames(bad, &kept, &error));
  const char* longName[kChannelCount] = {"12345678901234567", "", "", "", "", "", "", "", "", "", ""};
  TEST_ASSERT_FALSE(parseTurnoutNames(longName, &kept, &error));
}

void test_parse_behaviour_form() {
  Behaviour b;
  const char* error = nullptr;
  TEST_ASSERT_TRUE_MESSAGE(parseBehaviourForm("low", "low", "250", "3000", &b, &error), error);
  TEST_ASSERT_TRUE(b.startup == StartupLevel::Low && b.offline == OfflinePolicy::Low);
  TEST_ASSERT_EQUAL_UINT16(250, b.staggerMs);
  TEST_ASSERT_EQUAL_UINT16(3000, b.minIntervalMs);
  TEST_ASSERT_TRUE(parseBehaviourForm("restore", "hold", "0", "0", &b, &error));
  TEST_ASSERT_TRUE(b.startup == StartupLevel::Restore && b.offline == OfflinePolicy::Hold);
  TEST_ASSERT_EQUAL_UINT16(0, b.staggerMs);
  TEST_ASSERT_TRUE(parseBehaviourForm("restore", "hold", "5000", "10000", &b, &error));
  TEST_ASSERT_FALSE(parseBehaviourForm("restore", "hold", "5001", "0", &b, &error));
  TEST_ASSERT_FALSE(parseBehaviourForm("restore", "hold", "0", "10001", &b, &error));
  TEST_ASSERT_FALSE(parseBehaviourForm("restore", "hold", "", "0", &b, &error));
  TEST_ASSERT_FALSE(parseBehaviourForm("restore", "hold", "-5", "0", &b, &error));
  TEST_ASSERT_FALSE(parseBehaviourForm("Restore", "hold", "0", "0", &b, &error));
  TEST_ASSERT_FALSE(parseBehaviourForm("restore", nullptr, "0", "0", &b, &error));
  TEST_ASSERT_NOT_NULL(error);
}

void test_admin_password_rules() {
  TEST_ASSERT_TRUE(validAdminPassword("k7mq9x2a"));
  TEST_ASSERT_TRUE(validAdminPassword("Pass-word_1!"));
  TEST_ASSERT_FALSE(validAdminPassword("short"));
  TEST_ASSERT_FALSE(validAdminPassword("has space1"));
  TEST_ASSERT_FALSE(validAdminPassword(""));
  TEST_ASSERT_FALSE(validAdminPassword(nullptr));
  char tooLong[kMaxAdminPasswordLength + 2];
  memset(tooLong, 'a', sizeof(tooLong) - 1);
  tooLong[sizeof(tooLong) - 1] = '\0';
  TEST_ASSERT_FALSE(validAdminPassword(tooLong));
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
  RUN_TEST(test_net_config_defaults_and_fields);
  RUN_TEST(test_port_and_host_rules);
  RUN_TEST(test_net_config_validation);
  RUN_TEST(test_form_keeps_saved_passwords);
  RUN_TEST(test_form_changed_network_or_user);
  RUN_TEST(test_form_first_setup_and_errors);
  RUN_TEST(test_html_escape);
  RUN_TEST(test_jmri_state_topic);
  RUN_TEST(test_behaviour_defaults_match_the_pi);
  RUN_TEST(test_scheduler_without_limits_applies_everything_at_once);
  RUN_TEST(test_scheduler_drops_requests_that_already_match);
  RUN_TEST(test_scheduler_stagger_spaces_changes_oldest_first);
  RUN_TEST(test_scheduler_minimum_interval_keeps_latest);
  RUN_TEST(test_scheduler_reversal_cancelled_in_interval_does_nothing);
  RUN_TEST(test_scheduler_counts_console_changes);
  RUN_TEST(test_scheduler_survives_millis_wrap);
  RUN_TEST(test_scheduler_ignores_bad_channels);
  RUN_TEST(test_config_commands);
  RUN_TEST(test_default_turnout_names);
  RUN_TEST(test_parse_turnout_names);
  RUN_TEST(test_parse_behaviour_form);
  RUN_TEST(test_admin_password_rules);
  return UNITY_END();
}
