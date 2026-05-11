#include <unity.h>
#include "version_compare.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parses_basic_semver(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("1.2.3", v));
    TEST_ASSERT_EQUAL_UINT16(1, v.major);
    TEST_ASSERT_EQUAL_UINT16(2, v.minor);
    TEST_ASSERT_EQUAL_UINT16(3, v.patch);
}

static void test_parses_v_prefix(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("v0.10.5", v));
    TEST_ASSERT_EQUAL_UINT16(0, v.major);
    TEST_ASSERT_EQUAL_UINT16(10, v.minor);
    TEST_ASSERT_EQUAL_UINT16(5, v.patch);
}

static void test_parses_prerelease_suffix_stripped(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("1.2.3-rc1", v));
    TEST_ASSERT_EQUAL_UINT16(1, v.major);
    TEST_ASSERT_EQUAL_UINT16(2, v.minor);
    TEST_ASSERT_EQUAL_UINT16(3, v.patch);
}

static void test_dev_string_falls_back_to_zero(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("0.0.0-dev", v));
    TEST_ASSERT_EQUAL_UINT16(0, v.major);
    TEST_ASSERT_EQUAL_UINT16(0, v.minor);
    TEST_ASSERT_EQUAL_UINT16(0, v.patch);
}

static void test_malformed_falls_back_to_zero(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("garbage", v));
    TEST_ASSERT_EQUAL_UINT16(0, v.major);
    TEST_ASSERT_EQUAL_UINT16(0, v.minor);
    TEST_ASSERT_EQUAL_UINT16(0, v.patch);
}

static void test_compare_equal(void) {
    TEST_ASSERT_EQUAL_INT(0, version_compare::compare("1.2.3", "1.2.3"));
}

static void test_compare_newer_patch(void) {
    TEST_ASSERT_LESS_THAN_INT(0, version_compare::compare("1.2.3", "1.2.4"));
}

static void test_compare_older_patch(void) {
    TEST_ASSERT_GREATER_THAN_INT(0, version_compare::compare("1.2.4", "1.2.3"));
}

static void test_compare_newer_minor_beats_higher_patch(void) {
    TEST_ASSERT_LESS_THAN_INT(0, version_compare::compare("1.2.9", "1.3.0"));
}

static void test_compare_dev_less_than_release(void) {
    TEST_ASSERT_LESS_THAN_INT(0, version_compare::compare("0.0.0-dev", "0.0.1"));
}

static void test_is_newer_helper(void) {
    TEST_ASSERT_TRUE(version_compare::isNewer("1.2.3", "1.2.4"));
    TEST_ASSERT_FALSE(version_compare::isNewer("1.2.4", "1.2.3"));
    TEST_ASSERT_FALSE(version_compare::isNewer("1.2.3", "1.2.3"));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_basic_semver);
    RUN_TEST(test_parses_v_prefix);
    RUN_TEST(test_parses_prerelease_suffix_stripped);
    RUN_TEST(test_dev_string_falls_back_to_zero);
    RUN_TEST(test_malformed_falls_back_to_zero);
    RUN_TEST(test_compare_equal);
    RUN_TEST(test_compare_newer_patch);
    RUN_TEST(test_compare_older_patch);
    RUN_TEST(test_compare_newer_minor_beats_higher_patch);
    RUN_TEST(test_compare_dev_less_than_release);
    RUN_TEST(test_is_newer_helper);
    return UNITY_END();
}
