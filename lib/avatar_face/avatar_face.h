#pragma once

#include <stdint.h>

// Geometry for the avatar-core animations the buddy plays: `kIdle` on
// STATE_IDLE and `kThinking` on STATE_WORKING.
//
// This is a port of @bible-strong/avatar-core's render pipeline. Keyframes,
// the body surface and the device mapping are baked per animation into
// anim_<name>.h by tools/bake_avatar_anim.py.
//
// The upstream avatar draws a solid body with white eyes on a light page, so
// the body sweeps read as head turns. Here the body colour matches the panel
// background, so only the two eye outlines are ever drawn — the body exists
// solely as the surface the eyes are projected onto.
//
// Deliberately free of Arduino/Adafruit dependencies so test/test_avatar_face
// can exercise it natively against generator-produced vectors.
namespace avatar_face {

// ---- avatar-core constants, shared by every animation ----
constexpr float kRadius     = 120.0f;   // the flattening sphere, not the body
constexpr float kFocal      = 620.0f;
constexpr float kBlinkFloor = 5.0f;
constexpr int   kArcSamples = 14;       // per quarter turn
constexpr float kLineStep   = 1.5f;     // outline sample spacing, units

// Worst case across every baked animation; each anim header static_asserts
// that its own measured limits fit inside these.
constexpr int kMaxOutlinePoints = 192;
constexpr int kMaxCanvasW       = 64;
constexpr int kMaxCanvasH       = 76;

// Interpolatable pose. Field order matches the generator so the keyframe
// tables can be flat brace-initialised lists.
struct Pose {
    float head_x, head_y, head_z;      // degrees
    float w_l, w_r, h_l, h_r;          // eye size, units
    float spacing;                     // centre-to-centre, units
    float x_l, x_r, y_l, y_r;          // eye offset, units
    float angle_l, angle_r;            // eye tilt, degrees
    float perspective;
};

// The body the eyes are painted onto. `exponent` is the superquadric power
// avatar-core derives from a cube's roundness; <= 0 means a hard box, whose
// front face is a plain clamp. Unused for spheres.
enum SurfaceKind : uint8_t { kSurfaceSphere, kSurfaceCube };

struct Surface {
    SurfaceKind kind;
    float       hw, hh, hd;    // half extents, units
    float       exponent;
};

struct Anim {
    Surface     surface;
    const Pose* steps;
    const uint16_t* holds_ms;   // one per step; the ease-in is transition_ms
    Pose        neutral;
    int         step_count;
    uint32_t    transition_ms;
    uint32_t    blink_initial_delay_ms;
    uint32_t    blink_min_interval_ms;
    uint32_t    blink_max_interval_ms;
    uint32_t    blink_duration_ms;
    // Device-side style, applied on top of the upstream geometry.
    //   corner: eye corner radius as a fraction of the capsule radius. 1.0 is
    //           avatar-core's rounded rectangle; lower is squarer.
    //   travel: fraction of the projected eye motion kept. 1.0 is upstream;
    //           lower slides each eye toward (view_cx, view_cy) in proportion
    //           to how far out it sits, shrinking the roaming range without
    //           resizing the eye. The bake fits `scale` on the undamped
    //           extents, so this only ever adds margin.
    //   spread: extra centre-to-centre eye separation in device pixels, split
    //           evenly between the two eyes. In pixels rather than pose units
    //           so one number means the same distance in every animation.
    float       corner, travel, spread;
    // screen_x = screen_cx + scale * (proj_x - view_cx), likewise for y.
    float       scale, view_cx, view_cy, screen_cx, screen_cy;
    int16_t     canvas_w, canvas_h;    // largest single-eye bounding box, px
};

// One step's total length: the ease-in plus that step's own hold.
inline uint32_t stepMs(const Anim& a, int i) {
    return a.transition_ms + (uint32_t)a.holds_ms[i];
}

inline uint32_t loopMs(const Anim& a) {
    uint32_t total = 0;
    for (int i = 0; i < a.step_count; i++) total += stepMs(a, i);
    return total;
}

// Which step t lands in, how far into it, and which pass over the loop it is.
// Steps can have different hold lengths, so this walks rather than divides.
struct Cursor {
    int      index;
    uint32_t phase_ms;   // into the step, ease-in first
    uint32_t loop;
};

Cursor cursorAt(const Anim& a, uint32_t t_ms);

// True while the pose is still easing between keyframes. Holds are
// pixel-identical, so a caller can skip redrawing outside a transition.
inline bool inTransition(const Anim& a, uint32_t t_ms) {
    return cursorAt(a, t_ms).phase_ms < a.transition_ms;
}

struct Point {
    float x;   // device pixels
    float y;
};

// Pose at t ms after the animation started. Steps advance every stepMs and
// wrap; each step eases in over transition_ms with a smoothstep, then holds.
// The very first step blends from `neutral`, matching what avatar-core does on
// first play; every later loop blends from the preceding step.
Pose poseAt(const Anim& anim, uint32_t t_ms);

// Eye outline in device pixels. `side` is -1 for the left eye and +1 for the
// right; `blink` is 1.0 wide open and 0.0 fully squashed. Writes at most `cap`
// points (kMaxOutlinePoints is the measured worst case) and returns the count,
// or 0 when the eye has rotated to the far side of the body.
int eyeOutline(const Anim& anim, const Pose& pose, int side, float blink,
               Point* out, int cap);

// One row's horizontal extent, in canvas coordinates and inclusive on both
// ends. x1 < x0 marks a row the outline doesn't cover.
struct Span {
    int16_t x0;
    int16_t x1;
};

// Rasterise an eye outline to per-row spans, relative to a canvas whose
// top-left pixel is (origin_x, origin_y). Writes exactly `rows` entries.
//
// The projected outline is a rounded rectangle wrapped onto the body, so it
// stays convex; that means the leftmost and rightmost edge crossings on a row
// bound its span exactly and no even-odd winding pass is needed. Rows are
// sampled at their centre, so a span lights only where the outline actually
// covers the middle of the pixel.
void outlineSpans(const Point* pts, int n, int origin_x, int origin_y,
                  Span* spans, int rows, int cols);

// Blink scheduling, mirroring avatar-core: a blink becomes due after
// blink_initial_delay_ms, then every blink_duration_ms + [min, max) interval.
// The caller supplies randomness so this stays free of platform headers.
// A blink_duration_ms of 0 means the animation never blinks — kSleep's lids
// are already shut, and squashing them further would only make them flicker.
struct BlinkState {
    uint32_t started_ms;
    uint32_t due_ms;
    bool     ever_started;
};

void  blinkInit(const Anim& anim, BlinkState& blink, uint32_t now_ms);
void  blinkTick(const Anim& anim, BlinkState& blink, uint32_t now_ms, float rand01);
float blinkFactor(const Anim& anim, const BlinkState& blink, uint32_t now_ms);
bool  blinkActive(const Anim& anim, const BlinkState& blink, uint32_t now_ms);

}  // namespace avatar_face
