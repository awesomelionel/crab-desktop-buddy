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

enum PromptUiMode : uint8_t {
    PROMPT_UI_HIDDEN    = 0,
    PROMPT_UI_EXPANDED  = 1,
    PROMPT_UI_COLLAPSED = 2,
};

struct PromptView {
    PromptUiMode mode;
    const char*  tool;
    const char*  hint;
    PromptOption highlight;
    const char*  flash_text;   // null when not flashing
    uint16_t     flash_color;  // RGB565; meaningful only when flash_text != null
};

struct PromptUi {
    PromptUiMode mode;
    char         current_id[40];
    char         tool[16];
    char         hint[64];
    PromptOption highlight;
    char         last_decided_id[40];

    bool         flashing;
    char         flash_text[16];
    uint16_t     flash_color;
    uint32_t     flash_start_ms;

    bool         pending_outgoing_set;
    char         pending_outgoing[96];
};

void prompt_ui_init  (PromptUi* ui);

// Reconcile UI state against an incoming snapshot.
//
// Mode transitions:
//   HIDDEN    + new prompt id (not in last_decided_id) → EXPANDED
//   EXPANDED  + snapshot drops prompt or !live         → HIDDEN
//   EXPANDED  + new id (different from current_id)     → EXPANDED (replace)
//   EXPANDED  + same id continues                      → EXPANDED (no-op)
//   COLLAPSED + snapshot drops prompt or !live         → HIDDEN
//   COLLAPSED + new id (different from current_id)     → EXPANDED (replace)
//   COLLAPSED + same id continues                      → COLLAPSED
//
// Also fires the flash → hide transition once the deadline elapses
// (Approve / Deny only — Dismiss has no flash-then-hide path now;
//  see prompt_ui_button).
void prompt_ui_update(PromptUi* ui, const ClaudePrompt& prompt,
                      bool live, uint32_t now_ms);

// Feed a debounced button event.
//   EXPANDED  + UP/DOWN     → move highlight
//   EXPANDED  + CENTER      → confirm highlighted option
//   COLLAPSED + CENTER      → re-EXPAND, highlight reset to OPT_APPROVE
//   COLLAPSED + UP/DOWN     → ignored
//   HIDDEN    + (any)       → ignored
void prompt_ui_button(PromptUi* ui, ButtonEvent ev, uint32_t now_ms);

// Read-only view used by main.cpp's renderer.
PromptView prompt_ui_view(const PromptUi* ui);

// Drain the queued outgoing JSON line, if any.
bool prompt_ui_take_outgoing(PromptUi* ui, char* buf, size_t buf_len);
