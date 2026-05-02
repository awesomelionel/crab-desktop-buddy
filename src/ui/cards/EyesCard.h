#pragma once

#include <stdint.h>

#include "../Card.h"
#include "../../core/AppState.h"
#include "state.h"
#include "prompt_ui.h"

class Adafruit_ST7789;

// Animated face card. Owns both the animation state and the dirty-tracking
// state previously split between src/eyes.{h,cpp} and EyesCard's mirror
// fields — they are the same data viewed from two angles.
class EyesCard : public Card {
public:
    EyesCard(const AppState& state, const PromptUi& prompt);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;

private:
    void resetAnim();
    void armState(BuddyState s, uint32_t now_ms);
    void tickBlink(uint32_t now_ms);
    void tickGlanceIdle(uint32_t now_ms);
    void drawFrame(Adafruit_ST7789& tft, BuddyState state, bool full_clear);
    void drawRotatedSlit(Adafruit_ST7789& tft, int cx, int cy, int h, int sign);

    const AppState& state_;
    const PromptUi& prompt_;

    // Animation state.
    BuddyState prev_state_;
    uint32_t   disc_anim_start_ms_;
    uint32_t   disc_age_ms_;
    int8_t     blink_i_;                  // -1 between blinks, else 0..N-1
    uint32_t   next_blink_ms_;
    uint32_t   blink_step_deadline_ms_;
    int16_t    glance_x_;                 // IDLE side-glance x offset
    uint32_t   next_glance_ms_;
    uint32_t   glance_return_ms_;
    bool       glance_swing_pending_;     // true after first half of a L↔R sweep
    uint32_t   scan_epoch_ms_;            // WORKING animation time origin
    uint8_t    draw_h_;
    int16_t    draw_dx_;
    int16_t    draw_base_y_;

    // STATE_WORKING — rare blink, separate timer to avoid coupling with IDLE/WAITING blink_i_.
    uint32_t   next_work_blink_ms_;
    uint32_t   work_blink_step_deadline_ms_;
    int8_t     draw_work_blink_i_;        // -1 between blinks, else 0..kWorkBlinkN-1
    int8_t     draw_blink_h_;             // current blink-step height, or -1 when not blinking
    uint8_t    draw_dots_n_;              // typing-dots count, 0..3

    // Dirty-tracking against the last rendered frame: a snapshot of the
    // draw outputs so isDirty() can flip true any time the animation moved.
    bool       frame_valid_;
    BuddyState last_state_;
    uint8_t    last_h_;
    int16_t    last_dx_;
    int16_t    last_base_y_;
    int8_t     last_blink_h_;
    uint8_t    last_dots_n_;
    uint32_t   last_disc_age_;
};
