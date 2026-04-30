#pragma once

#include <stdint.h>

#include "../Card.h"
#include "../../core/AppState.h"
#include "state.h"

class Adafruit_ST7789;

// Animated face card. Owns both the animation state and the dirty-tracking
// state previously split between src/eyes.{h,cpp} and EyesCard's mirror
// fields — they are the same data viewed from two angles.
class EyesCard : public Card {
public:
    explicit EyesCard(const AppState& state);

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

    const AppState& state_;

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
    uint32_t   scan_epoch_ms_;            // WORKING scan + sweat origin
    uint8_t    draw_h_;
    int16_t    draw_dx_;
    int16_t    draw_base_y_;
    int8_t     draw_sweat_y_;

    // Dirty-tracking against the last rendered frame: a snapshot of the
    // draw outputs so isDirty() can flip true any time the animation moved.
    bool       frame_valid_;
    BuddyState last_state_;
    uint8_t    last_h_;
    int16_t    last_dx_;
    int16_t    last_base_y_;
    int8_t     last_sweat_y_;
    uint32_t   last_disc_age_;
};
