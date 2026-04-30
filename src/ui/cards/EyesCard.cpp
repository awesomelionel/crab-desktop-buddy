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
    next_blink_ms_           = now + 8000;
    blink_step_deadline_ms_  = 0;
    glance_x_                = 0;
    next_glance_ms_          = now + 5000 + (esp_random() % 4000);
    glance_return_ms_        = 0;
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
            blink_i_           = -1;
            next_blink_ms_     = now + 8000;
            glance_x_          = 0;
            next_glance_ms_    = now + 5000 + (esp_random() % 4000);
            glance_return_ms_  = 0;
            break;
        case STATE_WORKING:
            scan_epoch_ms_ = now;
            break;
        case STATE_WAITING:
            blink_i_       = -1;
            next_blink_ms_ = now + 8000;
            break;
    }
}

void EyesCard::tickBlink(uint32_t now) {
    if (blink_i_ >= 0) {
        if (now >= blink_step_deadline_ms_) {
            blink_i_++;
            if (blink_i_ >= kBlinkN) {
                blink_i_       = -1;
                next_blink_ms_ = now + 8000;
            } else {
                blink_step_deadline_ms_ = now + 100;
            }
        }
    } else if (now >= next_blink_ms_) {
        blink_i_                 = 0;
        blink_step_deadline_ms_  = now + 100;
    }
}

void EyesCard::tickGlanceIdle(uint32_t now) {
    if (glance_x_ != 0) {
        if (glance_return_ms_ != 0 && now >= glance_return_ms_) {
            glance_x_         = 0;
            glance_return_ms_ = 0;
            next_glance_ms_   = now + 5000 + (esp_random() % 4000);
        }
        return;
    }
    if (now >= next_glance_ms_) {
        glance_x_         = (esp_random() & 1) ? 20 : -20;
        // Hold offset ~1 s then return.
        glance_return_ms_ = now + 1000;
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
            draw_base_y_ = kBaseIdleY;
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
