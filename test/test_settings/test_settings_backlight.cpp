#include <unity.h>
#include <string.h>
#include "settings_model.h"

using namespace settings;

static Settings make_defaults() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

void test_backlight_defaults(void) {
    Settings s = make_defaults();
    TEST_ASSERT_EQUAL_UINT16(30, s.dim_timeout_s);
    TEST_ASSERT_EQUAL_UINT16(120, s.sleep_timeout_s);
    TEST_ASSERT_EQUAL_UINT8(40, s.dim_level_pct);
    TEST_ASSERT_EQUAL_UINT8(100, s.full_level_pct);
}

void test_dim_timeout_zero_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s = 0;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_dim_timeout_too_low_rejected(void) {
    Settings s = make_defaults();
    s.dim_timeout_s = 4;  // below DIM_TIMEOUT_MIN_S
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "dim_timeout_s") != nullptr);
}

void test_dim_level_zero_rejected(void) {
    Settings s = make_defaults();
    s.dim_level_pct = 0;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "dim_level_pct") != nullptr);
}

void test_full_level_over_100_rejected(void) {
    Settings s = make_defaults();
    s.full_level_pct = 101;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "full_level_pct") != nullptr);
}

void test_dim_must_precede_sleep(void) {
    Settings s = make_defaults();
    s.dim_timeout_s   = 60;
    s.sleep_timeout_s = 60;     // both non-zero, dim NOT < sleep
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "before") != nullptr);
}

void test_dim_zero_with_sleep_set_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s   = 0;       // dim disabled
    s.sleep_timeout_s = 60;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_dim_timeout_min_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s = DIM_TIMEOUT_MIN_S;       // 5
    // sleep_timeout_s defaults to 0 so cross-field rule does not kick in.
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_dim_timeout_max_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s   = DIM_TIMEOUT_MAX_S;       // 3600
    s.sleep_timeout_s = 0;                       // screen-off disabled so dim can be max
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_dim_timeout_above_max_rejected(void) {
    Settings s = make_defaults();
    s.dim_timeout_s = DIM_TIMEOUT_MAX_S + 1;   // 3601
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "dim_timeout_s") != nullptr);
}

void test_dim_level_max_accepted(void) {
    Settings s = make_defaults();
    s.dim_level_pct = DIM_LEVEL_MAX_PCT;       // 99
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_dim_level_above_max_rejected(void) {
    Settings s = make_defaults();
    s.dim_level_pct = DIM_LEVEL_MAX_PCT + 1;   // 100
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "dim_level_pct") != nullptr);
}

void test_full_level_max_accepted(void) {
    Settings s = make_defaults();
    s.full_level_pct = FULL_LEVEL_MAX_PCT;     // 100
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_full_level_zero_rejected(void) {
    Settings s = make_defaults();
    s.full_level_pct = 0;                       // below FULL_LEVEL_MIN_PCT
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "full_level_pct") != nullptr);
}

void test_sleep_zero_with_dim_set_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s   = 30;
    s.sleep_timeout_s = 0;     // sleep disabled, dim non-zero — fine
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_apply_backlight_ok(void) {
    Settings s = make_defaults();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBacklightFields(s, /*dim_s*/15, /*dim_pct*/25,
                                           /*full_pct*/90, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT16(15, s.dim_timeout_s);
    TEST_ASSERT_EQUAL_UINT8(25, s.dim_level_pct);
    TEST_ASSERT_EQUAL_UINT8(90, s.full_level_pct);
}

void test_apply_backlight_bad_pct_rejected(void) {
    Settings s = make_defaults();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyBacklightFields(s, 30, 0 /*invalid*/, 100,
                                            err, sizeof(err)));
    // Ensure the original was not mutated.
    TEST_ASSERT_EQUAL_UINT8(40, s.dim_level_pct);
}

void test_tojson_includes_backlight(void) {
    Settings s = make_defaults();
    char buf[1024] = {};
    size_t n = toJson(s, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_TRUE(strstr(buf, "\"dim_timeout_s\":30") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"dim_level_pct\":40") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"full_level_pct\":100") != nullptr);
}
