# DONE celebration animation — design

Date: 2026-05-09
Status: Draft for review
Related: `src/ui/cards/EyesCard.{h,cpp}` (sole implementation site),
`lib/state/state.{h,cpp}` (unchanged — referenced for context), `CLAUDE.md`
(fillScreen rule).

## Goal

When Claude finishes a task, the eyes briefly perform a delighted
celebration — a small bounce that settles into upward elliptical arcs
("^_^") with a single sparkle on the right, held for ~1.5 s, then return
to the normal IDLE animation. Today the eyes go straight from WORKING
back to IDLE with no acknowledgement.

This is intentionally narrow:

- Only the visual eye animation changes. No new wire protocol fields, no
  Footer / StatusCard / Card changes, no audio (the device has no
  buzzer).
- Only WORKING → IDLE transitions celebrate; WAITING → IDLE does not.
- Trivial-length tasks (< 1.5 s in WORKING) are suppressed to avoid the
  delighted face flashing on every quick tool call.

## Summary of what changes

1. **No change to the shared state machine.** `BuddyState`,
   `state_derive()`, and every consumer outside EyesCard stay as they
   are. DONE is **not** a `BuddyState` — it is a local animation phase
   inside `EyesCard` that renders on top of `STATE_IDLE`.

2. **EyesCard tracks one new edge and one new animation phase.** It
   already knows the previous `BuddyState` via `prev_state_`. It will
   additionally track:
    - `working_entered_ms_` — wall-clock time of the most recent entry
      into `STATE_WORKING`.
    - `done_active_` / `done_start_ms_` — whether a DONE celebration is
      in flight and when it started.

3. **EyesCard gains a fifth animation path.** `tick()` and `render()`
   detect the WORKING → IDLE edge, gate on the suppression threshold,
   and produce 1.5 s of celebration frames before falling through to
   normal IDLE rendering.

4. **Two small offscreen canvases (lazily allocated).** ~30 × 38 px per
   eye, used only during DONE — same lazy-allocation pattern as
   `work_canvas_` and `wait_q_canvas_`. ~5.1 KB RAM total, allocated
   once on the first DONE entry and reused thereafter.

## Trigger logic

DONE fires when **all** of the following are true on a given `tick()`:

1. The previous `state_.buddyState()` (as observed by EyesCard's
   `prev_state_`) was `STATE_WORKING`.
2. The current `state_.buddyState()` is `STATE_IDLE`.
3. `now_ms - working_entered_ms_ >= kDoneSuppressMs` (1500 ms).
4. `done_active_` is currently false (we are not already inside a
   celebration).

`working_entered_ms_` is set inside `armState()` whenever
`STATE_WORKING` is entered. It is **not** reset by intermediate
`STATE_WAITING` excursions — the existing `armState` for WAITING does
not touch it. This means a long WORKING phase that briefly dips into
WAITING for a permission prompt and then resolves directly back to
IDLE will **not** celebrate (per rule 1: the immediately preceding
state must be WORKING, not WAITING). If the work resumes after the
prompt (WAITING → WORKING → IDLE), the WORKING duration is measured
from the most recent WORKING entry.

## Animation timeline

Total duration: **1500 ms**, frame-paced at the existing 16 ms tick.
All phase boundaries are exact ms offsets from `done_start_ms_`.

| t (ms) | Phase | Eye geometry | Sparkle |
|---|---|---|---|
| 0 → 100 | Bounce up | H 30→38, W 30→32, top 52→44 (eased) | brightness 0 → 0.5 |
| 100 → 200 | Settle low | H 38→28, W 32→32, top 44→56 (eased) | brightness 0.5 → 1.0 |
| 200 → 350 | Morph to arc | H 28→16, W 30, top 56→56; round-rect → upper-half ellipse (lerp) | brightness 1.0 |
| 350 → 1100 | Hold arcs | H 16, W 30, top 56, half-ellipse | brightness 1.0 → 0 (linear over the window; quantized to 5 steps so the curve shape is dominated by the staircase) |
| 1100 → 1500 | Return to idle | H 16→30, W 30, top 56→52, half-ellipse throughout (at H=30 it's similar enough to the IDLE rect that the t=1500 handover reads as the eyes opening) | hidden |

**Easing:** the bounce, morph, and return phases use cubic ease-out
(`1 - (1 - k)^3`), matching the existing convention in
`tickGlanceIdle` and the WAITING gaze.

**Eye positions:** existing constants `kLeftX = 30`, `kRightX = 180`
remain the X anchors. Both eyes are rendered identically and
synchronously (no asymmetric phase offset).

**Arc shape (hold pose):** "upper half of an ellipse" — width 30 px,
height 16 px. In CSS terms `border-radius: 50% 50% 0 0 / 100% 100% 0
0`. In firmware terms: a horizontal `fillRect` for the bottom edge plus
the upper half of an ellipse drawn via `Adafruit_GFX::fillCircleHelper`
or a hand-stepped ellipse rasterization. (Implementation chooses
whichever produces a clean curve — see Rendering section.)

**Sparkle:** a 4-point cross at `(220, 56)` — center pixel plus four
satellites at `(±7, 0)` and `(0, ±7)`. Brightness follows the
right-hand column of the timeline table (rises 0 → 1.0 over phases
1–2, holds through phase 3, decays back to 0 over phase 4). The 0.0–1.0
brightness is implemented as the *count* of visible pixels (0..5),
with satellites disappearing in outer-to-inner order as brightness
drops — so the fade reads as a small visible step rather than a true
alpha.

## Interruption

DONE is cancelled — immediately, mid-frame — when
`state_.buddyState()` becomes anything other than `STATE_IDLE` while
`done_active_` is true.

- `STATE_WORKING` arrives → DONE ends; WORKING's `armState` runs and
  performs its own `full_clear=true` paint, which wipes the
  celebration pixels.
- `STATE_WAITING` arrives (e.g. a permission prompt fires while the
  user was idle for a moment) → same: WAITING's `armState` clears.
- `STATE_DISCONNECTED` arrives → same.

DONE never extends or blocks a state transition. It is a render-only
overlay; the underlying `BuddyState` continues to advance unchanged.

## Rendering

Three drawing primitives, all bbox-erase compliant per `CLAUDE.md`'s
"never call `fillScreen` in continuous animations" rule:

### Eye shapes (bounce + arc): per-eye offscreen canvas

- Two `GFXcanvas16` instances, sized 32 × 40 px. The 32-wide bound
  accommodates the bounce-wide phase (W up to 32 in phases 1–2). The
  canvas is positioned at `y = 44` and is 40 px tall, covering 44..84
  — the full vertical range from the bounce-up apex (top=44) to the
  settle-low bottom (top=56 + H=28 = 84). The canvas X is offset 1 px
  to the left of each eye so its centerline matches the eye's
  centerline. ~2.5 KB per eye, ~5.1 KB total.
- Allocated lazily on the first DONE entry (`if (!done_canvas_l_)
  done_canvas_l_ = new GFXcanvas16(...)`). Persisted for the lifetime
  of the EyesCard — DONE happens often enough that per-task
  realloc is wasteful.
- Each frame, for each eye:
    1. `canvas->fillScreen(BLACK)` (the canvas, not the LCD — ~1 KB
       memset).
    2. Draw the current eye shape into the canvas at its
       canvas-local coordinates. Round-rect during bounce; upper-half
       ellipse during arc; lerped between during the morph.
    3. `tft.drawRGBBitmap(eye_x, kDoneCanvasY, canvas->getBuffer(),
       kEyeW, kDoneCanvasH)` — single SPI burst per eye.

The canvas push naturally overwrites whatever pixels were previously
in the bbox, so no separate erase pass is needed for the eyes.

### Sparkle: direct primitives

Five `fillRect`s of 1×1 px for the center + four satellites. Erased
before each redraw with one `fillRect` over the ~17 × 17 px bbox
(`2 * kDoneSparkleArm + 3` on each axis — covers the cross plus a 1 px
margin).

The sparkle's bbox is fixed and small — no canvas needed. Brightness
is implemented as "draw N of 5 pixels" rather than a true alpha; the
satellites disappear in the order outer → inner as brightness drops.

### Entry/exit clearing

- **DONE entry:** no `fillScreen`. The first canvas push for each eye
  overwrites the prior IDLE eye pixels in place (the round-rect
  bounding box during phase 1 is identical in X to IDLE's eye box and
  fully contains the new shape). The sparkle bbox is initially black
  (no IDLE pixels there).
- **DONE successful exit (1500 ms reached):** no special clear
  needed. IDLE's animation loop resumes; its first dirty-frame paint
  redraws the eyes at their normal position, which is already correct
  given DONE's last frame returns the eyes to that geometry.
- **DONE interrupted exit:** the new state's `armState()` runs
  `full_clear=true` on its first render, which calls `fillScreen` once
  (this is the established pattern — single 13 ms flash at a state
  transition is acceptable per CLAUDE.md). Sparkle pixels and any
  in-flight eye pixels are wiped.

### Dirty-tracking

The existing `isDirty()` snapshot is extended with three fields:

- `last_done_active_` (bool)
- `last_done_phase_t_` (uint32_t — bucketed to 16 ms)
- `last_sparkle_brightness_n_` (uint8_t — 0..5, the integer count of
  visible sparkle pixels)

`isDirty()` returns true if `done_active_` is true and any of those
quantized values has changed since the last `render()`. While DONE is
inactive, dirty-tracking behaves as it does today.

## Constants

A new block in EyesCard.cpp's anonymous-namespace constants region:

```cpp
// ---- STATE_DONE celebration overlay ----
const uint32_t kDoneSuppressMs   = 1500;   // min WORKING duration to celebrate
const uint32_t kDoneTotalMs      = 1500;   // total celebration length
const uint32_t kDoneBounceUpMs   = 100;
const uint32_t kDoneSettleLowMs  = 100;
const uint32_t kDoneMorphMs      = 150;
const uint32_t kDoneHoldMs       = 750;
// kDoneReturnMs = 400 (implicit: total - sum(others))

const int      kDoneCanvasW      = 32;     // accommodates bounce-wide W=32
const int      kDoneCanvasH      = 40;     // covers 44..84
const int      kDoneCanvasY      = 44;     // top of bounce-up apex
const int      kDoneArcH         = 16;
const int      kDoneArcTop       = 56;
const int      kDoneSparkleCx    = 220;
const int      kDoneSparkleCy    = 56;
const int      kDoneSparkleArm   = 7;
```

## EyesCard surface changes

### Header (`EyesCard.h`)

New private fields, grouped after the existing WAITING block:

```cpp
// ---- STATE_DONE celebration overlay ----
uint32_t      working_entered_ms_;
bool          done_active_;
uint32_t      done_start_ms_;
GFXcanvas16*  done_canvas_l_;   // lazily allocated; ~2.5 KB
GFXcanvas16*  done_canvas_r_;   // lazily allocated; ~2.5 KB

// Dirty-tracking snapshot extension.
bool          last_done_active_;
uint32_t      last_done_phase_t_;
uint8_t       last_sparkle_brightness_n_;
```

New private method:

```cpp
void tickDone(uint32_t now_ms);
void drawDoneFrame(Adafruit_ST7789& tft, uint32_t t_into_done);
```

### Implementation (`EyesCard.cpp`)

1. `armState(STATE_WORKING, now)` sets `working_entered_ms_ = now`.
2. `tick()` — at the top, before the existing per-state switch:
    - If `state_.buddyState() == STATE_IDLE` and `prev_state_ ==
      STATE_WORKING` and the suppression threshold has been met and
      `!done_active_`: set `done_active_ = true; done_start_ms_ =
      now_ms;`.
    - If `done_active_`: call `tickDone(now_ms)`. If `state_` is no
      longer `STATE_IDLE`, set `done_active_ = false`. If `now_ms -
      done_start_ms_ >= kDoneTotalMs`, set `done_active_ = false`.
3. `render()` — at the top, before the existing per-state switch:
    - If `done_active_`: call `drawDoneFrame(tft, now_ms -
      done_start_ms_)`, update dirty-tracking snapshot, return.
4. `isDirty()` — extend with the three new fields when
   `done_active_` is true.
5. `resetAnim()` — initialize the new fields (`done_active_ = false`,
   pointers null, etc.).

`armState()` is **not** extended with a new `STATE_DONE` case — DONE
is not a `BuddyState`. The existing four cases stay as they are.

## Edge cases

- **WORKING for 0.5 s, then IDLE:** suppressed.
  `now - working_entered_ms < kDoneSuppressMs`. Eyes go straight to
  IDLE.
- **WORKING for 5 s → IDLE → WORKING again 0.3 s later:** DONE is
  cancelled mid-celebration. WORKING's `armState` runs as normal,
  including its `full_clear=true` paint that wipes the celebration
  pixels.
- **WORKING → WAITING → IDLE (prompt resolved without resuming):**
  not celebrated (rule 1: `prev_state_` must be WORKING).
- **WORKING → WAITING → WORKING → IDLE:** celebrated if the *last*
  WORKING phase met the threshold. `working_entered_ms_` is the time
  of the most recent WORKING entry.
- **DISCONNECTED at any point during DONE:** DONE is cancelled;
  DISCONNECTED's `armState` runs.
- **Boot directly into IDLE (no prior WORKING):** `prev_state_`
  starts as `0xFF`, never matches `STATE_WORKING`. No celebration.
- **Memory pressure:** the canvas allocations can fail. If
  `done_canvas_l_` or `done_canvas_r_` is null after a `new` attempt,
  silently skip the celebration for that DONE event (return to IDLE
  immediately). Manifests as a missing animation, not a crash.

## Testing

The existing test suite (`test_state`, `test_protocol`, etc.) covers
pure functions; `EyesCard` is not unit-tested today (it depends on
Adafruit_GFX + hardware drivers). DONE matches that pattern.

Manual on-device verification:

1. **Happy path:** trigger Claude on a > 1.5 s task; observe bounce →
   ^_^ arcs + sparkle → return to IDLE.
2. **Suppression:** trigger a < 1 s tool call; confirm no celebration.
3. **Interrupt by new WORKING:** issue back-to-back tasks; confirm
   DONE is cut off cleanly when WORKING resumes, no leftover pixels.
4. **Interrupt by WAITING:** long task ending in a permission prompt;
   confirm WORKING → WAITING → IDLE does not celebrate.
5. **Interrupt by disconnect:** unplug the bridge during a DONE;
   confirm DISCONNECTED takes over cleanly.
6. **No frame strobe:** stare at the eyes during the 1.5 s window; no
   full-screen flash. (CLAUDE.md fillScreen rule.)
7. **RAM:** `ESP.getFreeHeap()` after first DONE drops by ~5.1 KB
   once (canvas alloc) and stays stable across many subsequent DONEs.

No new automated tests in this iteration. The trigger logic is ~10
lines of conditional inside `tick()`; isolating it into a pure helper
is deferred until it grows.

## Out of scope

- Sound or haptic feedback (no buzzer / vibration motor on the
  current hardware).
- Footer / StatusCard acknowledgement of completion (DONE is purely an
  EyesCard concern).
- Multi-task celebration (e.g. "completed 3 tasks in a row" combo).
- Customizable celebration duration or visual style via Settings.
- Promoting DONE to a `BuddyState` enum value (would require
  `state_derive` to become stateful — explicitly rejected in design
  Section 1).
