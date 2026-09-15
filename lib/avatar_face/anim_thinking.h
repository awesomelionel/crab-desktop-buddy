// Frozen bake of the grok-bot "thinking" animation -- do not edit by hand.
//
// NOT WIRED TO ANY STATE: STATE_WORKING plays kWorking (anim_working.h) since
// the avatar moved to the Cubee body. This is kept because it is the only
// surviving copy of the grok-bot bake and the only spherical-body animation,
// so it is also what keeps the sphere branch of frontSample() under test.
//
// Originally generated from deskhog_src/grok-bot-avatar-react/grok-bot.avatar.json,
// whose geometry port was verified byte-identical to the upstream
// @bible-strong/avatar-core renderer. That definition is no longer in
// deskhog_src/ (gitignored reference material), so this header cannot be
// re-baked and tools/bake_avatar_anim.py has no `thinking` target. The values
// below are the generator's output verbatim, reshaped once for the shared
// avatar_face API; test/test_avatar_face pins them against the vectors the
// generator emitted alongside them.

#pragma once

#include "avatar_face.h"

namespace avatar_face {

// Blended in as the animation's starting pose, matching avatar-core's
// first play. Later loops blend from the previous step instead.
constexpr Pose kThinkingNeutral = { 0.0f, 0.0f, 0.0f, 20.0f, 20.0f, 50.0f, 50.0f, 35.0f, 0.0f, 0.0f, -7.0f, -7.0f, 0.0f, 0.0f, 1.0f };

constexpr uint16_t kThinkingHolds[5] = { 2300, 2300, 2300, 2300, 2300 };

constexpr Pose kThinkingSteps[5] = {
    { -12.3035154f, -17.6011715f, 5.91093731f, 20.6058598f, 20.6058598f, 47.7699203f, 47.7699203f, 54.9000015f, 0.0f, 0.0f, 0.0f, 0.0f, 23.5230465f, -24.0425777f, 1.0f },  // curious-left
    { -14.7507811f, -19.3500004f, 5.63164043f, 19.6023445f, 19.6023445f, 48.639843f, 48.639843f, 55.0999985f, 0.0f, 0.0f, 0.0f, 0.0f, -27.6066399f, 26.1484375f, 1.0f },  // angry-left
    { 3.52929688f, -7.0765624f, 9.83007812f, 24.3062496f, 48.9242172f, 59.2816391f, 13.4082031f, 62.2183609f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },  // skeptical-left
    { -4.39531231f, 14.0726566f, -16.1261711f, 19.045145f, 19.045145f, 43.3707047f, 43.3707047f, 51.7312508f, 0.0f, 0.0f, 0.0f, 0.0f, 26.2921867f, -20.249218f, 1.0f },  // playful-right
    { -16.5285149f, -3.76796865f, -13.7296877f, 23.0906258f, 49.9246101f, 57.6796875f, 12.4316406f, 56.2999992f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },  // skeptical-right
};

// screen_x = screen_cx + scale * (proj_x - view_cx)
// screen_y = screen_cy + scale * (proj_y - view_cy)
// `scale` is the largest factor that keeps the whole loop on the panel
// with a 4 px margin; this animation is height-limited.
constexpr Anim kThinking = {
    { kSurfaceSphere, 120.0f, 120.0f, 120.0f, 0.0f },  // surface: sphere
    kThinkingSteps,
    kThinkingHolds,
    kThinkingNeutral,
    5,      // step_count
    500,    // transition_ms
    2100,   // blink_initial_delay_ms
    2800,   // blink_min_interval_ms
    5000,   // blink_max_interval_ms
    260,    // blink_duration_ms
    1.0f,   // corner: upstream rounded rectangle
    1.0f,   // travel: upstream, undamped
    0.0f,   // spread: upstream separation
    0.978653369f,  // scale
    -5.3321045f,   // view_cx
    13.1588896f,   // view_cy
    120.0f,  // screen_cx
    67.5f,   // screen_cy
    61,      // canvas_w
    72,      // canvas_h
};

// Measured over the whole loop; the shared buffers must cover them.
static_assert(128 <= kMaxOutlinePoints, "raise kMaxOutlinePoints");
static_assert(61 <= kMaxCanvasW, "raise kMaxCanvasW");
static_assert(72 <= kMaxCanvasH, "raise kMaxCanvasH");

}  // namespace avatar_face
