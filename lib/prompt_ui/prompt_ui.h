#pragma once
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"   // ClaudePrompt
#include "buttons.h"    // ButtonEvent

enum PromptOption : uint8_t {
    OPT_APPROVE = 0,
    OPT_DENY    = 1,
    OPT_DISMISS = 2,
};

struct PromptView {
    bool         visible;
    const char*  tool;
    const char*  hint;
    PromptOption highlight;
    const char*  flash_text;   // null when not flashing
    uint16_t     flash_color;  // RGB565; meaningful only when flash_text != null
};

struct PromptUi {
    bool         visible;
    char         current_id[24];
    char         tool[16];
    char         hint[64];
    PromptOption highlight;
    char         last_decided_id[24];

    bool         flashing;
    char         flash_text[16];
    uint16_t     flash_color;
    uint32_t     flash_start_ms;

    bool         pending_outgoing_set;
    char         pending_outgoing[96];
};

void prompt_ui_init  (PromptUi* ui);

// Reconcile UI state against an incoming snapshot. Hides the UI when
// `prompt.present` flips false or `live` is false. Reveals when a fresh,
// non-dismissed prompt arrives. Replaces if the visible id changes.
// Also fires the flash → hide transition once the deadline elapses.
void prompt_ui_update(PromptUi* ui, const ClaudePrompt& prompt,
                      bool live, uint32_t now_ms);

// Feed a debounced button event. Ignored when `!visible`.
void prompt_ui_button(PromptUi* ui, ButtonEvent ev, uint32_t now_ms);

// Read-only view used by main.cpp's renderer.
PromptView prompt_ui_view(const PromptUi* ui);

// Drain the queued outgoing JSON line, if any. Returns false if nothing
// to send, or if `buf` is null / `buf_len` is 0. On success, fills `buf`
// with a NUL-terminated string (truncated if buf_len is smaller than
// the queued line) and clears the queue.
bool prompt_ui_take_outgoing(PromptUi* ui, char* buf, size_t buf_len);
