#pragma once

#include <stdint.h>
#include <string.h>

#include "../Card.h"
#include "../../core/AppState.h"
#include "protocol.h"
#include "state.h"

class StatusCard : public Card {
public:
    explicit StatusCard(const AppState& state);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;

    // The status card needs a periodic recheck so live → offline transitions
    // repaint even if the snapshot stops arriving.
    void tick(uint32_t now_ms) override;

private:
    const AppState& state_;
    bool            ever_drawn_;
    BuddyState      last_drawn_state_;
    char            last_drawn_msg_[sizeof(ClaudeStatus::msg)];
    bool            last_drawn_live_;
    uint32_t        last_recheck_ms_;
};
