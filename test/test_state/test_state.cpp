#include <unity.h>
#include <string.h>
#include "state.h"

void setUp(void) {}
void tearDown(void) {}

static ClaudeStatus mk(uint8_t t, uint8_t r, uint8_t w) {
    ClaudeStatus s = {};
    s.total = t; s.running = r; s.waiting = w; s.valid = true;
    return s;
}

static void test_disconnected_when_not_connected(void) {
    ClaudeStatus s = mk(3, 1, 1);
    TEST_ASSERT_EQUAL(STATE_DISCONNECTED, state_derive(s, false));
}

static void test_idle_when_no_running_no_waiting(void) {
    ClaudeStatus s = mk(0, 0, 0);
    TEST_ASSERT_EQUAL(STATE_IDLE, state_derive(s, true));
}

static void test_idle_with_total_but_nothing_active(void) {
    ClaudeStatus s = mk(2, 0, 0);
    TEST_ASSERT_EQUAL(STATE_IDLE, state_derive(s, true));
}

static void test_working_when_running_positive(void) {
    ClaudeStatus s = mk(2, 1, 0);
    TEST_ASSERT_EQUAL(STATE_WORKING, state_derive(s, true));
}

static void test_waiting_takes_priority_over_running(void) {
    ClaudeStatus s = mk(3, 2, 1);
    TEST_ASSERT_EQUAL(STATE_WAITING, state_derive(s, true));
}

static void test_state_name_strings(void) {
    TEST_ASSERT_EQUAL_STRING("disconnected", state_name(STATE_DISCONNECTED));
    TEST_ASSERT_EQUAL_STRING("idle",         state_name(STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("working",      state_name(STATE_WORKING));
    TEST_ASSERT_EQUAL_STRING("waiting",      state_name(STATE_WAITING));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_disconnected_when_not_connected);
    RUN_TEST(test_idle_when_no_running_no_waiting);
    RUN_TEST(test_idle_with_total_but_nothing_active);
    RUN_TEST(test_working_when_running_positive);
    RUN_TEST(test_waiting_takes_priority_over_running);
    RUN_TEST(test_state_name_strings);
    return UNITY_END();
}
