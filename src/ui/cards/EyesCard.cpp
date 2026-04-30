#include "EyesCard.h"

#include "../../display/Display.h"

EyesCard::EyesCard(const AppState& state)
    : state_(state),
      anim_{},
      frame_valid_(false),
      last_state_(static_cast<BuddyState>(0xFF)),
      last_h_(0),
      last_dx_(0),
      last_base_y_(0),
      last_disc_age_(0xFFFFFFFFu) {}

void EyesCard::invalidate() {
    frame_valid_ = false;
    eyes_reset(anim_);
}

void EyesCard::tick(uint32_t now_ms) {
    eyes_tick(anim_, state_.buddyState(), now_ms);
}

bool EyesCard::isDirty() const {
    if (!frame_valid_) return true;
    if (last_state_   != state_.buddyState()) return true;
    if (last_h_       != anim_.draw_h)       return true;
    if (last_dx_      != anim_.draw_dx)      return true;
    if (last_base_y_  != anim_.draw_base_y)  return true;
    if (last_disc_age_ != anim_.disc_age_ms) return true;
    return false;
}

void EyesCard::render(Display& display) {
    BuddyState bs = state_.buddyState();
    bool stateJustChanged = !frame_valid_ || (last_state_ != bs);
    // Per CLAUDE.md: incremental DISCONNECTED frames must use a partial erase
    // to avoid the ~13 ms full-screen flash. Other states either repaint
    // infrequently (idle blink, working scan) or change all at once on entry.
    bool full_clear = stateJustChanged || (bs != STATE_DISCONNECTED);
    eyes_render(display.tft(), anim_, bs, full_clear);

    last_state_   = bs;
    last_h_       = anim_.draw_h;
    last_dx_      = anim_.draw_dx;
    last_base_y_  = anim_.draw_base_y;
    last_disc_age_ = anim_.disc_age_ms;
    frame_valid_  = true;
}
