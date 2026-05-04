# Daily token count on StatusCard — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse the wire protocol's `tokens_today` field and display it as a prominent line on the StatusCard (`<count> tokens today`), with abbreviated formatting (`523`, `31.2K`, `1.0M`).

**Architecture:** Three layers, built bottom-up. (1) New `lib/protocol/format.{h,cpp}` provides a pure `format_token_count` helper with its own host-test suite. (2) `lib/protocol/protocol.{h,cpp}` gains a `tokens_today` field on `ClaudeStatus` and parses it from incoming snapshots, preserving previous values when the field is absent. (3) `src/ui/cards/StatusCard.{h,cpp}` renders the formatted count between the existing counters line and the message text, with all three blocks at adjusted y-coordinates to make room.

**Tech Stack:** C++17, Arduino-ESP32, Adafruit_GFX, ArduinoJson 7, Unity test framework via PlatformIO.

**Spec:** `docs/superpowers/specs/2026-05-04-daily-tokens-display-design.md`

---

## File map

**Create:**
- `lib/protocol/format.h` — `format_token_count` declaration + size constant
- `lib/protocol/format.cpp` — implementation
- `test/test_format/test_format.cpp` — host tests for `format_token_count`

**Modify:**
- `lib/protocol/protocol.h` — add `uint32_t tokens_today;` to `ClaudeStatus`
- `lib/protocol/protocol.cpp` — parse the field with partial-update semantics
- `test/test_protocol/test_protocol.cpp` — add 4 new tests covering the field
- `src/ui/cards/StatusCard.h` — add `last_drawn_tokens_today_` tracking field
- `src/ui/cards/StatusCard.cpp` — shift existing y-coords, render new size-2 token line, extend `isDirty()`

---

## Phase 1 — Format helper

A pure function with no Arduino dependencies, host-tested in isolation. Built first so the rendering layer in Phase 3 has a working primitive to call.

### Task 1: `format_token_count` helper + tests

**Files:**
- Create: `lib/protocol/format.h`
- Create: `lib/protocol/format.cpp`
- Create: `test/test_format/test_format.cpp`

- [ ] **Step 1: Write the header file**

```cpp
// lib/protocol/format.h
#pragma once
#include <stddef.h>
#include <stdint.h>

// Render `n` into `buf` as a compact human-readable string suitable for
// a small display.
//
// Boundaries are pre-rounded so values never spuriously cross a width
// threshold. Worst-case rendered width is 5 chars + NUL.
//
//   n <      1 000  →  integer        e.g. "0", "7", "523"
//   n <     99 500  →  one decimal K  e.g. "1.0K", "9.9K", "31.2K", "99.4K"
//   n <    999 500  →  integer K      e.g. "100K", "523K", "999K"
//   n <  9 950 000  →  one decimal M  e.g. "1.0M", "4.3M", "9.9M"
//   n >= 9 950 000  →  integer M      e.g. "10M", "42M"
//
// `buf_len` MUST be at least 8 (room for the longest 5-char string +
// NUL + 2 spare). Returns the number of chars written (excluding NUL),
// or 0 on error (null buf, buf_len < 8).
size_t format_token_count(uint32_t n, char* buf, size_t buf_len);

// Minimum buffer size callers must allocate. Use `char buf[kFormatTokenCountBufLen];`.
constexpr size_t kFormatTokenCountBufLen = 8;
```

- [ ] **Step 2: Write the failing test file**

Create `test/test_format/test_format.cpp`:

```cpp
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
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `pio test -e native -f test_format 2>&1 | tail -10`

Expected: build error — `format.h` doesn't exist yet (or is empty), no `format_token_count` symbol. The test_format suite fails to compile.

- [ ] **Step 4: Write the implementation**

Create `lib/protocol/format.cpp`:

```cpp
#include "format.h"
#include <stdio.h>

size_t format_token_count(uint32_t n, char* buf, size_t buf_len) {
    if (!buf || buf_len < kFormatTokenCountBufLen) {
        if (buf && buf_len > 0) buf[0] = 0;
        return 0;
    }

    int written;
    if (n < 1000) {
        // integer: "0".."999"
        written = snprintf(buf, buf_len, "%u", (unsigned)n);
    } else if (n < 99500) {
        // one decimal K: "1.0K".."99.4K"
        // Round half-up at the tenths place.
        unsigned tenths = (unsigned)((n + 50) / 100);   // n/100, rounded
        unsigned whole  = tenths / 10;
        unsigned frac   = tenths % 10;
        written = snprintf(buf, buf_len, "%u.%uK", whole, frac);
    } else if (n < 999500) {
        // integer K: "100K".."999K"
        // Round half-up at the unit place (1000).
        unsigned k = (unsigned)((n + 500) / 1000);
        written = snprintf(buf, buf_len, "%uK", k);
    } else if (n < 9950000) {
        // one decimal M: "1.0M".."9.9M"
        // Round half-up at the tenths-of-million (i.e. 100_000).
        unsigned tenths = (unsigned)((n + 50000) / 100000);
        unsigned whole  = tenths / 10;
        unsigned frac   = tenths % 10;
        written = snprintf(buf, buf_len, "%u.%uM", whole, frac);
    } else {
        // integer M: "10M" and above
        // Round half-up at the unit place (1_000_000).
        unsigned m = (unsigned)((n + 500000) / 1000000);
        written = snprintf(buf, buf_len, "%uM", m);
    }

    if (written < 0 || (size_t)written >= buf_len) {
        buf[0] = 0;
        return 0;
    }
    return (size_t)written;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native -f test_format 2>&1 | tail -25`

Expected: All 19 tests pass:
```
test_zero ... PASSED
test_single_digit ... PASSED
test_three_digits ... PASSED
test_just_below_1k ... PASSED
test_one_k ... PASSED
test_below_round_to_10k ... PASSED
test_31_2_k ... PASSED
test_just_below_100k ... PASSED
test_100k_boundary ... PASSED
test_523_k ... PASSED
test_just_below_1m ... PASSED
test_1m_boundary ... PASSED
test_one_m ... PASSED
test_4_3_m ... PASSED
test_just_below_10m ... PASSED
test_10m_boundary ... PASSED
test_large_m ... PASSED
test_buffer_too_small ... PASSED
test_null_buffer ... PASSED
```

If any test fails, the rounding boundary in `format_token_count` is off by one — adjust the `+50` / `+500` / `+50000` / `+500000` constants until matching expected values.

- [ ] **Step 6: Build the firmware to verify the new file compiles in the device target too**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -8`

Expected: PASS. (Nothing calls `format_token_count` from device code yet, but the file lives under `lib/protocol/` which `lib_ldf_mode = deep` in `platformio.ini` will auto-include.)

- [ ] **Step 7: Commit**

```bash
git add lib/protocol/format.h lib/protocol/format.cpp test/test_format/test_format.cpp
git commit -m "feat(protocol): format_token_count helper (523, 31.2K, 1.0M)"
```

---

## Phase 2 — Protocol parser extension

### Task 2: Parse `tokens_today` into `ClaudeStatus`

**Files:**
- Modify: `lib/protocol/protocol.h` (add field to struct)
- Modify: `lib/protocol/protocol.cpp` (parse the field)
- Modify: `test/test_protocol/test_protocol.cpp` (add 4 new tests)

- [ ] **Step 1: Write the failing tests**

Add these four functions to `test/test_protocol/test_protocol.cpp` (anywhere before `int main(...)`):

```cpp
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
```

And add the corresponding `RUN_TEST(...)` lines inside `main()` (find the existing `UNITY_BEGIN();` block, add these alongside the others):

```cpp
RUN_TEST(test_parse_tokens_today_present);
RUN_TEST(test_parse_tokens_today_missing_keeps_previous);
RUN_TEST(test_parse_tokens_today_zero_is_honoured);
RUN_TEST(test_parse_tokens_today_malformed_keeps_previous);
```

- [ ] **Step 2: Run the new tests — expect compile failure first**

Run: `pio test -e native -f test_protocol 2>&1 | tail -20`

Expected: Compile error — `'tokens_today' is not a member of 'ClaudeStatus'`. The test_protocol suite fails to build.

- [ ] **Step 3: Add the field to the struct**

In `lib/protocol/protocol.h`, replace:

```cpp
struct ClaudeStatus {
    uint8_t      total;
    uint8_t      running;
    uint8_t      waiting;
    char         msg[32];
    bool         valid;        // true once at least one snapshot has parsed
    ClaudePrompt prompt;
};
```

with:

```cpp
struct ClaudeStatus {
    uint8_t      total;
    uint8_t      running;
    uint8_t      waiting;
    char         msg[32];
    bool         valid;        // true once at least one snapshot has parsed
    ClaudePrompt prompt;
    uint32_t     tokens_today; // output tokens since local midnight (from bridge)
};
```

- [ ] **Step 4: Run the tests — should compile, but `test_parse_tokens_today_present` fails**

Run: `pio test -e native -f test_protocol 2>&1 | tail -20`

Expected: Compiles. Existing tests still pass. The four new tests:
- `test_parse_tokens_today_present` → FAIL (got 0, expected 31200)
- `test_parse_tokens_today_missing_keeps_previous` → PASS by luck (we never wrote to it)
- `test_parse_tokens_today_zero_is_honoured` → FAIL (got 99999, expected 0)
- `test_parse_tokens_today_malformed_keeps_previous` → PASS by luck

- [ ] **Step 5: Add the parser logic**

In `lib/protocol/protocol.cpp`, find the block:

```cpp
    out->total   = doc["total"]   | out->total;
    out->running = doc["running"] | out->running;
    out->waiting = doc["waiting"] | out->waiting;
```

Add immediately below it:

```cpp
    if (doc["tokens_today"].is<uint32_t>()) {
        out->tokens_today = doc["tokens_today"].as<uint32_t>();
    }
    // else: leave previous value unchanged (matches partial-update semantics)
```

- [ ] **Step 6: Run the tests — all pass**

Run: `pio test -e native -f test_protocol 2>&1 | tail -25`

Expected: All previous test_protocol tests still pass, plus all 4 new ones pass.

- [ ] **Step 7: Build the firmware to confirm the new struct member compiles in the device target**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -8`

Expected: PASS. The new `tokens_today` field defaults to 0 wherever `ClaudeStatus` is zero-initialised (e.g. in `AppState`).

- [ ] **Step 8: Commit**

```bash
git add lib/protocol/protocol.h lib/protocol/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(protocol): parse tokens_today into ClaudeStatus"
```

---

## Phase 3 — StatusCard renders the token line

The biggest change is purely visual. Built in two tasks: first move the existing y-coords (counters up 4, message down 12) and add the new dirty-tracking field; then add the actual render code for the token line.

### Task 3: Shift existing layout + add dirty-tracking field

**Files:**
- Modify: `src/ui/cards/StatusCard.h` (add tracking field, init in ctor)
- Modify: `src/ui/cards/StatusCard.cpp` (move y-coords, init field, snapshot field)

- [ ] **Step 1: Add the tracking field to the header**

In `src/ui/cards/StatusCard.h`, replace:

```cpp
private:
    const AppState& state_;
    bool            ever_drawn_;
    BuddyState      last_drawn_state_;
    char            last_drawn_msg_[sizeof(ClaudeStatus::msg)];
    bool            last_drawn_live_;
    uint32_t        last_recheck_ms_;
};
```

with:

```cpp
private:
    const AppState& state_;
    bool            ever_drawn_;
    BuddyState      last_drawn_state_;
    char            last_drawn_msg_[sizeof(ClaudeStatus::msg)];
    bool            last_drawn_live_;
    uint32_t        last_recheck_ms_;
    uint32_t        last_drawn_tokens_today_;
};
```

- [ ] **Step 2: Initialise the new field in the constructor**

In `src/ui/cards/StatusCard.cpp`, replace the constructor:

```cpp
StatusCard::StatusCard(const AppState& state)
    : state_(state),
      ever_drawn_(false),
      last_drawn_state_(STATE_DISCONNECTED),
      last_drawn_msg_{0},
      last_drawn_live_(false),
      last_recheck_ms_(0) {}
```

with:

```cpp
StatusCard::StatusCard(const AppState& state)
    : state_(state),
      ever_drawn_(false),
      last_drawn_state_(STATE_DISCONNECTED),
      last_drawn_msg_{0},
      last_drawn_live_(false),
      last_recheck_ms_(0),
      last_drawn_tokens_today_(0xFFFFFFFFu) {}
```

(`0xFFFFFFFFu` sentinel ensures the first render always paints because no real token count will equal it.)

- [ ] **Step 3: Shift the existing render y-coords**

In `src/ui/cards/StatusCard.cpp::render`, replace the entire body of `render()` *except* the final snapshot block with:

```cpp
void StatusCard::render(Display& display) {
    auto&               tft    = display.tft();
    const ClaudeStatus& status = state_.status();
    const BuddyState    bs     = state_.buddyState();
    const bool          live   = state_.isLive(millis());

    tft.fillScreen(ST77XX_BLACK);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    const char* name = state_name(bs);
    int16_t  x1, y1; uint16_t tw, th;
    tft.getTextBounds(name, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor((display.width() - (int)tw) / 2, 20);
    tft.print(name);

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(8, 58);            // moved up from 62
    tft.printf("total %u  run %u  wait %u",
               status.total, status.running, status.waiting);

    // Token line slot (y=70) — implemented in the next task.

    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    if (status.msg[0]) {
        tft.setCursor(8, 92);        // moved down from 80
        tft.printf("%.34s", status.msg);
        if (strlen(status.msg) > 34) {
            tft.setCursor(8, 104);   // moved down from 92
            tft.printf("%.34s", status.msg + 34);
        }
    }

    ui::drawFooter(tft, state_.deviceName(), live);

    last_drawn_state_ = bs;
    strncpy(last_drawn_msg_, status.msg, sizeof(last_drawn_msg_) - 1);
    last_drawn_msg_[sizeof(last_drawn_msg_) - 1] = 0;
    last_drawn_live_ = live;
    last_drawn_tokens_today_ = status.tokens_today;
    ever_drawn_      = true;
}
```

- [ ] **Step 4: Build the firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -6`

Expected: PASS. The card now leaves a gap at y=70 where the token line will go, and the message has shifted down. No functional difference yet from the user's POV (token line still missing).

- [ ] **Step 5: Run host tests to confirm nothing else broke**

Run: `pio test -e native 2>&1 | tail -10`

Expected: All suites pass.

- [ ] **Step 6: Commit**

```bash
git add src/ui/cards/StatusCard.h src/ui/cards/StatusCard.cpp
git commit -m "refactor(StatusCard): shift y-coords, prep for token line"
```

---

### Task 4: Render the daily-token line + isDirty integration

**Files:**
- Modify: `src/ui/cards/StatusCard.cpp` (add include, render block, dirty check)

- [ ] **Step 1: Add the format helper include**

At the top of `src/ui/cards/StatusCard.cpp`, alongside the existing includes, add:

```cpp
#include "format.h"
```

(`format.h` lives in `lib/protocol/`, the same library directory as `protocol.h` which is already included via `StatusCard.h`. PlatformIO's `lib_ldf_mode = deep` makes the include path automatic.)

- [ ] **Step 2: Insert the render block where Task 3 left a comment slot**

In `StatusCard::render`, find the comment line:

```cpp
    // Token line slot (y=70) — implemented in the next task.
```

Replace it with:

```cpp
    // Daily token count line (size 2, white, centred). Hidden until at
    // least one snapshot has arrived so we don't show "0 tokens today"
    // before any data has landed.
    if (status.valid) {
        char tok_buf[kFormatTokenCountBufLen];
        format_token_count(status.tokens_today, tok_buf, sizeof(tok_buf));
        char tok_line[32];
        snprintf(tok_line, sizeof(tok_line), "%s tokens today", tok_buf);

        tft.setTextSize(2);
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        int16_t tx1, ty1; uint16_t ttw, tth;
        tft.getTextBounds(tok_line, 0, 0, &tx1, &ty1, &ttw, &tth);
        tft.setCursor((display.width() - (int)ttw) / 2, 70);
        tft.print(tok_line);
        tft.setTextSize(1);  // restore for the message block below
    }
```

- [ ] **Step 3: Extend `isDirty()` to track token-count changes**

In `src/ui/cards/StatusCard.cpp`, replace:

```cpp
bool StatusCard::isDirty() const {
    if (!ever_drawn_) return true;
    if (state_.buddyState() != last_drawn_state_) return true;
    if (strncmp(last_drawn_msg_, state_.status().msg, sizeof(last_drawn_msg_)) != 0) return true;
    if (state_.isLive(millis()) != last_drawn_live_) return true;
    return false;
}
```

with:

```cpp
bool StatusCard::isDirty() const {
    if (!ever_drawn_) return true;
    if (state_.buddyState() != last_drawn_state_) return true;
    if (strncmp(last_drawn_msg_, state_.status().msg, sizeof(last_drawn_msg_)) != 0) return true;
    if (state_.isLive(millis()) != last_drawn_live_) return true;
    if (state_.status().tokens_today != last_drawn_tokens_today_) return true;
    return false;
}
```

- [ ] **Step 4: Build the firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -6`

Expected: PASS.

- [ ] **Step 5: Run all host tests to confirm no regressions**

Run: `pio test -e native 2>&1 | tail -12`

Expected: All previous tests pass; the new `test_format` and `test_protocol` cases from Phases 1–2 also pass.

- [ ] **Step 6: Commit**

```bash
git add src/ui/cards/StatusCard.cpp
git commit -m "feat(StatusCard): render '<count> tokens today' line"
```

---

## Phase 4 — Verification

### Task 5: End-to-end host + device verification

**Files:** none — verification only.

- [ ] **Step 1: Run the full host test suite**

Run: `pio test -e native 2>&1 | tail -10`

Expected: SUMMARY shows all suites passing — `test_buttons`, `test_format` (NEW), `test_prompt_ui`, `test_protocol`, `test_settings`, `test_state`. Total test count should be ~80+ (depending on existing counts; the new tests add 19 in `test_format` + 4 in `test_protocol`).

- [ ] **Step 2: Build firmware for the device target**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -8`

Expected: SUCCESS. Note flash size before vs after (should grow ~1 KB for the new code, well under the 64% headroom seen on previous builds).

- [ ] **Step 3: On-device smoke test**

Flash with `pio run -e adafruit_feather_esp32s3_reversetft -t upload`, then pair with Claude Desktop and verify on the StatusCard:

- Before any snapshot lands (just after boot), the token line is **not** shown — counters and message text appear at their new positions only.
- After the first snapshot, a centred line `<count> tokens today` appears between the counters (y=58) and the message text (y=92). At a fresh boot mid-day, expect a value like `5.4K tokens today`.
- Generate some output tokens (have Claude reply to a prompt). Within one snapshot interval, the displayed value updates.
- The line stays centred when the magnitude crosses a width boundary (e.g. `999` → `1.0K`). No left/right shifting visible.

- [ ] **Step 4: Verify nothing else regressed**

Switch to other cards (Eyes, Wifi if enabled, Prompt overlay during a permission prompt) and confirm:

- Eyes card animations unchanged.
- Footer (LIVE pill + device name + battery indicator if Phase 1 of the battery spec landed) renders identically on every card.
- Permission prompt overlay still pops up, accepts CENTER/UP/DOWN, etc.

- [ ] **Step 5: Final commit (only if any tweaks were needed during smoke)**

If the on-device smoke surfaced a layout tweak (e.g. y=70 needs to be y=68 to look balanced), apply the fix and:

```bash
git add <files>
git commit -m "fix(StatusCard): <on-device tweak>"
```

If nothing to fix, this task is purely a verification gate — no commit needed.

---

## Self-review

**Spec coverage:**

- "Protocol parser learns one new field" → Task 2 (struct + parser + 4 tests).
- "StatusCard renders a new prominent line" → Task 4 (render block + dirty check).
- "Number formatting helper" → Task 1 (helper + 19 tests).
- Layout shifts (counters up 4, message down 12) → Task 3 (y-coord move).
- Hidden when `status.valid == false` → Task 4 Step 2 (`if (status.valid)` gate).
- `0 tokens today` honoured when valid + zero → covered implicitly by the same gate; tested in Task 2 (`test_parse_tokens_today_zero_is_honoured`).
- Format boundaries (99 500, 999 500, 9 950 000) → Task 1's helper logic + boundary tests.
- Dirty tracking for `tokens_today` → Task 3 (field) + Task 4 (check).
- Backward compat (snapshots without `tokens_today`) → Task 2 test `test_parse_tokens_today_missing_keeps_previous`.
- Worst-case width fits (`99.4K tokens today` ≈ 17 chars × 12 px) → relies on `getTextBounds` re-centring; documented in Task 4.

No spec gaps.

**Placeholder scan:** No "TBD", no "implement later", no "similar to". Every code-changing step contains the actual code.

**Type/identifier consistency:**

- `format_token_count` signature consistent across Tasks 1, 4 (and `kFormatTokenCountBufLen` constant used in both).
- `tokens_today` name consistent across Tasks 2, 3, 4 (struct field, test inputs, render, dirty check).
- `last_drawn_tokens_today_` naming matches the existing `last_drawn_*` convention in StatusCard.
- y-coordinates: counters 58, token 70, message 92 / 104, footer 117 — same numbers in plan + spec + code.

No issues.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-04-daily-tokens-display.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
