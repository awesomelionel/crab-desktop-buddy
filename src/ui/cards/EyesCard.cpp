#include "EyesCard.h"

#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_GFX.h>     // GFXcanvas16 for tearing-free WORKING eyes
#include <esp_random.h>
#include <math.h>
#include <string.h>

#include "../../display/Display.h"
#include "../Footer.h"
#include "../PromptBadge.h"

namespace {

const uint16_t kDimGrey   = 0x18C3;
const int      kEyeW      = 30;
const int      kLeftX     = 30;
const int      kRightX    = 180;
const int      kBaseIdleY = 52;
const int      kBaseWaitY = 32;

const int      kLidH      = 10;
const int      kZSpawnX   = 210;
const int      kZSpawnY   = 50;
const int      kZDriftX   = 20;
const int      kZDriftY   = -45;
const uint32_t kZLoopMs   = 3000;

const uint8_t kBlinkH[] = {30, 20, 10, 0, 10, 20, 30};
const int     kBlinkN   = 7;

// Blink timing for the rectangle-eye states (WAITING). IDLE and WORKING
// blink on their own baked schedules instead.
const uint32_t kBlinkIntervalMs    = 4500;
const uint32_t kBlinkStepMs        = 70;

// ---- STATE_IDLE and STATE_WORKING ----
// Geometry, keyframes and timing all live in lib/avatar_face; the only
// device-side choices left here are how the per-eye canvas is placed and the
// WORKING-only dressing below.

// Working jitter: a 1 px wobble on two close-but-coprime periods, so the two
// axes drift in and out of phase instead of tracing a fixed ellipse. Keeps a
// sense of effort through the animation's 2-3 s holds.
const int      kShakeAmpPx    = 1;
const uint32_t kShakeXPeriodMs = 90;
const uint32_t kShakeYPeriodMs = 130;

// Sweat bead: forms at the top of its run, slides down, vanishes, repeats.
// Parked in the right margin, clear of the eyes — the baked working animation
// never reaches past x=209 (208.4 plus a pixel of jitter), and the bead's
// erase band starts at x=216. test_avatar_face pins that gap.
const int      kSweatCx       = 222;
const int      kSweatTopY     = 30;
const int      kSweatDripPx   = 13;
const uint32_t kSweatDripMs   = 1500;   // fall time
const uint32_t kSweatGapMs    = 700;    // dry pause before the next bead
const uint32_t kSweatCycleMs  = kSweatDripMs + kSweatGapMs;
const int      kSweatR        = 4;      // bead radius
const int      kSweatTipH     = 6;      // taper above the bead
const uint16_t kSweatBlue     = 0x555F; // RGB565 light blue
// Erase band covering every bead position, plus the tip and a pixel of slack.
const int      kSweatBandX    = kSweatCx - kSweatR - 2;
const int      kSweatBandY    = kSweatTopY - kSweatTipH - 2;
const int      kSweatBandW    = 2 * (kSweatR + 2);
const int      kSweatBandH    = kSweatTipH + kSweatDripPx + 2 * kSweatR + 5;
// ---- STATE_WAITING redesign (collapsed-prompt eyes) ----
const int      kBaseWaitYNew         = 22;     // top of eye when neutral; was 32, raised so down-glance has clearance
const int      kWaitGlanceDownDy     = 14;     // additional eye-top y when glancing at the badge
const uint32_t kWaitScanPeriodMs     = 3200;   // full forward → down → forward cycle
const uint32_t kWaitScanEaseMs       = 700;    // cubic ease in / out duration — longer = more in-between frames per px
const uint32_t kWaitScanHoldDownMs   = 250;    // dwell at the down position
const uint32_t kWaitBlinkIntervalMs  = 4500;   // identical to IDLE
const uint32_t kWaitBlinkStepMs      = 70;

// Question-mark cluster
const uint32_t kQIntervalMs  = 3200;
const uint32_t kQLifetimeMs  = 3500;
const int      kQRiseY       = 32;
const int      kQDriftX      = 24;
const int      kQClusterN    = 5;
const int      kQBubbleCap   = 8;     // ring capacity (allows brief overlap)
const int      kQAnchorX     = 120;   // face centre
const int      kQAnchorY     = kBaseWaitYNew + 30 / 2 + 8;  // eye-mid + 8 = 45
const int8_t   kQSlotsX[5]   = { -14,  -6,   0,   7,  14 };
const int8_t   kQSlotsY[5]   = {   6,   1,   0,   2,   7 };
const uint16_t kQStaggerMs[5] = {  0,  60, 130, 200, 280 };
const uint8_t  kQSizes[5]    = {  28,  14,  28,  14,  28 };

// Bright orange in RGB565: r=31, g=29, b=0 → (31<<11)|(29<<5)|0 = 0xFBA0
const uint16_t kQColor       = 0xFBA0;

// Badge geometry (sits above the shared 18-px footer)
const int      kBadgeH       = 18;
const int      kBadgeMargin  = 8;
const int      kBadgeX       = kBadgeMargin;
const int      kBadgeW       = 240 - 2 * kBadgeMargin;
const int      kBadgeBottomGap = 4;
// kFooterH = 18 from src/ui/Footer.h
const int      kBadgeY       = 135 - 18 - kBadgeBottomGap - kBadgeH;  // 95

// ---- STATE_DONE celebration overlay ----
// Suppression threshold: WORKING must have lasted at least this long for the
// DONE animation to fire. Avoids flashing a delighted face on every quick
// tool call.
const uint32_t kDoneSuppressMs   = 1500;

// DONE is a one-shot celebration overlaid on IDLE, playing avatar_face::kDone
// once end to end; its length is that animation's loop length.
//
// Sparkles: 4-point crosses at anchors around the eyes. Each anchor draws a
// centre block plus four satellites at (+-arm, 0) and (0, +-arm), each a 3x3
// block. They fade in, hold, then fade out over the burst, quantised to 0..5
// visible blocks that drop outer-first so the centre survives longest.
const uint32_t kDoneSparkleRampMs = 250;    // 0 -> full
const uint32_t kDoneSparkleHoldMs = 750;    // full
const uint32_t kDoneSparkleFadeMs = 1300;   // full -> 0
const int      kDoneSparkleArm   = 11;
struct SparkleAnchor { int16_t cx; int16_t cy; };
// Placed clear of the baked burst, which spans x 46..202, y 29..113: the
// side pair sits in the left and right margins, the third above the eyes.
// test_avatar_face pins the clearance.
const SparkleAnchor kDoneSparkleAnchors[] = {
    { 224,  36 },   // right margin
    {  14,  36 },   // left margin
    { 120,  14 },   // above the eyes, between them
};
const int kDoneSparkleN = sizeof(kDoneSparkleAnchors) / sizeof(SparkleAnchor);

// Cubic ease-out: 1 - (1-k)^3. Same convention as tickWaitGaze.
inline float ease_out_cubic(float k) {
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    const float inv = 1.0f - k;
    return 1.0f - inv * inv * inv;
}


// Fill the strips of rect A that are NOT inside rect B with the given
// colour. Used for tearing-free differential updates: pass (OLD, NEW,
// BLACK) to erase pixels that were in the previous frame but aren't
// now, then (NEW, OLD, WHITE) to paint pixels that should appear this
// frame. Pixels in (A ∩ B) are never touched, so the LCD scanline
// can't catch them mid-transition through black.
inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }

void drawRectsAMinusB(Adafruit_ST7789& tft,
                      int ax, int ay, int aw, int ah,
                      int bx, int by, int bw, int bh,
                      uint16_t color) {
    if (aw <= 0 || ah <= 0) return;
    const int ix1 = imax(ax, bx);
    const int iy1 = imax(ay, by);
    const int ix2 = imin(ax + aw, bx + bw);
    const int iy2 = imin(ay + ah, by + bh);
    if (ix1 >= ix2 || iy1 >= iy2) {
        // No intersection — A is entirely outside B.
        tft.fillRect(ax, ay, aw, ah, color);
        return;
    }
    if (ay < iy1) {
        tft.fillRect(ax, ay, aw, iy1 - ay, color);                  // top strip
    }
    if (ay + ah > iy2) {
        tft.fillRect(ax, iy2, aw, (ay + ah) - iy2, color);          // bottom strip
    }
    if (ax < ix1) {
        tft.fillRect(ax, iy1, ix1 - ax, iy2 - iy1, color);          // left strip
    }
    if (ax + aw > ix2) {
        tft.fillRect(ix2, iy1, (ax + aw) - ix2, iy2 - iy1, color);  // right strip
    }
}

}  // namespace

EyesCard::EyesCard(const AppState& state, PromptUi& prompt)
    : state_(state), prompt_(prompt) {
    resetAnim();
    frame_valid_   = false;
    last_state_    = static_cast<BuddyState>(0xFF);
    last_h_        = 0;
    last_dx_       = 0;
    last_base_y_   = 0;
    last_blink_h_  = -1;
    last_disc_age_ = 0xFFFFFFFFu;
    last_wait_gaze_dy_   = 0;
    last_badge_visible_  = false;
    footer_device_[0]    = 0;
    footer_live_         = false;
    last_footer_device_[0] = 0;
    last_footer_live_      = false;
    last_footer_drawn_     = false;
    face_canvas_           = nullptr;
    wait_q_canvas_         = nullptr;
    last_done_active_  = false;
    last_done_phase_t_ = 0;
    last_sparkle_brightness_n_ = 0;
}

void EyesCard::invalidate() {
    frame_valid_ = false;
    resetAnim();
}

void EyesCard::resetAnim() {
    uint32_t now = millis();
    // Force the next tick to arm timers for whatever BuddyState is live.
    prev_state_              = static_cast<BuddyState>(0xFF);
    disc_anim_start_ms_      = now;
    disc_age_ms_             = 0;
    blink_i_                 = -1;
    next_blink_ms_           = now + kBlinkIntervalMs;
    blink_step_deadline_ms_  = 0;
    scan_epoch_ms_           = now;
    draw_h_                  = 30;
    draw_dx_                 = 0;
    draw_base_y_             = kBaseIdleY;
    draw_blink_h_            = -1;

    face_anim_             = nullptr;
    face_pose_             = avatar_face::poseAt(avatar_face::kIdle, 0);
    face_blink_            = 1.0f;
    face_anim_active_      = false;
    last_face_anim_active_ = false;
    avatar_face::blinkInit(avatar_face::kIdle, face_blink_state_, now);
    face_eye_valid_[0] = false;
    face_eye_valid_[1] = false;
    shake_x_       = 0;
    shake_y_       = 0;
    sweat_y_       = -1;
    last_shake_x_  = 0;
    last_shake_y_  = 0;
    last_sweat_y_  = -1;

    wait_scan_epoch_ms_       = now;
    draw_wait_gaze_dy_        = 0;
    next_q_spawn_ms_          = now;
    for (auto& b : q_bubbles_) b.alive = false;

    last_wait_gaze_dy_        = 0;
    last_badge_visible_       = false;

    working_entered_ms_        = now;
    done_active_               = false;
    done_start_ms_             = 0;
    last_done_active_          = false;
    last_done_phase_t_         = 0;
    last_sparkle_brightness_n_ = 0;
    // face_canvas_ / wait_q_canvas_ are NOT reset — they are owned for the
    // card's lifetime.
}

void EyesCard::setFooter(const char* name, bool live) {
    if (name) {
        strncpy(footer_device_, name, sizeof(footer_device_) - 1);
        footer_device_[sizeof(footer_device_) - 1] = 0;
    } else {
        footer_device_[0] = 0;
    }
    footer_live_ = live;
}

bool EyesCard::handleButton(ButtonEvent ev, uint32_t now_ms) {
    // While a permission prompt is COLLAPSED to the badge, EyesCard is the
    // active carousel card. Forward button events to the prompt UI so a
    // CENTER press re-EXPANDs and UP/DOWN do not silently navigate the
    // carousel. EXPANDED never reaches here because PromptCard takes over
    // as the overlay; the check is mode != HIDDEN for symmetry.
    if (prompt_.mode != PROMPT_UI_HIDDEN) {
        prompt_ui_button(&prompt_, ev, now_ms);
        return true;
    }
    return false;
}

// Start one of the baked avatar animations. The entry frame full-clears the
// panel, so nothing is left to erase around either eye.
void EyesCard::armAvatar(const avatar_face::Anim& anim, uint32_t now) {
    face_anim_             = &anim;
    scan_epoch_ms_         = now;
    face_pose_             = avatar_face::poseAt(anim, 0);
    face_blink_            = 1.0f;
    face_anim_active_      = true;
    last_face_anim_active_ = true;
    avatar_face::blinkInit(anim, face_blink_state_, now);
    face_eye_valid_[0]     = false;
    face_eye_valid_[1]     = false;
}

void EyesCard::armState(BuddyState state, uint32_t now) {
    prev_state_ = state;
    switch (state) {
        case STATE_DISCONNECTED:
            armAvatar(avatar_face::kSleep, now);
            disc_anim_start_ms_ = now;
            disc_age_ms_        = 0;
            for (auto& b : q_bubbles_) b.alive = false;
            draw_wait_gaze_dy_   = 0;
            break;
        case STATE_IDLE:
            armAvatar(avatar_face::kIdle, now);
            blink_i_                = -1;
            draw_dx_                = 0;
            draw_base_y_            = kBaseIdleY;
            for (auto& b : q_bubbles_) b.alive = false;
            draw_wait_gaze_dy_      = 0;
            break;
        case STATE_WORKING:
            working_entered_ms_    = now;
            armAvatar(avatar_face::kWorking, now);
            draw_blink_h_          = -1;
            for (auto& b : q_bubbles_) b.alive = false;
            draw_wait_gaze_dy_     = 0;
            break;
        case STATE_WAITING:
            blink_i_                 = -1;
            next_blink_ms_           = now + kWaitBlinkIntervalMs;
            blink_step_deadline_ms_  = 0;
            wait_scan_epoch_ms_      = now;
            draw_wait_gaze_dy_       = 0;
            next_q_spawn_ms_         = now + 600;  // first cluster ~0.6 s after entering state
            for (auto& b : q_bubbles_) b.alive = false;
            break;
    }
}

void EyesCard::tickBlink(uint32_t now) {
    if (blink_i_ >= 0) {
        if (now >= blink_step_deadline_ms_) {
            blink_i_++;
            if (blink_i_ >= kBlinkN) {
                blink_i_       = -1;
                next_blink_ms_ = now + kBlinkIntervalMs;
            } else {
                blink_step_deadline_ms_ = now + kBlinkStepMs;
            }
        }
    } else if (now >= next_blink_ms_) {
        blink_i_                 = 0;
        blink_step_deadline_ms_  = now + kBlinkStepMs;
    }
}

void EyesCard::tickWaitGaze(uint32_t now) {
    const uint32_t t = (now - wait_scan_epoch_ms_) % kWaitScanPeriodMs;
    const uint32_t e1 = kWaitScanEaseMs;                            // 250
    const uint32_t e2 = e1 + kWaitScanHoldDownMs;                   // 650
    const uint32_t e3 = e2 + kWaitScanEaseMs;                       // 900

    int dy;
    if (t < e1) {
        // ease-down (cubic ease-out): k = t/e1, dy = D * (1 - (1-k)^3)
        float k = (float)t / (float)e1;
        float eased = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
        dy = (int)((float)kWaitGlanceDownDy * eased);
    } else if (t < e2) {
        dy = kWaitGlanceDownDy;
    } else if (t < e3) {
        float k = (float)(t - e2) / (float)kWaitScanEaseMs;
        float eased = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
        dy = (int)((float)kWaitGlanceDownDy * (1.0f - eased));
    } else {
        dy = 0;
    }
    draw_wait_gaze_dy_ = (int8_t)dy;
}

void EyesCard::tickQuestionMarks(uint32_t now) {
    // Prune dead bubbles. Use a signed cast so staggered bubbles whose
    // born_ms is still in the future (b.born_ms > now) read as negative
    // age and survive — without the cast, uint32_t underflow makes their
    // age look ~4.29 billion, far older than kQLifetimeMs.
    for (auto& b : q_bubbles_) {
        if (!b.alive) continue;
        const int32_t age = (int32_t)(now - b.born_ms);
        if (age >= 0 && (uint32_t)age > kQLifetimeMs) {
            b.alive = false;
        }
    }

    // Spawn new cluster if due
    if ((int32_t)(now - next_q_spawn_ms_) >= 0) {
        for (int i = 0; i < kQClusterN; ++i) {
            // Find a free slot
            for (auto& b : q_bubbles_) {
                if (!b.alive) {
                    b.alive          = true;
                    b.born_ms        = now + kQStaggerMs[i];
                    b.slot_x_offset  = kQSlotsX[i];
                    b.slot_y_offset  = kQSlotsY[i];
                    b.size           = kQSizes[i];
                    break;
                }
            }
        }
        next_q_spawn_ms_ = now + kQIntervalMs;
    }
}

void EyesCard::tickDone(uint32_t now_ms) {
    if (!done_active_) return;
    // Any state other than IDLE cancels DONE immediately. The new state's
    // armState will run its full_clear paint and wipe the celebration pixels.
    if (state_.buddyState() != STATE_IDLE) {
        done_active_ = false;
        return;
    }
    // Natural exit once the celebration has played through once.
    if ((now_ms - done_start_ms_) >= avatar_face::loopMs(avatar_face::kDone)) {
        done_active_ = false;
    }
}

void EyesCard::drawDoneFrame(Adafruit_ST7789& tft, uint32_t t, bool full_clear) {
    // Same canvas-per-eye path as every other avatar state; only the sparkles
    // below are specific to the celebration.
    if (!face_canvas_) {
        face_canvas_ = new GFXcanvas16(avatar_face::kMaxCanvasW,
                                       avatar_face::kMaxCanvasH);
    }
    if (!face_canvas_) return;  // OOM: silently skip the frame

    drawAvatarEye(tft, 0, -1, full_clear);
    drawAvatarEye(tft, 1, +1, full_clear);

    // Sparkles last, for the same reason as the sweat bead and sleep Zs: an
    // eye canvas can reach into these bands and would paint them out.
    // Each anchor erases its own bbox, then draws n blocks outer-first so the
    // visual centroid stays put as the brightness drops.
    const uint8_t n = doneSparkleCount(t);
    for (int i = 0; i < kDoneSparkleN; i++) {
        const int cx = kDoneSparkleAnchors[i].cx;
        const int cy = kDoneSparkleAnchors[i].cy;
        tft.fillRect(cx - kDoneSparkleArm - 2, cy - kDoneSparkleArm - 2,
                     2 * kDoneSparkleArm + 5, 2 * kDoneSparkleArm + 5,
                     ST77XX_BLACK);
        if (n >= 5) tft.fillRect(cx - 1, cy + kDoneSparkleArm - 1, 3, 3, ST77XX_WHITE);
        if (n >= 4) tft.fillRect(cx - 1, cy - kDoneSparkleArm - 1, 3, 3, ST77XX_WHITE);
        if (n >= 3) tft.fillRect(cx - kDoneSparkleArm - 1, cy - 1, 3, 3, ST77XX_WHITE);
        if (n >= 2) tft.fillRect(cx + kDoneSparkleArm - 1, cy - 1, 3, 3, ST77XX_WHITE);
        if (n >= 1) tft.fillRect(cx - 1, cy - 1, 3, 3, ST77XX_WHITE);
    }
}

uint8_t EyesCard::doneSparkleCount(uint32_t t) const {
    // Ramp in, hold, fade out across the one pass. Quantised to 0..5 blocks:
    // 5 means brightness in [0.9, 1.0], 4 in [0.7, 0.9), and so on.
    float b;
    if (t < kDoneSparkleRampMs) {
        b = (float)t / (float)kDoneSparkleRampMs;
    } else if (t < kDoneSparkleRampMs + kDoneSparkleHoldMs) {
        b = 1.0f;
    } else {
        const uint32_t into = t - kDoneSparkleRampMs - kDoneSparkleHoldMs;
        b = (into >= kDoneSparkleFadeMs)
            ? 0.0f
            : 1.0f - ((float)into / (float)kDoneSparkleFadeMs);
    }
    if (b <= 0.0f) return 0;
    if (b >= 1.0f) return 5;
    const int n = (int)lroundf(b * 5.0f);
    return (uint8_t)(n < 0 ? 0 : (n > 5 ? 5 : n));
}

void EyesCard::tick(uint32_t now_ms) {
    BuddyState state = state_.buddyState();

    // ---- DONE celebration: detect WORKING -> IDLE edge ----
    // prev_state_ still holds the OLD state at this point — armState (below)
    // overwrites it. Conditions: previous was WORKING, current is IDLE,
    // WORKING phase met the suppression threshold, and we're not already
    // celebrating.
    if (state == STATE_IDLE && prev_state_ == STATE_WORKING && !done_active_
        && (now_ms - working_entered_ms_) >= kDoneSuppressMs) {
        done_active_   = true;
        done_start_ms_ = now_ms;
    }

    if (state != prev_state_) armState(state, now_ms);

    if (done_active_) tickDone(now_ms);

    // The celebration borrows the avatar machinery: arm it once DONE is live
    // and hand back to the live state's own animation when it ends. Its epoch
    // is the celebration start, so t runs 0..loop across the burst.
    if (done_active_) {
        if (face_anim_ != &avatar_face::kDone) {
            armAvatar(avatar_face::kDone, done_start_ms_);
        }
    } else if (face_anim_ == &avatar_face::kDone) {
        armAvatar(state == STATE_WORKING ? avatar_face::kWorking
                : state == STATE_DISCONNECTED ? avatar_face::kSleep
                                              : avatar_face::kIdle, now_ms);
    }

    switch (state) {
        case STATE_DISCONNECTED:
            disc_age_ms_ = now_ms - disc_anim_start_ms_;
            // falls through: the sleeping face animates like the others
        case STATE_IDLE:
        case STATE_WORKING: {
            const avatar_face::Anim& anim = *face_anim_;
            const uint32_t t = now_ms - scan_epoch_ms_;
            // A DONE burst runs its own dressing, not IDLE's.

            // Blink scheduling owns the randomness; the geometry module stays
            // platform-free so it can be unit tested natively.
            avatar_face::blinkTick(anim, face_blink_state_, now_ms,
                                   (float)esp_random() / 4294967296.0f);

            face_pose_  = avatar_face::poseAt(anim, t);
            face_blink_ = avatar_face::blinkFactor(anim, face_blink_state_, now_ms);

            // The pose only moves during a step's transition; holds are
            // pixel-identical, so outside a transition or blink there is
            // nothing to redraw and isDirty() can report clean.
            face_anim_active_ = avatar_face::inTransition(anim, t) ||
                                avatar_face::blinkActive(anim, face_blink_state_, now_ms);

            if (state == STATE_WORKING) {
                const float twoPi = 2.0f * 3.14159265f;
                shake_x_ = (int8_t)lroundf(kShakeAmpPx *
                    sinf(twoPi * (float)(now_ms % kShakeXPeriodMs) / (float)kShakeXPeriodMs));
                shake_y_ = (int8_t)lroundf(kShakeAmpPx *
                    sinf(twoPi * (float)(now_ms % kShakeYPeriodMs) / (float)kShakeYPeriodMs));

                const uint32_t sweat_t = t % kSweatCycleMs;
                sweat_y_ = (sweat_t < kSweatDripMs)
                    ? (int16_t)(kSweatTopY + (int)((int32_t)kSweatDripPx *
                                                   (int32_t)sweat_t / (int32_t)kSweatDripMs))
                    : (int16_t)-1;
            } else {
                shake_x_ = 0;
                shake_y_ = 0;
                sweat_y_ = -1;
            }

            // The generic dirty-tracking fields below are unused by this
            // state (see isDirty), but keep them parked at a fixed value so a
            // later state change can't inherit stale motion.
            draw_dx_      = 0;
            draw_h_       = 30;
            draw_blink_h_ = -1;
            draw_base_y_  = kBaseIdleY;
            break;
        }

        case STATE_WAITING:
            tickBlink(now_ms);
            // Only run the new gaze-scan + question marks while a prompt is
            // actually live (EXPANDED or COLLAPSED). If WAITING was entered
            // without a prompt (defensive — currently impossible), fall
            // back to plain open eyes.
            if (prompt_.mode != PROMPT_UI_HIDDEN) {
                tickWaitGaze(now_ms);
                tickQuestionMarks(now_ms);
                draw_base_y_ = (int16_t)(kBaseWaitYNew + draw_wait_gaze_dy_);
            } else {
                draw_wait_gaze_dy_ = 0;
                draw_base_y_ = kBaseWaitYNew;
            }
            draw_dx_     = 0;
            draw_h_      = (blink_i_ >= 0) ? kBlinkH[blink_i_] : 30;
            break;
    }
}

bool EyesCard::isDirty() const {
    if (!frame_valid_) return true;
    if (last_state_      != state_.buddyState()) return true;

    // ---- DONE: redraw on each 16 ms tick so the eye morph and sparkle
    //      decay stay smooth. The bucketed phase clock advances every tick,
    //      so isDirty() effectively runs at the frame rate during DONE —
    //      same cadence as IDLE/WAITING animations. The bypass at the
    //      bottom of the block is what keeps the IDLE-era checks from
    //      firing (e.g. last_h_ != draw_h_) on celebration frames.
    if (last_done_active_ != done_active_) return true;
    if (done_active_) {
        const uint32_t t      = millis() - done_start_ms_;
        const uint32_t bucket = (t / 16) * 16;
        if (last_done_phase_t_         != bucket)              return true;
        if (last_sparkle_brightness_n_ != doneSparkleCount(t)) return true;
        return false;  // DONE bypasses the IDLE-era checks below
    }

    // ---- IDLE / WORKING: the avatar animation's own motion decides. One
    //      extra frame after motion stops commits the settled pose, then the
    //      state goes quiet for the rest of the hold.
    if (face_anim_) {
        // The WORKING jitter and sweat bead move on their own clock, so they
        // keep the state dirty right through the animation's holds.
        if (last_shake_x_ != shake_x_ || last_shake_y_ != shake_y_) return true;
        if (last_sweat_y_ != sweat_y_)                              return true;
        // Same for the sleep Zs, which drift a pixel every ~66 ms. Bucketing
        // at 32 ms cannot miss a step, and keeps the sleeping face from being
        // re-blitted on every one of its otherwise-idle frames.
        if (state_.buddyState() == STATE_DISCONNECTED &&
            (last_disc_age_ / 32) != (disc_age_ms_ / 32)) return true;
        return face_anim_active_ || last_face_anim_active_;
    }

    if (last_h_          != draw_h_)            return true;
    if (last_dx_         != draw_dx_)           return true;
    if (last_base_y_     != draw_base_y_)       return true;
    if (last_blink_h_    != draw_blink_h_)      return true;
    if (last_disc_age_   != disc_age_ms_)       return true;
    if (last_wait_gaze_dy_ != draw_wait_gaze_dy_) return true;
    const bool badge_now = (state_.buddyState() == STATE_WAITING &&
                            prompt_.mode == PROMPT_UI_COLLAPSED);
    if (last_badge_visible_ != badge_now) return true;
    // While bubbles live, force redraw so the rising/drifting ?s animate.
    for (const auto& b : q_bubbles_) if (b.alive) return true;
    return false;
}

void EyesCard::render(Display& display) {
    Adafruit_ST7789& tft = display.tft();
    BuddyState bs = state_.buddyState();

    // ---- DONE celebration overlay ----
    if (done_active_) {
        const uint32_t t = millis() - done_start_ms_;
        // First frame of DONE: wipe everything. Targeted band-erases were
        // not enough — the prior WORKING state leaves typing dots above the
        // eye band (y=22) that no eye-band or sparkle-bbox erase covers,
        // and they'd ghost into the celebration. A single fillScreen at
        // this state-transition moment matches the pattern every other
        // armState case uses (CLAUDE.md allows fillScreen at state
        // transitions; only continuous-animation frames must avoid it).
        const bool done_entry = !last_done_active_;
        if (done_entry) {
            tft.fillScreen(ST77XX_BLACK);
        }
        drawDoneFrame(tft, t, done_entry);
        last_done_active_          = true;
        last_done_phase_t_         = (t / 16) * 16;       // 16 ms buckets
        last_sparkle_brightness_n_ = doneSparkleCount(t);
        frame_valid_ = true;
        return;
    }

    // ---- DONE just ended: force a full clear so leftover celebration pixels
    //      are wiped and IDLE re-paints from a known-clean field.
    bool stateJustChanged = !frame_valid_ || (last_state_ != bs)
                          || last_done_active_;
    bool full_clear = stateJustChanged ||
                      (bs != STATE_DISCONNECTED && bs != STATE_WORKING &&
                       bs != STATE_WAITING && bs != STATE_IDLE);
    drawFrame(tft, bs, full_clear);

    last_state_    = bs;
    last_h_        = draw_h_;
    last_dx_       = draw_dx_;
    last_base_y_   = draw_base_y_;
    last_blink_h_  = draw_blink_h_;
    last_face_anim_active_ = face_anim_active_;
    last_shake_x_  = shake_x_;
    last_shake_y_  = shake_y_;
    last_sweat_y_  = sweat_y_;
    last_wait_gaze_dy_  = draw_wait_gaze_dy_;
    last_badge_visible_ = (bs == STATE_WAITING && prompt_.mode == PROMPT_UI_COLLAPSED);
    last_disc_age_ = disc_age_ms_;
    last_done_active_   = false;
    frame_valid_   = true;
}

void EyesCard::drawFrame(Adafruit_ST7789& tft, BuddyState state, bool full_clear) {
    if (state == STATE_DISCONNECTED || state == STATE_IDLE ||
        state == STATE_WORKING) {
        // render() can land before the first tick() after invalidate(), when
        // armState has not yet picked the animation for the live state.
        if (!face_anim_) {
            armAvatar(state == STATE_WORKING ? avatar_face::kWorking
                                             : avatar_face::kIdle, millis());
        }

        // Tearing-free render: each eye is composed off-screen in a
        // GFXcanvas16 sized to the worst-case single-eye bounding box, then
        // pushed to the LCD as one continuous SPI burst via drawRGBBitmap.
        // Composing in RAM means the LCD pixels go straight from OLD to NEW
        // with no black intermediate for the scanline to catch.
        //
        // Canvas allocated lazily so non-WORKING sessions pay nothing.
        if (!face_canvas_) {
            face_canvas_ = new GFXcanvas16(avatar_face::kMaxCanvasW,
                                           avatar_face::kMaxCanvasH);
        }
        if (!face_canvas_) return;  // OOM: silently skip the frame

        if (full_clear) {
            tft.fillScreen(ST77XX_BLACK);
        }

        // Slot 0 is the left eye (side -1), slot 1 the right (side +1).
        drawAvatarEye(tft, 0, -1, full_clear);
        drawAvatarEye(tft, 1, +1, full_clear);
        // Both after the eyes: a far-out eye's canvas reaches into these
        // bands and would otherwise paint them out with its black margin.
        if (state == STATE_WORKING)      drawSweatDrop(tft);
        if (state == STATE_DISCONNECTED) drawSleepZs(tft);
        return;
    }

    if (state == STATE_WAITING) {
        const bool prompt_live = (prompt_.mode != PROMPT_UI_HIDDEN);
        if (!prompt_live) {
            // Fallback: legacy plain WAITING (no badge, no ?s). Cheap.
            tft.fillScreen(ST77XX_BLACK);
            int h = draw_h_;
            if (h > 0) {
                int16_t top = (int16_t)(kBaseWaitYNew + 15 - h / 2);
                tft.fillRect(kLeftX,  top, kEyeW, h, ST77XX_WHITE);
                tft.fillRect(kRightX, top, kEyeW, h, ST77XX_WHITE);
            }
            return;
        }

        // Per-region dirty checks. Each region only erases + redraws
        // when its inputs actually changed, so during gaze-hold phases
        // (75 % of every 2 s scan cycle) the eye band is left alone and
        // the user sees solid white eyes instead of a strobe.
        //
        //   Eye band     : redraws when gaze_dy or eye height changed.
        //   ?-cluster    : redraws every frame any bubble is alive (the
        //                  bubbles themselves animate every tick).
        //   Badge        : already gated by last_badge_visible_ below.
        //
        // Layout used by the erase rects (no overlap with the badge or
        // each other; 1-px gap between right eye and ?-band):
        //
        //   Left eye     : x=29..60,   y=21..67   (32 × 47 = 1504 px)
        //   ?-cluster    : x=100..177, y=0..68    (78 × 69 = 5382 px)
        //   Right eye    : x=179..210, y=21..67   (32 × 47 = 1504 px)
        //
        // ?-band bounds derived from worst-case visible glyph extents:
        // cursor x ∈ [106, 158], glyph width ≤ ts*6 = 24, so right edge
        // reaches 182. The 158→182 tail only fires at t≈1 when alpha is
        // ~0 (invisible) so we shrink the erase to x≤177 to keep clear
        // of the right eye and let the invisible tail composite on
        // black. Cursor y ∈ [13, 52] minus ts*4 = 16 → top −3, bottom
        // 67; padded a couple of pixels each way.
        const bool eyes_dirty = full_clear ||
                                (last_wait_gaze_dy_ != draw_wait_gaze_dy_) ||
                                (last_h_            != draw_h_);
        bool q_dirty = full_clear;
        if (!q_dirty) {
            for (const auto& b : q_bubbles_) {
                if (b.alive) { q_dirty = true; break; }
            }
        }

        if (full_clear) {
            tft.fillScreen(ST77XX_BLACK);
        }

        // 1) Eyes — differential update against last_*. Pixels in the
        // (OLD ∩ NEW) overlap are never written, so the LCD scanline
        // can't catch them mid-transition through black. Same approach
        // as STATE_IDLE; kept simple because WAITING eyes are axis-
        // aligned rectangles with draw_dx_ pinned to 0.
        if (eyes_dirty) {
            const int new_top = draw_base_y_ + 15 - draw_h_ / 2;
            const int old_top = last_base_y_ + 15 - last_h_  / 2;
            if (full_clear) {
                if (draw_h_ > 0) {
                    tft.fillRect(kLeftX,  new_top, kEyeW, draw_h_, ST77XX_WHITE);
                    tft.fillRect(kRightX, new_top, kEyeW, draw_h_, ST77XX_WHITE);
                }
            } else {
                drawRectsAMinusB(tft, kLeftX,  old_top, kEyeW, last_h_,
                                      kLeftX,  new_top, kEyeW, draw_h_, ST77XX_BLACK);
                drawRectsAMinusB(tft, kLeftX,  new_top, kEyeW, draw_h_,
                                      kLeftX,  old_top, kEyeW, last_h_, ST77XX_WHITE);
                drawRectsAMinusB(tft, kRightX, old_top, kEyeW, last_h_,
                                      kRightX, new_top, kEyeW, draw_h_, ST77XX_BLACK);
                drawRectsAMinusB(tft, kRightX, new_top, kEyeW, draw_h_,
                                      kRightX, old_top, kEyeW, last_h_, ST77XX_WHITE);
            }
        }

        // 2) Question marks — back-buffer through GFXcanvas16 (78 × 69
        // = 10.5 KB) and push as one drawRGBBitmap SPI burst. The
        // previous direct-to-TFT path went through Adafruit_GFX text
        // rendering, ~2 ms per size-4 glyph × 5 glyphs = ~10 ms per
        // frame, with a separate bbox erase that briefly blacked the
        // region — both expensive and tearing-prone. Composing in RAM
        // first lets the LCD pixels go OLD → NEW directly with a
        // single continuous SPI sweep.
        if (q_dirty) {
            if (!wait_q_canvas_) {
                wait_q_canvas_ = new GFXcanvas16(78, 69);
            }
            wait_q_canvas_->fillScreen(ST77XX_BLACK);
            wait_q_canvas_->setTextColor(kQColor, ST77XX_BLACK);
            const uint32_t now = millis();
            // Canvas origin on screen is (100, 0); convert each glyph
            // position to canvas-local coords by subtracting 100/0.
            const int kCanvasOriginX = 100;
            const int kCanvasOriginY = 0;
            for (const auto& b : q_bubbles_) {
                if (!b.alive) continue;
                const uint32_t age = now - b.born_ms;
                if ((int32_t)age < 0) continue;
                if (age > kQLifetimeMs) continue;
                const float t    = (float)age / (float)kQLifetimeMs;
                const float ease = 1.0f - (1.0f - t) * (1.0f - t);
                const int   gy   = kQAnchorY - (int)((float)kQRiseY * ease) + b.slot_y_offset;
                const int   gx   = kQAnchorX + b.slot_x_offset + (int)((float)kQDriftX * ease);
                const uint8_t ts = (b.size >= 24) ? 4 : 2;
                wait_q_canvas_->setTextSize(ts);
                wait_q_canvas_->setCursor(gx - kCanvasOriginX,
                                          gy - kCanvasOriginY - ts * 4);
                wait_q_canvas_->print('?');
            }
            tft.drawRGBBitmap(kCanvasOriginX, kCanvasOriginY,
                              wait_q_canvas_->getBuffer(), 78, 69);
        }

        // 3) Badge (only if COLLAPSED — when EXPANDED, the overlay covers
        // the whole screen so we wouldn't be drawing this branch anyway,
        // but the check makes the intent explicit). Geometry + glyph
        // layout live in ui::drawPromptBadge so StatusCard renders the
        // exact same widget on the main page.
        if (prompt_.mode == PROMPT_UI_COLLAPSED) {
            const bool badge_dirty = full_clear || !last_badge_visible_;
            if (badge_dirty) {
                tft.fillRect(0, ui::kPromptBadgeEraseY, 240,
                             ui::kPromptBadgeEraseH, ST77XX_BLACK);
                ui::drawPromptBadge(tft, prompt_.tool);
            }
        }

        // 4) Footer — only redraw when its content actually changed
        // (live flag flipped, device name changed) or on first
        // appearance after a full_clear / state entry. Previously this
        // unconditionally repainted every frame in COLLAPSED mode,
        // strobing the LIVE pill and device label.
        if (prompt_.mode == PROMPT_UI_COLLAPSED) {
            const bool footer_dirty = full_clear ||
                                      !last_footer_drawn_ ||
                                      (last_footer_live_ != footer_live_) ||
                                      strncmp(last_footer_device_, footer_device_,
                                              sizeof(last_footer_device_)) != 0;
            if (footer_dirty) {
                tft.fillRect(0, ui::kFooterTopY, 240, ui::kFooterH, ST77XX_BLACK);
                ui::drawFooter(tft, footer_device_, footer_live_);
                last_footer_live_ = footer_live_;
                strncpy(last_footer_device_, footer_device_,
                        sizeof(last_footer_device_) - 1);
                last_footer_device_[sizeof(last_footer_device_) - 1] = 0;
                last_footer_drawn_ = true;
            }
        } else {
            // EXPANDED is hidden behind the overlay; HIDDEN doesn't
            // own this footer. Either way, our copy is stale.
            last_footer_drawn_ = false;
        }
        return;
    }

    // Catch-all fallback for any future state that doesn't have its own
    // branch above. Full-clear is fine here because we won't be running
    // a continuous animation.
    tft.fillScreen(ST77XX_BLACK);
    int h = draw_h_;
    if (h <= 0) return;
    int16_t top = (int16_t)(draw_base_y_ + 15 - h / 2);
    tft.fillRect(kLeftX  + draw_dx_, top, kEyeW, h, ST77XX_WHITE);
    tft.fillRect(kRightX + draw_dx_, top, kEyeW, h, ST77XX_WHITE);
}

// Render one WORKING eye. The eye roams most of the panel, so the canvas
// follows it: each frame it is centred on the eye's current bounding box, the
// strips the previous frame's canvas occupied but this one doesn't are erased,
// and the canvas is then blitted over the rest. Only the vacated strips ever
// go through black, which keeps the animation flicker-free without the
// per-frame fillScreen that CLAUDE.md rules out.
// Sleep Zs: three glyphs on one 3 s loop, each offset a second apart, drifting
// up and to the right as they grow and then dim out. Unchanged from the
// pre-avatar DISCONNECTED screen — only the eyes underneath them changed.
void EyesCard::drawSleepZs(Adafruit_ST7789& tft) {
    // Erase only the Z zone. It sits right of x=210 and the sleeping face
    // never reaches past x=187, so this can never bite into an eye.
    tft.fillRect(kZSpawnX, kZSpawnY + kZDriftY - 2,
                 240 - kZSpawnX, -kZDriftY + 3 * 8 + 4, ST77XX_BLACK);

    const uint32_t base = disc_age_ms_ % kZLoopMs;
    const uint32_t offsets[3] = {0, 1000, 2000};
    for (int i = 0; i < 3; i++) {
        uint32_t age = (base + offsets[i]) % kZLoopMs;  // 0..2999

        int x = kZSpawnX + (int)((int32_t)kZDriftX * (int32_t)age / (int32_t)kZLoopMs);
        int y = kZSpawnY + (int)((int32_t)kZDriftY * (int32_t)age / (int32_t)kZLoopMs);

        uint8_t size;
        if      (age < 1000) size = 1;
        else if (age < 2000) size = 2;
        else                 size = 3;

        uint16_t col;
        if      (age < 1800) col = ST77XX_WHITE;
        else if (age < 2550) col = kDimGrey;
        else                 continue;  // last ~450 ms: don't draw

        tft.setCursor(x, y);
        tft.setTextSize(size);
        tft.setTextColor(col);
        tft.print('Z');
    }
}

// Anime-style bead: a rounded drop with a taper on top, sliding down the
// right margin. Erase-then-draw over a 12x28 band is ~340 px, well inside the
// per-frame budget CLAUDE.md sets for continuous animations.
void EyesCard::drawSweatDrop(Adafruit_ST7789& tft) {
    tft.fillRect(kSweatBandX, kSweatBandY, kSweatBandW, kSweatBandH, ST77XX_BLACK);
    if (sweat_y_ < 0) return;

    const int cy = sweat_y_;
    tft.fillTriangle(kSweatCx, cy - kSweatTipH - kSweatR + 1,
                     kSweatCx - kSweatR, cy,
                     kSweatCx + kSweatR, cy, kSweatBlue);
    tft.fillCircle(kSweatCx, cy, kSweatR, kSweatBlue);
}

void EyesCard::drawAvatarEye(Adafruit_ST7789& tft, int slot, int side,
                               bool full_clear) {
    // One canvas size serves both animations; see face_canvas_.
    const int cw = avatar_face::kMaxCanvasW;
    const int ch = avatar_face::kMaxCanvasH;

    const int n = avatar_face::eyeOutline(*face_anim_, face_pose_, side,
                                          face_blink_, face_pts_,
                                          avatar_face::kMaxOutlinePoints);
    // Shift the outline, not the canvas: moving the canvas alone would slide
    // the spans the other way inside it and leave the eye pixel-static.
    for (int i = 0; i < n; i++) {
        face_pts_[i].x += (float)shake_x_;
        face_pts_[i].y += (float)shake_y_;
    }
    if (n == 0) {
        // The eye has rotated behind the body. Wipe whatever it left behind.
        // The bundled keyframes never reach this, but the upstream animation
        // format allows it and the erase is cheap insurance.
        if (!full_clear && face_eye_valid_[slot]) {
            tft.fillRect(face_eye_x_[slot], face_eye_y_[slot], cw, ch, ST77XX_BLACK);
        }
        face_eye_valid_[slot] = false;
        return;
    }

    float x0 = face_pts_[0].x, x1 = face_pts_[0].x;
    float y0 = face_pts_[0].y, y1 = face_pts_[0].y;
    for (int i = 1; i < n; i++) {
        if (face_pts_[i].x < x0) x0 = face_pts_[i].x;
        if (face_pts_[i].x > x1) x1 = face_pts_[i].x;
        if (face_pts_[i].y < y0) y0 = face_pts_[i].y;
        if (face_pts_[i].y > y1) y1 = face_pts_[i].y;
    }

    // Centre the canvas on the eye, then pull it back inside the panel. The
    // canvas is at least as large as any single eye in either animation (both
    // asserted by test_avatar_face), so clamping can only ever shift it toward
    // the screen interior while still covering the eye.
    int ox = (int)lroundf((x0 + x1) * 0.5f) - cw / 2;
    int oy = (int)lroundf((y0 + y1) * 0.5f) - ch / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox > 240 - cw) ox = 240 - cw;
    if (oy > 135 - ch) oy = 135 - ch;

    avatar_face::Span spans[avatar_face::kMaxCanvasH];
    avatar_face::outlineSpans(face_pts_, n, ox, oy, spans, ch, cw);
    face_canvas_->fillScreen(ST77XX_BLACK);
    for (int row = 0; row < ch; row++) {
        if (spans[row].x1 < spans[row].x0) continue;
        face_canvas_->drawFastHLine(spans[row].x0, row,
                                    spans[row].x1 - spans[row].x0 + 1,
                                    ST77XX_WHITE);
    }

    if (!full_clear && face_eye_valid_[slot]) {
        drawRectsAMinusB(tft, face_eye_x_[slot], face_eye_y_[slot], cw, ch,
                              ox, oy, cw, ch, ST77XX_BLACK);
    }
    tft.drawRGBBitmap(ox, oy, face_canvas_->getBuffer(), cw, ch);

    face_eye_x_[slot]     = (int16_t)ox;
    face_eye_y_[slot]     = (int16_t)oy;
    face_eye_valid_[slot] = true;
}
