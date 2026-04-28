# Card navigation + Eyes card implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add hardware button cycling between display cards (Status first, Eyes second, wrapping), with Eyes behavior and layout per `docs/superpowers/specs/2026-04-27-eyes-card-design.md`. Match deskhog-style semantics: **on-press** (after debounce), **next** on D0, **previous** on D2. No center-button / permission work on this branch.

**Architecture:** Keep a small `enum CardId` and `CARD_COUNT` in `main.cpp`. Millis-based debounce and edge detection (one fire per press) on GPIO0 and GPIO2—no new libraries, per eyes spec. Extract eye drawing and animation into `src/eyes.cpp` + `src/eyes.h` (`EyesAnim`, `eyes_tick`, `eyes_render`). Main loop stays non-blocking: drain BLE every iteration, update `BuddyState`, poll buttons, then either repaint Status when its data changed or repaint Eyes every frame when that card is active (animations). Optional ~16 ms frame pacing with `yield()` only (no `delay()` in `loop()` after setup).

**Tech stack:** Arduino (ESP32-S3), Adafruit ST7789/GFX, existing `lib/state`, `lib/protocol`, `src/ble_bridge`.

**References:** `docs/superpowers/specs/2026-04-27-eyes-card-design.md` (geometry, states, buttons, file split).

---

## File map

| File | Role |
|------|------|
| `src/eyes.h` | `struct EyesAnim`, `void eyes_reset(EyesAnim&)`, `void eyes_tick(EyesAnim&, BuddyState, uint32_t now_ms)`, `void eyes_render(Adafruit_ST7789&, const EyesAnim&, BuddyState)` |
| `src/eyes.cpp` | Implementation: timers, blink/squish/glance/scan phases, DISCONNECTED pulse |
| `src/main.cpp` | `CardId`, button pins/debounce, card wrap navigation, dispatch `render_status` vs `eyes_*`, non-blocking loop |
| `docs/superpowers/specs/2026-04-27-eyes-card-design.md` | Read-only spec (update only if you find a contradiction during implementation) |

---

### Task 1: `eyes` module (animation + render)

**Files:**

- Create: `src/eyes.h`
- Create: `src/eyes.cpp`
- Test: Manual on device (animations visible for each `BuddyState`)

- [ ] **Step 1: Add `src/eyes.h`**

Declare pixel geometry constants to match the spec (240×135, block 10 px, eye 30×30, gap 120 px, positions left X=30, right X=180, base Y=52; WAITING: eyes shifted up 20 px from that baseline).

```cpp
#pragma once
#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include "state.h"

struct EyesAnim {
    // Implementation fills: phase enums, last_ms, next_event_ms, horizontal offset, eye height (squish), etc.
};

void eyes_reset(EyesAnim& e);
void eyes_tick(EyesAnim& e, BuddyState state, uint32_t now_ms);
void eyes_render(Adafruit_ST7789& tft, const EyesAnim& e, BuddyState state);
```

- [ ] **Step 2: Implement `src/eyes.cpp` per design doc**

Behavior checklist (from spec):

- **DISCONNECTED:** Black screen; ~3 s dim pulse: draw eyes at grey `0x18C3` for 200 ms, then black; no blink/glance.
- **IDLE:** White eyes; ~8 s squish-blink (height 30→20→10→0→10→20→30, 100 ms/step); ~7 s ±2 s random glance (±20 px horizontal ~1 s return).
- **WORKING:** White eyes; horizontal scan ±30 px, ~1 s full cycle (sinusoidal OK).
- **WAITING:** White eyes, Y offset −20 px from IDLE center; same squish as IDLE; no horizontal movement.

Use `uint32_t` millis; handle wrap with `int32_t delta = (int32_t)(now_ms - e.last_ms)` (or re-init on backward time). Call `eyes_reset` when entering Eyes card from another card so timers don’t jump.

Drawing: `fillScreen(ST77XX_BLACK)` then draw each eye as a filled rect (width fixed 30, height from squish state), centered for squish on vertical axis per spec.

- [ ] **Step 3: Build firmware**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS (eyes module linked; main not calling it yet—temporarily add a forward declaration include only in a scratch build if needed, or implement Task 2 in same branch before final compile).

- [ ] **Step 4: Commit**

```bash
git add src/eyes.h src/eyes.cpp
git commit -m "feat(eyes): add eyes card animation and render module"
```

---

### Task 2: Card navigation + main integration

**Files:**

- Modify: `src/main.cpp` (full file refactor as below)

- [ ] **Step 1: Add card and button constants**

At top of `main.cpp` after includes:

```cpp
enum CardId : uint8_t { CARD_STATUS = 0, CARD_EYES, CARD_COUNT };

static const int PIN_BTN_NEXT  = 0;  // D0, next card
static const int PIN_BTN_PREV  = 2;  // D2, previous card
static const uint32_t BTN_DEBOUNCE_MS = 50;

static CardId   currentCard = CARD_STATUS;
static EyesAnim eyesAnim    = {};
```

In `setup()`, after `initDisplay()`:

```cpp
pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
pinMode(PIN_BTN_PREV, INPUT_PULLUP);
eyes_reset(eyesAnim);
```

**Hardware note:** Spec assumes both buttons active-LOW with internal pull-up. If D2 is wired like deskhog (external pull-down, active HIGH), change only `pinMode` / pressed test for that pin in one helper (e.g. `btn_prev_pressed()`).

- [ ] **Step 2: Rename status render and add dispatcher**

Rename `render()` to `render_status()` (same body as today). Add:

```cpp
static void paint_current_card() {
    if (currentCard == CARD_STATUS) {
        render_status();
    } else {
        eyes_render(tft, eyesAnim, currentState);
    }
}
```

Replace every `render();` that should refresh the visible UI with logic that considers `currentCard` (see Step 4).

- [ ] **Step 3: Debounced on-press helpers**

Add static state: previous stable readings, last debounce time, “already fired” latch per button.

Pattern (conceptual—implement inline in `loop` or small `static` functions):

```cpp
static bool edge_low(uint8_t pin, bool& stable_low, uint32_t& last_change_ms,
                     bool& consumed, uint32_t now) {
    bool raw = (digitalRead(pin) == LOW);
    if (raw != stable_low && (now - last_change_ms) >= BTN_DEBOUNCE_MS) {
        stable_low = raw;
        last_change_ms = now;
        if (stable_low && !consumed) { consumed = true; return true; }
    }
    if (!stable_low) consumed = false;
    return false;
}
```

On `true` return: for NEXT pin `currentCard = (CardId)((currentCard + 1) % CARD_COUNT);`, for PREV `currentCard = (CardId)((currentCard + CARD_COUNT - 1) % CARD_COUNT);`, then `eyes_reset(eyesAnim)` if `currentCard == CARD_EYES`, and call `paint_current_card()` immediately.

- [ ] **Step 4: Non-blocking `loop()` and repaint rules**

- Remove trailing `delay(20)`.
- At start of `loop()`, capture `uint32_t now = millis()`.
- After BLE drain and `protocol_parse_line` updates, compute `BuddyState next = state_derive(status, isLive())` and `stateChanged` / `msgChanged` as today.
- **Status card:** If `currentCard == CARD_STATUS` and (`stateChanged` || `msgChanged` || live-timeout transition), update `currentState`, `lastDrawnState`, `lastDrawnMsg`, call `paint_current_card()`.
- **Eyes card:** If `currentCard == CARD_EYES`, set `currentState = next` when `next != currentState` (optional: always sync), call `eyes_tick(eyesAnim, currentState, now)`, then `paint_current_card()` every iteration (or add `eyes_needs_redraw` if you optimize later).
- **1 s live timeout recheck:** When it fires, if `currentCard == CARD_STATUS` use existing repaint rules; if on Eyes, still update `currentState` and let `eyes_tick`/`eyes_render` pick up DISCONNECTED etc.
- **Frame pacing:** After work, `static uint32_t loop_start = now;` at top; at bottom `while ((millis() - loop_start) < 16) { yield(); }` to cap ~60 Hz without `delay()`.

- [ ] **Step 5: `setup()` initial paint**

After `ble_init`, call `paint_current_card()` instead of `render()` so the first card is consistent.

- [ ] **Step 6: Build and flash**

Run: `pio run -e adafruit_feather_esp32s3_reversetft`

Expected: PASS.

- [ ] **Step 7: Manual test checklist**

- Boot: Status shows; D0 → Eyes; D0 → Status (wrap); D2 steps backward.
- Eyes: disconnect BLE / wait for OFFLN—DISCONNECTED pulse; connect and exercise IDLE / WORKING / WAITING if desktop sends those states.
- No `delay()` inside `loop()` (only early `setup()` delays for display/BLE allowed).

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat(ui): card navigation (status/eyes) with debounced D0/D2"
```

---

## Spec coverage (self-review)

| Requirement | Task |
|-------------|------|
| Status + Eyes cards, D0 next, D2 prev, wrap | Task 2 |
| On-press after debounce | Task 2 Step 3 |
| Eyes layout + state animations | Task 1 |
| `eyes.h` / `eyes.cpp` + main dispatch | Tasks 1–2 |
| No new library deps | Plan uses millis debounce only |
| No `delay()` in main loop | Task 2 Step 4 |
| Center / permissions | Explicitly out of scope |

---

## Execution handoff

**Plan complete and saved to `docs/superpowers/plans/2026-04-28-card-navigation-eyes.md`.**

**1. Subagent-driven (recommended)** — one subagent per task, review between tasks.

**2. Inline execution** — run tasks sequentially in one session with checkpoints after each commit.

Which approach do you want?
