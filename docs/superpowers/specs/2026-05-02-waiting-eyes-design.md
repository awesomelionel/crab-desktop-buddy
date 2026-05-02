# WAITING-state eyes — design

Date: 2026-05-02
Status: Draft for review
Related: `docs/superpowers/specs/2026-05-01-working-eyes-redesign.md` (sibling state),
`docs/superpowers/specs/2026-04-27-permission-buttons-design.md` (today's prompt UI),
`tools/waiting-eyes-demo.html` (visual prototype)

## Goal

Give `STATE_WAITING` a real visual identity. Today the eyes briefly use
`kBaseWaitY` and the IDLE blink loop, but they are almost never seen because
the full-screen prompt UI overlay covers the entire eyes card whenever a
permission prompt is pending. The user wants a distinct "buddy is waiting on
*you*" expression to be visible during the prompt, and the prompt UI itself
demoted to a small persistent badge so it doesn't hide the eyes.

## Summary of what changes

1. **Prompt UI gains a third state — `COLLAPSED` (badge).** Today the
   `PromptUi` struct is binary: `visible` true (full-screen overlay) or false
   (hidden). The new design adds a collapsed mode where the prompt is still
   live but rendered as a slim ~18 px-tall pill above the existing footer.
   The eyes carousel card draws full-screen behind it.

2. **First prompt for a new id auto-expands (today's behavior preserved).**
   When a never-seen prompt id arrives, the full-screen `PromptCard` overlay
   appears immediately. The user picks Approve / Deny / Dismiss as today.

3. **Dismiss collapses to badge instead of hiding.** Dismiss removes the
   full-screen overlay but the prompt is still pending — so the badge appears
   at the bottom of the eyes card. It stays for the lifetime of that prompt
   id (until snapshot drops `prompt`, the link goes OFFLINE, or the user
   re-expands and decides). There is no way to fully hide the badge while
   the prompt is still pending.

4. **Center press on the badge re-expands to the full prompt UI.** While
   only the badge is visible, D1 (center) re-shows the full-screen overlay.
   Up/Down do nothing. Other buttons do nothing.

5. **WAITING eyes get a new animation.** Eyes scan between forward gaze and
   a down-glance toward the badge, on a slow ~2 s oscillation. A cluster of
   five floating question marks (alternating big and small) drifts up and
   to the right from the centre of the face every ~3.2 s.

6. **WAITING `kBaseWaitY` moves up.** Today it is 32 (eye top). The new
   design uses 22 so the down-glance has clear vertical room and the eyes
   visually look "alert / leaning forward" rather than mid-screen.

## State model

### Three prompt UI modes

```
HIDDEN     ── snapshot has prompt with id Y not in last_decided_id ──▶ EXPANDED
EXPANDED   ── user presses Approve / Deny ────────────────────────▶ HIDDEN (id added to last_decided_id)
EXPANDED   ── user presses Dismiss ───────────────────────────────▶ COLLAPSED
COLLAPSED  ── user presses Center ────────────────────────────────▶ EXPANDED
COLLAPSED  ── snapshot drops prompt or link goes OFFLINE ─────────▶ HIDDEN
EXPANDED   ── snapshot drops prompt or link goes OFFLINE ─────────▶ HIDDEN
```

The currently-tracked `last_decided_id` (sticky-dismiss) field changes
meaning subtly: today it suppresses re-display for *any* dismissed id; in
the new design it should only be set when the user actually decided
(Approve / Deny). A Dismiss no longer marks the id as "done"; it just
collapses to the badge for the rest of that id's lifetime. This matches
the user intent: Dismiss = "I'll deal with it in a moment", not "ignore
forever".

### State derivation (unchanged)

`STATE_WAITING` is still derived from `status.waiting > 0`. The snapshot
field `prompt` drives the prompt UI state machine independently; the eyes
card just reads "is the prompt UI in COLLAPSED mode?" to decide whether to
render the badge.

## Layout (240 × 135 screen)

All coordinates are pixel-exact and validated in `tools/waiting-eyes-demo.html`.

```
y =   0 ┌────────────────────────────────────────┐
        │                                         │
        │             ?         ?                 │  question marks
        │         ?      ?                        │  rise from face
y =  22 │   ████████          ████████            │  centre, drift
        │   ████████          ████████            │  up-right
        │   ████████          ████████            │
        │   ████████          ████████            │  ← WAIT_BASE_Y = 22
        │                                         │     eyes glance down
        │                                         │     ~14 px every 2 s
        │   ░░░░░░░░          ░░░░░░░░            │  (down-glance shown
        │   ░░░░░░░░          ░░░░░░░░            │   in dotted form)
        │                                         │
y =  95 │   ┌──────────────────────────────────┐  │  badge: 8 px
        │   │ ? Bash · approve?       press ●  │  │  margins, full-
y = 113 │   └──────────────────────────────────┘  │  width − 16 px
        │   ┌────┐                                │
y = 117 │   │LIVE│ Claude-A1B2                    │  footer: 18 px tall
y = 135 └────────────────────────────────────────┘
```

### Geometry constants

| Element                 | Value                          | Notes                                          |
|-------------------------|--------------------------------|------------------------------------------------|
| Screen                  | 240 × 135                      | unchanged                                      |
| Eye width               | `kEyeW = 30`                   | unchanged                                      |
| Eye open height         | 30                             | unchanged                                      |
| Left eye top-left       | `kLeftX = 30`                  | unchanged                                      |
| Right eye top-left      | `kRightX = 180`                | unchanged                                      |
| WAIT eye baseline y     | `kBaseWaitY = 22`              | **changed** from 32 → 22                       |
| Down-glance offset      | `kWaitGlanceDownDy = 14`       | new                                            |
| Footer band height      | `kFooterH = 18`                | bumped from existing footer by 4 px            |
| Badge height            | `kBadgeH = 18`                 | new                                            |
| Badge x margin          | `kBadgeMargin = 8` each side   | new                                            |
| Badge width             | `240 − 2·kBadgeMargin = 224`   | derived                                        |
| Badge top y             | `135 − kFooterH − 4 − kBadgeH = 95` | 4 px gap between badge and footer        |

### Colour palette

| Element                | Colour                          | Notes                                       |
|------------------------|---------------------------------|---------------------------------------------|
| Background             | `ST77XX_BLACK`                  | unchanged                                   |
| Eyes                   | `ST77XX_WHITE`                  | unchanged                                   |
| Question marks         | bright orange, RGB565 `0xFBA0` (≈ `#FF7700`) | applies to both floating ?s and the badge `?` icon |
| Badge body             | `ST77XX_BLACK`                  | inherits screen background                  |
| Badge border (1 px)    | mid-grey (e.g. `0x7BEF`)        | distinct from white eyes, less attention than badge label |
| Badge `?` icon         | bright orange, same as floating ?s | visual link to the question marks         |
| Badge label text       | white                           | "Tool · approve?"                           |
| Badge press hint       | dim grey                        | "press ●"                                   |
| Footer LIVE pill       | green (`ST77XX_GREEN`)          | unchanged behaviour                         |
| Footer device label    | white                           | unchanged                                   |

## Animation

### Gaze scan

Two-position oscillation between forward (eye top at `kBaseWaitY = 22`) and
down (eye top at `kBaseWaitY + kWaitGlanceDownDy = 36`).

| Phase       | Duration | Description                              |
|-------------|----------|------------------------------------------|
| Ease-down   | 250 ms   | Cubic ease-out from forward to down      |
| Hold-down   | 400 ms   | Eyes hold the down position              |
| Ease-up     | 250 ms   | Cubic ease-out from down back to forward |
| Hold-fwd    | 1100 ms  | Eyes hold the forward position           |
| **Total**   | **2000 ms** | Full cycle                            |

Easing is computed in fixed-point or as a small lookup table — no `sinf()`
needed (one-axis cubic is cheap). The down-glance amount is small enough
(14 px) that the eyes do not approach the badge area.

### Blinks

Identical to IDLE: `kBlinkH = {30, 20, 10, 0, 10, 20, 30}` over 7 × 70 ms
steps, with a `kBlinkIntervalMs = 4500` ms idle period between blinks. The
blink is independent of the gaze scan — the eye height varies while the
baseline y still follows the scan animation, so a blink mid-down-glance
just shrinks the eyes vertically without breaking the scan.

### Question marks

A short-lived particle system. State lives in `EyesCard`. The cluster
re-spawns every `kQIntervalMs = 3200` ms while in COLLAPSED-or-EXPANDED
prompt-pending mode (i.e. while the prompt UI is non-HIDDEN — not just
WAITING state — see "When the animation runs" below).

Each spawn pushes 5 bubbles into a fixed-size circular buffer (capacity 8
to allow brief overlap when timings drift). Per-bubble fields:

| Field           | Bytes | Notes                                                  |
|-----------------|-------|--------------------------------------------------------|
| `born_ms`       | 4     | Spawn time, with stagger applied                       |
| `slot_x_offset` | 1     | Per-slot horizontal anchor, relative to face centre    |
| `slot_y_offset` | 1     | Per-slot vertical jitter                               |
| `size`          | 1     | Big (28) or small (14)                                 |

Per-slot constants (mirrors the demo):

| Slot | x offset | y offset | stagger (ms) | size (px) |
|------|---------:|---------:|-------------:|----------:|
| 0    | −14      | 6        | 0            | 28        |
| 1    | −6       | 1        | 60           | 14        |
| 2    | 0        | 0        | 130          | 28        |
| 3    | 7        | 2        | 200          | 14        |
| 4    | 14       | 7        | 280          | 28        |

Per-frame motion for each live bubble:

```
t        = (now - born_ms) / kQLifetimeMs            // 0..1, clamp
ease     = 1 - (1 - t)^2                             // ease-out quadratic
y_draw   = start_y - kQRiseY  * ease + slot_y_offset // 32 px upward total
x_draw   = start_x + kQDriftX * ease                 // 24 px rightward total
alpha    = 1 - t                                     // linear fade-out
size_px  = base_size * (0.9 + 0.1 * (1 - t))         // shrinks ~10 %
```

Constants:

| Constant         | Value | Notes                                            |
|------------------|------:|--------------------------------------------------|
| `kQIntervalMs`   | 3200  | Time between cluster spawns                      |
| `kQLifetimeMs`   | 3500  | Per-bubble lifetime; bubble removed at `t > 1`   |
| `kQRiseY`        | 32    | Total vertical rise across lifetime              |
| `kQDriftX`       | 24    | Total horizontal drift to the right              |
| `kQClusterN`     | 5     | Bubbles per spawn                                |
| Spawn anchor     | (face cx = 120, eye_mid_y + 8 = 22 + 15 + 8 = 45) | Origin between the eyes, slightly below midline |

Alpha is mapped to colour by linear blend against background: since the
background is always black under the question-mark area and the TFT is
RGB565 with no alpha, we approximate by scaling the amber RGB components
by `alpha`. This is cheap (single multiply per channel per draw) and
visually identical to true alpha against black.

### When the animation runs

The eyes card runs the WAITING animation when:

```
state == STATE_WAITING && prompt_ui_mode != HIDDEN
```

i.e. whenever a prompt is live (not yet decided), regardless of whether
the user has it expanded or collapsed. While `prompt_ui_mode == EXPANDED`
the eyes card is hidden behind the overlay so its render output is not
visible — but its tick still runs so timers stay coherent when the user
collapses back to badge mode.

If `STATE_WAITING` is true but `prompt_ui_mode == HIDDEN` (e.g. waiting
on a session that never sent a `prompt` field — currently impossible but
defensible), the eyes fall back to the legacy WAITING behaviour: open
eyes, occasional blinks, no scan, no question marks, no badge. We do not
render a badge for a non-existent prompt.

## Rendering & dirty-rect strategy

Per CLAUDE.md: never `fillScreen` in a continuous animation. Every-tick
redraws must erase only the bounding box of pixels that changed.

The WAITING animation has three independently-changing regions:

1. **Eyes region** — two rectangles `kLeftX..kLeftX+kEyeW` and
   `kRightX..kRightX+kEyeW`, vertical extent
   `kBaseWaitY..kBaseWaitY+kWaitGlanceDownDy+30 = 22..66` (44 px tall).
   Erase + redraw on any change to gaze position, blink height, or first
   entry into the state.

2. **Question-marks region** — a single bounding box covering all
   possible bubble positions: `x ∈ [120 − 18, 120 + 18 + kQDriftX] =
   [102, 162]`, `y ∈ [start_y − kQRiseY − 4, start_y + 8] = [9, 53]`. So
   ~60 × 44 px (~2 640 px). Cheaper than fillScreen by ~25×. Erase the
   whole bounding box and redraw all live bubbles each frame any bubble
   moves (i.e. effectively every frame while bubbles are live). When no
   bubbles are live, no redraw.

3. **Badge + footer region** — bottom strip
   `y ∈ [kBadgeY − 1, 135] = [94, 135]`. Only repainted on:
   - First entry into COLLAPSED mode (badge appears)
   - Footer LIVE/OFFLINE flip (existing behaviour)
   - Device-name change (existing behaviour)
   Not repainted per frame — badge content is static.

The eye and question-mark regions overlap: bubbles spawn at `y = 45`
(inside the eye band `[22..52]`) and rise to `y ≈ 13` (above the eye
top), passing through the eyes en route. This is intentional — it gives
the "thought emerging from the head" effect the demo validated.

Compositing rule: when the question-mark region needs a redraw, the
eye region within the same y-range must also be redrawn. In practice
the gaze animation is in motion most of the time so both regions are
already dirty every frame. Implementation: union the eye dirty rect
with the question-mark dirty rect into a single erase + redraw pass
when either is dirty, drawing eyes first then question marks on top.

`isDirty()` gains new tracked fields:

```cpp
int8_t   last_wait_gaze_dy_;        // tracks gaze position
uint8_t  last_q_live_count_;        // any change in live bubble count
uint32_t last_q_anim_tick_;         // changes every frame while bubbles live
bool     last_badge_visible_;       // tracks COLLAPSED → HIDDEN/EXPANDED transitions
```

`last_q_anim_tick_` is just `now_ms` rounded to the frame pacing — a
cheap way to force a redraw of the question-mark region every frame
while bubbles are live, without checking each bubble's position.

## Code shape

### `prompt_ui.h` / `prompt_ui.cpp`

Replace the `bool visible` field with an enum:

```cpp
enum PromptUiMode : uint8_t {
    PROMPT_UI_HIDDEN    = 0,
    PROMPT_UI_EXPANDED  = 1,
    PROMPT_UI_COLLAPSED = 2,
};

struct PromptUi {
    PromptUiMode mode;
    char         current_id[40];
    // ... existing fields ...
    char         last_decided_id[40];   // only set on Approve/Deny, not Dismiss
    // ...
};
```

`PromptView` gains a corresponding mode field so consumers don't need to
care about the internal struct:

```cpp
struct PromptView {
    PromptUiMode mode;
    const char*  tool;
    const char*  hint;
    PromptOption highlight;
    const char*  flash_text;
    uint16_t     flash_color;
};
```

`prompt_ui_button` semantics:

| Mode        | Button   | Action                                                  |
|-------------|----------|---------------------------------------------------------|
| EXPANDED    | Up       | Highlight ↑ (no wrap)                                   |
| EXPANDED    | Down     | Highlight ↓ (no wrap)                                   |
| EXPANDED    | Center   | Confirm highlighted option (Approve / Deny / Dismiss)   |
| EXPANDED    | (other)  | Ignored                                                 |
| COLLAPSED   | Center   | Mode → EXPANDED, restore highlight to OPT_APPROVE       |
| COLLAPSED   | Up/Down  | Ignored                                                 |
| HIDDEN      | (any)    | Ignored                                                 |

`prompt_ui_update` reconciliation:

| Current mode | Snapshot has prompt? | Same id?           | New mode    |
|--------------|----------------------|--------------------|-------------|
| HIDDEN       | no                   | —                  | HIDDEN      |
| HIDDEN       | yes                  | id ∈ last_decided  | HIDDEN      |
| HIDDEN       | yes                  | new id             | EXPANDED    |
| EXPANDED     | no  *or* not live    | —                  | HIDDEN      |
| EXPANDED     | yes                  | same as current_id | EXPANDED    |
| EXPANDED     | yes                  | different id       | EXPANDED (replace) |
| COLLAPSED    | no  *or* not live    | —                  | HIDDEN      |
| COLLAPSED    | yes                  | same as current_id | COLLAPSED   |
| COLLAPSED    | yes                  | different id       | EXPANDED (new prompt re-expands) |

### `EyesCard.h` / `EyesCard.cpp`

Add WAITING-specific state:

```cpp
// gaze scan
uint32_t wait_scan_epoch_ms_;
int8_t   draw_wait_gaze_dy_;        // 0..kWaitGlanceDownDy

// question-mark cluster
struct QBubble {
    uint32_t born_ms;
    int8_t   slot_x_offset;
    int8_t   slot_y_offset;
    uint8_t  size;
};
QBubble  q_bubbles_[8];
uint8_t  q_count_;
uint32_t next_q_spawn_ms_;
```

Add to `armState(STATE_WAITING)`: reset scan epoch, blink timer, clear
question-mark buffer, set first-spawn deadline to `now + 600 ms` (so
the first cluster appears shortly after entering the state, not
immediately).

Add new helpers:
- `tickWaitGaze(uint32_t now)` — updates `draw_wait_gaze_dy_`
- `tickQuestionMarks(uint32_t now)` — spawns/prunes bubbles
- `drawBadge(GFXcanvas&, const PromptView&)` — renders the bottom badge
- `drawQuestionMarks(GFXcanvas&)` — renders live bubbles

Reading the prompt state inside `EyesCard` requires plumbing the
`PromptView` (or just the `PromptUiMode`) into the card. The cleanest
existing pattern is what `PromptCard` already does — take a `PromptUi&`
reference at construction. Apply the same here: `EyesCard` constructor
gains a `const PromptUi&` parameter.

### `CardController.cpp`

Today: when `prompt_ui.visible` flips on, push `PromptCard` overlay; off
→ pop. Update for three modes:

| Transition                   | Action                                  |
|------------------------------|-----------------------------------------|
| → EXPANDED                   | Push `PromptCard` overlay               |
| EXPANDED → COLLAPSED         | Pop overlay (eyes card now visible)     |
| COLLAPSED → EXPANDED         | Push `PromptCard` overlay again         |
| → HIDDEN                     | Pop overlay (if present)                |

Button events: today, when prompt is visible, all button events go to
`prompt_ui_button` and skip carousel navigation. Update so that **any**
non-HIDDEN mode routes button events to `prompt_ui_button` (so center
press while collapsed correctly re-expands, and other buttons don't
navigate the carousel while a prompt is pending). The event router does
not need to know the mode — `prompt_ui_button` handles per-mode logic.

### Footer

The footer is currently rendered by individual cards. `kFooterH` is
bumped to 18 px (LIVE pill recentred, label font bumped to 9 px) and
the change is applied project-wide so every card gets the more legible
footer, not just the WAITING/COLLAPSED card. Existing card layouts that
draw content right up to the old footer line need to lose a few pixels
of vertical room on the bottom — audit each card during implementation
and adjust if needed.

## Performance budget

- Gaze update: 1 modulo + 1 cubic ease (3 multiplies). Negligible.
- Blink: unchanged from IDLE.
- Question-mark tick: up to 5 bubble cull checks per frame, 1 spawn check.
- Question-mark draw: up to 5 glyph renders per frame at variable size.
  Adafruit GFX text rendering at 14–28 px is a few thousand pixel writes
  per frame; comfortably within the ~5 ms budget for the WAITING tick.
- Eyes redraw: two `fillRect` erases (~1 800 px each) + two redraws.
  Same order of magnitude as the existing IDLE redraw.
- Badge: not redrawn per frame (static between mode transitions).

Total per-frame WAITING cost: dominated by the question-marks region
(~2 600 px erase + 5 glyph blits) ≈ 1.5–2 ms over SPI. Within the 16 ms
frame budget.

## Testing

Host tests (`pio test -e native`) — extend `test_prompt_ui` (or add it
if absent) with:

- New mode is HIDDEN on init.
- New prompt id → EXPANDED.
- EXPANDED + Center on Dismiss → COLLAPSED, NOT in `last_decided_id`.
- EXPANDED + Center on Approve → HIDDEN, id added to `last_decided_id`,
  outgoing decision queued.
- COLLAPSED + Center → EXPANDED, highlight reset to OPT_APPROVE.
- COLLAPSED + snapshot drops prompt → HIDDEN.
- COLLAPSED + new prompt id arrives → EXPANDED with new id.
- COLLAPSED + same prompt id continues → COLLAPSED unchanged.
- Re-prompt of an already-decided id → HIDDEN (sticky-decided still works).

The eye animation itself is harder to unit-test. Smoke-test on device
by triggering a permission prompt from Claude Desktop, dismissing, and
verifying:

- Badge appears with correct label.
- Eyes scan down toward badge every ~2 s.
- Question-mark cluster appears every ~3.2 s, drifts up-right, fades.
- Center press re-shows full-screen prompt UI.
- Approving from re-expanded UI removes badge and returns to IDLE.
- New unrelated prompt mid-collapse re-expands to full UI.

## Out of scope (deferred)

- Time-aware urgency escalation (eyes get more agitated after N minutes
  pending). Could be a v2 if the design feels too calm in practice.
- Long-press to fully hide the badge while prompt still pending — design
  decision was that the badge persists until the prompt resolves.
- Other gaze patterns explored during brainstorming (three-position
  cycle, random scan, slow continuous arc).
- Question-mark behaviour variants (alternating eye anchor, follow-the-gaze
  emission). Locked to face-centre origin, alternating big/small.
- Animating the badge itself (pulse, shimmer). Static for v1.
- Changes to other states (IDLE, WORKING, DISCONNECTED).

## Resolved during review

- Footer height bump applies globally, not only to the WAITING card.
- Question-mark colour is bright orange (`#FF7700` / RGB565 `0xFBA0`).
- Dismiss no longer adds the id to `last_decided_id`. Only Approve and
  Deny are sticky; a `Dismiss → drops → re-sends same id` cycle re-expands.
