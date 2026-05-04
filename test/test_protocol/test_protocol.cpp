#include <unity.h>
#include <string.h>
#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parse_full_snapshot(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":3,\"running\":1,\"waiting\":2,\"msg\":\"approve: Bash\"}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT8(3, s.total);
    TEST_ASSERT_EQUAL_UINT8(1, s.running);
    TEST_ASSERT_EQUAL_UINT8(2, s.waiting);
    TEST_ASSERT_EQUAL_STRING("approve: Bash", s.msg);
}

static void test_parse_missing_fields_keeps_previous(void) {
    ClaudeStatus s = {};
    s.total = 5; s.running = 2; s.waiting = 1;
    strcpy(s.msg, "old");
    bool ok = protocol_parse_line("{\"running\":4}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(5, s.total);     // preserved
    TEST_ASSERT_EQUAL_UINT8(4, s.running);   // updated
    TEST_ASSERT_EQUAL_UINT8(1, s.waiting);   // preserved
    TEST_ASSERT_EQUAL_STRING("old", s.msg);  // preserved
}

static void test_parse_rejects_non_object(void) {
    ClaudeStatus s = {};
    TEST_ASSERT_FALSE(protocol_parse_line("not json", &s));
    TEST_ASSERT_FALSE(protocol_parse_line("[1,2,3]", &s));
    TEST_ASSERT_FALSE(protocol_parse_line("", &s));
    TEST_ASSERT_FALSE(protocol_parse_line(nullptr, &s));
    TEST_ASSERT_FALSE(s.valid);
}

static void test_parse_rejects_malformed_json(void) {
    ClaudeStatus s = {};
    TEST_ASSERT_FALSE(protocol_parse_line("{not json", &s));
    TEST_ASSERT_FALSE(s.valid);
}

static void test_parse_truncates_long_msg(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"msg\":\"this string is intentionally longer than the buffer fits\"}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(s.msg) - 1, strlen(s.msg));
}

static void test_parse_prompt_full(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"running\":0,\"waiting\":1,\"prompt\":"
        "{\"id\":\"req_abc123\",\"tool\":\"Bash\",\"hint\":\"rm -rf /tmp/foo\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.prompt.present);
    TEST_ASSERT_EQUAL_STRING("req_abc123", s.prompt.id);
    TEST_ASSERT_EQUAL_STRING("Bash", s.prompt.tool);
    TEST_ASSERT_EQUAL_STRING("rm -rf /tmp/foo", s.prompt.hint);
}

static void test_parse_snapshot_without_prompt_clears(void) {
    ClaudeStatus s = {};
    s.prompt.present = true;
    strcpy(s.prompt.id, "req_old");
    bool ok = protocol_parse_line("{\"total\":1,\"running\":1,\"waiting\":0}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(s.prompt.present);
}

static void test_parse_non_snapshot_does_not_clear_prompt(void) {
    ClaudeStatus s = {};
    s.prompt.present = true;
    strcpy(s.prompt.id, "req_keep");
    // An ack-shaped JSON (no `total` field) should leave prompt alone.
    bool ok = protocol_parse_line("{\"ack\":\"name\",\"ok\":true}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.prompt.present);
    TEST_ASSERT_EQUAL_STRING("req_keep", s.prompt.id);
}

static void test_parse_prompt_missing_id_drops(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"prompt\":{\"tool\":\"Bash\",\"hint\":\"x\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(s.prompt.present);
}

static void test_parse_prompt_id_too_long_drops(void) {
    // sizeof(id) == 40, so 39 chars is the longest that fits with NUL.
    // Send 40 chars → must NOT silently truncate and echo back a wrong id.
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"prompt\":{\"id\":\"abcdefghijklmnopqrstuvwxyz12345678901234\","
        "\"tool\":\"Bash\",\"hint\":\"x\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(s.prompt.present);
}

static void test_parse_prompt_real_id_fits(void) {
    // Real Anthropic request IDs look like "req_01abc..." (~30 chars).
    // Regression test: this was the bug — real IDs were silently dropped.
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"running\":0,\"waiting\":1,\"prompt\":"
        "{\"id\":\"req_01abc123xyz456def789ghi0\",\"tool\":\"Bash\",\"hint\":\"ls\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.prompt.present);
    TEST_ASSERT_EQUAL_STRING("req_01abc123xyz456def789ghi0", s.prompt.id);
}

static void test_parse_prompt_hint_sanitizes_unprintable(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":1,\"prompt\":{\"id\":\"r1\",\"tool\":\"Bash\","
        "\"hint\":\"a\\nb\\tc\"}}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.prompt.present);
    TEST_ASSERT_EQUAL_STRING("a?b?c", s.prompt.hint);
}

static void test_parse_tokens_today_present(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":3,\"running\":1,\"waiting\":2,\"tokens_today\":31200}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(31200u, s.tokens_today);
}

static void test_parse_tokens_today_missing_keeps_previous(void) {
    ClaudeStatus s = {};
    s.tokens_today = 12345;
    bool ok = protocol_parse_line("{\"running\":1}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(12345u, s.tokens_today);
}

static void test_parse_tokens_today_zero_is_honoured(void) {
    // After local midnight, the bridge can legitimately send 0 to reset
    // the counter — distinct from the field being absent.
    ClaudeStatus s = {};
    s.tokens_today = 99999;
    bool ok = protocol_parse_line(
        "{\"total\":0,\"tokens_today\":0}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0u, s.tokens_today);
}

static void test_parse_tokens_today_malformed_keeps_previous(void) {
    ClaudeStatus s = {};
    s.tokens_today = 7777;
    bool ok = protocol_parse_line(
        "{\"running\":1,\"tokens_today\":\"not-a-number\"}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(7777u, s.tokens_today);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_full_snapshot);
    RUN_TEST(test_parse_missing_fields_keeps_previous);
    RUN_TEST(test_parse_rejects_non_object);
    RUN_TEST(test_parse_rejects_malformed_json);
    RUN_TEST(test_parse_truncates_long_msg);
    RUN_TEST(test_parse_prompt_full);
    RUN_TEST(test_parse_snapshot_without_prompt_clears);
    RUN_TEST(test_parse_non_snapshot_does_not_clear_prompt);
    RUN_TEST(test_parse_prompt_missing_id_drops);
    RUN_TEST(test_parse_prompt_id_too_long_drops);
    RUN_TEST(test_parse_prompt_real_id_fits);
    RUN_TEST(test_parse_prompt_hint_sanitizes_unprintable);
    RUN_TEST(test_parse_tokens_today_present);
    RUN_TEST(test_parse_tokens_today_missing_keeps_previous);
    RUN_TEST(test_parse_tokens_today_zero_is_honoured);
    RUN_TEST(test_parse_tokens_today_malformed_keeps_previous);
    return UNITY_END();
}
