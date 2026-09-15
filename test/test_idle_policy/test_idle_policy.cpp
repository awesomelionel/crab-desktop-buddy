#include <unity.h>
#include "idle_policy.h"
#include "settings_model.h"
#include "state.h"

using namespace settings;

static Settings defaults_with(uint16_t dim_s, uint16_t sleep_s) {
    Settings s = {};
    setDefaults(s, "Test");
    s.dim_timeout_s   = dim_s;
    s.sleep_timeout_s = sleep_s;
    return s;
}

void setUp(void) {}
void tearDown(void) {}

static void test_working_and_waiting_hold_awake(void) {
    TEST_ASSERT_TRUE(idle_policy::holdsAwake(STATE_WORKING));
    TEST_ASSERT_TRUE(idle_policy::holdsAwake(STATE_WAITING));
    TEST_ASSERT_FALSE(idle_policy::holdsAwake(STATE_IDLE));
    TEST_ASSERT_FALSE(idle_policy::holdsAwake(STATE_DISCONNECTED));
}

static void test_nap_timeout_follows_dim_when_set(void) {
    Settings s = defaults_with(/*dim_s*/30, /*sleep_s*/120);
    TEST_ASSERT_EQUAL_UINT32(30'000, idle_policy::napTimeoutMs(s));
}

static void test_nap_timeout_falls_back_when_dim_disabled(void) {
    Settings s = defaults_with(/*dim_s*/0, /*sleep_s*/120);
    TEST_ASSERT_EQUAL_UINT32(30'000, idle_policy::napTimeoutMs(s));
}

static void test_idle_does_not_nap_before_threshold(void) {
    Settings s = defaults_with(30, 120);
    TEST_ASSERT_FALSE(idle_policy::shouldNap(STATE_IDLE, 29'999, s));
}

static void test_idle_naps_at_threshold(void) {
    Settings s = defaults_with(30, 120);
    TEST_ASSERT_TRUE(idle_policy::shouldNap(STATE_IDLE, 30'000, s));
}

static void test_working_never_naps(void) {
    Settings s = defaults_with(30, 120);
    TEST_ASSERT_FALSE(idle_policy::shouldNap(STATE_WORKING, 60'000, s));
}

static void test_waiting_never_naps(void) {
    Settings s = defaults_with(30, 120);
    TEST_ASSERT_FALSE(idle_policy::shouldNap(STATE_WAITING, 60'000, s));
}

static void test_disconnected_does_not_use_connected_nap(void) {
    Settings s = defaults_with(30, 120);
    TEST_ASSERT_FALSE(idle_policy::shouldNap(STATE_DISCONNECTED, 60'000, s));
}

// Activity stamped with millis() after the loop captured now_ms must not
// underflow into ~49 days of idle (that blanked the screen for a frame and
// the wake-invalidate wiped the DONE celebration).
static void test_idle_ms_clamps_activity_after_now(void) {
    TEST_ASSERT_EQUAL_UINT32(0,   idle_policy::idleMs(1'000, 1'005));
    TEST_ASSERT_EQUAL_UINT32(500, idle_policy::idleMs(1'500, 1'000));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_ms_clamps_activity_after_now);
    RUN_TEST(test_working_and_waiting_hold_awake);
    RUN_TEST(test_nap_timeout_follows_dim_when_set);
    RUN_TEST(test_nap_timeout_falls_back_when_dim_disabled);
    RUN_TEST(test_idle_does_not_nap_before_threshold);
    RUN_TEST(test_idle_naps_at_threshold);
    RUN_TEST(test_working_never_naps);
    RUN_TEST(test_waiting_never_naps);
    RUN_TEST(test_disconnected_does_not_use_connected_nap);
    return UNITY_END();
}
