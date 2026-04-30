#include "NavTestCard.h"

#include <Arduino.h>

#include "../../display/Display.h"

NavTestCard::NavTestCard(int pin_next, uint8_t next_pressed_level,
                         int pin_prev, uint8_t prev_pressed_level)
    : pin_next_(pin_next),
      next_pressed_level_(next_pressed_level),
      pin_prev_(pin_prev),
      prev_pressed_level_(prev_pressed_level),
      ever_drawn_(false),
      last_next_pressed_(false),
      last_prev_pressed_(false),
      cur_next_pressed_(false),
      cur_prev_pressed_(false) {}

void NavTestCard::invalidate() {
    ever_drawn_ = false;
}

void NavTestCard::tick(uint32_t /*now_ms*/) {
    cur_next_pressed_ = (digitalRead(pin_next_) == next_pressed_level_);
    cur_prev_pressed_ = (digitalRead(pin_prev_) == prev_pressed_level_);
}

bool NavTestCard::isDirty() const {
    if (!ever_drawn_) return true;
    return cur_next_pressed_ != last_next_pressed_ ||
           cur_prev_pressed_ != last_prev_pressed_;
}

void NavTestCard::render(Display& display) {
    auto& tft = display.tft();

    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setCursor(18, 20);
    tft.print("card 3: nav test");

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(20, 62);
    tft.print("D0: ");
    tft.print(cur_next_pressed_ ? "PRESSED " : "released");

    tft.setCursor(20, 90);
    tft.print("D2: ");
    tft.print(cur_prev_pressed_ ? "PRESSED " : "released");

    last_next_pressed_ = cur_next_pressed_;
    last_prev_pressed_ = cur_prev_pressed_;
    ever_drawn_ = true;
}
