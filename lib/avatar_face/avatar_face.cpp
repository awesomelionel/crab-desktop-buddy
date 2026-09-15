#include "avatar_face.h"

#include <math.h>

namespace avatar_face {
namespace {

struct Quat { float w, x, y, z; };
struct Vec3 { float x, y, z; };

const float kPi = 3.14159265358979323846f;

inline float deg2rad(float d) { return d * kPi / 180.0f; }

inline Quat qnorm(Quat q) {
    const float n = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    const float inv = (n > 0.0f) ? 1.0f / n : 1.0f;
    return { q.w * inv, q.x * inv, q.y * inv, q.z * inv };
}

inline Quat qmul(const Quat& a, const Quat& b) {
    return qnorm({
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    });
}

inline Quat qaxis(float ax, float ay, float az, float angle) {
    const float h = angle * 0.5f;
    const float s = sinf(h);
    return qnorm({ cosf(h), ax * s, ay * s, az * s });
}

// avatar-core composes the head orientation as z * x * y.
Quat headQuat(const Pose& p) {
    const Quat qx = qaxis(1.0f, 0.0f, 0.0f, deg2rad(p.head_x));
    const Quat qy = qaxis(0.0f, 1.0f, 0.0f, deg2rad(p.head_y));
    const Quat qz = qaxis(0.0f, 0.0f, 1.0f, deg2rad(p.head_z));
    return qmul(qmul(qz, qx), qy);
}

inline Vec3 qrot(const Quat& q, const Vec3& p) {
    const float tx = 2.0f * (q.y * p.z - q.z * p.y);
    const float ty = 2.0f * (q.z * p.x - q.x * p.z);
    const float tz = 2.0f * (q.x * p.y - q.y * p.x);
    return {
        p.x + q.w * tx + (q.y * tz - q.z * ty),
        p.y + q.w * ty + (q.z * tx - q.x * tz),
        p.z + q.w * tz + (q.x * ty - q.y * tx),
    };
}

inline float smoothstep(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float sgnpow(float v, float p) {
    if (v == 0.0f) return 0.0f;
    const float m = powf(fabsf(v), p);
    return (v < 0.0f) ? -m : m;
}

const int kPoseFields = 15;
static_assert(sizeof(Pose) == kPoseFields * sizeof(float),
              "Pose must stay a flat run of floats for the blend loop below");

Pose lerpPose(const Pose& a, const Pose& b, float t) {
    // Every angle pair in these animations is already within 180 degrees of
    // its neighbour (asserted by the generator), so avatar-core's
    // nearestEquivalentAngle reduces to a plain lerp here.
    const float* fa = &a.head_x;
    const float* fb = &b.head_x;
    Pose out{};      // every field is written below; {} silences cppcheck
    float* fo = &out.head_x;
    for (int i = 0; i < kPoseFields; i++) fo[i] = fa[i] + (fb[i] - fa[i]) * t;
    return out;
}

inline Vec3 vnorm(Vec3 v) {
    const float n = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    const float inv = (n > 0.0f) ? 1.0f / n : 1.0f;
    return { v.x * inv, v.y * inv, v.z * inv };
}

// avatar-core's surfaceFrontSampleAt: where a flattened face coordinate lands
// on the front of the body, plus that point's unit outward normal.
struct FrontSample { Vec3 p; Vec3 n; };

FrontSample frontSample(const Surface& s, float x, float y) {
    if (s.kind == kSurfaceSphere) {
        float rem = 1.0f - (x / s.hw) * (x / s.hw) - (y / s.hh) * (y / s.hh);
        if (rem < 0.0f) rem = 0.0f;
        const Vec3 p = { x, y, s.hd * sqrtf(rem) };
        return { p, vnorm({ p.x / (s.hw * s.hw),
                            p.y / (s.hh * s.hh),
                            p.z / (s.hd * s.hd) }) };
    }

    // Cube: a superquadric whose exponent comes from the definition's
    // roundness. exponent <= 0 marks a hard box, whose front face is a clamp.
    const float r = s.exponent;
    Vec3 p;
    if (r <= 0.0f) {
        p = { clampf(x, -s.hw, s.hw), clampf(y, -s.hh, s.hh), s.hd };
    } else {
        const float cy       = clampf(y / s.hh, -1.0f, 1.0f);
        float       row_rem  = 1.0f - powf(fabsf(cy), r);
        if (row_rem < 0.0f) row_rem = 0.0f;
        const float row_half = powf(row_rem, 1.0f / r);
        const float px       = clampf(x, -s.hw * row_half, s.hw * row_half);
        const float d        = px / s.hw;
        float       rem      = 1.0f - powf(fabsf(d), r) - powf(fabsf(cy), r);
        if (rem < 0.0f) rem = 0.0f;
        p = { px, cy * s.hh, s.hd * powf(rem, 1.0f / r) };
    }
    return { p, vnorm({ sgnpow(p.x / s.hw, r - 1.0f) / s.hw,
                        sgnpow(p.y / s.hh, r - 1.0f) / s.hh,
                        sgnpow(p.z / s.hd, r - 1.0f) / s.hd }) };
}

// One quadrant of the outline sampler; mirrors avatar-core's roundedRectangle
// so the point ordering and count match the generated test vectors exactly.
struct OutlineBuilder {
    Point* out;
    int    cap;
    int    n;

    void push(float x, float y) {
        if (n < cap) {
            out[n].x = x;
            out[n].y = y;
        }
        n++;
    }

    void line(float ax, float ay, float bx, float by) {
        // Sample count is computed in double to match the generator's ceil()
        // on values that can land exactly on an integer boundary.
        const double len = sqrt((double)(bx - ax) * (bx - ax) +
                                (double)(by - ay) * (by - ay));
        int samples = (int)ceil(len / (double)kLineStep);
        if (samples < 2) samples = 2;
        for (int i = 0; i < samples; i++) {
            const float t = (float)i / (float)samples;
            push(ax + (bx - ax) * t, ay + (by - ay) * t);
        }
    }

    void arc(float cx, float cy, float start, float r) {
        for (int i = 0; i < kArcSamples; i++) {
            const float a = start + ((float)i / (float)kArcSamples) * (kPi * 0.5f);
            push(cx + cosf(a) * r, cy + sinf(a) * r);
        }
    }
};

}  // namespace

Cursor cursorAt(const Anim& anim, uint32_t t_ms) {
    const uint32_t loop_ms = loopMs(anim);
    Cursor c = { 0, t_ms % loop_ms, t_ms / loop_ms };
    for (int i = 0; i < anim.step_count; i++) {
        const uint32_t len = stepMs(anim, i);
        if (c.phase_ms < len) {
            c.index = i;
            return c;
        }
        c.phase_ms -= len;
    }
    // Unreachable: the phase is taken modulo the sum of every step.
    c.index    = anim.step_count - 1;
    c.phase_ms = 0;
    return c;
}

Pose poseAt(const Anim& anim, uint32_t t_ms) {
    const Cursor c = cursorAt(anim, t_ms);
    const Pose& target = anim.steps[c.index];
    if (c.phase_ms >= anim.transition_ms) return target;

    const Pose& source = (c.loop == 0 && c.index == 0)
        ? anim.neutral
        : anim.steps[(c.index + anim.step_count - 1) % anim.step_count];
    return lerpPose(source, target,
                    smoothstep((float)c.phase_ms / (float)anim.transition_ms));
}

int eyeOutline(const Anim& anim, const Pose& pose, int side, float blink,
               Point* out, int cap) {
    if (!out || cap <= 0) return 0;

    const bool  left   = (side < 0);
    const float width  = left ? pose.w_l : pose.w_r;
    const float rest   = left ? pose.h_l : pose.h_r;
    const float height = kBlinkFloor + (rest - kBlinkFloor) * blink;
    const float cx     = (float)(left ? -1 : 1) * pose.spacing * 0.5f +
                         (left ? pose.x_l : pose.x_r);
    const float cy     = left ? pose.y_l : pose.y_r;
    const float tilt   = deg2rad(left ? pose.angle_l : pose.angle_r);

    // Local outline: a rounded rectangle whose corner radius is half its
    // shorter side scaled by anim.corner, so at corner 1.0 a squat eye is a
    // horizontal capsule and a tall one a vertical capsule, and lower values
    // square it off.
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float r  = ((hh < hw) ? hh : hw) * anim.corner;

    OutlineBuilder b{ out, cap, 0 };
    b.line(-hw + r, -hh, hw - r, -hh);
    b.arc(hw - r, -hh + r, -kPi * 0.5f, r);
    b.line(hw, -hh + r, hw, hh - r);
    b.arc(hw - r, hh - r, 0.0f, r);
    b.line(hw - r, hh, -hw + r, hh);
    b.arc(-hw + r, hh - r, kPi * 0.5f, r);
    b.line(-hw, hh - r, -hw, -hh + r);
    b.arc(-hw + r, -hh + r, kPi, r);

    int n = b.n;
    if (n > cap) n = cap;

    const Quat  orient = headQuat(pose);
    const float ct = cosf(tilt);
    const float st = sinf(tilt);
    float normal_z_sum = 0.0f;
    float px0 =  1e30f, px1 = -1e30f, py0 = 1e30f, py1 = -1e30f;

    // First pass: project into viewBox space, keeping the raw values so the
    // travel shift below can be measured from this eye's own bounding box.
    for (int i = 0; i < n; i++) {
        // Tilt the local point, then place it on the flat face.
        const float lx = out[i].x;
        const float ly = out[i].y;
        const float fx = cx + lx * ct - ly * st;
        const float fy = cy + lx * st + ly * ct;

        // Flatten the face coordinate onto the reference sphere. avatar-core
        // does this for every body type, so kRadius is not the body's size.
        const float lon = fx / kRadius;
        const float lat = fy / kRadius;
        const float sx  = kRadius * cosf(lat) * sinf(lon);
        const float sy  = kRadius * sinf(lat);

        // Then drop it onto the body's own front surface.
        const FrontSample fs = frontSample(anim.surface, sx, sy);
        const Vec3 p = qrot(orient, fs.p);
        normal_z_sum += qrot(orient, fs.n).z;

        const float den   = kFocal - p.z * pose.perspective;
        const float scale = (fabsf(den) < 0.0001f) ? (kFocal / 0.0001f) : (kFocal / den);

        out[i].x = p.x * scale;
        out[i].y = p.y * scale;
        if (out[i].x < px0) px0 = out[i].x;
        if (out[i].x > px1) px1 = out[i].x;
        if (out[i].y < py0) py0 = out[i].y;
        if (out[i].y > py1) py1 = out[i].y;
    }

    // Travel damping slides the whole eye toward the view centre without
    // resizing it, then the device mapping runs as usual.
    const float dx = (1.0f - anim.travel) * ((px0 + px1) * 0.5f - anim.view_cx);
    const float dy = (1.0f - anim.travel) * ((py0 + py1) * 0.5f - anim.view_cy);
    // The spread is in device pixels, so it lands after the scale.
    const float spread = (float)(left ? -1 : 1) * anim.spread * 0.5f;
    for (int i = 0; i < n; i++) {
        out[i].x = anim.screen_cx + anim.scale * (out[i].x - dx - anim.view_cx)
                 + spread;
        out[i].y = anim.screen_cy + anim.scale * (out[i].y - dy - anim.view_cy);
    }

    // Matches avatar-core's visibility test: the eye is hidden once its
    // outline has, on average, rotated to face away from the camera.
    if (normal_z_sum <= 0.0f) return 0;
    return n;
}

void outlineSpans(const Point* pts, int n, int origin_x, int origin_y,
                  Span* spans, int rows, int cols) {
    if (!spans || rows <= 0) return;
    for (int r = 0; r < rows; r++) {
        spans[r].x0 = 0;
        spans[r].x1 = -1;      // empty
    }
    if (!pts || n < 3 || cols <= 0) return;

    // Track extents in float first; rounding to pixels only at the end keeps
    // a span from wobbling by a pixel as an edge drifts sub-pixel.
    float lo[kMaxCanvasH];
    float hi[kMaxCanvasH];
    const int limit = (rows < kMaxCanvasH) ? rows : kMaxCanvasH;
    for (int r = 0; r < limit; r++) {
        lo[r] =  1e9f;
        hi[r] = -1e9f;
    }

    for (int i = 0; i < n; i++) {
        const int j = (i + 1 == n) ? 0 : i + 1;
        float x0 = pts[i].x - (float)origin_x;
        float y0 = pts[i].y - (float)origin_y;
        float x1 = pts[j].x - (float)origin_x;
        float y1 = pts[j].y - (float)origin_y;

        if (y0 == y1) {
            // A horizontal edge constrains only the row it sits on.
            const int row = (int)lroundf(y0);
            if (row >= 0 && row < limit) {
                if (x0 < lo[row]) lo[row] = x0;
                if (x1 < lo[row]) lo[row] = x1;
                if (x0 > hi[row]) hi[row] = x0;
                if (x1 > hi[row]) hi[row] = x1;
            }
            continue;
        }
        if (y0 > y1) {
            const float sx = x0, sy = y0;
            x0 = x1; y0 = y1;
            x1 = sx; y1 = sy;
        }
        int r0 = (int)ceilf(y0 - 0.5f);
        int r1 = (int)floorf(y1 - 0.5f);
        if (r0 < 0) r0 = 0;
        if (r1 > limit - 1) r1 = limit - 1;
        for (int row = r0; row <= r1; row++) {
            const float t = ((float)row + 0.5f - y0) / (y1 - y0);
            const float x = x0 + (x1 - x0) * t;
            if (x < lo[row]) lo[row] = x;
            if (x > hi[row]) hi[row] = x;
        }
    }

    for (int row = 0; row < limit; row++) {
        if (hi[row] < lo[row]) continue;
        int a = (int)lroundf(lo[row]);
        int b = (int)lroundf(hi[row]);
        if (b < 0 || a > cols - 1) continue;
        if (a < 0) a = 0;
        if (b > cols - 1) b = cols - 1;
        spans[row].x0 = (int16_t)a;
        spans[row].x1 = (int16_t)b;
    }
}

void blinkInit(const Anim& anim, BlinkState& blink, uint32_t now_ms) {
    blink.started_ms   = 0;
    blink.due_ms       = now_ms + anim.blink_initial_delay_ms;
    blink.ever_started = false;
}

void blinkTick(const Anim& anim, BlinkState& blink, uint32_t now_ms, float rand01) {
    if (anim.blink_duration_ms == 0) return;          // animation never blinks
    if ((int32_t)(now_ms - blink.due_ms) < 0) return;
    if (rand01 < 0.0f) rand01 = 0.0f;
    if (rand01 > 1.0f) rand01 = 1.0f;
    // avatar-core anchors the blink to the moment it came due rather than to
    // "now", so a late tick doesn't drift the schedule.
    blink.started_ms   = blink.due_ms;
    blink.ever_started = true;
    blink.due_ms       = blink.started_ms + anim.blink_duration_ms +
                         anim.blink_min_interval_ms +
                         (uint32_t)(rand01 * (float)(anim.blink_max_interval_ms -
                                                     anim.blink_min_interval_ms));
}

float blinkFactor(const Anim& anim, const BlinkState& blink, uint32_t now_ms) {
    if (!blink.ever_started) return 1.0f;
    const int32_t elapsed = (int32_t)(now_ms - blink.started_ms);
    if (elapsed < 0 || elapsed >= (int32_t)anim.blink_duration_ms) return 1.0f;
    const float progress = (float)elapsed / (float)anim.blink_duration_ms;
    return fabsf(progress * 2.0f - 1.0f);
}

bool blinkActive(const Anim& anim, const BlinkState& blink, uint32_t now_ms) {
    if (!blink.ever_started) return false;
    const int32_t elapsed = (int32_t)(now_ms - blink.started_ms);
    return elapsed >= 0 && elapsed < (int32_t)anim.blink_duration_ms;
}

}  // namespace avatar_face
