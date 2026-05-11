#pragma once

#include <stdint.h>

#include "../Card.h"

class FactoryResetCoordinator;

// Full-screen takeover during the factory-reset hold-to-confirm flow.
// Force-shown by CardController whenever the coordinator is in
// AwaitingHold or Resetting.
class FactoryResetCard : public Card {
public:
    explicit FactoryResetCard(FactoryResetCoordinator& coord)
        : coord_(coord) {}

    void tick(uint32_t now_ms) override;
    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;

    bool isActive() const;

private:
    FactoryResetCoordinator& coord_;
    bool     full_clear_     = true;
    uint8_t  last_pct_drawn_ = 255;
    uint8_t  last_phase_     = 255;
    uint32_t last_render_ms_ = 0;
    uint32_t now_ms_         = 0;
};
