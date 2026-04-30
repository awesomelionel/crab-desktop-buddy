#pragma once

#include <stdint.h>

#include "buttons.h"
#include "../ui/CardStack.h"

// Centralizes the feather button polling and exposes a normalized ButtonEvent.
// In its terminal shape (after PromptCard lands as an overlay), `tick()`
// reads the buttons, dispatches to CardStack, and falls through to carousel
// nav. While the prompt still lives outside the stack, callers can split:
//   ButtonEvent ev = router.update(now);
//   if (prompt.visible) prompt_ui_button(...);
//   else                router.dispatch(ev, now);
class InputRouter {
public:
    InputRouter(int pin_next, uint8_t next_pressed_level,
                int pin_prev, uint8_t prev_pressed_level,
                int pin_center, uint8_t center_pressed_level,
                CardStack& stack);

    void begin();

    // Reads the buttons, debounces, returns the resulting event.
    ButtonEvent update(uint32_t now_ms);

    // Routes `ev` to the active card; if the card returns false, treats
    // BTN_UP / BTN_DOWN as carousel prev/next.
    void dispatch(ButtonEvent ev, uint32_t now_ms);

    // Convenience: update + dispatch in one call.
    void tick(uint32_t now_ms) { dispatch(update(now_ms), now_ms); }

private:
    int       pin_next_;
    uint8_t   next_pressed_level_;
    int       pin_prev_;
    uint8_t   prev_pressed_level_;
    int       pin_center_;
    uint8_t   center_pressed_level_;
    CardStack& stack_;

    Buttons buttons_;
};
