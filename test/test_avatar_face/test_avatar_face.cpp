// Checks lib/avatar_face against the vectors_*.h headers, which
// tools/bake_avatar_anim.py generates from a geometry port verified
// byte-identical to the upstream @bible-strong/avatar-core renderer. Both
// sides start from the same float32 keyframes, so the only permitted drift is
// float-vs-double intermediate rounding.
//
// Every structural test runs against all three baked animations. kIdle and
// kWorking are what the device plays, both on the Cubee rounded-cube body;
// kThinking is the frozen grok-bot bake, kept because it is the only
// spherical-body animation and so the only cover for that surface branch.
// kWorking also has per-step hold lengths, and kSleep is the one animation
// with blinking switched off, which kIdle and kThinking do not exercise.
#include <unity.h>
#include <math.h>
#include <stdio.h>

#include "anim_done.h"
#include "anim_idle.h"
#include "anim_sleep.h"
#include "anim_thinking.h"
#include "anim_working.h"
#include "avatar_face.h"
#include "vectors_done.h"
#include "vectors_idle.h"
#include "vectors_sleep.h"
#include "vectors_thinking.h"
#include "vectors_working.h"

using namespace avatar_face;

void setUp(void) {}
void tearDown(void) {}

// Generous next to a 240x135 panel but far tighter than any visible error;
// observed worst case is ~1e-4 px.
static const float kTol = 0.02f;

struct Target {
    const Anim* anim;
    const char* name;
};

static const Target kTargets[] = {
    { &kThinking, "thinking" },
    { &kIdle,     "idle" },
    { &kWorking,  "working" },
    { &kSleep,    "sleep" },
    { &kDone,     "done" },
};
static const int kTargetCount = (int)(sizeof(kTargets) / sizeof(kTargets[0]));

// ---- generator agreement ----

template <typename CaseT>
static void check_poses(const Anim& anim, const CaseT* cases, int count,
                        const char* name) {
    for (int i = 0; i < count; i++) {
        const CaseT& c = cases[i];
        const Pose   p = poseAt(anim, c.t_ms);
        const float* got = &p.head_x;
        for (int f = 0; f < 15; f++) {
            char msg[80];
            snprintf(msg, sizeof(msg), "%s case %d t=%lu field %d",
                     name, i, c.t_ms, f);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, c.pose[f], got[f], msg);
        }
    }
}

template <typename CaseT, typename EyeT>
static void check_eye(const Anim& anim, int case_index, const CaseT& c, int side,
                      const EyeT& want, const char* name) {
    Point pts[kMaxOutlinePoints];
    const Pose p = poseAt(anim, c.t_ms);
    const int  n = eyeOutline(anim, p, side, c.blink, pts, kMaxOutlinePoints);

    char msg[96];
    snprintf(msg, sizeof(msg), "%s case %d t=%lu side %d visibility",
             name, case_index, c.t_ms, side);
    TEST_ASSERT_EQUAL_MESSAGE(want.visible ? want.n : 0, n, msg);
    if (n == 0) return;

    float x0 = pts[0].x, x1 = pts[0].x, y0 = pts[0].y, y1 = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].x < x0) x0 = pts[i].x;
        if (pts[i].x > x1) x1 = pts[i].x;
        if (pts[i].y < y0) y0 = pts[i].y;
        if (pts[i].y > y1) y1 = pts[i].y;
    }

    snprintf(msg, sizeof(msg), "%s case %d t=%lu side %d bbox",
             name, case_index, c.t_ms, side);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, want.x0, x0, msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, want.x1, x1, msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, want.y0, y0, msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, want.y1, y1, msg);

    snprintf(msg, sizeof(msg), "%s case %d t=%lu side %d points",
             name, case_index, c.t_ms, side);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, want.first_x, pts[0].x, msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, want.first_y, pts[0].y, msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, want.mid_x, pts[n / 2].x, msg);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(kTol, want.mid_y, pts[n / 2].y, msg);
}

template <typename CaseT>
static void check_outlines(const Anim& anim, const CaseT* cases, int count,
                           const char* name) {
    for (int i = 0; i < count; i++) {
        check_eye(anim, i, cases[i], -1, cases[i].left, name);
        check_eye(anim, i, cases[i], +1, cases[i].right, name);
    }
}

static void test_pose_matches_generator(void) {
    check_poses(kThinking, thinking_vectors::kCases,
                thinking_vectors::kCaseCount, "thinking");
    check_poses(kIdle, idle_vectors::kCases, idle_vectors::kCaseCount, "idle");
    check_poses(kWorking, working_vectors::kCases,
                working_vectors::kCaseCount, "working");
    check_poses(kSleep, sleep_vectors::kCases, sleep_vectors::kCaseCount, "sleep");
    check_poses(kDone, done_vectors::kCases, done_vectors::kCaseCount, "done");
}

static void test_eye_outlines_match_generator(void) {
    check_outlines(kThinking, thinking_vectors::kCases,
                   thinking_vectors::kCaseCount, "thinking");
    check_outlines(kIdle, idle_vectors::kCases, idle_vectors::kCaseCount, "idle");
    check_outlines(kWorking, working_vectors::kCases,
                   working_vectors::kCaseCount, "working");
    check_outlines(kSleep, sleep_vectors::kCases, sleep_vectors::kCaseCount, "sleep");
    check_outlines(kDone, done_vectors::kCases, done_vectors::kCaseCount, "done");
}

// ---- structural invariants, both animations ----

// The generated limit is the measured worst case plus slack; if a future
// keyframe edit blows past it the outline would silently truncate into an
// open polygon, so assert we never come close to the cap.
static void test_outline_fits_buffer(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        int worst = 0;
        for (uint32_t t = 0; t < loopMs(anim) * 2; t += 7) {
            const Pose p = poseAt(anim, t);
            for (int side = -1; side <= 1; side += 2) {
                for (float blink = 0.0f; blink <= 1.0f; blink += 0.25f) {
                    Point pts[kMaxOutlinePoints];
                    const int n = eyeOutline(anim, p, side, blink, pts,
                                             kMaxOutlinePoints);
                    if (n > worst) worst = n;
                }
            }
        }
        TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, worst, kTargets[k].name);
        TEST_ASSERT_LESS_THAN_INT_MESSAGE(kMaxOutlinePoints, worst, kTargets[k].name);
    }
}

// Every point must land on the panel: the bake picks `scale` so the whole loop
// fits with a small margin, and a regression here would clip the eyes.
static void test_outline_stays_on_panel(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        for (uint32_t t = 0; t < loopMs(anim) * 2; t += 11) {
            const Pose p = poseAt(anim, t);
            for (int side = -1; side <= 1; side += 2) {
                for (float blink = 0.0f; blink <= 1.0f; blink += 0.25f) {
                    Point pts[kMaxOutlinePoints];
                    const int n = eyeOutline(anim, p, side, blink, pts,
                                             kMaxOutlinePoints);
                    for (int i = 0; i < n; i++) {
                        TEST_ASSERT_TRUE_MESSAGE(
                            pts[i].x >= 0.0f && pts[i].x <= 239.0f,
                            kTargets[k].name);
                        TEST_ASSERT_TRUE_MESSAGE(
                            pts[i].y >= 0.0f && pts[i].y <= 134.0f,
                            kTargets[k].name);
                    }
                }
            }
        }
    }
}

// No single eye may exceed its animation's blit canvas, which is sized from
// the same measurement pass.
static void test_eye_fits_canvas(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        for (uint32_t t = 0; t < loopMs(anim) * 2; t += 11) {
            const Pose p = poseAt(anim, t);
            for (int side = -1; side <= 1; side += 2) {
                for (float blink = 0.0f; blink <= 1.0f; blink += 0.25f) {
                    Point pts[kMaxOutlinePoints];
                    const int n = eyeOutline(anim, p, side, blink, pts,
                                             kMaxOutlinePoints);
                    if (n == 0) continue;
                    float x0 = pts[0].x, x1 = pts[0].x;
                    float y0 = pts[0].y, y1 = pts[0].y;
                    for (int i = 1; i < n; i++) {
                        if (pts[i].x < x0) x0 = pts[i].x;
                        if (pts[i].x > x1) x1 = pts[i].x;
                        if (pts[i].y < y0) y0 = pts[i].y;
                        if (pts[i].y > y1) y1 = pts[i].y;
                    }
                    TEST_ASSERT_TRUE_MESSAGE(x1 - x0 <= (float)anim.canvas_w - 1.0f,
                                             kTargets[k].name);
                    TEST_ASSERT_TRUE_MESSAGE(y1 - y0 <= (float)anim.canvas_h - 1.0f,
                                             kTargets[k].name);
                }
            }
        }
    }
}

static void test_pose_loops(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        const uint32_t loop = loopMs(anim);
        // Second and third loops are identical; only the very first transition
        // differs, because it eases in from the neutral pose.
        for (uint32_t t = 0; t < loop; t += 13) {
            const Pose a = poseAt(anim, loop + t);
            const Pose b = poseAt(anim, 2 * loop + t);
            const float* fa = &a.head_x;
            const float* fb = &b.head_x;
            for (int f = 0; f < 15; f++) TEST_ASSERT_EQUAL_FLOAT(fa[f], fb[f]);
        }
    }
}

static void test_first_transition_starts_neutral(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        const Pose p = poseAt(anim, 0);
        TEST_ASSERT_EQUAL_FLOAT(anim.neutral.head_x, p.head_x);
        TEST_ASSERT_EQUAL_FLOAT(anim.neutral.spacing, p.spacing);
        // The looped pass over the same instant comes from the last step.
        const Pose q = poseAt(anim, loopMs(anim));
        TEST_ASSERT_EQUAL_FLOAT(anim.steps[anim.step_count - 1].spacing, q.spacing);
    }
}

static void test_hold_phase_is_static(void) {
    // Holds must be pixel-stable so the renderer can skip redrawing them.
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        uint32_t edge = 0;
        for (int step = 0; step < anim.step_count; step++) {
            const uint32_t base = edge + anim.transition_ms;
            const Pose a = poseAt(anim, base);
            const Pose b = poseAt(anim, base + anim.holds_ms[step] - 1);
            const float* fa = &a.head_x;
            const float* fb = &b.head_x;
            for (int f = 0; f < 15; f++) TEST_ASSERT_EQUAL_FLOAT(fa[f], fb[f]);
            TEST_ASSERT_FALSE(inTransition(anim, base));
            TEST_ASSERT_TRUE(inTransition(anim, edge));
            // cursorAt must land on this step for every instant inside it.
            TEST_ASSERT_EQUAL_INT(step, cursorAt(anim, base).index);
            TEST_ASSERT_EQUAL_INT(step, cursorAt(anim, edge).index);
            edge += stepMs(anim, step);
        }
        TEST_ASSERT_EQUAL_UINT32(loopMs(anim), edge);
    }
}

static void test_blink_schedule(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        BlinkState b;
        blinkInit(anim, b, 1000);
        TEST_ASSERT_FALSE(b.ever_started);
        TEST_ASSERT_EQUAL_FLOAT(1.0f, blinkFactor(anim, b, 1000));

        if (anim.blink_duration_ms == 0) {
            // Blinking off: no tick ever starts one, and the lids stay open.
            for (uint32_t t = 1000; t < 1000 + loopMs(anim); t += 250) {
                blinkTick(anim, b, t, 0.5f);
                TEST_ASSERT_FALSE_MESSAGE(b.ever_started, kTargets[k].name);
                TEST_ASSERT_EQUAL_FLOAT(1.0f, blinkFactor(anim, b, t));
                TEST_ASSERT_FALSE(blinkActive(anim, b, t));
            }
            continue;
        }

        // Nothing happens before the initial delay elapses.
        blinkTick(anim, b, 1000 + anim.blink_initial_delay_ms - 1, 0.0f);
        TEST_ASSERT_FALSE(b.ever_started);

        const uint32_t due = 1000 + anim.blink_initial_delay_ms;
        blinkTick(anim, b, due + 5, 0.0f);   // a late tick must not drift the anchor
        TEST_ASSERT_TRUE(b.ever_started);
        TEST_ASSERT_EQUAL_UINT32(due, b.started_ms);

        // Fully open at both ends, fully shut in the middle.
        TEST_ASSERT_EQUAL_FLOAT(1.0f, blinkFactor(anim, b, due));
        TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f,
                                 blinkFactor(anim, b, due + anim.blink_duration_ms / 2));
        TEST_ASSERT_EQUAL_FLOAT(1.0f, blinkFactor(anim, b, due + anim.blink_duration_ms));
        TEST_ASSERT_TRUE(blinkActive(anim, b, due + 10));
        TEST_ASSERT_FALSE(blinkActive(anim, b, due + anim.blink_duration_ms));

        // rand01 = 0 picks the minimum interval, 1 the maximum.
        TEST_ASSERT_EQUAL_UINT32(due + anim.blink_duration_ms + anim.blink_min_interval_ms,
                                 b.due_ms);
        BlinkState c;
        blinkInit(anim, c, 0);
        blinkTick(anim, c, anim.blink_initial_delay_ms, 1.0f);
        TEST_ASSERT_EQUAL_UINT32(anim.blink_initial_delay_ms + anim.blink_duration_ms +
                                     anim.blink_max_interval_ms,
                                 c.due_ms);
    }
}

static void test_blink_squashes_height(void) {
    // A shut eye must be shorter than an open one but never vanish.
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        const Pose p = poseAt(anim, anim.transition_ms + 10);   // first hold
        Point open_pts[kMaxOutlinePoints];
        Point shut_pts[kMaxOutlinePoints];
        const int no = eyeOutline(anim, p, -1, 1.0f, open_pts, kMaxOutlinePoints);
        const int ns = eyeOutline(anim, p, -1, 0.0f, shut_pts, kMaxOutlinePoints);
        TEST_ASSERT_GREATER_THAN_INT(0, no);
        TEST_ASSERT_GREATER_THAN_INT(0, ns);

        float y0 = open_pts[0].y, y1 = open_pts[0].y;
        for (int i = 1; i < no; i++) {
            if (open_pts[i].y < y0) y0 = open_pts[i].y;
            if (open_pts[i].y > y1) y1 = open_pts[i].y;
        }
        const float open_h = y1 - y0;
        y0 = shut_pts[0].y; y1 = shut_pts[0].y;
        for (int i = 1; i < ns; i++) {
            if (shut_pts[i].y < y0) y0 = shut_pts[i].y;
            if (shut_pts[i].y > y1) y1 = shut_pts[i].y;
        }
        const float shut_h = y1 - y0;

        // Strictly shorter, never gone. A proportional bound would not hold
        // for kSleep: its lids are wide and tilted 20 degrees, so the tilt
        // alone accounts for most of the bounding box height.
        TEST_ASSERT_TRUE_MESSAGE(shut_h < open_h - 1.0f, kTargets[k].name);
        TEST_ASSERT_TRUE_MESSAGE(shut_h > 1.0f, kTargets[k].name);
    }
}

// ---- rasteriser ----

static int span_rows(const Span* spans, int rows) {
    int used = 0;
    for (int r = 0; r < rows; r++) if (spans[r].x1 >= spans[r].x0) used++;
    return used;
}

// Rasterise the eye the way EyesCard does: centre the canvas on the outline,
// clamped to the panel.
static int rasterise(const Anim& anim, uint32_t t, int side, float blink,
                     Span* spans) {
    Point pts[kMaxOutlinePoints];
    const Pose p = poseAt(anim, t);
    const int  n = eyeOutline(anim, p, side, blink, pts, kMaxOutlinePoints);
    if (n == 0) return 0;

    float x0 = pts[0].x, x1 = pts[0].x, y0 = pts[0].y, y1 = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].x < x0) x0 = pts[i].x;
        if (pts[i].x > x1) x1 = pts[i].x;
        if (pts[i].y < y0) y0 = pts[i].y;
        if (pts[i].y > y1) y1 = pts[i].y;
    }
    int ox = (int)lroundf((x0 + x1) * 0.5f) - anim.canvas_w / 2;
    int oy = (int)lroundf((y0 + y1) * 0.5f) - anim.canvas_h / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox > 240 - anim.canvas_w) ox = 240 - anim.canvas_w;
    if (oy > 135 - anim.canvas_h) oy = 135 - anim.canvas_h;

    outlineSpans(pts, n, ox, oy, spans, anim.canvas_h, anim.canvas_w);
    return n;
}

static void test_spans_stay_in_canvas(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        for (uint32_t t = 0; t < loopMs(anim); t += 17) {
            for (int side = -1; side <= 1; side += 2) {
                for (float blink = 0.0f; blink <= 1.0f; blink += 0.5f) {
                    Span spans[kMaxCanvasH];
                    if (rasterise(anim, t, side, blink, spans) == 0) continue;
                    for (int r = 0; r < anim.canvas_h; r++) {
                        if (spans[r].x1 < spans[r].x0) continue;
                        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, spans[r].x0);
                        TEST_ASSERT_LESS_THAN_INT(anim.canvas_w, spans[r].x1);
                    }
                }
            }
        }
    }
}

// A convex outline covers one unbroken run of rows, so a gap would mean the
// span pass dropped an edge.
static void test_spans_are_contiguous(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        for (uint32_t t = 0; t < loopMs(anim); t += 17) {
            for (int side = -1; side <= 1; side += 2) {
                for (float blink = 0.0f; blink <= 1.0f; blink += 0.5f) {
                    Span spans[kMaxCanvasH];
                    if (rasterise(anim, t, side, blink, spans) == 0) continue;
                    int runs = 0;
                    bool inside = false;
                    for (int r = 0; r < anim.canvas_h; r++) {
                        const bool filled = spans[r].x1 >= spans[r].x0;
                        if (filled && !inside) runs++;
                        inside = filled;
                    }
                    TEST_ASSERT_EQUAL_INT_MESSAGE(1, runs, kTargets[k].name);
                }
            }
        }
    }
}

// Summed span pixels must track the outline's true area; a fill that leaked or
// collapsed would show up here immediately.
static void test_span_area_matches_polygon(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        for (uint32_t t = 0; t < loopMs(anim); t += 29) {
            for (int side = -1; side <= 1; side += 2) {
                Point pts[kMaxOutlinePoints];
                const Pose p = poseAt(anim, t);
                const int  n = eyeOutline(anim, p, side, 1.0f, pts, kMaxOutlinePoints);
                if (n == 0) continue;

                double shoelace = 0.0;
                for (int i = 0; i < n; i++) {
                    const int j = (i + 1 == n) ? 0 : i + 1;
                    shoelace += (double)pts[i].x * pts[j].y - (double)pts[j].x * pts[i].y;
                }
                const double area = fabs(shoelace) * 0.5;

                Span spans[kMaxCanvasH];
                rasterise(anim, t, side, 1.0f, spans);
                long filled = 0;
                for (int r = 0; r < anim.canvas_h; r++) {
                    if (spans[r].x1 >= spans[r].x0) filled += spans[r].x1 - spans[r].x0 + 1;
                }

                // Pixel quantisation on a shape this size lands well inside 15%.
                TEST_ASSERT_TRUE(filled > 0);
                TEST_ASSERT_TRUE(fabs((double)filled - area) < area * 0.15 + 8.0);
            }
        }
    }
}

static void test_spans_cover_outline_bbox(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        Point pts[kMaxOutlinePoints];
        const uint32_t t = anim.transition_ms + 100;
        const Pose p = poseAt(anim, t);
        const int  n = eyeOutline(anim, p, -1, 1.0f, pts, kMaxOutlinePoints);
        TEST_ASSERT_GREATER_THAN_INT(0, n);

        Span spans[kMaxCanvasH];
        rasterise(anim, t, -1, 1.0f, spans);

        int top = -1, bottom = -1;
        for (int r = 0; r < anim.canvas_h; r++) {
            if (spans[r].x1 < spans[r].x0) continue;
            if (top < 0) top = r;
            bottom = r;
        }
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, top);

        float y0 = pts[0].y, y1 = pts[0].y;
        for (int i = 1; i < n; i++) {
            if (pts[i].y < y0) y0 = pts[i].y;
            if (pts[i].y > y1) y1 = pts[i].y;
        }
        // Row-centre sampling can shave at most one row off each end.
        TEST_ASSERT_FLOAT_WITHIN(1.5f, y1 - y0, (float)(bottom - top));
    }
}

static void test_spans_handle_degenerate_input(void) {
    const int rows = kIdle.canvas_h;
    const int cols = kIdle.canvas_w;
    Span spans[kMaxCanvasH];
    Point pts[3] = { { 10.0f, 10.0f }, { 20.0f, 10.0f }, { 15.0f, 20.0f } };

    // Too few points, no points, and a null buffer must all clear cleanly.
    outlineSpans(pts, 2, 0, 0, spans, rows, cols);
    TEST_ASSERT_EQUAL_INT(0, span_rows(spans, rows));
    outlineSpans(nullptr, 3, 0, 0, spans, rows, cols);
    TEST_ASSERT_EQUAL_INT(0, span_rows(spans, rows));
    outlineSpans(pts, 3, 0, 0, nullptr, rows, cols);   // must not crash

    // A triangle well outside the canvas leaves every row empty.
    Point far_off[3] = { { 900.0f, 900.0f }, { 950.0f, 900.0f }, { 925.0f, 950.0f } };
    outlineSpans(far_off, 3, 0, 0, spans, rows, cols);
    TEST_ASSERT_EQUAL_INT(0, span_rows(spans, rows));

    // ...and a valid one covers something.
    outlineSpans(pts, 3, 0, 0, spans, rows, cols);
    TEST_ASSERT_GREATER_THAN_INT(0, span_rows(spans, rows));
}

// EyesCard blits the left eye's canvas first, then the right eye's. The
// canvases are sized at the shared maximum and are black outside their own
// eye, so where the right canvas overhangs the left eye it repaints those
// pixels black.
//
// Every animation the device actually plays clips nothing. Only the unwired
// kThinking overlaps at all, and only while its first play blends out of the
// wide-set neutral pose: 3 px off the left eye's inner edge for one 500 ms
// window. It is allowed here rather than fixed because nothing renders it;
// were it ever wired up again, both eyes would need composing into one canvas.
static float maxInnerEdgeClipPx(const Anim& anim) {
    return (&anim == &kThinking) ? 3.0f : 0.0f;
}

static void test_second_eye_blit_barely_touches_the_first(void) {
    for (int k = 0; k < kTargetCount; k++) {
        const Anim& anim = *kTargets[k].anim;
        for (uint32_t t = 0; t < loopMs(anim); t += 13) {
            for (float blink = 0.0f; blink <= 1.0f; blink += 0.25f) {
                const Pose p = poseAt(anim, t);
                Point pts[kMaxOutlinePoints];

                // Left eye's lit bounding box.
                const int nl = eyeOutline(anim, p, -1, blink, pts, kMaxOutlinePoints);
                if (nl == 0) continue;
                float lx0 = pts[0].x, lx1 = pts[0].x;
                float ly0 = pts[0].y, ly1 = pts[0].y;
                for (int i = 1; i < nl; i++) {
                    if (pts[i].x < lx0) lx0 = pts[i].x;
                    if (pts[i].x > lx1) lx1 = pts[i].x;
                    if (pts[i].y < ly0) ly0 = pts[i].y;
                    if (pts[i].y > ly1) ly1 = pts[i].y;
                }

                // Right eye's canvas rect, placed exactly as EyesCard does.
                const int nr = eyeOutline(anim, p, +1, blink, pts, kMaxOutlinePoints);
                if (nr == 0) continue;
                float rx0 = pts[0].x, rx1 = pts[0].x;
                float ry0 = pts[0].y, ry1 = pts[0].y;
                for (int i = 1; i < nr; i++) {
                    if (pts[i].x < rx0) rx0 = pts[i].x;
                    if (pts[i].x > rx1) rx1 = pts[i].x;
                    if (pts[i].y < ry0) ry0 = pts[i].y;
                    if (pts[i].y > ry1) ry1 = pts[i].y;
                }
                int ox = (int)lroundf((rx0 + rx1) * 0.5f) - kMaxCanvasW / 2;
                int oy = (int)lroundf((ry0 + ry1) * 0.5f) - kMaxCanvasH / 2;
                if (ox < 0) ox = 0;
                if (oy < 0) oy = 0;
                if (ox > 240 - kMaxCanvasW) ox = 240 - kMaxCanvasW;
                if (oy > 135 - kMaxCanvasH) oy = 135 - kMaxCanvasH;

                const bool hits_y = (ly0 <= (float)(oy + kMaxCanvasH - 1)) &&
                                    ((float)oy <= ly1);
                if (!hits_y) continue;
                // In whole pixels: the eye's rightmost lit column vs the
                // right canvas's leftmost column.
                const float clipped = (float)lroundf(lx1) - (float)ox + 1.0f;
                char msg[96];
                snprintf(msg, sizeof(msg), "%s t=%u blink=%.2f clips %.1f px",
                         kTargets[k].name, (unsigned)t, (double)blink,
                         (double)clipped);
                TEST_ASSERT_TRUE_MESSAGE(clipped <= maxInnerEdgeClipPx(anim), msg);
            }
        }
    }
}

// EyesCard paints two overlays in the right margin — the WORKING sweat bead
// and the DISCONNECTED sleep Zs. Both are drawn after the eyes, but their
// erase rects are not clipped, so the eyes of the animation each one
// accompanies must stay left of that overlay's band. Keep these in step with
// kSweatBandX / kZSpawnX in src/ui/cards/EyesCard.cpp.
static void test_right_margin_overlays_never_erase_an_eye(void) {
    struct Owner { const Anim* anim; const char* name; int band_x; };
    const Owner owners[] = {
        { &kWorking, "working sweat bead", 216 },
        { &kSleep,   "sleep Zs",           210 },
    };
    for (int k = 0; k < 2; k++) {
        const Anim& anim = *owners[k].anim;
        const int kOverlayBandX = owners[k].band_x;
        for (uint32_t t = 0; t < loopMs(anim); t += 7) {
            const Pose p = poseAt(anim, t);
            for (int side = -1; side <= 1; side += 2) {
                for (float blink = 0.0f; blink <= 1.0f; blink += 0.5f) {
                    Point pts[kMaxOutlinePoints];
                    const int n = eyeOutline(anim, p, side, blink, pts,
                                             kMaxOutlinePoints);
                    for (int i = 0; i < n; i++) {
                        // +1 px of slack for the WORKING jitter.
                        TEST_ASSERT_TRUE_MESSAGE(
                            pts[i].x + 1.0f < (float)kOverlayBandX, owners[k].name);
                    }
                }
            }
        }
    }
}

// The DONE celebration erases a 27x27 bbox around each sparkle anchor before
// redrawing it, and those erases are not clipped, so the burst's eyes must
// stay out of all three. Keep in step with kDoneSparkleAnchors / Arm in
// src/ui/cards/EyesCard.cpp.
static void test_done_sparkles_never_erase_an_eye(void) {
    struct Anchor { int cx, cy; };
    const Anchor anchors[] = { { 224, 36 }, { 14, 36 }, { 120, 14 } };
    const int arm = 11 + 2;      // kDoneSparkleArm plus the erase padding

    for (uint32_t t = 0; t < loopMs(kDone); t += 7) {
        const Pose p = poseAt(kDone, t);
        for (int side = -1; side <= 1; side += 2) {
            for (float blink = 0.0f; blink <= 1.0f; blink += 0.5f) {
                Point pts[kMaxOutlinePoints];
                const int n = eyeOutline(kDone, p, side, blink, pts,
                                         kMaxOutlinePoints);
                for (int i = 0; i < n; i++) {
                    for (int a = 0; a < 3; a++) {
                        const bool in_x = pts[i].x >= (float)(anchors[a].cx - arm) &&
                                          pts[i].x <= (float)(anchors[a].cx + arm);
                        const bool in_y = pts[i].y >= (float)(anchors[a].cy - arm) &&
                                          pts[i].y <= (float)(anchors[a].cy + arm);
                        char msg[80];
                        snprintf(msg, sizeof(msg), "t=%u anchor %d at (%.1f, %.1f)",
                                 (unsigned)t, a, (double)pts[i].x, (double)pts[i].y);
                        TEST_ASSERT_FALSE_MESSAGE(in_x && in_y, msg);
                    }
                }
            }
        }
    }
}

static void test_rejects_bad_buffer(void) {
    const Pose p = poseAt(kIdle, 0);
    Point pts[kMaxOutlinePoints];
    TEST_ASSERT_EQUAL_INT(0, eyeOutline(kIdle, p, -1, 1.0f, nullptr, kMaxOutlinePoints));
    TEST_ASSERT_EQUAL_INT(0, eyeOutline(kIdle, p, -1, 1.0f, pts, 0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pose_matches_generator);
    RUN_TEST(test_eye_outlines_match_generator);
    RUN_TEST(test_outline_fits_buffer);
    RUN_TEST(test_outline_stays_on_panel);
    RUN_TEST(test_eye_fits_canvas);
    RUN_TEST(test_pose_loops);
    RUN_TEST(test_first_transition_starts_neutral);
    RUN_TEST(test_hold_phase_is_static);
    RUN_TEST(test_blink_schedule);
    RUN_TEST(test_blink_squashes_height);
    RUN_TEST(test_spans_stay_in_canvas);
    RUN_TEST(test_spans_are_contiguous);
    RUN_TEST(test_span_area_matches_polygon);
    RUN_TEST(test_spans_cover_outline_bbox);
    RUN_TEST(test_spans_handle_degenerate_input);
    RUN_TEST(test_second_eye_blit_barely_touches_the_first);
    RUN_TEST(test_right_margin_overlays_never_erase_an_eye);
    RUN_TEST(test_done_sparkles_never_erase_an_eye);
    RUN_TEST(test_rejects_bad_buffer);
    return UNITY_END();
}
