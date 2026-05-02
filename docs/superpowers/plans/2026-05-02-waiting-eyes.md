# WAITING-state Eyes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the prompt UI's full-screen overlay behaviour with a collapsible bottom badge so the eyes card can render a new WAITING animation (forward↔down gaze scan + bright-orange floating question marks) full-screen behind it.

**Architecture:** Three layers of change. (1) `lib/prompt_ui` gains a third `COLLAPSED` mode and changes `Dismiss` to collapse rather than hide. (2) `src/ui/CardController` routes the overlay push/pop on EXPANDED only, so the eyes card stays visible during COLLAPSED. (3) `src/ui/cards/EyesCard` learns the new WAITING animation, owns the badge rendering, and gets a shared bumped-height footer applied to all cards.

**Tech Stack:** C++17, Arduino-ESP32, Adafruit_GFX / Adafruit_ST7789, Unity test framework via PlatformIO.

**Spec:** `docs/superpowers/specs/2026-05-02-waiting-eyes-design.md`
**Visual reference:** `tools/waiting-eyes-demo.html`

---

## File map

**Create:**
- `src/ui/Footer.h` — shared footer helper interface
- `src/ui/Footer.cpp` — shared footer helper implementation
- `test/test_prompt_ui/test_prompt_ui.cpp` — extend with COLLAPSED mode tests (existing file, but new tests added)

**Modify:**
- `lib/prompt_ui/prompt_ui.h` — `bool visible` → `PromptUiMode mode`; `PromptView` updated
- `lib/prompt_ui/prompt_ui.cpp` — three-mode state machine; Dismiss collapses; `last_decided_id` only set on Approve/Deny
- `src/ui/CardController.cpp` — overlay push/pop driven by `mode == EXPANDED`
- `src/ui/cards/EyesCard.h` — add WAITING fields, accept `const PromptUi&` in ctor
- `src/ui/cards/EyesCard.cpp` — WAITING redesign: gaze scan, question-mark cluster, badge+footer rendering
- `src/ui/cards/StatusCard.cpp` — call shared `drawFooter()` helper
- `src/ui/cards/PromptCard.cpp` — call shared `drawFooter()` helper

**Test:**
- `test/test_prompt_ui/test_prompt_ui.cpp` — replace stale dismiss-sticky test, add COLLAPSED tests

---

## Phase 1 — Shared footer helper

The new EyesCard WAITING render needs the LIVE/device-name footer that StatusCard and PromptCard already draw inline. Per the spec, the footer is bumped to 18 px tall globally and gets a recentred LIVE pill. Extract a shared helper first so the bump applies in one place.

### Task 1: Create the shared footer helper

**Files:**
- Create: `src/ui/Footer.h`
- Create: `src/ui/Footer.cpp`

- [ ] **Step 1: Write the header file**

```cpp
// src/ui/Footer.h
#pragma once

#include <stdint.h>

class Adafruit_ST7789;

namespace ui {

// Shared bottom footer used by every card that wants to surface link
// state. Renders a green LIVE / red OFFLN pill on the left followed by
// the device name. Geometry is fixed and globally consistent: 18 px
// band, 12 px tall pill, 9 px (textSize 1) labels recentred on the
// band midline. Drawn directly to the TFT — no compositor.
//
// The caller is responsible for ensuring the band area (y >= 117) was
// erased to black before this is called. The helper does not erase its
// own background — that lets it composite cleanly on top of partial
// redraws driven by per-card dirty rects.
constexpr int   kFooterH         = 18;
constexpr int   kFooterTopY      = 135 - kFooterH;   // 117
constexpr int   kFooterPillX     = 4;
constexpr int   kFooterPillW     = 34;
constexpr int   kFooterPillH     = 12;

void drawFooter(Adafruit_ST7789& tft, const char* device_name, bool live);

}  // namespace ui
```

- [ ] **Step 2: Write the implementation file**

```cpp
// src/ui/Footer.cpp
#include "Footer.h"

#include <Adafruit_ST7789.h>

namespace ui {

void drawFooter(Adafruit_ST7789& tft, const char* device_name, bool live) {
    const int mid_y   = kFooterTopY + kFooterH / 2;
    const int pill_y  = mid_y - kFooterPillH / 2;

    // Pill body
    tft.fillRect(kFooterPillX, pill_y,
                 kFooterPillW, kFooterPillH,
                 live ? ST77XX_GREEN : ST77XX_RED);

    // Label baseline: textSize 1 is 8 px tall ascender, +1 to nudge
    // visually centred against the pill.
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE, live ? ST77XX_GREEN : ST77XX_RED);
    tft.setCursor(kFooterPillX + 4, pill_y + 3);
    tft.print(live ? "LIVE" : "OFFL");

    if (device_name && device_name[0]) {
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(kFooterPillX + kFooterPillW + 6, pill_y + 3);
        tft.print(device_name);
    }
}

}  // namespace ui
```

- [ ] **Step 3: Build firmware to verify the file compiles standalone**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS. (No call sites yet; just verifying header + cpp link cleanly.)

- [ ] **Step 4: Commit**

```bash
git add src/ui/Footer.h src/ui/Footer.cpp
git commit -m "feat(ui): shared drawFooter helper (kFooterH=18, recentred pill)"
```

---

### Task 2: Migrate StatusCard to the shared footer

**Files:**
- Modify: `src/ui/cards/StatusCard.cpp` (around lines 65–75; existing footer call uses `tft.setCursor(8, 118)` then prints LIVE / device name).

- [ ] **Step 1: Read the existing StatusCard footer block**

Run: `grep -n "118\|LIVE\|footer\|isLive\|deviceName" src/ui/cards/StatusCard.cpp`

Expected: Locate the inline footer block (currently at y=118, `tft.print(live ? "LIVE  " : "OFFLN ")`).

- [ ] **Step 2: Replace the inline footer block with a call to `ui::drawFooter`**

In `src/ui/cards/StatusCard.cpp` add the include at the top:

```cpp
#include "../Footer.h"
```

Replace the inline footer block (the `tft.setCursor(8, 118); tft.print(...)`-style lines) with:

```cpp
ui::drawFooter(tft, app_.deviceName(), app_.isLive(now_ms));
```

Confirm the surrounding code that erased the bottom area (if any) still erases the full new band: any prior `tft.fillRect` clearing the bottom strip should now cover at least `[0, ui::kFooterTopY .. 240, 135]`. If a stale narrower erase exists, widen it to `ui::kFooterTopY` and `ui::kFooterH`.

- [ ] **Step 3: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/StatusCard.cpp
git commit -m "refactor(ui): StatusCard uses shared drawFooter helper"
```

---

### Task 3: Migrate PromptCard to the shared footer

**Files:**
- Modify: `src/ui/cards/PromptCard.cpp` (around line 99: `if (has_footer_) { ... tft.setCursor(8, 118); tft.print(footer_live_ ? "LIVE  " : "OFFLN "); ... }`).

- [ ] **Step 1: Read the existing PromptCard footer block**

Run: `grep -n "has_footer_\|footer_device_\|footer_live_\|118" src/ui/cards/PromptCard.cpp`

Expected: Locate the `if (has_footer_) { ... }` block.

- [ ] **Step 2: Replace the inline footer block with the shared helper**

In `src/ui/cards/PromptCard.cpp` add the include at the top:

```cpp
#include "../Footer.h"
```

Replace the body of the `if (has_footer_)` block with:

```cpp
if (has_footer_) {
    ui::drawFooter(tft, footer_device_, footer_live_);
}
```

If the surrounding draw routine clears a band at the bottom of the screen, ensure that band is at least `ui::kFooterH` tall (the 18 px band starting at `ui::kFooterTopY = 117`). PromptCard currently draws option rows in the middle of the screen — confirm their y coordinates do not overlap `[117..135]`. If any do, raise them by `(18 - 8) = 10 px` so they clear the new footer.

- [ ] **Step 3: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/PromptCard.cpp
git commit -m "refactor(ui): PromptCard uses shared drawFooter helper"
```

---

## Phase 2 — Three-mode prompt UI

Replace the binary `bool visible` with a tri-state enum. Update Dismiss semantics. New tests drive the change.

### Task 4: Add the `PromptUiMode` enum (header + view)

**Files:**
- Modify: `lib/prompt_ui/prompt_ui.h`

- [ ] **Step 1: Edit `prompt_ui.h` to add the enum and migrate the struct fields**

Replace the existing enum block + struct in `lib/prompt_ui/prompt_ui.h` with:

```cpp
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
```

- [ ] **Step 2: Build host tests so we see the failing call sites**

Run: `pio test -e native -f test_prompt_ui 2>&1 | head -40`

Expected: COMPILE FAILURES at every reference to `ui->visible` and `prompt_ui_view(&ui).visible`. That's the signal we need — the rest of Phase 2 fixes those.

- [ ] **Step 3: Do not commit yet — Task 5 lands the implementation that makes the build green again.**

---

### Task 5: Implement the three-mode state machine

**Files:**
- Modify: `lib/prompt_ui/prompt_ui.cpp`

- [ ] **Step 1: Replace `lib/prompt_ui/prompt_ui.cpp` with the new implementation**

```cpp
#include "prompt_ui.h"
#include <string.h>
#include <stdio.h>

static const uint16_t COLOR_GREEN  = 0x07E0;
static const uint16_t COLOR_RED    = 0xF800;
static const uint16_t COLOR_YELLOW = 0xFFE0;

static const uint32_t FLASH_MS = 500;

static void hide(PromptUi* ui) {
    ui->mode             = PROMPT_UI_HIDDEN;
    ui->flashing         = false;
    ui->flash_text[0]    = 0;
    ui->flash_start_ms   = 0;
    // Note: do NOT clear pending_outgoing here. A pending response
    // queued just before hiding (e.g., from CENTER) must still drain.
}

static void show(PromptUi* ui, const ClaudePrompt& p) {
    ui->mode = PROMPT_UI_EXPANDED;
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

static void collapse(PromptUi* ui) {
    ui->mode           = PROMPT_UI_COLLAPSED;
    ui->flashing       = false;
    ui->flash_text[0]  = 0;
    ui->flash_start_ms = 0;
    // current_id, tool, hint, highlight all preserved so re-EXPAND
    // restores the same prompt context.
}

void prompt_ui_init(PromptUi* ui) {
    memset(ui, 0, sizeof(*ui));
    ui->mode = PROMPT_UI_HIDDEN;
}

void prompt_ui_update(PromptUi* ui, const ClaudePrompt& p,
                      bool live, uint32_t now_ms) {
    // Fire flash → hide first (only Approve/Deny set flashing now;
    // Dismiss collapses synchronously without a flash).
    if (ui->mode == PROMPT_UI_EXPANDED && ui->flashing &&
        (uint32_t)(now_ms - ui->flash_start_ms) >= FLASH_MS) {
        hide(ui);
    }

    if (!live) {
        if (ui->mode != PROMPT_UI_HIDDEN) hide(ui);
        return;
    }

    if (!p.present) {
        if (ui->mode != PROMPT_UI_HIDDEN) hide(ui);
        return;
    }

    // p.present && live
    // While flashing (within the flash window), let the flash run its
    // course even if the prompt id is in the dismissed set. The
    // dismissed-id check only suppresses re-showing after the flash
    // hides the UI.
    if (!ui->flashing && strcmp(p.id, ui->last_decided_id) == 0) {
        // Same id was previously DECIDED (Approve or Deny). Stay (or
        // become) hidden. Note: Dismiss no longer writes
        // last_decided_id, so a Dismiss → drop → re-send cycle will
        // re-EXPAND on the next update.
        if (ui->mode != PROMPT_UI_HIDDEN) hide(ui);
        return;
    }

    if (ui->mode != PROMPT_UI_HIDDEN &&
        strcmp(p.id, ui->current_id) == 0) {
        return;  // same prompt, no change (preserves COLLAPSED)
    }

    // Either a new id arrived, or we were HIDDEN. EXPAND.
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
    if (ui->mode == PROMPT_UI_HIDDEN) return;

    if (ui->mode == PROMPT_UI_COLLAPSED) {
        // Only CENTER re-expands; UP/DOWN ignored.
        if (ev == BTN_CENTER) {
            ui->mode      = PROMPT_UI_EXPANDED;
            ui->highlight = OPT_APPROVE;
        }
        return;
    }

    // EXPANDED
    if (ui->flashing) return;

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
                    // No flash, no last_decided_id write — Dismiss now
                    // collapses to badge so the user can re-engage later.
                    collapse(ui);
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
    v.mode        = ui->mode;
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
```

- [ ] **Step 2: Do not run tests yet.** Existing tests still reference `.visible`. Task 6 fixes them in lockstep.

---

### Task 6: Update existing tests to the new API + add COLLAPSED tests

**Files:**
- Modify: `test/test_prompt_ui/test_prompt_ui.cpp`

- [ ] **Step 1: Replace stale `.visible` checks with `mode == PROMPT_UI_*`**

In `test/test_prompt_ui/test_prompt_ui.cpp`, replace every `prompt_ui_view(&ui).visible` access with a mode check. Specifically:

```cpp
// Before
TEST_ASSERT_FALSE(prompt_ui_view(&ui).visible);
// After
TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);

// Before
TEST_ASSERT_TRUE(prompt_ui_view(&ui).visible);
// After
TEST_ASSERT_EQUAL(PROMPT_UI_EXPANDED, prompt_ui_view(&ui).mode);
```

(Apply throughout the file. `test_shows_with_default_approve`, `test_auto_hide_when_prompt_disappears`, `test_auto_hide_when_offline`, `test_new_id_replaces_visible_prompt`, `test_flash_clears_after_500ms`, etc.)

- [ ] **Step 2: Replace `test_dismiss_is_sticky_per_id` with the new collapse-not-sticky behaviour**

Find this function (currently around line 87):

```cpp
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
```

Replace with:

```cpp
static void test_dismiss_collapses_to_badge(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);  // dismiss rA → COLLAPSED
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);

    // Same id keeps it COLLAPSED — does not re-expand.
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 600);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);

    // Snapshot drops the prompt → fully HIDDEN.
    prompt_ui_update(&ui, make_absent(), true, 700);
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}
```

And update the `RUN_TEST` block at the bottom to swap `test_dismiss_is_sticky_per_id` for `test_dismiss_collapses_to_badge`.

- [ ] **Step 3: Add new tests covering COLLAPSED mode transitions**

Add the following new tests above `int main(int, char**)`:

```cpp
static void test_dismiss_then_redrop_resends_reexpands(void) {
    // Spec: Dismiss does NOT add id to last_decided_id, so a
    // Dismiss → drop → re-send cycle re-EXPANDS the same id.
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);  // dismiss → COLLAPSED
    prompt_ui_update(&ui, make_absent(), true, 4);  // drop → HIDDEN
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 5);
    TEST_ASSERT_EQUAL(PROMPT_UI_EXPANDED, prompt_ui_view(&ui).mode);
}

static void test_collapsed_center_reexpands(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);  // dismiss → COLLAPSED
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);

    prompt_ui_button(&ui, BTN_CENTER, 4);  // re-expand
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_EQUAL(PROMPT_UI_EXPANDED, v.mode);
    TEST_ASSERT_EQUAL(OPT_APPROVE, v.highlight);
}

static void test_collapsed_up_down_ignored(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);  // dismiss → COLLAPSED
    prompt_ui_button(&ui, BTN_UP,    4);
    prompt_ui_button(&ui, BTN_DOWN,  5);
    TEST_ASSERT_EQUAL(PROMPT_UI_COLLAPSED, prompt_ui_view(&ui).mode);
}

static void test_new_id_during_collapse_reexpands(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);  // dismiss → COLLAPSED on rA
    prompt_ui_update(&ui, make_prompt("rB", "Read", "y"), true, 4);
    PromptView v = prompt_ui_view(&ui);
    TEST_ASSERT_EQUAL(PROMPT_UI_EXPANDED, v.mode);
    TEST_ASSERT_EQUAL_STRING("Read", v.tool);
}

static void test_collapsed_offline_hides(void) {
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);  // dismiss → COLLAPSED
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), false, 4);  // OFFLINE
    TEST_ASSERT_EQUAL(PROMPT_UI_HIDDEN, prompt_ui_view(&ui).mode);
}

static void test_dismiss_does_not_add_to_decided(void) {
    // Approve the same id after a previous Dismiss to prove
    // last_decided_id is empty post-dismiss.
    PromptUi ui; prompt_ui_init(&ui);
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 0);
    prompt_ui_button(&ui, BTN_DOWN, 1);
    prompt_ui_button(&ui, BTN_DOWN, 2);
    prompt_ui_button(&ui, BTN_CENTER, 3);     // dismiss → COLLAPSED
    prompt_ui_update(&ui, make_absent(), true, 4);  // drop → HIDDEN
    prompt_ui_update(&ui, make_prompt("rA", "Bash", ""), true, 5);  // returns
    TEST_ASSERT_EQUAL(PROMPT_UI_EXPANDED, prompt_ui_view(&ui).mode);
    prompt_ui_button(&ui, BTN_CENTER, 6);     // approve rA
    char buf[128] = {};
    TEST_ASSERT_TRUE(prompt_ui_take_outgoing(&ui, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"cmd\":\"permission\",\"id\":\"rA\",\"decision\":\"once\"}", buf);
}
```

Add the matching `RUN_TEST` lines at the bottom:

```cpp
RUN_TEST(test_dismiss_then_redrop_resends_reexpands);
RUN_TEST(test_collapsed_center_reexpands);
RUN_TEST(test_collapsed_up_down_ignored);
RUN_TEST(test_new_id_during_collapse_reexpands);
RUN_TEST(test_collapsed_offline_hides);
RUN_TEST(test_dismiss_does_not_add_to_decided);
```

- [ ] **Step 4: Run host tests — expect all to pass**

Run: `pio test -e native -f test_prompt_ui`

Expected: All tests pass (renamed dismiss test + 6 new COLLAPSED tests + the unchanged Approve/Deny/auto-hide/etc).

- [ ] **Step 5: Commit**

```bash
git add lib/prompt_ui/prompt_ui.h lib/prompt_ui/prompt_ui.cpp \
        test/test_prompt_ui/test_prompt_ui.cpp
git commit -m "feat(prompt_ui): tri-state mode (HIDDEN/EXPANDED/COLLAPSED), Dismiss collapses

Dismiss now collapses to a persistent badge instead of marking the
prompt id as decided. Only Approve/Deny add to last_decided_id. New
COLLAPSED mode is reachable via Dismiss and exits via CENTER (re-EXPAND)
or snapshot drop / OFFLINE."
```

---

## Phase 3 — CardController routes overlay on EXPANDED only

### Task 7: Push the prompt overlay only when EXPANDED

**Files:**
- Modify: `src/ui/CardController.cpp` (around lines 124–138).

- [ ] **Step 1: Replace the overlay push/pop block**

In `src/ui/CardController.cpp`, replace the current overlay reconciliation block:

```cpp
PromptView pv = prompt_ui_view(&prompt_);
if (pv.visible && !stack_.hasOverlay()) {
    prompt_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
    stack_.pushOverlay(&prompt_card_);
    prompt_visible_ = true;
    bus_.publish(EventKind::PromptShow);
} else if (!pv.visible && stack_.hasOverlay()) {
    stack_.clearOverlay();
    prompt_visible_ = false;
    bus_.publish(EventKind::PromptHide);
} else if (pv.visible) {
    prompt_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
}
```

with:

```cpp
PromptView pv = prompt_ui_view(&prompt_);
const bool want_overlay = (pv.mode == PROMPT_UI_EXPANDED);
if (want_overlay && !stack_.hasOverlay()) {
    prompt_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
    stack_.pushOverlay(&prompt_card_);
    prompt_visible_ = true;
    bus_.publish(EventKind::PromptShow);
} else if (!want_overlay && stack_.hasOverlay()) {
    stack_.clearOverlay();
    prompt_visible_ = false;
    bus_.publish(EventKind::PromptHide);
} else if (want_overlay) {
    prompt_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
}
```

So the eyes card stays the active carousel card while the prompt is COLLAPSED, and the overlay pops back in when the user re-EXPANDS via CENTER or when a new id arrives.

- [ ] **Step 2: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add src/ui/CardController.cpp
git commit -m "feat(ui): CardController shows prompt overlay only when EXPANDED

COLLAPSED mode no longer pushes the full-screen overlay, so the active
carousel card (typically EyesCard) stays visible behind the badge."
```

---

## Phase 4 — EyesCard WAITING redesign

The biggest task. Built up incrementally so the firmware compiles and behaves sensibly after each step.

### Task 8: Wire `PromptUi&` into EyesCard

**Files:**
- Modify: `src/ui/cards/EyesCard.h`
- Modify: `src/ui/cards/EyesCard.cpp`
- Modify: `src/ui/CardController.h` and `src/ui/CardController.cpp`

- [ ] **Step 1: Update `EyesCard.h` to take a `const PromptUi&`**

In `src/ui/cards/EyesCard.h`:

```cpp
#pragma once

#include <stdint.h>

#include "../Card.h"
#include "../../core/AppState.h"
#include "state.h"
#include "prompt_ui.h"      // NEW

class Adafruit_ST7789;

class EyesCard : public Card {
public:
    EyesCard(const AppState& state, const PromptUi& prompt);   // CHANGED

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;

private:
    // ... existing private declarations unchanged ...

    const AppState& state_;
    const PromptUi& prompt_;     // NEW

    // ... existing members unchanged ...
};
```

- [ ] **Step 2: Update `EyesCard.cpp` constructor signature + initializer list**

In `src/ui/cards/EyesCard.cpp`, change:

```cpp
EyesCard::EyesCard(const AppState& state)
    : state_(state) {
```

to:

```cpp
EyesCard::EyesCard(const AppState& state, const PromptUi& prompt)
    : state_(state), prompt_(prompt) {
```

- [ ] **Step 3: Update `CardController` to pass the `PromptUi` reference**

In `src/ui/CardController.h`, the `eyes_card_` member is constructed in the controller's initializer list with just `app`. Update its construction in `src/ui/CardController.cpp` line 16:

```cpp
// Before
eyes_card_(app),
// After
eyes_card_(app, prompt),
```

(The `prompt` constructor parameter is already passed to `CardController` and stored as `prompt_`.)

- [ ] **Step 4: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ui/cards/EyesCard.h src/ui/cards/EyesCard.cpp src/ui/CardController.cpp
git commit -m "refactor(eyes): EyesCard takes PromptUi& so it can render the badge"
```

---

### Task 9: Add WAITING constants and member fields

**Files:**
- Modify: `src/ui/cards/EyesCard.h`
- Modify: `src/ui/cards/EyesCard.cpp` (top-of-file `namespace { ... }` constants block + member init in `resetAnim()`).

- [ ] **Step 1: Add the WAITING constants block to `EyesCard.cpp`**

In `src/ui/cards/EyesCard.cpp`, immediately after the existing `kWorkBlinkH` / `kWorkEraseH` constants and before the closing `}  // namespace`, add:

```cpp
// ---- STATE_WAITING redesign (collapsed-prompt eyes) ----
const int      kBaseWaitYNew         = 22;     // top of eye when neutral; was 32, raised so down-glance has clearance
const int      kWaitGlanceDownDy     = 14;     // additional eye-top y when glancing at the badge
const uint32_t kWaitScanPeriodMs     = 2000;   // full forward → down → forward cycle
const uint32_t kWaitScanEaseMs       = 250;    // cubic ease in / out duration
const uint32_t kWaitScanHoldDownMs   = 400;    // dwell at the down position
const uint32_t kWaitBlinkIntervalMs  = 4500;   // identical to IDLE
const uint32_t kWaitBlinkStepMs      = 70;

// Question-mark cluster
const uint32_t kQIntervalMs  = 3200;
const uint32_t kQLifetimeMs  = 3500;
const int      kQRiseY       = 32;
const int      kQDriftX      = 24;
const int      kQClusterN    = 5;
const int      kQBubbleCap   = 8;     // ring capacity (allows brief overlap)
const int      kQAnchorX     = 120;   // face centre
const int      kQAnchorY     = kBaseWaitYNew + 30 / 2 + 8;  // eye-mid + 8 = 45
const int8_t   kQSlotsX[5]   = { -14,  -6,   0,   7,  14 };
const int8_t   kQSlotsY[5]   = {   6,   1,   0,   2,   7 };
const uint16_t kQStaggerMs[5] = {  0,  60, 130, 200, 280 };
const uint8_t  kQSizes[5]    = {  28,  14,  28,  14,  28 };

// Bright orange in RGB565: r=31, g=29, b=0 → (31<<11)|(29<<5)|0 = 0xFBA0
const uint16_t kQColor       = 0xFBA0;

// Badge geometry (sits above the shared 18-px footer)
const int      kBadgeH       = 18;
const int      kBadgeMargin  = 8;
const int      kBadgeX       = kBadgeMargin;
const int      kBadgeW       = 240 - 2 * kBadgeMargin;
const int      kBadgeBottomGap = 4;
// kFooterH = 18 from src/ui/Footer.h
const int      kBadgeY       = 135 - 18 - kBadgeBottomGap - kBadgeH;  // 95
```

- [ ] **Step 2: Add the new EyesCard private members in `EyesCard.h`**

Add to the `private:` section of `EyesCard`, after the existing fields (preferably grouped at the bottom with a brief structural comment):

```cpp
    // ---- STATE_WAITING ----
    uint32_t   wait_scan_epoch_ms_;
    int8_t     draw_wait_gaze_dy_;        // 0..kWaitGlanceDownDy

    struct QBubble {
        uint32_t born_ms;
        int8_t   slot_x_offset;
        int8_t   slot_y_offset;
        uint8_t  size;
        bool     alive;
    };
    QBubble    q_bubbles_[8];     // capacity = kQBubbleCap
    uint32_t   next_q_spawn_ms_;

    // Dirty tracking for WAITING
    int8_t     last_wait_gaze_dy_;
    bool       last_badge_visible_;
    uint32_t   last_q_anim_tick_;   // bumped every frame while bubbles are live
```

- [ ] **Step 3: Initialise the new fields in `resetAnim()`**

In `EyesCard.cpp`, inside `resetAnim()`, before the closing brace:

```cpp
    wait_scan_epoch_ms_       = now;
    draw_wait_gaze_dy_        = 0;
    next_q_spawn_ms_          = now;
    for (auto& b : q_bubbles_) b.alive = false;

    last_wait_gaze_dy_        = 0;
    last_badge_visible_       = false;
    last_q_anim_tick_         = 0;
```

- [ ] **Step 4: Initialise the new dirty-tracking fields in the constructor body**

In `EyesCard.cpp`'s constructor body (the block initialising `frame_valid_`, `last_state_`, etc. around line 82), append:

```cpp
    last_wait_gaze_dy_   = 0;
    last_badge_visible_  = false;
    last_q_anim_tick_    = 0;
```

- [ ] **Step 5: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS. The constants are unused until later tasks; the compiler may warn — that's fine until Task 11 wires them up.

- [ ] **Step 6: Commit**

```bash
git add src/ui/cards/EyesCard.h src/ui/cards/EyesCard.cpp
git commit -m "refactor(eyes): add STATE_WAITING constants + member fields"
```

---

### Task 10: Add `tickWaitGaze` and `tickQuestionMarks` helpers

**Files:**
- Modify: `src/ui/cards/EyesCard.h` (private method declarations)
- Modify: `src/ui/cards/EyesCard.cpp`

- [ ] **Step 1: Add the helper declarations to the `private:` section of `EyesCard.h`**

```cpp
    void tickWaitGaze(uint32_t now_ms);
    void tickQuestionMarks(uint32_t now_ms);
```

- [ ] **Step 2: Implement `tickWaitGaze` in `EyesCard.cpp`**

Add (anywhere among the other tick helpers — near `tickGlanceIdle`):

```cpp
void EyesCard::tickWaitGaze(uint32_t now) {
    const uint32_t t = (now - wait_scan_epoch_ms_) % kWaitScanPeriodMs;
    const uint32_t e1 = kWaitScanEaseMs;                            // 250
    const uint32_t e2 = e1 + kWaitScanHoldDownMs;                   // 650
    const uint32_t e3 = e2 + kWaitScanEaseMs;                       // 900

    int dy;
    if (t < e1) {
        // ease-down (cubic ease-out): k = t/e1, dy = D * (1 - (1-k)^3)
        float k = (float)t / (float)e1;
        float eased = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
        dy = (int)((float)kWaitGlanceDownDy * eased);
    } else if (t < e2) {
        dy = kWaitGlanceDownDy;
    } else if (t < e3) {
        float k = (float)(t - e2) / (float)kWaitScanEaseMs;
        float eased = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
        dy = (int)((float)kWaitGlanceDownDy * (1.0f - eased));
    } else {
        dy = 0;
    }
    draw_wait_gaze_dy_ = (int8_t)dy;
}
```

- [ ] **Step 3: Implement `tickQuestionMarks` in `EyesCard.cpp`**

Add immediately after `tickWaitGaze`:

```cpp
void EyesCard::tickQuestionMarks(uint32_t now) {
    // Prune dead bubbles
    for (auto& b : q_bubbles_) {
        if (b.alive && (now - b.born_ms) > kQLifetimeMs) {
            b.alive = false;
        }
    }

    // Spawn new cluster if due
    if ((int32_t)(now - next_q_spawn_ms_) >= 0) {
        for (int i = 0; i < kQClusterN; ++i) {
            // Find a free slot
            for (auto& b : q_bubbles_) {
                if (!b.alive) {
                    b.alive          = true;
                    b.born_ms        = now + kQStaggerMs[i];
                    b.slot_x_offset  = kQSlotsX[i];
                    b.slot_y_offset  = kQSlotsY[i];
                    b.size           = kQSizes[i];
                    break;
                }
            }
        }
        next_q_spawn_ms_ = now + kQIntervalMs;
    }

    // Bump the anim tick whenever any bubble is live so isDirty() picks it up.
    bool any_live = false;
    for (const auto& b : q_bubbles_) if (b.alive) { any_live = true; break; }
    if (any_live) last_q_anim_tick_ = now;
}
```

- [ ] **Step 4: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS. (The functions still aren't called from `tick()` — the next task does that.)

- [ ] **Step 5: Commit**

```bash
git add src/ui/cards/EyesCard.h src/ui/cards/EyesCard.cpp
git commit -m "feat(eyes): tickWaitGaze + tickQuestionMarks helpers (logic only)"
```

---

### Task 11: Re-arm WAITING in `armState()` and re-route `tick()`

**Files:**
- Modify: `src/ui/cards/EyesCard.cpp` (`armState()` line ~145, `tick()` line ~250).

- [ ] **Step 1: Update the `STATE_WAITING` case in `armState()`**

Find in `EyesCard.cpp`:

```cpp
case STATE_WAITING:
    blink_i_       = -1;
    next_blink_ms_ = now + kBlinkIntervalMs;
    break;
```

Replace with:

```cpp
case STATE_WAITING:
    blink_i_              = -1;
    next_blink_ms_        = now + kWaitBlinkIntervalMs;
    blink_step_deadline_ms_ = 0;
    wait_scan_epoch_ms_   = now;
    draw_wait_gaze_dy_    = 0;
    next_q_spawn_ms_      = now + 600;  // first cluster appears ~0.6 s after entering state
    for (auto& b : q_bubbles_) b.alive = false;
    last_q_anim_tick_     = 0;
    break;
```

- [ ] **Step 2: Update the `STATE_WAITING` case in `tick()`**

Find in `EyesCard.cpp`:

```cpp
case STATE_WAITING:
    tickBlink(now_ms);
    draw_base_y_ = kBaseWaitY;
    draw_dx_     = 0;
    draw_h_      = (blink_i_ >= 0) ? kBlinkH[blink_i_] : 30;
    break;
```

Replace with:

```cpp
case STATE_WAITING:
    tickBlink(now_ms);
    // Only run the new gaze-scan + question marks while a prompt is
    // actually live (EXPANDED or COLLAPSED). If WAITING was entered
    // without a prompt (defensive — currently impossible), fall back
    // to plain open eyes.
    if (prompt_.mode != PROMPT_UI_HIDDEN) {
        tickWaitGaze(now_ms);
        tickQuestionMarks(now_ms);
        draw_base_y_ = (int16_t)(kBaseWaitYNew + draw_wait_gaze_dy_);
    } else {
        draw_wait_gaze_dy_ = 0;
        draw_base_y_ = kBaseWaitYNew;
    }
    draw_dx_     = 0;
    draw_h_      = (blink_i_ >= 0) ? kBlinkH[blink_i_] : 30;
    break;
```

- [ ] **Step 3: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS. Eyes now physically scan during WAITING, but the question marks and badge aren't drawn yet (rendering tasks below).

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/EyesCard.cpp
git commit -m "feat(eyes): WAITING tick drives new gaze scan + ?-cluster updates"
```

---

### Task 12: Render the WAITING frame (eyes + question marks + badge + footer)

**Files:**
- Modify: `src/ui/cards/EyesCard.cpp` (`drawFrame()` — add a `STATE_WAITING` branch).

- [ ] **Step 1: Add a `drawFooter` include path**

At the top of `src/ui/cards/EyesCard.cpp`, with the other includes:

```cpp
#include "../Footer.h"
```

- [ ] **Step 2: Insert a STATE_WAITING branch in `drawFrame()`**

In `drawFrame()`, the existing structure handles `STATE_DISCONNECTED` and `STATE_WORKING` with explicit branches and falls through to the legacy IDLE/WAITING two-rect renderer. Insert a dedicated `STATE_WAITING` branch *before* the fall-through `tft.fillScreen(...)`. Place it right after the `STATE_WORKING` branch (just before the fall-through `tft.fillScreen(ST77XX_BLACK);`):

```cpp
if (state == STATE_WAITING) {
    const bool prompt_live = (prompt_.mode != PROMPT_UI_HIDDEN);
    if (!prompt_live) {
        // Fallback: legacy plain WAITING (no badge, no ?s). Cheap.
        tft.fillScreen(ST77XX_BLACK);
        int h = draw_h_;
        if (h > 0) {
            int16_t top = (int16_t)(kBaseWaitYNew + 15 - h / 2);
            tft.fillRect(kLeftX,  top, kEyeW, h, ST77XX_WHITE);
            tft.fillRect(kRightX, top, kEyeW, h, ST77XX_WHITE);
        }
        return;
    }

    // Per CLAUDE.md: never fillScreen during a continuous animation.
    // State entry does one full clear; subsequent frames erase only
    // the (eyes ∪ question-mark) region, then redraw both, then
    // touch up the badge + footer if their state changed.
    if (full_clear) {
        tft.fillScreen(ST77XX_BLACK);
    } else {
        // Union erase rect: eye band y ∈ [kBaseWaitYNew,
        // kBaseWaitYNew + kWaitGlanceDownDy + 30] = [22, 66] AND
        // question-mark band y ∈ [kQAnchorY - kQRiseY - 4,
        // kQAnchorY + 8] = [9, 53]. Union: [9, 66] (58 px tall).
        // Width: full screen for simplicity (~13 920 px erase, still
        // 4–5 ms cheaper than fillScreen).
        const int erase_y = 9;
        const int erase_h = 66 - erase_y + 1;
        tft.fillRect(0, erase_y, 240, erase_h, ST77XX_BLACK);
    }

    // 1) Eyes
    int h = draw_h_;
    if (h > 0) {
        int16_t top = (int16_t)(kBaseWaitYNew + draw_wait_gaze_dy_ + 15 - h / 2);
        tft.fillRect(kLeftX,  top, kEyeW, h, ST77XX_WHITE);
        tft.fillRect(kRightX, top, kEyeW, h, ST77XX_WHITE);
    }

    // 2) Question marks (drawn after eyes so they composite on top)
    const uint32_t now = millis();
    tft.setTextColor(kQColor, ST77XX_BLACK);
    for (const auto& b : q_bubbles_) {
        if (!b.alive) continue;
        const uint32_t age = now - b.born_ms;
        if ((int32_t)age < 0) continue;       // staggered, not yet born
        if (age > kQLifetimeMs) continue;
        const float t    = (float)age / (float)kQLifetimeMs;
        const float ease = 1.0f - (1.0f - t) * (1.0f - t);   // quadratic ease-out
        const int   y    = kQAnchorY - (int)((float)kQRiseY * ease) + b.slot_y_offset;
        const int   x    = kQAnchorX + b.slot_x_offset + (int)((float)kQDriftX * ease);
        // GFX text size 1 = 6×8 px; the design wants 14 / 28 px, so
        // textSize 2 ≈ 14 px and textSize 4 ≈ 28 px. Round per-bubble.
        const uint8_t ts = (b.size >= 24) ? 4 : 2;
        tft.setTextSize(ts);
        tft.setCursor(x, y - ts * 4);          // baseline-ish nudge
        tft.print('?');
    }

    // 3) Badge (only if COLLAPSED — when EXPANDED, the overlay covers
    // the whole screen so we wouldn't be drawing this branch anyway,
    // but the check makes the intent explicit).
    if (prompt_.mode == PROMPT_UI_COLLAPSED) {
        // Erase + redraw the badge band on first entry to COLLAPSED or
        // on full_clear; otherwise it's static and untouched per frame.
        const bool badge_dirty = full_clear || !last_badge_visible_;
        if (badge_dirty) {
            tft.fillRect(0, kBadgeY - 1, 240,
                         kBadgeH + 2, ST77XX_BLACK);
            // Border (1-px frame in mid-grey)
            const uint16_t border = 0x7BEF;
            tft.drawRect(kBadgeX, kBadgeY, kBadgeW, kBadgeH, border);
            // Orange ? icon at left
            tft.setTextSize(1);
            tft.setTextColor(kQColor, ST77XX_BLACK);
            tft.setCursor(kBadgeX + 6, kBadgeY + 5);
            tft.print('?');
            // Tool · "approve?" label
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(kBadgeX + 16, kBadgeY + 5);
            tft.print(prompt_.tool[0] ? prompt_.tool : "?");
            tft.print(" \xB7 approve?");        // 0xB7 ≈ middle dot in CP437
            // Press hint at right
            tft.setTextColor(0x7BEF, ST77XX_BLACK);
            const char* hint = "press \x7";    // 0x07 ≈ small bullet in CP437
            tft.setCursor(kBadgeX + kBadgeW - 50, kBadgeY + 5);
            tft.print(hint);
        }
    }

    // 4) Footer — drawn every frame the badge changes; otherwise the
    // footer band is below the union erase so it's untouched.
    if (full_clear || prompt_.mode == PROMPT_UI_COLLAPSED) {
        // Footer area must be cleared first since we may be drawing
        // here on a full clear, or rebuilding when the badge appears.
        tft.fillRect(0, ui::kFooterTopY, 240, ui::kFooterH, ST77XX_BLACK);
        ui::drawFooter(tft, "", true);   // device name plumbed in Step 4
    }
    return;
}
```

- [ ] **Step 3: Plumb `device_name` and `live` into the WAITING render**

The badge/footer rendering needs the device name and live state, which `EyesCard` does not currently hold. Two options:

  a. Add a `setFooter(const char* name, bool live)` setter mirroring `PromptCard::setFooter`, called by `CardController` on every `tick()` while the prompt is COLLAPSED. (Symmetric with PromptCard.)
  b. Pass `AppState::deviceName()` / `AppState::isLive(now)` directly. `EyesCard` already has `state_` (an `AppState&`) but `isLive()` requires a `now_ms` argument that the renderer doesn't have.

Pick (a) for symmetry. Add to `EyesCard.h`:

```cpp
public:
    void setFooter(const char* device_name, bool live);

private:
    char       footer_device_[20];
    bool       footer_live_;
```

In `EyesCard.cpp`, add:

```cpp
#include <string.h>

void EyesCard::setFooter(const char* name, bool live) {
    if (name) {
        strncpy(footer_device_, name, sizeof(footer_device_) - 1);
        footer_device_[sizeof(footer_device_) - 1] = 0;
    } else {
        footer_device_[0] = 0;
    }
    footer_live_ = live;
}
```

Initialise in the constructor body (`footer_device_[0] = 0; footer_live_ = false;`).

In the WAITING render block (Step 2), replace:

```cpp
ui::drawFooter(tft, "", true);
```

with:

```cpp
ui::drawFooter(tft, footer_device_, footer_live_);
```

In `src/ui/CardController.cpp`, alongside the existing `prompt_card_.setFooter(...)` call (after Task 7's edit), unconditionally update the eyes card's footer state every tick:

```cpp
eyes_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
```

Place this just before the `prompt_ui_take_outgoing(...)` call.

- [ ] **Step 4: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS. WAITING now renders the full design when a prompt is COLLAPSED.

- [ ] **Step 5: Commit**

```bash
git add src/ui/cards/EyesCard.h src/ui/cards/EyesCard.cpp src/ui/CardController.cpp
git commit -m "feat(eyes): render WAITING — gaze scan, ? cluster, badge, footer

Eyes draw full-screen with the new forward<->down gaze scan and
floating bright-orange ? cluster. Badge appears above the shared
footer when prompt_ui mode is COLLAPSED. Per CLAUDE.md, the eye and
?-region share a union erase rect (~14 KB vs ~65 KB fillScreen)."
```

---

### Task 13: Wire `isDirty()` for the new WAITING fields

**Files:**
- Modify: `src/ui/cards/EyesCard.cpp` (`isDirty()` and the trailing `last_*` snapshot in `render()`).

- [ ] **Step 1: Extend `isDirty()` with the new fields**

Find `EyesCard::isDirty()` and replace it with:

```cpp
bool EyesCard::isDirty() const {
    if (!frame_valid_) return true;
    if (last_state_      != state_.buddyState()) return true;
    if (last_h_          != draw_h_)            return true;
    if (last_dx_         != draw_dx_)           return true;
    if (last_base_y_     != draw_base_y_)       return true;
    if (last_blink_h_    != draw_blink_h_)      return true;
    if (last_dots_n_     != draw_dots_n_)       return true;
    if (last_disc_age_   != disc_age_ms_)       return true;
    if (last_wait_gaze_dy_ != draw_wait_gaze_dy_) return true;
    const bool badge_now = (state_.buddyState() == STATE_WAITING &&
                            prompt_.mode == PROMPT_UI_COLLAPSED);
    if (last_badge_visible_ != badge_now) return true;
    // While bubbles live, last_q_anim_tick_ moves every tick → always dirty.
    if (last_q_anim_tick_ != 0) {
        // Compare against current tick: if current tickQuestionMarks
        // wrote a fresh stamp (any_live), force redraw.
        // (Tick is bumped to `now` during tickQuestionMarks; the
        // render() snapshot below caches it. If they differ, redraw.)
        // Implementation: any_live check.
        for (const auto& b : q_bubbles_) if (b.alive) return true;
    }
    return false;
}
```

- [ ] **Step 2: Snapshot the new fields in `render()`**

In `EyesCard::render()`, append to the existing `last_* = ...;` snapshot block:

```cpp
last_wait_gaze_dy_  = draw_wait_gaze_dy_;
last_badge_visible_ = (last_state_ == STATE_WAITING &&
                      prompt_.mode == PROMPT_UI_COLLAPSED);
// last_q_anim_tick_ updated in tickQuestionMarks; render leaves it.
```

- [ ] **Step 3: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/EyesCard.cpp
git commit -m "feat(eyes): WAITING dirty-tracking (gaze, badge visibility, ? cluster)"
```

---

### Task 14: Run host tests + build firmware end-to-end

**Files:** none — verification only.

- [ ] **Step 1: Run host tests**

Run: `pio test -e native`

Expected: All test suites pass — `test_protocol`, `test_state`, `test_settings`, `test_buttons`, `test_prompt_ui`. The new COLLAPSED-mode tests added in Task 6 should appear in the `test_prompt_ui` output.

- [ ] **Step 2: Build firmware for the device target**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS, no warnings introduced beyond what was already there.

- [ ] **Step 3: Stage on-device test plan in commit message**

No commit needed unless prior steps were stashed. If everything is clean, this task is purely a verification gate.

---

### Task 15: On-device smoke test

**Files:** none — manual verification on hardware.

- [ ] **Step 1: Flash the firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft -t upload`

- [ ] **Step 2: Pair with Claude Desktop and trigger a permission prompt**

Trigger any tool that asks for permission (e.g. `Bash` running anything new). Verify:

- Full-screen prompt UI appears as today.
- The "Dismiss" highlight + CENTER press now collapses to a bottom badge instead of clearing the screen entirely.
- The eyes are visible above the badge, scanning between forward and down-toward-the-badge every ~2 s.
- A cluster of 5 bright-orange `?` glyphs (alternating big/small) drifts up-and-right from face centre every ~3.2 s.
- Pressing CENTER while the badge is visible re-shows the full prompt UI.
- Pressing Approve from the re-expanded UI sends `{"cmd":"permission","id":"...","decision":"once"}` and returns to IDLE.
- The shared LIVE pill / device name footer is now 18 px tall and visually centred on every card (Status, Eyes during waiting, Prompt overlay).

- [ ] **Step 3: Negative path — re-prompt of an Approved id**

After approving once, ensure the desktop re-sending the same id (within the heartbeat window) does NOT re-EXPAND the prompt — sticky-decided semantics still hold for Approve/Deny.

- [ ] **Step 4: Negative path — re-prompt of a Dismissed id**

Dismiss a prompt, wait for the desktop to drop the prompt field, then trigger the desktop to re-send it. Verify the UI re-EXPANDS (Dismiss is no longer sticky — this is the spec change).

- [ ] **Step 5: Final commit (if any clean-ups)**

```bash
git status
# If anything is uncommitted from minor on-device fixes:
git add <files>
git commit -m "fix(eyes): <on-device finding>"
```

---

## Self-review

**Spec coverage check.** Walking through each section of `2026-05-02-waiting-eyes-design.md`:

- "Three prompt UI modes" → Task 4 (header), Task 5 (impl), Task 6 (tests).
- "First prompt for a new id auto-expands" → Preserved in `prompt_ui_update`'s "new id" branch (Task 5).
- "Dismiss collapses to badge" → `collapse()` in Task 5; tested in `test_dismiss_collapses_to_badge` (Task 6).
- "Center press on badge re-expands" → `prompt_ui_button` COLLAPSED branch (Task 5); tested in `test_collapsed_center_reexpands` (Task 6).
- "Last-decided semantics" → `last_decided_id` only written in OPT_APPROVE / OPT_DENY (Task 5); tested in `test_dismiss_does_not_add_to_decided` (Task 6).
- "WAITING eye scan" → `tickWaitGaze` (Task 10), wired in `tick()` (Task 11), rendered in `drawFrame()` (Task 12).
- "Question marks" — origin, alternation, drift, lifetime, colour → Task 9 (constants), Task 10 (logic), Task 12 (render).
- "Badge geometry" + "Colour palette" → Task 9 (constants) + Task 12 (render).
- "Footer global bump" → Tasks 1–3 + helper used in Tasks 12 & wired by CardController in Task 12 Step 3.
- "Compositing rule" (eyes + ?s share dirty rect) → Union erase in Task 12 Step 2.
- "When the animation runs" — only when `prompt.mode != HIDDEN` → Task 11 Step 2 + Task 12 Step 2 fallback branch.
- "Performance budget" → Implementation matches: `fillRect` for the union region, no `fillScreen` per frame.

**Placeholder scan.** No "TBD", no "implement later", no "similar to". All code shown literally.

**Type/identifier consistency.** `PromptUiMode`, `PROMPT_UI_HIDDEN/EXPANDED/COLLAPSED`, `kBaseWaitYNew`, `kQColor`, `tickWaitGaze`, `tickQuestionMarks`, `setFooter`, `ui::drawFooter`, `ui::kFooterH`, `ui::kFooterTopY` — used consistently across all tasks.

**Risk notes.** PromptCard's existing draw routine (modified in Task 3) draws option rows whose y coordinates I haven't fully traced. The task's Step 2 instructs the implementer to verify they don't overlap the new 18-px footer band and to bump them up by 10 px if needed — that's the right hand-off but it's a micro-investigation the implementer must perform on the spot.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-02-waiting-eyes.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
