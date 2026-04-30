#include "InputRouter.h"

#include <Arduino.h>

InputRouter::InputRouter(int pin_next, uint8_t next_pressed_level,
                         int pin_prev, uint8_t prev_pressed_level,
                         int pin_center, uint8_t center_pressed_level,
                         CardStack& stack)
    : pin_next_(pin_next),
      next_pressed_level_(next_pressed_level),
      pin_prev_(pin_prev),
      prev_pressed_level_(prev_pressed_level),
      pin_center_(pin_center),
      center_pressed_level_(center_pressed_level),
      stack_(stack),
      buttons_{} {}

void InputRouter::begin() {
    pinMode(pin_next_,   next_pressed_level_   == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    pinMode(pin_prev_,   prev_pressed_level_   == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    pinMode(pin_center_, center_pressed_level_ == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    buttons_init(&buttons_);
}

ButtonEvent InputRouter::update(uint32_t now_ms) {
    bool next_raw   = (digitalRead(pin_next_)   == next_pressed_level_);
    bool prev_raw   = (digitalRead(pin_prev_)   == prev_pressed_level_);
    bool center_raw = (digitalRead(pin_center_) == center_pressed_level_);

    // Center long-press tracking is independent of buttons_step's edge
    // detection, since we want to fire while the button is still held.
    if (center_long_press_ && center_hold_ms_ > 0) {
        if (center_raw && !center_held_) {
            center_held_       = true;
            center_press_ms_   = now_ms;
            center_long_fired_ = false;
        } else if (!center_raw) {
            center_held_       = false;
            center_long_fired_ = false;
        } else if (center_held_ && !center_long_fired_ &&
                   (now_ms - center_press_ms_) >= center_hold_ms_) {
            center_long_fired_ = true;
            center_long_press_();
        }
    }

    // Map physical layout (D0 = next = "down", D2 = prev = "up", D1 = center)
    // onto the buttons.h enum.
    return buttons_step(&buttons_, now_ms,
                        /*up_raw=*/    prev_raw,
                        /*down_raw=*/  next_raw,
                        /*center_raw=*/center_raw);
}

void InputRouter::dispatch(ButtonEvent ev, uint32_t now_ms) {
    if (ev == BTN_NONE) return;
    if (stack_.handleButton(ev, now_ms)) return;
    if (ev == BTN_DOWN)    stack_.next();
    else if (ev == BTN_UP) stack_.prev();
}
