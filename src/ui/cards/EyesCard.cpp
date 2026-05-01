#include "EyesCard.h"

#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <esp_random.h>
#include <math.h>

#include "../../display/Display.h"

namespace {

const uint16_t kDimGrey   = 0x18C3;
const uint16_t kSweatBlue = 0x65FF;  // R≈99 G≈190 B≈255
const int      kEyeW      = 30;
const int      kLeftX     = 30;
const int      kRightX    = 180;
const int      kBaseIdleY = 52;
const int      kBaseWaitY = 32;
const int      kActionH   = 14;   // squint half-height for "> <"
const int      kSweatCX   = 195;  // x-centre of sweat drop (above right eye)
const int      kSweatTipY = 25;   // resting y of teardrop tip
const uint32_t kDripMs    = 2000; // drip cycle length in ms
const int      kDripSteps = 8;    // how many px it drops per cycle

const int      kLidH      = 10;
const int      kZSpawnX   = 210;
const int      kZSpawnY   = 50;
const int      kZDriftX   = 20;
const int      kZDriftY   = -45;
const uint32_t kZLoopMs   = 3000;

const uint8_t kBlinkH[] = {30, 20, 10, 0, 10, 20, 30};
const int     kBlinkN   = 7;

// Idle-face timing knobs. Curious-and-alert feel: blink ~every 4.5 s with
// snappier per-step animation, and dart eyes diagonally up-and-side every
// 2.5–4.5 s with a short ~0.6 s hold before returning to centre.
const uint32_t kBlinkIntervalMs = 4500;
const uint32_t kBlinkStepMs     = 70;
const uint32_t kGlanceMinMs     = 2500;
const uint32_t kGlanceJitterMs  = 2000;
const uint32_t kGlanceHoldMs    = 600;
const int      kGlanceDy        = -10;  // negative = up; applied while glancing

// ---- STATE_WORKING (focused-thinking redesign) ----
// All times in ms; angles in radians.
const uint32_t kWorkBreatheMs       = 1100;
const uint32_t kWorkDriftMs         = 1400;
const uint32_t kWorkDotsMs          = 1500;
const uint32_t kWorkBlinkIntervalMs = 7000;
const uint32_t kWorkBlinkStepMs     = 40;     // 7 steps × 40 ms = 280 ms total
const int      kWorkBaseH           = 9;
const int      kWorkBreatheAmp      = 2;      // h ranges 7..11
const int      kWorkDriftAmp        = 8;      // dx ranges -8..+8
const float    kWorkRotRad          = 0.2618f; // 15° in radians
const uint8_t  kWorkBlinkH[]        = {9, 6, 3, 0, 3, 6, 9};
const int      kWorkBlinkN          = 7;

const int      kWorkLeftCx          = 45;     // matches kLeftX  + kEyeW/2
const int      kWorkRightCx         = 195;    // matches kRightX + kEyeW/2
const int      kWorkEyeCy           = 67;     // matches kBaseIdleY + 15
const int      kDotsX[3]            = {184, 194, 204};
const int      kDotsY               = 22;
const int      kDotR                = 2;      // 5 px diameter via fillCircle r=2

// Rotation basis — precomputed; width is fixed at kEyeW.
const float    kCos15               = 0.9659258f;  // cosf(0.2618f)
const float    kSin15               = 0.2588190f;  // sinf(0.2618f)

// Per-frame erase rect for one eye (covers rotated 30×11 max bbox + ±drift).
const int      kWorkEraseW          = 52;     // = 32 (bbox) + 2*kWorkDriftAmp + 4 margin
const int      kWorkEraseH          = 17;     // = 30*sin15 + 11*cos15 + small margin

// Dots erase rect — covers all three positions (5 px dots).
const int      kDotsEraseX          = 180;
const int      kDotsEraseY          = kDotsY - 3;
const int      kDotsEraseW          = 30;
const int      kDotsEraseH          = 7;

}  // namespace

EyesCard::EyesCard(const AppState& state)
    : state_(state) {
    resetAnim();
    frame_valid_   = false;
    last_state_    = static_cast<BuddyState>(0xFF);
    last_h_        = 0;
    last_dx_       = 0;
    last_base_y_   = 0;
    last_sweat_y_  = 0;
    last_disc_age_ = 0xFFFFFFFFu;
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
    glance_x_                = 0;
    next_glance_ms_          = now + kGlanceMinMs + (esp_random() % kGlanceJitterMs);
    glance_return_ms_        = 0;
    glance_swing_pending_    = false;
    scan_epoch_ms_           = now;
    draw_h_                  = 30;
    draw_dx_                 = 0;
    draw_base_y_             = kBaseIdleY;
    draw_sweat_y_            = 0;
}

void EyesCard::armState(BuddyState state, uint32_t now) {
    prev_state_ = state;
    switch (state) {
        case STATE_DISCONNECTED:
            disc_anim_start_ms_ = now;
            disc_age_ms_        = 0;
            break;
        case STATE_IDLE:
            blink_i_              = -1;
            next_blink_ms_        = now + kBlinkIntervalMs;
            glance_x_             = 0;
            next_glance_ms_       = now + kGlanceMinMs + (esp_random() % kGlanceJitterMs);
            glance_return_ms_     = 0;
            glance_swing_pending_ = false;
            break;
        case STATE_WORKING:
            scan_epoch_ms_ = now;
            break;
        case STATE_WAITING:
            blink_i_       = -1;
            next_blink_ms_ = now + kBlinkIntervalMs;
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

void EyesCard::tickGlanceIdle(uint32_t now) {
    if (glance_x_ != 0) {
        if (glance_return_ms_ != 0 && now >= glance_return_ms_) {
            if (glance_swing_pending_) {
                // Swing across to the opposite side for the second half of
                // the sweep, so each curiosity event covers both directions.
                glance_x_             = -glance_x_;
                glance_swing_pending_ = false;
                glance_return_ms_     = now + kGlanceHoldMs;
            } else {
                glance_x_         = 0;
                glance_return_ms_ = 0;
                next_glance_ms_   = now + kGlanceMinMs + (esp_random() % kGlanceJitterMs);
            }
        }
        return;
    }
    if (now >= next_glance_ms_) {
        glance_x_             = (esp_random() & 1) ? 20 : -20;
        glance_swing_pending_ = true;
        glance_return_ms_     = now + kGlanceHoldMs;
    }
}

void EyesCard::tick(uint32_t now_ms) {
    BuddyState state = state_.buddyState();
    if (state != prev_state_) armState(state, now_ms);

    switch (state) {
        case STATE_DISCONNECTED:
            disc_age_ms_ = now_ms - disc_anim_start_ms_;
            break;

        case STATE_IDLE:
            tickBlink(now_ms);
            tickGlanceIdle(now_ms);
            draw_base_y_ = kBaseIdleY + (glance_x_ != 0 ? kGlanceDy : 0);
            draw_dx_     = glance_x_;
            draw_h_      = (blink_i_ >= 0) ? kBlinkH[blink_i_] : 30;
            break;

        case STATE_WORKING: {
            float ph = (now_ms - scan_epoch_ms_) * (2.0f * 3.14159265f) / 1000.0f;
            draw_dx_     = (int16_t)(sinf(ph) * 30.0f);
            draw_base_y_ = kBaseIdleY;
            draw_h_      = 30;
            // Integer-only drip: 0→kDripSteps px over kDripMs, then snaps back.
            draw_sweat_y_ = (int8_t)((now_ms - scan_epoch_ms_) % kDripMs
                                     * kDripSteps / kDripMs);
            break;
        }

        case STATE_WAITING:
            tickBlink(now_ms);
            draw_base_y_ = kBaseWaitY;
            draw_dx_     = 0;
            draw_h_      = (blink_i_ >= 0) ? kBlinkH[blink_i_] : 30;
            break;
    }
}

bool EyesCard::isDirty() const {
    if (!frame_valid_) return true;
    if (last_state_    != state_.buddyState()) return true;
    if (last_h_        != draw_h_)            return true;
    if (last_dx_       != draw_dx_)           return true;
    if (last_base_y_   != draw_base_y_)       return true;
    if (last_sweat_y_  != draw_sweat_y_)      return true;
    if (last_disc_age_ != disc_age_ms_)       return true;
    return false;
}

void EyesCard::render(Display& display) {
    BuddyState bs = state_.buddyState();
    bool stateJustChanged = !frame_valid_ || (last_state_ != bs);
    // Per CLAUDE.md: incremental DISCONNECTED frames must use a partial erase
    // to avoid the ~13 ms full-screen flash that causes flicker at 62 fps.
    bool full_clear = stateJustChanged || (bs != STATE_DISCONNECTED);
    drawFrame(display.tft(), bs, full_clear);

    last_state_    = bs;
    last_h_        = draw_h_;
    last_dx_       = draw_dx_;
    last_base_y_   = draw_base_y_;
    last_sweat_y_  = draw_sweat_y_;
    last_disc_age_ = disc_age_ms_;
    frame_valid_   = true;
}

void EyesCard::drawFrame(Adafruit_ST7789& tft, BuddyState state, bool full_clear) {
    if (state == STATE_DISCONNECTED) {
        if (full_clear) {
            tft.fillScreen(ST77XX_BLACK);
        } else {
            // Erase only the Z glyph bounding zone: ~2100 px vs 32400
            // for fillScreen — eliminates the ~13 ms full-screen black flash
            // that causes flicker at 62 fps.
            tft.fillRect(kZSpawnX, kZSpawnY + kZDriftY - 2,
                         240 - kZSpawnX, -kZDriftY + 3 * 8 + 4,
                         ST77XX_BLACK);
        }

        const int cy  = kBaseIdleY + 15;       // 67
        const int top = cy - kLidH / 2;        // 62
        tft.fillRect(kLeftX,  top, kEyeW, kLidH, ST77XX_WHITE);
        tft.fillRect(kRightX, top, kEyeW, kLidH, ST77XX_WHITE);

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
        return;
    }

    tft.fillScreen(ST77XX_BLACK);

    if (state == STATE_WORKING) {
        // "> <" squint — left eye ">" (tip left), right eye "<" (tip right)
        int cy   = draw_base_y_ + 15;
        int half = kActionH / 2;
        int lx   = kLeftX  + draw_dx_;
        int rx   = kRightX + draw_dx_;
        tft.fillTriangle(lx,         cy,
                         lx + kEyeW, cy - half,
                         lx + kEyeW, cy + half, ST77XX_WHITE);
        tft.fillTriangle(rx,         cy - half,
                         rx,         cy + half,
                         rx + kEyeW, cy,        ST77XX_WHITE);
        // Sweat drop: triangle tip + circle body, drips kDripSteps px then resets.
        int ty = kSweatTipY + draw_sweat_y_;
        tft.fillTriangle(kSweatCX - 4, ty + 8,
                         kSweatCX + 4, ty + 8,
                         kSweatCX,     ty,      kSweatBlue);
        tft.fillCircle(kSweatCX, ty + 13, 5, kSweatBlue);
        return;
    }

    int h = draw_h_;
    if (h <= 0) return;

    int16_t top = (int16_t)(draw_base_y_ + 15 - h / 2);
    tft.fillRect(kLeftX  + draw_dx_, top, kEyeW, h, ST77XX_WHITE);
    tft.fillRect(kRightX + draw_dx_, top, kEyeW, h, ST77XX_WHITE);
}

void EyesCard::drawRotatedSlit(Adafruit_ST7789& tft, int cx, int cy, int h, int sign) {
    if (h <= 0) return;

    // Width-axis basis (constant; width = kEyeW). For sign = -1, negate the y component.
    const float ux = (kEyeW * 0.5f) * kCos15;
    const float uy = (kEyeW * 0.5f) * kSin15 * (float)sign;

    // Height-axis basis (per-frame because h varies). For sign = -1, negate the x component.
    const float halfH = h * 0.5f;
    const float vx = -halfH * kSin15 * (float)sign;
    const float vy =  halfH * kCos15;

    // Four corners (top-left, top-right, bottom-right, bottom-left).
    // R(+θ)·(±W/2, ±h/2) — y-axis points DOWN in screen coords, but the same
    // formula works because we treat (vx, vy) as the vertical-edge offset.
    const int16_t tlx = (int16_t)lroundf(cx - ux + vx);
    const int16_t tly = (int16_t)lroundf(cy - uy + vy);
    const int16_t trx = (int16_t)lroundf(cx + ux + vx);
    const int16_t try_ = (int16_t)lroundf(cy + uy + vy);
    const int16_t brx = (int16_t)lroundf(cx + ux - vx);
    const int16_t bry = (int16_t)lroundf(cy + uy - vy);
    const int16_t blx = (int16_t)lroundf(cx - ux - vx);
    const int16_t bly = (int16_t)lroundf(cy - uy - vy);

    // Two triangles split the parallelogram along the TL→BR diagonal.
    tft.fillTriangle(tlx, tly, trx, try_, brx, bry, ST77XX_WHITE);
    tft.fillTriangle(tlx, tly, brx, bry, blx, bly, ST77XX_WHITE);
}
