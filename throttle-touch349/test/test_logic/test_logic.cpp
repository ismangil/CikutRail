#include <unity.h>

#include "FunctionSlots.h"
#include "Slider.h"

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

static void test_slider_ends_and_centre() {
    TEST_ASSERT_EQUAL(126, Slider::speedFromY(100, 100, 300));
    TEST_ASSERT_EQUAL(-126, Slider::speedFromY(300, 100, 300));
    TEST_ASSERT_EQUAL(0, Slider::speedFromY(200, 100, 300));
    TEST_ASSERT_EQUAL(126, Slider::speedFromY(50, 100, 300));   // clamps
}

static void test_slider_snaps_near_zero() {
    TEST_ASSERT_EQUAL(0, Slider::speedFromY(203, 100, 300));
    TEST_ASSERT_EQUAL(0, Slider::speedFromY(197, 100, 300));
    TEST_ASSERT_TRUE(Slider::speedFromY(190, 100, 300) > 0);
}

static void test_slider_roundtrip() {
    for (int v = -126; v <= 126; v += 7) {
        const int y = Slider::yFromSpeed(v, 100, 300);
        const int back = Slider::speedFromY(y, 100, 300);
        const int want = (v >= -Slider::SNAP && v <= Slider::SNAP) ? 0 : v;
        TEST_ASSERT_INT_WITHIN(2, want, back);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_hunslet);
    RUN_TEST(test_gp40_third_slot_open);
    RUN_TEST(test_gp40_with_delayed_fills_middle);
    RUN_TEST(test_no_keywords_fall_back_in_order);
    RUN_TEST(test_case_insensitive_and_momentary);
    RUN_TEST(test_slider_ends_and_centre);
    RUN_TEST(test_slider_snaps_near_zero);
    RUN_TEST(test_slider_roundtrip);
    return UNITY_END();
}
