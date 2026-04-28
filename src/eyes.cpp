#include "eyes.h"

#include <esp_random.h>
#include <math.h>

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

const int      kLidH       = 10;
const int      kZSpawnX    = 210;
const int      kZSpawnY    = 50;
const int      kZDriftX    = 20;
const int      kZDriftY    = -45;
const uint32_t kZLoopMs    = 3000;

const uint8_t kBlinkH[] = {30, 20, 10, 0, 10, 20, 30};
const int     kBlinkN   = 7;

void arm_state(EyesAnim& e, BuddyState state, uint32_t now) {
    e.prev_state = state;
    switch (state) {
    case STATE_DISCONNECTED:
        e.disc_anim_start_ms = now;
        e.disc_age_ms        = 0;
        break;
    case STATE_IDLE:
        e.blink_i              = -1;
        e.next_blink_ms        = now + 8000;
        e.glance_x             = 0;
        e.next_glance_ms       = now + 5000 + (esp_random() % 4000);
        e.glance_return_ms     = 0;
        break;
    case STATE_WORKING:
        e.scan_epoch_ms = now;
        break;
    case STATE_WAITING:
        e.blink_i        = -1;
        e.next_blink_ms  = now + 8000;
        break;
    }
}

void tick_blink(EyesAnim& e, uint32_t now) {
    if (e.blink_i >= 0) {
        if (now >= e.blink_step_deadline_ms) {
            e.blink_i++;
            if (e.blink_i >= kBlinkN) {
                e.blink_i       = -1;
                e.next_blink_ms = now + 8000;
            } else {
                e.blink_step_deadline_ms = now + 100;
            }
        }
    } else if (now >= e.next_blink_ms) {
        e.blink_i                = 0;
        e.blink_step_deadline_ms = now + 100;
    }
}

void tick_glance_idle(EyesAnim& e, uint32_t now) {
    if (e.glance_x != 0) {
        if (e.glance_return_ms != 0 && now >= e.glance_return_ms) {
            e.glance_x         = 0;
            e.glance_return_ms = 0;
            e.next_glance_ms   = now + 5000 + (esp_random() % 4000);
        }
        return;
    }
    if (now >= e.next_glance_ms) {
        e.glance_x = (esp_random() & 1) ? 20 : -20;
        // Hold offset ~1 s then return
        e.glance_return_ms = now + 1000;
    }
}

}  // namespace

void eyes_reset(EyesAnim& e) {
    uint32_t now = millis();
    // Force eyes_tick to arm timers for the live BuddyState on next tick.
    e.prev_state = static_cast<BuddyState>(255);
    e.disc_anim_start_ms   = now;
    e.disc_age_ms          = 0;
    e.blink_i              = -1;
    e.next_blink_ms        = now + 8000;
    e.blink_step_deadline_ms = 0;
    e.glance_x             = 0;
    e.next_glance_ms       = now + 5000 + (esp_random() % 4000);
    e.glance_return_ms     = 0;
    e.scan_epoch_ms        = now;
    e.draw_h               = 30;
    e.draw_dx              = 0;
    e.draw_base_y          = kBaseIdleY;
    e.draw_sweat_y         = 0;
}

void eyes_tick(EyesAnim& e, BuddyState state, uint32_t now) {
    if (state != e.prev_state) {
        arm_state(e, state, now);
    }

    switch (state) {
    case STATE_DISCONNECTED:
        e.disc_age_ms = now - e.disc_anim_start_ms;
        break;

    case STATE_IDLE:
        tick_blink(e, now);
        tick_glance_idle(e, now);
        e.draw_base_y = kBaseIdleY;
        e.draw_dx     = e.glance_x;
        e.draw_h      = (e.blink_i >= 0) ? kBlinkH[e.blink_i] : 30;
        break;

    case STATE_WORKING: {
        float ph = (now - e.scan_epoch_ms) * (2.0f * 3.14159265f) / 1000.0f;
        e.draw_dx      = (int16_t)(sinf(ph) * 30.0f);
        e.draw_base_y  = kBaseIdleY;
        e.draw_h       = 30;
        // Integer-only drip: 0→kDripSteps px over kDripMs, then snaps back.
        // uint32_t % uint32_t is exact regardless of uptime.
        e.draw_sweat_y = (int8_t)((now - e.scan_epoch_ms) % kDripMs
                                   * kDripSteps / kDripMs);
        break;
    }

    case STATE_WAITING:
        tick_blink(e, now);
        e.draw_base_y = kBaseWaitY;
        e.draw_dx     = 0;
        e.draw_h      = (e.blink_i >= 0) ? kBlinkH[e.blink_i] : 30;
        break;
    }
}

void eyes_render(Adafruit_ST7789& tft, const EyesAnim& e, BuddyState state) {
    tft.fillScreen(ST77XX_BLACK);

    if (state == STATE_DISCONNECTED) {
        const int cy  = kBaseIdleY + 15;       // 67
        const int top = cy - kLidH / 2;        // 62
        tft.fillRect(kLeftX,  top, kEyeW, kLidH, ST77XX_WHITE);
        tft.fillRect(kRightX, top, kEyeW, kLidH, ST77XX_WHITE);

        const uint32_t base = e.disc_age_ms % kZLoopMs;
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

    if (state == STATE_WORKING) {
        // "> <" squint — left eye ">" (tip left), right eye "<" (tip right)
        int cy   = e.draw_base_y + 15;
        int half = kActionH / 2;
        int lx   = kLeftX  + e.draw_dx;
        int rx   = kRightX + e.draw_dx;
        tft.fillTriangle(lx,         cy,
                         lx + kEyeW, cy - half,
                         lx + kEyeW, cy + half, ST77XX_WHITE);
        tft.fillTriangle(rx,         cy - half,
                         rx,         cy + half,
                         rx + kEyeW, cy,        ST77XX_WHITE);
        // Sweat drop: triangle tip + circle body, drips kDripSteps px then resets
        int ty = kSweatTipY + e.draw_sweat_y;
        tft.fillTriangle(kSweatCX - 4, ty + 8,
                         kSweatCX + 4, ty + 8,
                         kSweatCX,     ty,      kSweatBlue);
        tft.fillCircle(kSweatCX, ty + 13, 5, kSweatBlue);
        return;
    }

    int h = e.draw_h;
    if (h <= 0) {
        return;
    }

    int16_t top = (int16_t)(e.draw_base_y + 15 - h / 2);
    tft.fillRect(kLeftX + e.draw_dx, top, kEyeW, h, ST77XX_WHITE);
    tft.fillRect(kRightX + e.draw_dx, top, kEyeW, h, ST77XX_WHITE);
}
