#include <unity.h>
#include <string.h>
#include "format.h"

void setUp(void) {}
void tearDown(void) {}

static void check(uint32_t n, const char* expected) {
    char buf[kFormatTokenCountBufLen] = {};
    size_t written = format_token_count(n, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(expected), written);
    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_zero(void)              { check(0, "0"); }
static void test_single_digit(void)      { check(7, "7"); }
static void test_three_digits(void)      { check(523, "523"); }
static void test_just_below_1k(void)     { check(999, "999"); }

static void test_one_k(void)             { check(1000, "1.0K"); }
static void test_below_round_to_10k(void){ check(9949, "9.9K"); }
static void test_31_2_k(void)            { check(31200, "31.2K"); }
static void test_just_below_100k(void)   { check(99499, "99.5K"); }
static void test_100k_boundary(void)     { check(99500, "100K"); }

static void test_523_k(void)             { check(523000, "523K"); }
static void test_just_below_1m(void)     { check(999499, "999K"); }
static void test_1m_boundary(void)       { check(999500, "1.0M"); }

static void test_one_m(void)             { check(1000000, "1.0M"); }
static void test_4_3_m(void)             { check(4321000, "4.3M"); }
static void test_just_below_10m(void)    { check(9949999, "9.9M"); }
static void test_10m_boundary(void)      { check(9950000, "10M"); }
static void test_large_m(void)           { check(42000000, "42M"); }

static void test_buffer_too_small(void) {
    char buf[4] = { 'x', 'x', 'x', 'x' };
    size_t written = format_token_count(31200, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(0, written);
    TEST_ASSERT_EQUAL_CHAR(0, buf[0]);
}

static void test_null_buffer(void) {
    size_t written = format_token_count(31200, nullptr, 16);
    TEST_ASSERT_EQUAL_size_t(0, written);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zero);
    RUN_TEST(test_single_digit);
    RUN_TEST(test_three_digits);
    RUN_TEST(test_just_below_1k);
    RUN_TEST(test_one_k);
    RUN_TEST(test_below_round_to_10k);
    RUN_TEST(test_31_2_k);
    RUN_TEST(test_just_below_100k);
    RUN_TEST(test_100k_boundary);
    RUN_TEST(test_523_k);
    RUN_TEST(test_just_below_1m);
    RUN_TEST(test_1m_boundary);
    RUN_TEST(test_one_m);
    RUN_TEST(test_4_3_m);
    RUN_TEST(test_just_below_10m);
    RUN_TEST(test_10m_boundary);
    RUN_TEST(test_large_m);
    RUN_TEST(test_buffer_too_small);
    RUN_TEST(test_null_buffer);
    return UNITY_END();
}
