# Working-state eyes redesign — focused thinking

**Status:** spec
**Date:** 2026-05-01
**Supersedes (in part):** `2026-04-27-eyes-card-design.md` — WORKING section only.

## Motivation

The current `STATE_WORKING` look — `> <` triangle squint, blue sweat drop dripping above the right eye, eyes scanning ±30 px on a 1 Hz sine — reads as **stressed and pained** rather than focused. The buddy looks like it is struggling, not thinking.

This spec replaces the WORKING expression with a **soft, furrowed, concentrated** look: narrow horizontal slit eyes tilted 15° inward, with subtle continuous micro-motion (breathing height, slow drift) and a typing-dots indicator above the right eye.

All other states (DISCONNECTED, IDLE, WAITING) are unchanged.

## Visual specification

| Element | Value |
|---|---|
| Eye shape | 30 × 9 px white parallelogram (rotated rectangle) |
| Left eye anchor | x-center = 45, y-center = 67 |
| Right eye anchor | x-center = 195, y-center = 67 |
| Rotation | Left eye **+15°**, right eye **−15°** (inner ends down — "furrowed") |
| Color | `ST77XX_WHITE` |
| Thinking indicator | Three 5 px white dots above right eye, x-centers = {184, 194, 204}, y-center = 22 |
| Background | `ST77XX_BLACK` |

**Removed elements:** `> <` triangle squint, blue sweat drop (`kSweatBlue`, `kSweatCX`, `kSweatTipY`, `kDripMs`, `kDripSteps`), ±30 px sinusoidal scan.

## Animation

Three concurrent loops, all driven from `t = now_ms - scan_epoch_ms_`. Periods are deliberately different so the loops phase against each other and never repeat the exact same combined pose — this avoids a robotic look.

| Animation | Period | Range | Formula |
|---|---|---|---|
| Breathing height | 1100 ms | 7 ↔ 11 px | `h = 9 + round(2 * sinf(2π·t/1100))` |
| Gaze drift | 1400 ms | ±8 px (horizontal) | `dx = round(8 * sinf(2π·t/1400))` |
| Typing dots | 1500 ms cycle | 0/1/2/3 dots visible | `phase = (t / 375) % 4`; phase _n_ shows _n_ leftmost dots (phase 0 = clear) |
| Rare blink | every 7000 ms | 9 → 0 → 9 over 280 ms | 7 step heights {9, 6, 3, 0, 3, 6, 9} at 40 ms each |

While a blink is in progress, the breathing height value is **overridden** by the blink height for that frame (so the eye smoothly closes from whatever breathing position it was at). Drift continues underneath the blink.

The blink interval is fixed at 7 s (no jitter) — predictability reads as deliberate concentration, not nervous twitching.

Both eyes animate identically (same drift direction, same breathing phase, simultaneous blink). Each eye's rotation is mirrored.

## Render strategy

Per `CLAUDE.md`: continuous-animation states must **not** call `fillScreen` per tick. The WORKING state changes its drift/breathing every loop iteration, so it must follow the same partial-erase pattern that DISCONNECTED already uses.

Per-frame steps:

1. Erase only the dirty regions with `fillRect(... ST77XX_BLACK)`:
   - Left eye region: ~52 × 17 px (rotated bbox 32 × 17 plus ±10 px horizontal padding for drift), centered on the left anchor.
   - Right eye region: same dimensions, centered on the right anchor.
   - Dots region: ~26 × 7 px covering all three dot positions.
2. Draw left eye as **2 × `fillTriangle`** forming a rotated parallelogram (30×9 rectangle rotated +15° about its center, then translated by `(cx + drift, cy)` and current breathing-height applied).
3. Draw right eye the same way, with rotation negated and `cx` swapped to right anchor.
4. Draw `draw_dots_n_` white dots via `fillCircle(x, kDotsY, 2, ST77XX_WHITE)` for the leftmost _n_ dot positions.

State entry (transition into WORKING from any other state) still does **one** `fillScreen` to clear the previous state's pixels — the existing `full_clear=true` pattern.

The rotation `cos`/`sin` and the width-axis basis vectors are precomputed once. Per-frame work is two `sinf` calls (drift + breathing), four float multiplies for the height-axis basis, and the corner additions — no trig per corner.

## Code changes

### `src/ui/cards/EyesCard.h`

Replace working-state state vars:

- **Remove:** `int8_t draw_sweat_y_;`, `int8_t last_sweat_y_;`
- **Add:**
  - `uint32_t next_work_blink_ms_;` — when the next 7 s blink fires
  - `int8_t draw_blink_h_;` — current blink-step height, or `-1` when not blinking
  - `int8_t draw_work_blink_i_;` — current step index in the blink animation (mirrors the existing `blink_i_` for IDLE/WAITING)
  - `uint32_t work_blink_step_deadline_ms_;`
  - `uint8_t draw_dots_n_;` — 0..3
  - `uint8_t last_dots_n_;`
  - `int8_t last_blink_h_;`
- Keep `scan_epoch_ms_` (now anchors all three working-state animations).

### `src/ui/cards/EyesCard.cpp`

#### Constants (replace sweat block):

```cpp
const uint32_t kWorkBreatheMs       = 1100;
const uint32_t kWorkDriftMs         = 1400;
const uint32_t kWorkDotsMs          = 1500;
const uint32_t kWorkBlinkIntervalMs = 7000;
const int      kWorkDriftAmp        = 8;
const int      kWorkBaseH           = 9;
const int      kWorkBreatheAmp      = 2;     // h ranges 7..11
const float    kWorkRotRad          = 0.2618f; // 15° in radians
const int      kDotsX[3]            = {184, 194, 204};
const int      kDotsY               = 22;
const int      kDotR                = 2;     // 5 px diameter via fillCircle r=2
const uint8_t  kWorkBlinkH[]        = {9, 6, 3, 0, 3, 6, 9};
const int      kWorkBlinkN          = 7;
const uint32_t kWorkBlinkStepMs     = 40;    // ~280 ms total
```

The four parallelogram corners are derived per frame from the rotation's basis vectors. The width-axis basis is constant (width is fixed at 30 px); the height-axis basis scales with the current eye height `h`.

For the **left** eye at rotation +15°:

```cpp
// Constants — computed once.
const float kCos15 = cosf(kWorkRotRad);  // ≈ 0.9659
const float kSin15 = sinf(kWorkRotRad);  // ≈ 0.2588
const float kHalfW = 15.0f;              // 30 / 2

// Per-frame, given current eye height h:
float halfH = h * 0.5f;
float ux = kHalfW * kCos15;   // width-axis x component
float uy = kHalfW * kSin15;   // width-axis y component
float vx = -halfH * kSin15;   // height-axis x component
float vy =  halfH * kCos15;   // height-axis y component

// Corners (in screen coords) — round to int for fillTriangle.
int16_t cx = 45 + draw_dx_, cy = 67;
int16_t tlx = cx - ux + vx, tly = cy - uy + vy;
int16_t trx = cx + ux + vx, try_ = cy + uy + vy;
int16_t brx = cx + ux - vx, bry = cy + uy - vy;
int16_t blx = cx - ux - vx, bly = cy - uy - vy;
```

For the **right** eye at rotation −15°: negate `uy` and `vx`. Cache `kCos15`, `kSin15`, `ux`, `uy` as constants since width and rotation are fixed; only `vx`/`vy` recompute per frame.

#### `armState(STATE_WORKING, now)`:

```cpp
scan_epoch_ms_           = now;
next_work_blink_ms_      = now + kWorkBlinkIntervalMs;
draw_work_blink_i_       = -1;
draw_blink_h_            = -1;
draw_dots_n_             = 0;
```

#### `tick()` for `STATE_WORKING`:

```cpp
uint32_t t = now_ms - scan_epoch_ms_;
draw_dx_     = (int16_t)roundf(kWorkDriftAmp * sinf(2.0f * PI * t / (float)kWorkDriftMs));
draw_base_y_ = kBaseIdleY;

int breathe_h = kWorkBaseH + (int)roundf(kWorkBreatheAmp *
                  sinf(2.0f * PI * t / (float)kWorkBreatheMs));

// Blink scheduling — separate timer from IDLE/WAITING blink to avoid coupling.
if (draw_work_blink_i_ >= 0) {
    if (now_ms >= work_blink_step_deadline_ms_) {
        draw_work_blink_i_++;
        if (draw_work_blink_i_ >= kWorkBlinkN) {
            draw_work_blink_i_  = -1;
            next_work_blink_ms_ = now_ms + kWorkBlinkIntervalMs;
        } else {
            work_blink_step_deadline_ms_ = now_ms + kWorkBlinkStepMs;
        }
    }
} else if (now_ms >= next_work_blink_ms_) {
    draw_work_blink_i_           = 0;
    work_blink_step_deadline_ms_ = now_ms + kWorkBlinkStepMs;
}

draw_h_ = (draw_work_blink_i_ >= 0) ? kWorkBlinkH[draw_work_blink_i_] : breathe_h;
draw_blink_h_ = (draw_work_blink_i_ >= 0) ? kWorkBlinkH[draw_work_blink_i_] : -1;

// Typing dots — phase 0..3
draw_dots_n_ = (uint8_t)((t * 4 / kWorkDotsMs) % 4);
```

#### `drawFrame()` for `STATE_WORKING` (replaces the existing block):

```cpp
// Per-frame partial erase — see CLAUDE.md.
const int kBboxW = 52, kBboxH = 17;  // padded for drift
tft.fillRect(45 - kBboxW/2,  67 - kBboxH/2,  kBboxW, kBboxH, ST77XX_BLACK);
tft.fillRect(195 - kBboxW/2, 67 - kBboxH/2,  kBboxW, kBboxH, ST77XX_BLACK);
tft.fillRect(180,            kDotsY - 3,     30,     7,      ST77XX_BLACK);

// Eyes — 2 fillTriangle each, rotated parallelogram of (30 × draw_h_).
drawRotatedSlit(tft,  45 + draw_dx_, 67, draw_h_, +1);  // +15°
drawRotatedSlit(tft, 195 + draw_dx_, 67, draw_h_, -1);  // -15°

// Typing dots
for (int i = 0; i < draw_dots_n_; i++) {
    tft.fillCircle(kDotsX[i], kDotsY, kDotR, ST77XX_WHITE);
}
```

`drawRotatedSlit(tft, cx, cy, h, sign)` is a private helper that takes the cached corner offsets, scales the height-axis component by `h`, applies `sign` to invert rotation for the right eye, rounds to int, and emits two `fillTriangle` calls covering the parallelogram.

#### `isDirty()`:

- Remove `last_sweat_y_` check.
- Add `last_dots_n_ != draw_dots_n_` and `last_blink_h_ != draw_blink_h_` checks.

#### `render()`:

Extend the partial-erase carve-out from DISCONNECTED-only to include WORKING:

```cpp
bool full_clear = stateJustChanged ||
                  (bs != STATE_DISCONNECTED && bs != STATE_WORKING);
```

#### Mirror updates at end of `render()`:

```cpp
last_dots_n_  = draw_dots_n_;
last_blink_h_ = draw_blink_h_;
// (last_sweat_y_ removed)
```

## Verification

This is firmware on a TFT display. Verification is on-device visual plus a static check.

- **Compiles cleanly** via `pio run` for the `adafruit_feather_esp32s3_reversetft` env.
- **No flicker on WORKING:** hold the device in `STATE_WORKING` for ≥30 s, expect smooth animation with no full-screen black flashes (the `CLAUDE.md` rule).
- **State transitions clean:** IDLE↔WORKING and WORKING↔WAITING — first frame after entry should `fillScreen` once, then steady incremental frames.
- **Drift symmetry:** both eyes shift in the same direction at the same time.
- **Blink during drift:** the ~7 s blink doesn't interrupt drift — eyes drop and rise while continuing to slide.
- **Dots cycle correctly:** 0 → 1 → 2 → 3 → 0 every 1.5 s, no leftover dot pixels.
- **No sweat-drop residue:** the corner where the old blue teardrop appeared shows pure black.

No simulator covers `EyesCard.cpp` (the `cdb/` desktop project only renders Buddy characters, not the eyes card). Iteration requires flashing to the Adafruit Feather ESP32-S3 device.

## Out of scope

- DISCONNECTED, IDLE, WAITING animations — unchanged.
- Card navigation, button handling, settings — unchanged.
- Any new color palette or theme changes.
- Replacing the existing IDLE/WAITING blink mechanism — the working-state blink uses its own timer to avoid coupling, even though it shares the step pattern.
