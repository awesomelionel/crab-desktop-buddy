#pragma once

#include <stdint.h>

#include "../Card.h"
#include "../../core/AppState.h"
#include "state.h"
#include "prompt_ui.h"
#include "anim_done.h"
#include "anim_idle.h"
#include "anim_sleep.h"
#include "anim_working.h"
#include "avatar_face.h"
class Adafruit_ST7789;
class Adafruit_GFX;
class GFXcanvas16;

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
    void armAvatar(const avatar_face::Anim& anim, uint32_t now_ms);
    void tickBlink(uint32_t now_ms);
    void tickWaitGaze(uint32_t now_ms);
    void tickQuestionMarks(uint32_t now_ms);
    void tickDone(uint32_t now_ms);
    void drawDoneFrame(Adafruit_ST7789& tft, uint32_t t_into_done, bool full_clear);
    uint8_t doneSparkleCount(uint32_t t_into_done) const;
    void drawFrame(Adafruit_ST7789& tft, BuddyState state, bool full_clear);
    void drawAvatarEye(Adafruit_ST7789& tft, int slot, int side, bool full_clear);
    void drawSweatDrop(Adafruit_ST7789& tft);
    void drawSleepZs(Adafruit_ST7789& tft);

    const AppState& state_;
    PromptUi&       prompt_;

    // Animation state.
    BuddyState prev_state_;
    uint32_t   disc_anim_start_ms_;
    uint32_t   disc_age_ms_;
    int8_t     blink_i_;                  // -1 between blinks, else 0..N-1
    uint32_t   next_blink_ms_;
    uint32_t   blink_step_deadline_ms_;
    uint32_t   scan_epoch_ms_;            // avatar animation time origin
    uint8_t    draw_h_;
    int16_t    draw_dx_;
    int16_t    draw_base_y_;

    int8_t     draw_blink_h_;             // current blink-step height, or -1 when not blinking

    // ---- STATE_IDLE, STATE_WORKING and STATE_DISCONNECTED ----
    // The baked avatar-core animations (see lib/avatar_face): kIdle on IDLE,
    // kWorking on WORKING, kSleep on DISCONNECTED. In these states the eyes are
    // projected onto a virtual body, so they change shape, tilt and position
    // together and roam most of the panel. scan_epoch_ms_ is the time origin;
    // face_anim_ is null in every state that draws plain rectangle eyes.
    const avatar_face::Anim*  face_anim_;
    avatar_face::Pose         face_pose_;         // pose for the current frame
    float                     face_blink_;        // 1.0 open, 0.0 fully shut
    avatar_face::BlinkState   face_blink_state_;
    bool                      face_anim_active_;      // pose or blink moving this frame
    bool                      last_face_anim_active_; // ...on the frame we last drew

    // ---- STATE_WORKING flourishes ----
    // A per-frame jitter of the whole face plus a dripping sweat bead, so
    // WORKING reads as effort even through the animation's multi-second
    // holds. Both are device-side dressing, not part of the baked avatar,
    // and both are zero/hidden in every other state.
    int8_t   shake_x_, shake_y_;        // px, applied to both eyes together
    int16_t  sweat_y_;                  // bead centre y, or -1 when hidden
    int8_t   last_shake_x_, last_shake_y_;
    int16_t  last_sweat_y_;

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

    // ---- STATE_DONE celebration overlay ----
    // DONE is NOT a BuddyState — it is a render-only animation phase that
    // overlays STATE_IDLE for 1.5 s following a direct WORKING -> IDLE
    // transition. See docs/superpowers/specs/2026-05-09-done-eyes-design.md.
    uint32_t      working_entered_ms_;
    bool          done_active_;
    uint32_t      done_start_ms_;

    // Dirty-tracking against the last rendered frame: a snapshot of the
    // draw outputs so isDirty() can flip true any time the animation moved.
    bool       frame_valid_;
    BuddyState last_state_;
    uint8_t    last_h_;
    int16_t    last_dx_;
    int16_t    last_base_y_;
    int8_t     last_blink_h_;
    uint32_t   last_disc_age_;
    int8_t     last_wait_gaze_dy_;
    bool       last_badge_visible_;
    char       footer_device_[20];
    bool       footer_live_;
    // Mirrors of the last-rendered footer content so the WAITING render
    // only redraws when something actually changed. Without these the
    // footer band repainted every frame in COLLAPSED mode and flickered.
    char       last_footer_device_[20];
    bool       last_footer_live_;
    bool       last_footer_drawn_;     // false until the footer first appears

    // ---- DONE dirty-tracking snapshot ----
    bool          last_done_active_;
    uint32_t      last_done_phase_t_;
    uint8_t       last_sparkle_brightness_n_;

    // Off-screen canvas for the avatar eye render. Each eye is composed
    // here in RAM (61×72 px = 8.6 KB, the worst-case single-eye bounding box
    // across both animations) then pushed to the LCD as one continuous SPI
    // burst — no black-flash intermediate state for the LCD scanline to
    // catch, which is what eliminated the tearing the bbox-erase approach
    // used to produce. Sized once at the shared maximum so IDLE and WORKING
    // can share it; the two eyes never come within 90 px of overlapping, so
    // the oversized canvas can't clip its neighbour (pinned by
    // test_avatar_face). Allocated lazily and reused for both eyes.
    GFXcanvas16* face_canvas_;

    // Scratch outline buffer, kept off the stack because the render path
    // would otherwise put 1 KB of Points on it.
    avatar_face::Point face_pts_[avatar_face::kMaxOutlinePoints];

    // Where each eye's canvas landed last frame, so the next frame can erase
    // only the strips it vacated instead of clearing the screen.
    int16_t face_eye_x_[2];
    int16_t face_eye_y_[2];
    bool    face_eye_valid_[2];

    // Off-screen canvas for the WAITING question-marks band (78×69 px =
    // 10.5 KB). Same rationale as work_canvas_ — the glyph render goes
    // through expensive Adafruit_GFX text drawing which writes pixels
    // one fillRect at a time. Composing in RAM and pushing as one
    // drawRGBBitmap is both ~5× faster and tearing-free.
    GFXcanvas16* wait_q_canvas_;
};
