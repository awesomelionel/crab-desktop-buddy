#pragma once

#include <stdint.h>
#include <IPAddress.h>

#include "../Card.h"
#include "../../net/WifiManager.h"

class WifiCard : public Card {
public:
    explicit WifiCard(const WifiManager& wifi);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;

private:
    const WifiManager& wifi_;

    bool      ever_drawn_;
    WifiState last_state_;
    uint32_t  last_ip_;
    char      last_ssid_[ConfigStore::SSID_MAX_LEN + 1];
    uint8_t   spinner_phase_;       // for the connecting-dots animation
    uint32_t  last_spinner_tick_ms_;
    uint32_t  now_ms_;
};
