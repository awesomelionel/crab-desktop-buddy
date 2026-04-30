#pragma once

#include <stdint.h>

#include "CardStack.h"
#include "../core/AppState.h"
#include "../core/EventBus.h"
#include "../net/WifiManager.h"
#include "prompt_ui.h"
#include "cards/EyesCard.h"
#include "cards/NavTestCard.h"
#include "cards/PromptCard.h"
#include "cards/StatusCard.h"
#include "cards/WifiCard.h"

class Display;
class BleLink;

// Owns the card carousel + prompt overlay, listens to EventBus for the
// transitions that should cause cards to repaint, and drains outgoing
// PromptUi decisions to BleLink.
class CardController {
public:
    CardController(AppState& app, EventBus& bus, WifiManager& wifi,
                   PromptUi& prompt, BleLink& ble,
                   int pin_btn_next, uint8_t btn_next_pressed_level,
                   int pin_btn_prev, uint8_t btn_prev_pressed_level);

    void begin();
    void tick(uint32_t now_ms, Display& display);

    CardStack& stack() { return stack_; }

private:
    AppState&    app_;
    EventBus&    bus_;
    WifiManager& wifi_;
    PromptUi&    prompt_;
    BleLink&     ble_;

    StatusCard   status_card_;
    EyesCard     eyes_card_;
    WifiCard     wifi_card_;
    NavTestCard  nav_card_;
    PromptCard   prompt_card_;
    CardStack    stack_;

    bool         prompt_visible_;  // tracks last-seen overlay state
};
