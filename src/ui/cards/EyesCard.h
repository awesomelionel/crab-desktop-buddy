#pragma once

#include <stdint.h>

#include "../Card.h"
#include "../../core/AppState.h"
#include "../../eyes.h"
#include "state.h"

class EyesCard : public Card {
public:
    explicit EyesCard(const AppState& state);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;

private:
    const AppState& state_;
    EyesAnim        anim_;
    bool            frame_valid_;
    BuddyState      last_state_;
    uint8_t         last_h_;
    int16_t         last_dx_;
    int16_t         last_base_y_;
    uint32_t        last_disc_age_;
};
