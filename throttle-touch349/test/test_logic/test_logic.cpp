#include <unity.h>

#include "FunctionSlots.h"
#include "Speed.h"

using namespace FunctionSlots;

void setUp() {}
void tearDown() {}

static void test_hunslet() {
    const char *l[13] = {"Headlight", "", "", "", "", "", "", "", "", "",
                         "Uncouple", "", "Beacon"};
    int s[SLOTS];
    pick(l, 13, s);
    TEST_ASSERT_EQUAL(0, s[0]);
    TEST_ASSERT_EQUAL(12, s[1]);
    TEST_ASSERT_EQUAL(10, s[2]);
}

static void test_gp40_third_slot_open() {
    const char *l[13] = {"Light", "", "", "", "", "", "", "", "", "",
                         "Uncouple", "", ""};
    int s[SLOTS];
    pick(l, 13, s);
    TEST_ASSERT_EQUAL(0, s[0]);
    TEST_ASSERT_EQUAL(NONE, s[1]);
    TEST_ASSERT_EQUAL(10, s[2]);
}

static void test_gp40_with_delayed_fills_middle() {
    const char *l[13] = {"Light", "", "", "", "", "", "", "", "", "",
                         "Uncouple", "Delayed", ""};
    int s[SLOTS];
    pick(l, 13, s);
    TEST_ASSERT_EQUAL(0, s[0]);
    TEST_ASSERT_EQUAL(11, s[1]);
    TEST_ASSERT_EQUAL(10, s[2]);
}

static void test_no_keywords_fall_back_in_order() {
    const char *l[5] = {"Bell", "Horn", "", "Coupler", "Sound"};
    int s[SLOTS];
    pick(l, 5, s);
    TEST_ASSERT_EQUAL(0, s[0]);
    TEST_ASSERT_EQUAL(1, s[1]);
    TEST_ASSERT_EQUAL(3, s[2]);
}

static void test_case_insensitive_and_momentary() {
    TEST_ASSERT_TRUE(containsNoCase("DELAYED UNCOUPLE", "uncouple"));
    TEST_ASSERT_TRUE(looksMomentary("Uncouple"));
    TEST_ASSERT_FALSE(looksMomentary("Light"));
}

static void test_nudge_steps_by_one_across_zero() {
    TEST_ASSERT_EQUAL(1, Speed::nudge(0, 1));
    TEST_ASSERT_EQUAL(-1, Speed::nudge(0, -1));
    TEST_ASSERT_EQUAL(0, Speed::nudge(1, -1));
    TEST_ASSERT_EQUAL(0, Speed::nudge(-1, 1));
}

static void test_nudge_clamps() {
    TEST_ASSERT_EQUAL(126, Speed::nudge(126, 1));
    TEST_ASSERT_EQUAL(-126, Speed::nudge(-126, -1));
    TEST_ASSERT_EQUAL(126, Speed::clamp(500));
    TEST_ASSERT_EQUAL(-126, Speed::clamp(-500));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_hunslet);
    RUN_TEST(test_gp40_third_slot_open);
    RUN_TEST(test_gp40_with_delayed_fills_middle);
    RUN_TEST(test_no_keywords_fall_back_in_order);
    RUN_TEST(test_case_insensitive_and_momentary);
    RUN_TEST(test_nudge_steps_by_one_across_zero);
    RUN_TEST(test_nudge_clamps);
    return UNITY_END();
}
