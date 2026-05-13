#include <unity.h>
#include <string.h>
#include "settings_model.h"

// Defined in test_settings_backlight.cpp
void test_backlight_defaults(void);
void test_dim_timeout_zero_accepted(void);
void test_dim_timeout_too_low_rejected(void);
void test_dim_level_zero_rejected(void);
void test_full_level_over_100_rejected(void);
void test_dim_must_precede_sleep(void);
void test_dim_zero_with_sleep_set_accepted(void);
void test_dim_timeout_min_accepted(void);
void test_dim_timeout_max_accepted(void);
void test_dim_timeout_above_max_rejected(void);
void test_dim_level_max_accepted(void);
void test_dim_level_above_max_rejected(void);
void test_full_level_max_accepted(void);
void test_full_level_zero_rejected(void);
void test_sleep_zero_with_dim_set_accepted(void);
void test_apply_backlight_ok(void);
void test_apply_backlight_bad_pct_rejected(void);
void test_tojson_includes_backlight(void);

// Defined in test_settings_daily_cap.cpp
void test_daily_cap_default_zero(void);
void test_daily_cap_zero_accepted(void);
void test_daily_cap_in_range_accepted(void);
void test_daily_cap_at_max_accepted(void);
void test_daily_cap_over_max_rejected(void);
void test_apply_daily_cap_ok(void);
void test_apply_daily_cap_rejects_over_max(void);
void test_tojson_includes_daily_cap(void);

// Defined in test_settings_bus_stops.cpp
void test_bus_stops_default_all_empty(void);
void test_apply_bus_stop_valid_code_accepted(void);
void test_apply_bus_stop_empty_code_clears_slot_and_label(void);
void test_apply_bus_stop_empty_code_clears_card_mask_bit(void);
void test_apply_bus_stop_non_digit_rejected(void);
void test_apply_bus_stop_wrong_length_rejected(void);
void test_apply_bus_stop_label_too_long_rejected(void);
void test_apply_bus_stop_label_with_control_char_rejected(void);
void test_apply_bus_stop_slot_out_of_range_rejected(void);
void test_validate_accepts_default_settings(void);
void test_validate_rejects_card_enabled_with_empty_code(void);
void test_apply_bus_stop_clear_when_only_bus_card_enabled_rejected(void);

using namespace settings;

void setUp(void) {}
void tearDown(void) {}

static Settings defaults() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

static void test_defaults_are_valid(void) {
    Settings s = defaults();
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("", err);
    TEST_ASSERT_EQUAL_STRING("Claude-AABB", s.device_name);
    TEST_ASSERT_EQUAL_UINT16(30, s.live_timeout_s);
    TEST_ASSERT_EQUAL_UINT16(0, s.sleep_timeout_s);
    TEST_ASSERT_EQUAL_UINT8(CARD_STATUS, s.boot_card_id);
}

static void test_empty_device_name_rejected(void) {
    Settings s = defaults();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyDeviceFields(s, "", 30, 0, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "empty") != nullptr);
}

static void test_long_device_name_rejected(void) {
    Settings s = defaults();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyDeviceFields(s, "ThisNameIsWayTooLong", 30, 0, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "too long") != nullptr);
}

static void test_non_printable_device_name_rejected(void) {
    Settings s = defaults();
    char err[64] = {};
    char bad[] = {'a', 'b', 0x01, 0};
    TEST_ASSERT_FALSE(applyDeviceFields(s, bad, 30, 0, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "non-printable") != nullptr);
}

static void test_live_timeout_range(void) {
    Settings s = defaults();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyDeviceFields(s, "x", 4, 0, err, sizeof(err)));
    TEST_ASSERT_FALSE(applyDeviceFields(s, "x", 301, 0, err, sizeof(err)));
    TEST_ASSERT_TRUE(applyDeviceFields(s, "x", 5, 0, err, sizeof(err)));
    TEST_ASSERT_TRUE(applyDeviceFields(s, "x", 300, 0, err, sizeof(err)));
}

static void test_sleep_timeout_zero_or_range(void) {
    Settings s = defaults();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyDeviceFields(s, "x", 30, 0, err, sizeof(err)));
    TEST_ASSERT_FALSE(applyDeviceFields(s, "x", 30, 1, err, sizeof(err)));
    TEST_ASSERT_FALSE(applyDeviceFields(s, "x", 30, 29, err, sizeof(err)));
    TEST_ASSERT_TRUE(applyDeviceFields(s, "x", 30, 30, err, sizeof(err)));
    TEST_ASSERT_TRUE(applyDeviceFields(s, "x", 30, 3600, err, sizeof(err)));
    TEST_ASSERT_FALSE(applyDeviceFields(s, "x", 30, 3601, err, sizeof(err)));
}

static void test_cards_enabled_must_be_nonempty(void) {
    Settings s = defaults();
    char err[64] = {};
    uint8_t order[] = {};
    TEST_ASSERT_FALSE(applyCardsFields(s, 0, order, 0, CARD_STATUS, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "at least one") != nullptr);
}

static void test_cards_order_must_match_enabled(void) {
    Settings s = defaults();
    char err[64] = {};
    // enabled = STATUS+EYES, but order says only STATUS
    uint8_t order[] = {CARD_STATUS};
    uint8_t mask = (1u << CARD_STATUS) | (1u << CARD_EYES);
    TEST_ASSERT_FALSE(applyCardsFields(s, mask, order, 1, CARD_STATUS, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "match enabled") != nullptr);
}

static void test_cards_order_rejects_duplicates(void) {
    Settings s = defaults();
    char err[64] = {};
    uint8_t order[] = {CARD_STATUS, CARD_STATUS};
    uint8_t mask = (1u << CARD_STATUS) | (1u << CARD_EYES);
    TEST_ASSERT_FALSE(applyCardsFields(s, mask, order, 2, CARD_STATUS, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "duplicate") != nullptr);
}

static void test_cards_order_rejects_unknown_id(void) {
    Settings s = defaults();
    char err[64] = {};
    uint8_t order[] = {CARD_STATUS, 99};
    uint8_t mask = (1u << CARD_STATUS) | (1u << CARD_EYES);
    TEST_ASSERT_FALSE(applyCardsFields(s, mask, order, 2, CARD_STATUS, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "unknown") != nullptr);
}

static void test_boot_card_silently_defaults_when_disabled(void) {
    Settings s = defaults();
    char err[64] = {};
    // enabled = EYES only
    uint8_t order[] = {CARD_EYES};
    uint8_t mask = (1u << CARD_EYES);
    TEST_ASSERT_TRUE(applyCardsFields(s, mask, order, 1, CARD_STATUS, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT8(CARD_EYES, s.boot_card_id);
}

static void test_apply_cards_happy_path(void) {
    Settings s = defaults();
    char err[64] = {};
    uint8_t order[] = {CARD_EYES, CARD_WIFI, CARD_STATUS};
    uint8_t mask = (1u << CARD_STATUS) | (1u << CARD_EYES) | (1u << CARD_WIFI);
    TEST_ASSERT_TRUE(applyCardsFields(s, mask, order, 3, CARD_EYES, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT8(3, s.cards_order_count);
    TEST_ASSERT_EQUAL_UINT8(CARD_EYES,  s.cards_order[0]);
    TEST_ASSERT_EQUAL_UINT8(CARD_WIFI,  s.cards_order[1]);
    TEST_ASSERT_EQUAL_UINT8(CARD_STATUS, s.cards_order[2]);
    TEST_ASSERT_EQUAL_UINT8(CARD_EYES,   s.boot_card_id);
}

static void test_to_json_contains_expected_keys(void) {
    Settings s = defaults();
    char buf[2048];
    size_t n = toJson(s, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_TRUE(strstr(buf, "\"device_name\":\"Claude-AABB\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"live_timeout_s\":30") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"sleep_timeout_s\":0") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"boot_card_id\":0") != nullptr);
    // Cards array contains all four with stable IDs
    TEST_ASSERT_TRUE(strstr(buf, "\"name\":\"Status\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"name\":\"NavTest\"") != nullptr);
    // NavTest is disabled in defaults
    TEST_ASSERT_TRUE(strstr(buf, "\"name\":\"NavTest\",\"enabled\":false") != nullptr);
}

static void test_to_json_buffer_too_small_returns_zero(void) {
    Settings s = defaults();
    char buf[16];
    size_t n = toJson(s, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(0, n);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_empty_device_name_rejected);
    RUN_TEST(test_long_device_name_rejected);
    RUN_TEST(test_non_printable_device_name_rejected);
    RUN_TEST(test_live_timeout_range);
    RUN_TEST(test_sleep_timeout_zero_or_range);
    RUN_TEST(test_cards_enabled_must_be_nonempty);
    RUN_TEST(test_cards_order_must_match_enabled);
    RUN_TEST(test_cards_order_rejects_duplicates);
    RUN_TEST(test_cards_order_rejects_unknown_id);
    RUN_TEST(test_boot_card_silently_defaults_when_disabled);
    RUN_TEST(test_apply_cards_happy_path);
    RUN_TEST(test_to_json_contains_expected_keys);
    RUN_TEST(test_to_json_buffer_too_small_returns_zero);
    RUN_TEST(test_backlight_defaults);
    RUN_TEST(test_dim_timeout_zero_accepted);
    RUN_TEST(test_dim_timeout_too_low_rejected);
    RUN_TEST(test_dim_level_zero_rejected);
    RUN_TEST(test_full_level_over_100_rejected);
    RUN_TEST(test_dim_must_precede_sleep);
    RUN_TEST(test_dim_zero_with_sleep_set_accepted);
    RUN_TEST(test_dim_timeout_min_accepted);
    RUN_TEST(test_dim_timeout_max_accepted);
    RUN_TEST(test_dim_timeout_above_max_rejected);
    RUN_TEST(test_dim_level_max_accepted);
    RUN_TEST(test_dim_level_above_max_rejected);
    RUN_TEST(test_full_level_max_accepted);
    RUN_TEST(test_full_level_zero_rejected);
    RUN_TEST(test_sleep_zero_with_dim_set_accepted);
    RUN_TEST(test_apply_backlight_ok);
    RUN_TEST(test_apply_backlight_bad_pct_rejected);
    RUN_TEST(test_tojson_includes_backlight);
    RUN_TEST(test_daily_cap_default_zero);
    RUN_TEST(test_daily_cap_zero_accepted);
    RUN_TEST(test_daily_cap_in_range_accepted);
    RUN_TEST(test_daily_cap_at_max_accepted);
    RUN_TEST(test_daily_cap_over_max_rejected);
    RUN_TEST(test_apply_daily_cap_ok);
    RUN_TEST(test_apply_daily_cap_rejects_over_max);
    RUN_TEST(test_tojson_includes_daily_cap);
    RUN_TEST(test_bus_stops_default_all_empty);
    RUN_TEST(test_apply_bus_stop_valid_code_accepted);
    RUN_TEST(test_apply_bus_stop_empty_code_clears_slot_and_label);
    RUN_TEST(test_apply_bus_stop_empty_code_clears_card_mask_bit);
    RUN_TEST(test_apply_bus_stop_non_digit_rejected);
    RUN_TEST(test_apply_bus_stop_wrong_length_rejected);
    RUN_TEST(test_apply_bus_stop_label_too_long_rejected);
    RUN_TEST(test_apply_bus_stop_label_with_control_char_rejected);
    RUN_TEST(test_apply_bus_stop_slot_out_of_range_rejected);
    RUN_TEST(test_validate_accepts_default_settings);
    RUN_TEST(test_validate_rejects_card_enabled_with_empty_code);
    RUN_TEST(test_apply_bus_stop_clear_when_only_bus_card_enabled_rejected);
    return UNITY_END();
}
