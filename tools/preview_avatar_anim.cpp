// Renders a baked avatar animation through the real lib/avatar_face code path
// and writes a PGM contact sheet, so the shipped geometry and rasteriser can be
// eyeballed without flashing hardware.
//
// Build and run from the repo root:
//   c++ -std=gnu++17 -I lib/avatar_face -o /tmp/preview_avatar_anim \
//       tools/preview_avatar_anim.cpp lib/avatar_face/avatar_face.cpp
//   /tmp/preview_avatar_anim idle /tmp/idle.pgm
//
// Animated preview (raw rgb24 240x135 on stdout, two loops at 25 fps):
//   /tmp/preview_avatar_anim idle --anim 25 2 | ffmpeg -f rawvideo \
//       -pix_fmt rgb24 -s 240x135 -r 25 -i - idle.gif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "anim_done.h"
#include "anim_idle.h"
#include "anim_sleep.h"
#include "anim_thinking.h"
#include "anim_working.h"
#include "avatar_face.h"

using namespace avatar_face;

static const int W = 240;
static const int H = 135;
static const int COLS = 2;
static const int PAD = 6;

// Mirrors EyesCard::drawAvatarEye: place the canvas on the eye, clamp it
// inside the panel, rasterise into it, then blit.
static void draw_eye(unsigned char* frame, const Anim& anim, const Pose& pose,
                     int side, float blink) {
    Point pts[kMaxOutlinePoints];
    const int n = eyeOutline(anim, pose, side, blink, pts, kMaxOutlinePoints);
    if (n == 0) return;

    float x0 = pts[0].x, x1 = pts[0].x, y0 = pts[0].y, y1 = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].x < x0) x0 = pts[i].x;
        if (pts[i].x > x1) x1 = pts[i].x;
        if (pts[i].y < y0) y0 = pts[i].y;
        if (pts[i].y > y1) y1 = pts[i].y;
    }
    int ox = (int)lroundf((x0 + x1) * 0.5f) - kMaxCanvasW / 2;
    int oy = (int)lroundf((y0 + y1) * 0.5f) - kMaxCanvasH / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox > W - kMaxCanvasW) ox = W - kMaxCanvasW;
    if (oy > H - kMaxCanvasH) oy = H - kMaxCanvasH;

    Span spans[kMaxCanvasH];
    outlineSpans(pts, n, ox, oy, spans, kMaxCanvasH, kMaxCanvasW);
    for (int row = 0; row < kMaxCanvasH; row++) {
        if (spans[row].x1 < spans[row].x0) continue;
        const int y = oy + row;
        if (y < 0 || y >= H) continue;
        for (int x = spans[row].x0; x <= spans[row].x1; x++) {
            const int sx = ox + x;
            if (sx >= 0 && sx < W) frame[y * W + sx] = 255;
        }
    }
}

// Mirrors EyesCard's STATE_WORKING dressing: a 1 px jitter on both eyes and a
// blue sweat bead dripping down the right margin. Kept in step with the
// constants at the top of src/ui/cards/EyesCard.cpp by hand — this is a
// preview, the device is the source of truth.
static const int      SHAKE_AMP = 1;
static const uint32_t SHAKE_XMS = 90, SHAKE_YMS = 130;
static const int      SWEAT_CX = 222, SWEAT_TOP_Y = 30, SWEAT_DRIP = 13;
static const int      SWEAT_R = 4, SWEAT_TIP = 6;
static const uint32_t SWEAT_DRIP_MS = 1500, SWEAT_CYCLE_MS = 1500 + 700;

static void put_rgb(unsigned char* rgb, int x, int y, int r, int g, int b) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    unsigned char* px = &rgb[(y * W + x) * 3];
    px[0] = (unsigned char)r; px[1] = (unsigned char)g; px[2] = (unsigned char)b;
}

static void draw_sweat(unsigned char* rgb, uint32_t t) {
    const uint32_t phase = t % SWEAT_CYCLE_MS;
    if (phase >= SWEAT_DRIP_MS) return;
    const int cy = SWEAT_TOP_Y + (int)((int32_t)SWEAT_DRIP * (int32_t)phase /
                                       (int32_t)SWEAT_DRIP_MS);
    // Bead.
    for (int dy = -SWEAT_R; dy <= SWEAT_R; dy++) {
        for (int dx = -SWEAT_R; dx <= SWEAT_R; dx++) {
            if (dx * dx + dy * dy <= SWEAT_R * SWEAT_R)
                put_rgb(rgb, SWEAT_CX + dx, cy + dy, 0x55, 0xAA, 0xFF);
        }
    }
    // Taper above it.
    const int tip_y = cy - SWEAT_TIP - SWEAT_R + 1;
    for (int y = tip_y; y <= cy; y++) {
        const float k = (float)(y - tip_y) / (float)(cy - tip_y);
        const int half = (int)lroundf(k * SWEAT_R);
        for (int x = -half; x <= half; x++)
            put_rgb(rgb, SWEAT_CX + x, y, 0x55, 0xAA, 0xFF);
    }
}

// ponytail: raw rgb24 frames on stdout, let ffmpeg own the encoding.
// Zs mirror EyesCard::drawSleepZs, at the same 3 s loop. Drawn as filled
// blocks rather than the device's font glyph — close enough to judge placement.
static void draw_zs(unsigned char* rgb, uint32_t t) {
    const uint32_t loop = 3000;
    const uint32_t offs[3] = { 0, 1000, 2000 };
    for (int i = 0; i < 3; i++) {
        const uint32_t age = (t % loop + offs[i]) % loop;
        if (age >= 2550) continue;
        const int x = 210 + (int)(20 * (int32_t)age / (int32_t)loop);
        const int y = 50  + (int)(-45 * (int32_t)age / (int32_t)loop);
        const int sz = (age < 1000) ? 1 : (age < 2000) ? 2 : 3;
        const int v = (age < 1800) ? 255 : 110;
        // A blocky Z: top bar, diagonal, bottom bar, in `sz`-sized cells.
        for (int c = 0; c < 5; c++) {
            for (int px = 0; px < sz; px++) {
                for (int py = 0; py < sz; py++) {
                    put_rgb(rgb, x + c * sz + px, y + py, v, v, v);
                    put_rgb(rgb, x + c * sz + px, y + 4 * sz + py, v, v, v);
                    put_rgb(rgb, x + (4 - c) * sz + px, y + c * sz + py, v, v, v);
                }
            }
        }
    }
}

// Sparkles mirror EyesCard::drawDoneFrame.
static void draw_sparkles(unsigned char* rgb, uint32_t t) {
    const int ax[3] = { 224, 14, 120 }, ay[3] = { 36, 36, 14 }, arm = 11;
    float b;
    if (t < 250)         b = t / 250.0f;
    else if (t < 1000)   b = 1.0f;
    else if (t < 2300)   b = 1.0f - (t - 1000) / 1300.0f;
    else                 b = 0.0f;
    int n = (b <= 0.0f) ? 0 : (b >= 1.0f) ? 5 : (int)lroundf(b * 5.0f);
    for (int a = 0; a < 3; a++) {
        const int cx = ax[a], cy = ay[a];
        const int ox[5] = { 0, arm, -arm, 0, 0 };
        const int oy[5] = { 0, 0, 0, -arm, arm };
        for (int k = 0; k < n; k++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    put_rgb(rgb, cx + ox[k] + dx, cy + oy[k] + dy, 255, 255, 255);
    }
}

static int animate(const Anim& anim, int fps, int loops, bool working,
                   bool sleeping, bool celebrating) {
    const uint32_t period = loopMs(anim);
    const uint32_t dt     = 1000u / (uint32_t)fps;
    BlinkState blink;
    blinkInit(anim, blink, 0);
    std::vector<unsigned char> frame(W * H);
    std::vector<unsigned char> rgb(W * H * 3);
    for (uint32_t t = 0; t < period * (uint32_t)loops; t += dt) {
        blinkTick(anim, blink, t, 0.5f);    // deterministic stand-in for esp_random()
        memset(frame.data(), 0, frame.size());
        const Pose p = poseAt(anim, t);
        const float b = blinkFactor(anim, blink, t);
        draw_eye(frame.data(), anim, p, -1, b);
        draw_eye(frame.data(), anim, p, +1, b);

        int sx = 0, sy = 0;
        if (working) {
            const float twoPi = 2.0f * 3.14159265f;
            sx = (int)lroundf(SHAKE_AMP * sinf(twoPi * (float)(t % SHAKE_XMS) / SHAKE_XMS));
            sy = (int)lroundf(SHAKE_AMP * sinf(twoPi * (float)(t % SHAKE_YMS) / SHAKE_YMS));
        }
        memset(rgb.data(), 0, rgb.size());
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                if (!frame[y * W + x]) continue;
                put_rgb(rgb.data(), x + sx, y + sy, 255, 255, 255);
            }
        }
        if (working)  draw_sweat(rgb.data(), t);
        if (sleeping) draw_zs(rgb.data(), t);
        if (celebrating) draw_sparkles(rgb.data(), t);
        fwrite(rgb.data(), 1, rgb.size(), stdout);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s {idle|working|sleep|done|thinking} "
                "[out.pgm | --anim FPS LOOPS]\n",
                argv[0]);
        return 2;
    }
    const Anim& anim = (strcmp(argv[1], "done")     == 0) ? kDone
                     : (strcmp(argv[1], "working")  == 0) ? kWorking
                     : (strcmp(argv[1], "sleep")    == 0) ? kSleep
                     : (strcmp(argv[1], "thinking") == 0) ? kThinking
                                                          : kIdle;

    if (argc > 2 && strcmp(argv[2], "--anim") == 0) {
        return animate(anim, argc > 3 ? atoi(argv[3]) : 25,
                       argc > 4 ? atoi(argv[4]) : 1,
                       strcmp(argv[1], "working") == 0,
                       strcmp(argv[1], "sleep") == 0,
                       strcmp(argv[1], "done") == 0);
    }
    const char* out = (argc > 2) ? argv[2] : "/tmp/avatar_anim.pgm";

    struct Frame { uint32_t t; float blink; };
    std::vector<Frame> frames;
    // Midpoint of each step's transition, then its settled hold pose.
    uint32_t edge = 0;
    for (int step = 0; step < anim.step_count; step++) {
        frames.push_back({ edge + anim.transition_ms / 2, 1.0f });
        frames.push_back({ edge + anim.transition_ms + anim.holds_ms[step] / 2u,
                           1.0f });
        edge += stepMs(anim, step);
    }
    // Blink phases on a settled pose.
    const uint32_t settled = anim.transition_ms + 100;
    frames.push_back({ settled, 0.5f });
    frames.push_back({ settled, 0.0f });

    const int rows = ((int)frames.size() + COLS - 1) / COLS;
    const int SW = COLS * W + (COLS + 1) * PAD;
    const int SH = rows * H + (rows + 1) * PAD;
    std::vector<unsigned char> sheet(SW * SH, 40);

    for (size_t i = 0; i < frames.size(); i++) {
        std::vector<unsigned char> frame(W * H, 0);
        // Offset past the first loop so poses come from the cyclic path
        // rather than the one-off blend in from neutral.
        const uint32_t t = frames[i].t + loopMs(anim);
        const Pose p = poseAt(anim, t);
        draw_eye(frame.data(), anim, p, -1, frames[i].blink);
        draw_eye(frame.data(), anim, p, +1, frames[i].blink);

        const int ox = PAD + (int)(i % COLS) * (W + PAD);
        const int oy = PAD + (int)(i / COLS) * (H + PAD);
        for (int y = 0; y < H; y++) {
            memcpy(&sheet[(oy + y) * SW + ox], &frame[y * W], W);
        }
        printf("frame %2zu  t=%6u  blink=%.2f\n", i, frames[i].t, frames[i].blink);
    }

    FILE* f = fopen(out, "wb");
    if (!f) { perror("fopen"); return 1; }
    fprintf(f, "P5\n%d %d\n255\n", SW, SH);
    fwrite(sheet.data(), 1, sheet.size(), f);
    fclose(f);
    printf("wrote %s (%dx%d)\n", out, SW, SH);
    return 0;
}
