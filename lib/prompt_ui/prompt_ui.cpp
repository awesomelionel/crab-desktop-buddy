#include "prompt_ui.h"
#include <string.h>
#include <stdio.h>

// RGB565 — same encoding the rest of the firmware uses (Adafruit GFX
// ST77XX colors). Defined here as plain constants so the library
// compiles host-side without pulling Adafruit_GFX in.
static const uint16_t COLOR_GREEN  = 0x07E0;
static const uint16_t COLOR_RED    = 0xF800;
static const uint16_t COLOR_YELLOW = 0xFFE0;

static const uint32_t FLASH_MS = 500;

static void hide(PromptUi* ui) {
    ui->visible          = false;
    ui->flashing         = false;
    ui->flash_text[0]    = 0;
    ui->flash_start_ms   = 0;
    // Note: do NOT clear pending_outgoing here. A pending response
    // queued just before hiding (e.g., from CENTER) must still drain.
}

static void show(PromptUi* ui, const ClaudePrompt& p) {
    ui->visible = true;
    strncpy(ui->current_id, p.id,   sizeof(ui->current_id) - 1);
    ui->current_id[sizeof(ui->current_id) - 1] = 0;
    strncpy(ui->tool,       p.tool, sizeof(ui->tool) - 1);
    ui->tool[sizeof(ui->tool) - 1] = 0;
    strncpy(ui->hint,       p.hint, sizeof(ui->hint) - 1);
    ui->hint[sizeof(ui->hint) - 1] = 0;
    ui->highlight = OPT_APPROVE;
    ui->flashing  = false;
    ui->flash_text[0] = 0;
    ui->flash_start_ms = 0;
}

void prompt_ui_init(PromptUi* ui) {
    memset(ui, 0, sizeof(*ui));
}

void prompt_ui_update(PromptUi* ui, const ClaudePrompt& p,
                      bool live, uint32_t now_ms) {
    // Fire flash → hide first so the rest of the function operates on
    // post-flash state.
    if (ui->visible && ui->flashing &&
        (uint32_t)(now_ms - ui->flash_start_ms) >= FLASH_MS) {
        hide(ui);
    }

    if (!live) {
        if (ui->visible) hide(ui);
        return;
    }

    if (!p.present) {
        if (ui->visible) hide(ui);
        return;
    }

    // p.present && live
    // While flashing (within the flash window), let the flash run its course
    // even if the prompt id is in the dismissed set. The dismissed-id check
    // only suppresses re-showing after the flash hides the UI.
    if (!ui->flashing && strcmp(p.id, ui->last_decided_id) == 0) {
        // Same id was previously decided (approve/deny/dismiss) and flash has
        // already expired. Stay (or become) hidden.
        if (ui->visible) hide(ui);
        return;
    }

    if (ui->visible && strcmp(p.id, ui->current_id) == 0) {
        return;  // same prompt, no change
    }

    show(ui, p);
}

static void queue_outgoing(PromptUi* ui, const char* decision) {
    snprintf(ui->pending_outgoing, sizeof(ui->pending_outgoing),
             "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}",
             ui->current_id, decision);
    ui->pending_outgoing_set = true;
}

static void start_flash(PromptUi* ui, const char* text, uint16_t color,
                        uint32_t now_ms) {
    ui->flashing = true;
    strncpy(ui->flash_text, text, sizeof(ui->flash_text) - 1);
    ui->flash_text[sizeof(ui->flash_text) - 1] = 0;
    ui->flash_color = color;
    ui->flash_start_ms = now_ms;
}

void prompt_ui_button(PromptUi* ui, ButtonEvent ev, uint32_t now_ms) {
    if (!ui->visible || ui->flashing) return;

    switch (ev) {
        case BTN_UP:
            if (ui->highlight > 0)
                ui->highlight = (PromptOption)(ui->highlight - 1);
            return;
        case BTN_DOWN:
            if (ui->highlight < OPT_DISMISS)
                ui->highlight = (PromptOption)(ui->highlight + 1);
            return;
        case BTN_CENTER:
            switch (ui->highlight) {
                case OPT_APPROVE:
                    queue_outgoing(ui, "once");
                    strncpy(ui->last_decided_id, ui->current_id,
                            sizeof(ui->last_decided_id) - 1);
                    ui->last_decided_id[sizeof(ui->last_decided_id) - 1] = 0;
                    start_flash(ui, "SENT: APPROVE", COLOR_GREEN, now_ms);
                    return;
                case OPT_DENY:
                    queue_outgoing(ui, "deny");
                    strncpy(ui->last_decided_id, ui->current_id,
                            sizeof(ui->last_decided_id) - 1);
                    ui->last_decided_id[sizeof(ui->last_decided_id) - 1] = 0;
                    start_flash(ui, "SENT: DENY", COLOR_RED, now_ms);
                    return;
                case OPT_DISMISS:
                    strncpy(ui->last_decided_id, ui->current_id,
                            sizeof(ui->last_decided_id) - 1);
                    ui->last_decided_id[sizeof(ui->last_decided_id) - 1] = 0;
                    start_flash(ui, "DISMISSED", COLOR_YELLOW, now_ms);
                    return;
            }
            return;
        case BTN_NONE:
        default:
            return;
    }
}

PromptView prompt_ui_view(const PromptUi* ui) {
    PromptView v = {};
    v.visible     = ui->visible;
    v.tool        = ui->tool;
    v.hint        = ui->hint;
    v.highlight   = ui->highlight;
    v.flash_text  = ui->flashing ? ui->flash_text : nullptr;
    v.flash_color = ui->flash_color;
    return v;
}

bool prompt_ui_take_outgoing(PromptUi* ui, char* buf, size_t buf_len) {
    if (!ui->pending_outgoing_set) return false;
    if (!buf || buf_len == 0) return false;
    strncpy(buf, ui->pending_outgoing, buf_len - 1);
    buf[buf_len - 1] = 0;
    ui->pending_outgoing_set = false;
    ui->pending_outgoing[0]  = 0;
    return true;
}
