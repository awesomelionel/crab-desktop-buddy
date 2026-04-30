#pragma once

#include <stdint.h>

#include "../Card.h"

class NavTestCard : public Card {
public:
    NavTestCard(int pin_next, uint8_t next_pressed_level,
                int pin_prev, uint8_t prev_pressed_level);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;

private:
    int      pin_next_;
    uint8_t  next_pressed_level_;
    int      pin_prev_;
    uint8_t  prev_pressed_level_;

    bool     ever_drawn_;
    bool     last_next_pressed_;
    bool     last_prev_pressed_;
    bool     cur_next_pressed_;
    bool     cur_prev_pressed_;
};
