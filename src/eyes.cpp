#include "eyes.h"

#include <esp_random.h>
#include <math.h>

namespace {

const uint16_t kDimGrey   = 0x18C3;
const int      kEyeW      = 30;
const int      kLeftX     = 30;
const int      kRightX    = 180;
const int      kBaseIdleY = 52;
const int      kBaseWaitY = 32;

const uint8_t kBlinkH[] = {30, 20, 10, 0, 10, 20, 30};
const int     kBlinkN   = 7;

void arm_state(EyesAnim& e, BuddyState state, uint32_t now) {
    e.prev_state = state;
    switch (state) {
    case STATE_DISCONNECTED:
        e.disc_phase    = 0;
        e.disc_next_ms  = now + 3000;
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

void tick_disconnected(EyesAnim& e, uint32_t now) {
    if (e.disc_phase == 0) {
        if (now >= e.disc_next_ms) {
            e.disc_phase   = 1;
            e.disc_next_ms = now + 200;
        }
    } else {
        if (now >= e.disc_next_ms) {
            e.disc_phase   = 0;
            e.disc_next_ms = now + 3000;
        }
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
    e.disc_phase           = 0;
    e.disc_next_ms         = now + 3000;
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
}

void eyes_tick(EyesAnim& e, BuddyState state, uint32_t now) {
    if (state != e.prev_state) {
        arm_state(e, state, now);
    }

    switch (state) {
    case STATE_DISCONNECTED:
        tick_disconnected(e, now);
        e.draw_base_y = kBaseIdleY;
        e.draw_dx     = 0;
        if (e.disc_phase == 1) {
            e.draw_h = 30;
        } else {
            e.draw_h = 0;
        }
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
        e.draw_dx     = (int16_t)(sinf(ph) * 30.0f);
        e.draw_base_y = kBaseIdleY;
        e.draw_h      = 30;
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

    if (state == STATE_DISCONNECTED && e.disc_phase == 0) {
        return;
    }

    int h = e.draw_h;
    if (h <= 0) {
        return;
    }

    uint16_t col = ST77XX_WHITE;
    if (state == STATE_DISCONNECTED && e.disc_phase == 1) {
        col = kDimGrey;
    }

    int16_t top = (int16_t)(e.draw_base_y + 15 - h / 2);
    tft.fillRect(kLeftX + e.draw_dx, top, kEyeW, h, col);
    tft.fillRect(kRightX + e.draw_dx, top, kEyeW, h, col);
}
