#include <unity.h>
#include <string.h>
#include "prompt_ui.h"

void setUp(void) {}
void tearDown(void) {}

static ClaudePrompt make_prompt(const char* id, const char* tool, const char* hint) {
    ClaudePrompt p = {};
    p.present = true;
    strncpy(p.id, id, sizeof(p.id) - 1);
    strncpy(p.tool, tool, sizeof(p.tool) - 1);
    strncpy(p.hint, hint, sizeof(p.hint) - 1);
    return p;
}

static ClaudePrompt make_absent(void) {
    ClaudePrompt p = {};
    p.present = false;
    return p;
}

static void test_hidden_until_prompt_present(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_absent(), true, 0);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
}

static void test_shows_with_default_approve(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", "ls"), true, 100);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_TRUE(v.visible);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
    TEST_ASSERT_EQUAL_STRING("Bash", v.tool);
    TEST_ASSERT_EQUAL_STRING("ls", v.hint);
}

static void test_up_down_navigation_clamped(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    TEST_ASSERT_EQUAL(OPT_DENY, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    TEST_ASSERT_EQUAL(OPT_DISMISS, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_DOWN, 3);  // clamp
    TEST_ASSERT_EQUAL(OPT_DISMISS, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_UP,   4);
    prompt_ui_button(&ui, BTN_UP,   5);
    prompt_ui_button(&ui, BTN_UP,   6);  // clamp
    TEST_ASSERT_EQUAL(OPT_APPROVE, prompt_ui_view(&ui).highlight);
}

static void test_center_on_approve_emits_once_json(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r_xyz", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_CENTER, 1);
    char buf[128] = {};
    TEST_ASSERT_TRUE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"cmd\":\"permission\",\"id\":\"r_xyz\",\"decision\":\"once\"}", buf);
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_center_on_deny_emits_deny_json(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r_xyz", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);   // → DENY
    prompt_ui_button(&ui, BTN_CENTER, 2);
    char buf[128] = {};
    TEST_ASSERT_TRUE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"cmd\":\"permission\",\"id\":\"r_xyz\",\"decision\":\"deny\"}", buf);
}

static void test_center_on_dismiss_emits_nothing(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);   // → DISMISS
    prompt_ui_button(&ui, BTN_CENTER, 3);
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_dismiss_is_sticky_per_id(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);  // dismiss rA
    // After flash window, UI hides.
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 600);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
    // New id rB shows again.
    prompt_ui_update(&ui, make_prompt("rB", "Bash", ""), true, 700);
    TEST_ASSERT_TRUE(prompt_ui_view(&ui).visible);
}

static void test_approve_is_sticky_per_id(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_CENTER, 1);  // approve rA
    // flash expires
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 600);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
}

static void test_deny_is_sticky_per_id(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);   // → DENY
    prompt_ui_button(&ui, BTN_CENTER, 2); // deny rA
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 600);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
}

static void test_auto_hide_when_prompt_disappears(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    TEST_ASSERT_TRUE(prompt_ui_view(&ui).visible);
    prompt_ui_update(&ui, make_absent(), true, 1);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_auto_hide_when_offline(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), false, 1);  // OFFLINE
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
}

static void test_new_id_replaces_visible_prompt(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", "x"), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);  // highlight = DENY
    TEST_ASSERT_EQUAL(OPT_DENY, prompt_ui_view(&ui).highlight);
    prompt_ui_update(&ui, make_prompt("rB", "Read", "y"), true, 2);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_TRUE(v.visible);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
    TEST_ASSERT_EQUAL_STRING("Read", v.tool);
}

static void test_flash_clears_after_500ms(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_CENTER, 1);  // approve at t=1
    // At t=499 still visible + flashing.
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 499);
    PromptView v1 = prompt_ui_view(&ui);
    TEST_ASSERT_TRUE(v1.visible);
    TEST_ASSERT_NOT_NULL(v1.flash_text);
    // At t=501 hidden.
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 501);
    TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
}

static void test_press_while_hidden_is_noop(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_button(&ui, BTN_CENTER, 1);  // no UI to act on
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hidden_until_prompt_present);
    RUN_TEST(test_shows_with_default_approve);
    RUN_TEST(test_up_down_navigation_clamped);
    RUN_TEST(test_center_on_approve_emits_once_json);
    RUN_TEST(test_center_on_deny_emits_deny_json);
    RUN_TEST(test_center_on_dismiss_emits_nothing);
    RUN_TEST(test_dismiss_is_sticky_per_id);
    RUN_TEST(test_approve_is_sticky_per_id);
    RUN_TEST(test_deny_is_sticky_per_id);
    RUN_TEST(test_auto_hide_when_prompt_disappears);
    RUN_TEST(test_auto_hide_when_offline);
    RUN_TEST(test_new_id_replaces_visible_prompt);
    RUN_TEST(test_flash_clears_after_500ms);
    RUN_TEST(test_press_while_hidden_is_noop);
    return UNITY_END();
}
