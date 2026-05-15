#include <unity.h>
#include <stdint.h>
#include <string.h>

#include "bus_fetch_logic.h"

void setUp(void) {}
void tearDown(void) {}

using bus_fetch_logic::FetchPriority;
using bus_fetch_logic::SlotRequest;
using bus_fetch_logic::pickHighestPriority;
using bus_fetch_logic::applyRequest;

static SlotRequest blank_table[settings::MAX_BUS_STOPS];

static void clearTable() {
    for (size_t i = 0; i < settings::MAX_BUS_STOPS; ++i) {
        blank_table[i].code[0] = '\0';
        blank_table[i].prio    = FetchPriority::LOW_PRIO;
        blank_table[i].wanted  = false;
    }
}

// Populate a slot directly (no applyRequest) so pick tests are independent
// of applyRequest correctness.
static void setSlot(size_t idx, FetchPriority prio, bool wanted,
                    const char* code = "00000") {
    strncpy(blank_table[idx].code, code, settings::BUS_STOP_CODE_LEN);
    blank_table[idx].code[settings::BUS_STOP_CODE_LEN] = '\0';
    blank_table[idx].prio   = prio;
    blank_table[idx].wanted = wanted;
}

static void test_pick_empty_returns_minus_one(void) {
    clearTable();
    TEST_ASSERT_EQUAL_INT(-1, pickHighestPriority(blank_table));
}

static void test_pick_single_low_returns_that_slot(void) {
    clearTable();
    setSlot(2, FetchPriority::LOW_PRIO, true);
    TEST_ASSERT_EQUAL_INT(2, pickHighestPriority(blank_table));
}

static void test_pick_single_high_returns_that_slot(void) {
    clearTable();
    setSlot(1, FetchPriority::HIGH_PRIO, true);
    TEST_ASSERT_EQUAL_INT(1, pickHighestPriority(blank_table));
}

static void test_pick_high_wins_over_low_in_different_slots(void) {
    clearTable();
    setSlot(0, FetchPriority::LOW_PRIO,  true);
    setSlot(3, FetchPriority::HIGH_PRIO, true);
    TEST_ASSERT_EQUAL_INT(3, pickHighestPriority(blank_table));
}

static void test_pick_equal_priority_lowest_index_wins(void) {
    clearTable();
    setSlot(2, FetchPriority::LOW_PRIO, true);
    setSlot(3, FetchPriority::LOW_PRIO, true);
    TEST_ASSERT_EQUAL_INT(2, pickHighestPriority(blank_table));
}

static void test_pick_ignores_not_wanted(void) {
    clearTable();
    setSlot(0, FetchPriority::HIGH_PRIO, false);   // present but not wanted
    setSlot(2, FetchPriority::LOW_PRIO,  true);
    TEST_ASSERT_EQUAL_INT(2, pickHighestPriority(blank_table));
}

static void test_apply_to_empty_entry_sets_low(void) {
    clearTable();
    applyRequest(blank_table[1], "50171", FetchPriority::LOW_PRIO);
    TEST_ASSERT_TRUE(blank_table[1].wanted);
    TEST_ASSERT_EQUAL_INT((int)FetchPriority::LOW_PRIO, (int)blank_table[1].prio);
    TEST_ASSERT_EQUAL_STRING("50171", blank_table[1].code);
}

static void test_apply_low_then_high_upgrades_in_place(void) {
    clearTable();
    applyRequest(blank_table[1], "50171", FetchPriority::LOW_PRIO);
    applyRequest(blank_table[1], "50171", FetchPriority::HIGH_PRIO);
    TEST_ASSERT_TRUE(blank_table[1].wanted);
    TEST_ASSERT_EQUAL_INT((int)FetchPriority::HIGH_PRIO, (int)blank_table[1].prio);
}

static void test_apply_high_then_low_does_not_downgrade(void) {
    clearTable();
    applyRequest(blank_table[1], "50171", FetchPriority::HIGH_PRIO);
    applyRequest(blank_table[1], "50171", FetchPriority::LOW_PRIO);
    TEST_ASSERT_TRUE(blank_table[1].wanted);
    TEST_ASSERT_EQUAL_INT((int)FetchPriority::HIGH_PRIO, (int)blank_table[1].prio);
}

static void test_apply_refreshes_code_each_call(void) {
    clearTable();
    applyRequest(blank_table[0], "50171", FetchPriority::LOW_PRIO);
    applyRequest(blank_table[0], "54321", FetchPriority::LOW_PRIO);
    TEST_ASSERT_EQUAL_STRING("54321", blank_table[0].code);
}

static void test_apply_truncates_overlong_code_safely(void) {
    clearTable();
    applyRequest(blank_table[0], "1234567", FetchPriority::LOW_PRIO);
    TEST_ASSERT_EQUAL_size_t(settings::BUS_STOP_CODE_LEN,
                             strlen(blank_table[0].code));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_pick_empty_returns_minus_one);
    RUN_TEST(test_pick_single_low_returns_that_slot);
    RUN_TEST(test_pick_single_high_returns_that_slot);
    RUN_TEST(test_pick_high_wins_over_low_in_different_slots);
    RUN_TEST(test_pick_equal_priority_lowest_index_wins);
    RUN_TEST(test_pick_ignores_not_wanted);
    RUN_TEST(test_apply_to_empty_entry_sets_low);
    RUN_TEST(test_apply_low_then_high_upgrades_in_place);
    RUN_TEST(test_apply_high_then_low_does_not_downgrade);
    RUN_TEST(test_apply_refreshes_code_each_call);
    RUN_TEST(test_apply_truncates_overlong_code_safely);
    return UNITY_END();
}
