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

// Common arrival pattern: drop a fresh prompt then center-press to enter
// EXPANDED. Most existing tests want to drive Approve/Deny/Dismiss
// behaviour, which is only reachable via EXPANDED.
static void arrive_and_expand(PromptUi* ui, const char* id,
                              const char* tool, const char* hint,
                              uint32_t now) {
    prompt_ui_update(ui, make_prompt(id, tool, hint), true, now);
    prompt_ui_button(ui, BTN_CENTER, now + 1);
}

static void test_hidden_until_prompt_present(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_absent(), true, 0);
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}

static void test_arrives_collapsed_with_fields_set(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", "ls"), true, 100);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, v.mode);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
    TEST_ASSERT_EQUAL_STRING("Bash", v.tool);
    TEST_ASSERT_EQUAL_STRING("ls", v.hint);
}

static void test_collapsed_center_expands(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);
    prompt_ui_button(&ui, BTN_CENTER, 1);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_EQUAL(PROMPT_UI_EXPANDED, v.mode);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
}

static void test_collapsed_up_down_ignored_on_arrival(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_UP,   1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);
    TEST_ASSERT_EQUAL(OPT_APPROVE, prompt_ui_view(&ui).highlight);
}

static void test_up_down_navigation_clamped(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "r1", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN, 10);
    TEST_ASSERT_EQUAL(OPT_DENY, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_DOWN, 11);
    TEST_ASSERT_EQUAL(OPT_DISMISS, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_DOWN, 12);  // clamp
    TEST_ASSERT_EQUAL(OPT_DISMISS, prompt_ui_view(&ui).highlight);
    prompt_ui_button(&ui, BTN_UP,   13);
    prompt_ui_button(&ui, BTN_UP,   14);
    prompt_ui_button(&ui, BTN_UP,   15);  // clamp
    TEST_ASSERT_EQUAL(OPT_APPROVE, prompt_ui_view(&ui).highlight);
}

static void test_center_on_approve_emits_once_json(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "r_xyz", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_CENTER, 10);  // confirm APPROVE
    char buf[128] = {};
    TEST_ASSERT_TRUE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"cmd\":\"permission\",\"id\":\"r_xyz\",\"decision\":\"once\"}", buf);
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_center_on_deny_emits_deny_json(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "r_xyz", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);  // → DENY
    prompt_ui_button(&ui, BTN_CENTER, 11);  // confirm DENY
    char buf[128] = {};
    TEST_ASSERT_TRUE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"cmd\":\"permission\",\"id\":\"r_xyz\",\"decision\":\"deny\"}", buf);
}

static void test_center_on_dismiss_emits_nothing(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "r1", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);
    prompt_ui_button(&ui, BTN_DOWN,   11);   // → DISMISS
    prompt_ui_button(&ui, BTN_CENTER, 12);   // confirm DISMISS → COLLAPSED
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_dismiss_collapses_to_badge(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);
    prompt_ui_button(&ui, BTN_DOWN,   11);
    prompt_ui_button(&ui, BTN_CENTER, 12);   // dismiss rA → COLLAPSED
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);

    // Same id keeps it COLLAPSED — does not auto-expand.
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 600);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);

    // Snapshot drops the prompt → fully HIDDEN.
    prompt_ui_update(&ui, make_absent(), true, 700);
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}

static void test_collapsed_after_dismiss_center_reexpands(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);
    prompt_ui_button(&ui, BTN_DOWN,   11);
    prompt_ui_button(&ui, BTN_CENTER, 12);   // dismiss → COLLAPSED
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);

    prompt_ui_button(&ui, BTN_CENTER, 13);   // re-expand
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_EQUAL(PROMPT_UI_EXPANDED, v.mode);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
}

static void test_collapsed_after_dismiss_up_down_ignored(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);
    prompt_ui_button(&ui, BTN_DOWN,   11);
    prompt_ui_button(&ui, BTN_CENTER, 12);   // dismiss → COLLAPSED
    prompt_ui_button(&ui, BTN_UP,     13);
    prompt_ui_button(&ui, BTN_DOWN,   14);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);
}

static void test_new_id_during_collapse_replaces_with_collapsed(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);
    prompt_ui_button(&ui, BTN_DOWN,   11);
    prompt_ui_button(&ui, BTN_CENTER, 12);   // dismiss rA → COLLAPSED
    prompt_ui_update(&ui, make_prompt("rB", "Read", "y"), true, 13);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, v.mode);
    TEST_ASSERT_EQUAL_STRING("Read", v.tool);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
}

static void test_collapsed_offline_hides(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);   // → COLLAPSED
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), false, 1);  // OFFLINE
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}

static void test_dismiss_does_not_add_to_decided(void) {
    // Approving the same id after a previous Dismiss proves the id was
    // not added to last_decided_id by the Dismiss.
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);
    prompt_ui_button(&ui, BTN_DOWN,   11);
    prompt_ui_button(&ui, BTN_CENTER, 12);     // dismiss → COLLAPSED
    prompt_ui_update(&ui, make_absent(), true, 13);  // drop → HIDDEN
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 14);  // returns
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);
    prompt_ui_button(&ui, BTN_CENTER, 15);     // expand
    prompt_ui_button(&ui, BTN_CENTER, 16);     // approve rA
    char buf[128] = {};
    TEST_ASSERT_TRUE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"cmd\":\"permission\",\"id\":\"rA\",\"decision\":\"once\"}", buf);
}

static void test_approve_is_sticky_per_id(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_CENTER, 10);     // approve rA
    // flash expires
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 600);
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}

static void test_deny_is_sticky_per_id(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);     // → DENY
    prompt_ui_button(&ui, BTN_CENTER, 11);     // deny rA
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 600);
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}

static void test_auto_hide_when_prompt_disappears(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);
    prompt_ui_update(&ui, make_absent(), true, 1);
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_auto_hide_when_offline(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 0);
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), false, 1);  // OFFLINE
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}

static void test_new_id_replaces_collapsed_prompt(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "x", 0);
    prompt_ui_button(&ui, BTN_DOWN, 10);  // highlight = DENY (in EXPANDED)
    TEST_ASSERT_EQUAL(OPT_DENY, prompt_ui_view(&ui).highlight);
    prompt_ui_update(&ui, make_prompt("rB", "Read", "y"), true, 11);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, v.mode);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
    TEST_ASSERT_EQUAL_STRING("Read", v.tool);
}

static void test_flash_clears_after_500ms(void) {
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "r1", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_CENTER, 10);     // approve at t=10
    // At t=509 still EXPANDED + flashing.
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 509);
    PromptView v1 = prompt_ui_view(&ui);
    TEST_ASSERT_EQUAL(PROMPT_UI_EXPANDED, v1.mode);
    TEST_ASSERT_NOT_NULL(v1.flash_text);
    // At t=520 hidden (>= 500 ms after flash start at 10).
    prompt_ui_update(&ui, make_prompt("r1", "Bash", ""), true, 520);
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}

static void test_press_while_hidden_is_noop(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_button(&ui, BTN_CENTER, 1);  // no UI to act on
    char buf[128] = {};
    TEST_ASSERT_FALSE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
}

static void test_dismiss_then_redrop_resends_returns_collapsed(void) {
    // Dismiss does NOT add id to last_decided_id, so a Dismiss → drop →
    // re-send cycle returns to COLLAPSED on the same id (not HIDDEN).
    PromptUi ui; prompt_ui_init(&ui);
    arrive_and_expand(&ui, "rA", "Bash", "", 0);
    prompt_ui_button(&ui, BTN_DOWN,   10);
    prompt_ui_button(&ui, BTN_DOWN,   11);
    prompt_ui_button(&ui, BTN_CENTER, 12);     // dismiss → COLLAPSED
    prompt_ui_update(&ui, make_absent(), true, 13);  // drop → HIDDEN
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 14);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hidden_until_prompt_present);
    RUN_TEST(test_arrives_collapsed_with_fields_set);
    RUN_TEST(test_collapsed_center_expands);
    RUN_TEST(test_collapsed_up_down_ignored_on_arrival);
    RUN_TEST(test_up_down_navigation_clamped);
    RUN_TEST(test_center_on_approve_emits_once_json);
    RUN_TEST(test_center_on_deny_emits_deny_json);
    RUN_TEST(test_center_on_dismiss_emits_nothing);
    RUN_TEST(test_dismiss_collapses_to_badge);
    RUN_TEST(test_dismiss_then_redrop_resends_returns_collapsed);
    RUN_TEST(test_collapsed_after_dismiss_center_reexpands);
    RUN_TEST(test_collapsed_after_dismiss_up_down_ignored);
    RUN_TEST(test_new_id_during_collapse_replaces_with_collapsed);
    RUN_TEST(test_collapsed_offline_hides);
    RUN_TEST(test_dismiss_does_not_add_to_decided);
    RUN_TEST(test_approve_is_sticky_per_id);
    RUN_TEST(test_deny_is_sticky_per_id);
    RUN_TEST(test_auto_hide_when_prompt_disappears);
    RUN_TEST(test_auto_hide_when_offline);
    RUN_TEST(test_new_id_replaces_collapsed_prompt);
    RUN_TEST(test_flash_clears_after_500ms);
    RUN_TEST(test_press_while_hidden_is_noop);
    return UNITY_END();
}
