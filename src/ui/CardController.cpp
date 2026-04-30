#include "CardController.h"

#include <Arduino.h>

#include "../display/Display.h"
#include "../net/BleLink.h"

CardController::CardController(AppState& app, EventBus& bus, WifiManager& wifi,
                               PromptUi& prompt, BleLink& ble,
                               int pin_btn_next, uint8_t btn_next_pressed_level,
                               int pin_btn_prev, uint8_t btn_prev_pressed_level)
    : app_(app), bus_(bus), wifi_(wifi), prompt_(prompt), ble_(ble),
      status_card_(app),
      eyes_card_(app),
      wifi_card_(wifi),
      nav_card_(pin_btn_next, btn_next_pressed_level,
                pin_btn_prev, btn_prev_pressed_level),
      prompt_card_(prompt),
      stack_(),
      prompt_visible_(false) {}

void CardController::begin() {
    stack_.addCard(&status_card_);
    stack_.addCard(&eyes_card_);
    stack_.addCard(&wifi_card_);
    stack_.addCard(&nav_card_);

    bus_.subscribe(EventKind::SnapshotReceived,
                   [this] { status_card_.invalidate(); });
    bus_.subscribe(EventKind::WifiConnected,
                   [this] { wifi_card_.invalidate(); });
    bus_.subscribe(EventKind::WifiDisconnected,
                   [this] { wifi_card_.invalidate(); });
}

void CardController::tick(uint32_t now_ms, Display& display) {
    prompt_ui_update(&prompt_, app_.status().prompt, app_.isLive(now_ms), now_ms);

    PromptView pv = prompt_ui_view(&prompt_);
    if (pv.visible && !stack_.hasOverlay()) {
        prompt_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
        stack_.pushOverlay(&prompt_card_);
        prompt_visible_ = true;
        bus_.publish(EventKind::PromptShow);
    } else if (!pv.visible && stack_.hasOverlay()) {
        stack_.clearOverlay();
        prompt_visible_ = false;
        bus_.publish(EventKind::PromptHide);
    } else if (pv.visible) {
        prompt_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
    }

    char outBuf[96];
    if (prompt_ui_take_outgoing(&prompt_, outBuf, sizeof(outBuf))) {
        ble_.writeLine(outBuf);
    }

    stack_.tick(now_ms, display);
}
