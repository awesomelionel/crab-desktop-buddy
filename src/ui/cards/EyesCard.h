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
    EyesCard(const AppState& state, PromptUi& prompt);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;
    bool handleButton(ButtonEvent ev, uint32_t now_ms) override;

    // CardController feeds device name + liveness every tick so the
    // collapsed-prompt badge / footer can render without holding an
    // AppState ref directly. Mirrors PromptCard::setFooter.
    void setFooter(const char* device_name, bool live);

private:
    void resetAnim();
    void armState(BuddyState s, uint32_t now_ms);
    void tickBlink(uint32_t now_ms);
    void tickGlanceIdle(uint32_t now_ms);
    void tickWaitGaze(uint32_t now_ms);
    void tickQuestionMarks(uint32_t now_ms);
    void drawFrame(Adafruit_ST7789& tft, BuddyState state, bool full_clear);
    void drawRotatedSlit(Adafruit_ST7789& tft, int cx, int cy, int h, int sign);

    const AppState& state_;
    PromptUi&       prompt_;

    // Animation state.
    BuddyState prev_state_;
    uint32_t   disc_anim_start_ms_;
    uint32_t   disc_age_ms_;
    int8_t     blink_i_;                  // -1 between blinks, else 0..N-1
    uint32_t   next_blink_ms_;
    uint32_t   blink_step_deadline_ms_;
    uint32_t   next_glance_ms_;           // when the next IDLE glance event may start
    uint32_t   glance_event_start_ms_;    // 0 when no event is in flight
    int8_t     glance_event_side_;        // ±1; first peak direction for the in-flight event
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

    // ---- STATE_WAITING ----
    uint32_t   wait_scan_epoch_ms_;
    int8_t     draw_wait_gaze_dy_;        // 0..kWaitGlanceDownDy

    struct QBubble {
        uint32_t born_ms;
        int8_t   slot_x_offset;
        int8_t   slot_y_offset;
        uint8_t  size;
        bool     alive;
    };
    QBubble    q_bubbles_[8];     // capacity = kQBubbleCap
    uint32_t   next_q_spawn_ms_;

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
    int8_t     last_wait_gaze_dy_;
    bool       last_badge_visible_;
    char       footer_device_[20];
    bool       footer_live_;
};
