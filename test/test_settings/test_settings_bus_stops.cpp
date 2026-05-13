#include <unity.h>
#include <string.h>
#include "settings_model.h"

using namespace settings;

static Settings make_defaults_bus() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

void test_bus_stops_default_all_empty(void) {
    Settings s = make_defaults_bus();
    for (uint8_t i = 0; i < MAX_BUS_STOPS; ++i) {
        TEST_ASSERT_EQUAL_STRING("", s.bus_stops[i].code);
        TEST_ASSERT_EQUAL_STRING("", s.bus_stops[i].label);
    }
}

void test_apply_bus_stop_valid_code_accepted(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBusStopField(s, 0, "50171", "Home",
                                       err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("50171", s.bus_stops[0].code);
    TEST_ASSERT_EQUAL_STRING("Home",  s.bus_stops[0].label);
}

void test_apply_bus_stop_empty_code_clears_slot_and_label(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBusStopField(s, 0, "50171", "Home",
                                       err, sizeof(err)));
    TEST_ASSERT_TRUE(applyBusStopField(s, 0, "", "ignored",
                                       err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("", s.bus_stops[0].code);
    TEST_ASSERT_EQUAL_STRING("", s.bus_stops[0].label);
}

void test_apply_bus_stop_empty_code_clears_card_mask_bit(void) {
    Settings s = make_defaults_bus();
    s.cards_enabled_mask |= (1u << CARD_BUS_2);
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBusStopField(s, 1, "54321", "Office",
                                       err, sizeof(err)));
    // Now clear it. The CARD_BUS_2 bit must be auto-cleared.
    TEST_ASSERT_TRUE(applyBusStopField(s, 1, "", "",
                                       err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT8(0u, s.cards_enabled_mask & (1u << CARD_BUS_2));
}

void test_apply_bus_stop_non_digit_rejected(void) {
    Settings s = make_defaults_bus();
    s.bus_stops[0].code[0]  = '\0';
    char err[64] = {};
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "5017a", "Home",
                                        err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "code") != nullptr);
    TEST_ASSERT_EQUAL_STRING("", s.bus_stops[0].code);
}

void test_apply_bus_stop_wrong_length_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "501",   "X", err, sizeof(err)));
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "501712", "X", err, sizeof(err)));
}

void test_apply_bus_stop_label_too_long_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    // MAX_BUS_LABEL_LEN is 12, so a 13-char label is too long.
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "50171", "Thirteenchars",
                                        err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "label") != nullptr);
}

void test_apply_bus_stop_label_with_control_char_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    char bad_label[] = {'H','o','m','\x01','e','\0'};
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "50171", bad_label,
                                        err, sizeof(err)));
}

void test_apply_bus_stop_slot_out_of_range_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyBusStopField(s, MAX_BUS_STOPS, "50171", "X",
                                        err, sizeof(err)));
}

void test_validate_accepts_default_settings(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_validate_rejects_card_enabled_with_empty_code(void) {
    Settings s = make_defaults_bus();
    // Force a bus card bit on without setting its code.
    s.cards_enabled_mask |= (1u << CARD_BUS_1);
    s.cards_order[s.cards_order_count++] = CARD_BUS_1;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "bus") != nullptr);
}

void test_apply_bus_stop_clear_when_only_bus_card_enabled_rejected(void) {
    Settings s = make_defaults_bus();
    char err[64] = {};
    // First set up slot 0 with a valid stop and make the corresponding card
    // the only enabled card.
    TEST_ASSERT_TRUE(applyBusStopField(s, 0, "50171", "Home",
                                       err, sizeof(err)));
    s.cards_enabled_mask = (uint8_t)(1u << CARD_BUS_1);
    s.cards_order[0]     = CARD_BUS_1;
    s.cards_order_count  = 1;
    s.boot_card_id       = CARD_BUS_1;
    // Now attempting to clear slot 0 must fail and leave s unchanged.
    TEST_ASSERT_FALSE(applyBusStopField(s, 0, "", "",
                                        err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("50171", s.bus_stops[0].code);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(1u << CARD_BUS_1),
                            s.cards_enabled_mask);
    TEST_ASSERT_TRUE(strstr(err, "enabled") != nullptr);
}
