#include "CardController.h"

#include <Arduino.h>
#include <string.h>

#include "../display/Display.h"
#include "../net/BleLink.h"

CardController::CardController(AppState& app, EventBus& bus, WifiManager& wifi,
                               PromptUi& prompt, BleLink& ble, Settings& settings,
                               int pin_btn_next, uint8_t btn_next_pressed_level,
                               int pin_btn_prev, uint8_t btn_prev_pressed_level)
    : app_(app), bus_(bus), wifi_(wifi), prompt_(prompt), ble_(ble),
      settings_(settings),
      status_card_(app),
      eyes_card_(app, prompt),
      wifi_card_(wifi),
      nav_card_(pin_btn_next, btn_next_pressed_level,
                pin_btn_prev, btn_prev_pressed_level),
      prompt_card_(prompt),
      stack_(),
      prompt_visible_(false),
      last_cards_mask_(0),
      last_cards_order_count_(0),
      last_cards_order_{0},
      last_boot_card_(0),
      applied_boot_card_(false) {}

void CardController::begin() {
    rebuildStack();

    bus_.subscribe(EventKind::SnapshotReceived,
                   [this] { status_card_.invalidate(); });
    bus_.subscribe(EventKind::WifiConnected,
                   [this] { wifi_card_.invalidate(); });
    bus_.subscribe(EventKind::WifiDisconnected,
                   [this] { wifi_card_.invalidate(); });
    bus_.subscribe(EventKind::SettingsChanged,
                   [this] { rebuildStack(); });
}

namespace {
Card* cardForId(uint8_t id, StatusCard& s, EyesCard& e, WifiCard& w, NavTestCard& n) {
    switch (id) {
        case settings::CARD_STATUS:  return &s;
        case settings::CARD_EYES:    return &e;
        case settings::CARD_WIFI:    return &w;
        case settings::CARD_NAVTEST: return &n;
    }
    return nullptr;
}
}  // namespace

void CardController::rebuildStack() {
    const settings::Settings& d = settings_.data();

    // No-op if nothing card-related changed.
    if (last_cards_mask_         == d.cards_enabled_mask &&
        last_cards_order_count_  == d.cards_order_count &&
        last_boot_card_          == d.boot_card_id &&
        memcmp(last_cards_order_, d.cards_order, sizeof(last_cards_order_)) == 0 &&
        applied_boot_card_) {
        return;
    }

    // Remember the old active card id (if any) so we can keep the user on
    // the same card across edits when possible.
    Card* prev_active = stack_.active();
    uint8_t prev_id = 0xFF;
    for (uint8_t i = 0; i < d.cards_order_count; ++i) {
        Card* c = cardForId(last_cards_order_[i], status_card_, eyes_card_,
                            wifi_card_, nav_card_);
        if (c == prev_active) { prev_id = last_cards_order_[i]; break; }
    }

    stack_.clear();
    for (uint8_t i = 0; i < d.cards_order_count; ++i) {
        Card* c = cardForId(d.cards_order[i], status_card_, eyes_card_,
                            wifi_card_, nav_card_);
        if (c) stack_.addCard(c);
    }

    // Pick the index: on first call, use boot_card_id; on later calls,
    // try to keep the user on prev_id, else fall back to the boot card,
    // else 0.
    uint8_t target_id = applied_boot_card_
        ? (prev_id != 0xFF ? prev_id : d.boot_card_id)
        : d.boot_card_id;
    size_t target_index = 0;
    for (uint8_t i = 0; i < d.cards_order_count; ++i) {
        if (d.cards_order[i] == target_id) { target_index = i; break; }
    }
    stack_.setIndex(target_index);

    // Cache for the next diff.
    last_cards_mask_         = d.cards_enabled_mask;
    last_cards_order_count_  = d.cards_order_count;
    memcpy(last_cards_order_, d.cards_order, sizeof(last_cards_order_));
    last_boot_card_          = d.boot_card_id;
    applied_boot_card_       = true;
}

void CardController::runSleepManager(uint32_t now_ms, Display& display) {
    if (!input_) return;
    uint16_t timeout_s = settings_.data().sleep_timeout_s;
    if (timeout_s == 0) {
        if (display.isAsleep()) display.setBacklight(true);
        return;
    }
    uint32_t since = now_ms - input_->lastInputMs();
    bool want_sleep = since >= static_cast<uint32_t>(timeout_s) * 1000UL;
    if (want_sleep && !display.isAsleep()) {
        display.setBacklight(false);
    } else if (!want_sleep && display.isAsleep()) {
        display.setBacklight(true);
        Card* a = stack_.active();
        if (a) a->invalidate();
    }
}

void CardController::tick(uint32_t now_ms, Display& display) {
    runSleepManager(now_ms, display);

    prompt_ui_update(&prompt_, app_.status().prompt, app_.isLive(now_ms), now_ms);

    PromptView pv = prompt_ui_view(&prompt_);
    const bool want_overlay = (pv.mode == PROMPT_UI_EXPANDED);
    if (want_overlay && !stack_.hasOverlay()) {
        prompt_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
        stack_.pushOverlay(&prompt_card_);
        prompt_visible_ = true;
        bus_.publish(EventKind::PromptShow);
    } else if (!want_overlay && stack_.hasOverlay()) {
        stack_.clearOverlay();
        prompt_visible_ = false;
        bus_.publish(EventKind::PromptHide);
    } else if (want_overlay) {
        prompt_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));
    }

    char outBuf[96];
    if (prompt_ui_take_outgoing(&prompt_, outBuf, sizeof(outBuf))) {
        ble_.writeLine(outBuf);
    }

    // Don't bother rendering while asleep — saves SPI traffic.
    if (!display.isAsleep()) {
        stack_.tick(now_ms, display);
    }
}
