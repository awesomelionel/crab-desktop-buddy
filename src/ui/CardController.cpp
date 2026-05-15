#include "CardController.h"

#include <Arduino.h>
#include <string.h>

#include "../core/FactoryResetCoordinator.h"
#include "../core/UpdateManager.h"
#include "../display/Display.h"
#include "../net/BleLink.h"
#include "backlight.h"

CardController::CardController(AppState& app, EventBus& bus, WifiManager& wifi,
                               PromptUi& prompt, BleLink& ble, Settings& settings,
                               UpdateManager& um, FactoryResetCoordinator& fr,
                               net::BusFetchService& service)
    : app_(app), bus_(bus), wifi_(wifi), prompt_(prompt), ble_(ble),
      settings_(settings),
      um_(um),
      fr_(fr),
      service_(service),
      status_card_(app, prompt),
      eyes_card_(app, prompt),
      wifi_card_(wifi),
      prompt_card_(prompt),
      updating_card_(um),
      factory_reset_card_(fr),
      bus_card_0_(0, settings, wifi, service),
      bus_card_1_(1, settings, wifi, service),
      bus_card_2_(2, settings, wifi, service),
      bus_card_3_(3, settings, wifi, service),
      stack_(),
      prompt_visible_(false),
      last_cards_mask_(0),
      last_cards_order_count_(0),
      last_cards_order_{0},
      last_boot_card_(0),
      applied_boot_card_(false) {}

void CardController::begin() {
    rebuildStack();

    // Snapshots no longer invalidate StatusCard: invalidate() forces a
    // full fillScreen on the next render (it's used by card switches +
    // sleep-wake where pixels are stale), and BLE snapshots arrive often
    // enough that this strobed the whole card every second. StatusCard's
    // per-region dirty checks already pick up changes to total / running
    // / waiting / tokens_today / msg / valid / live without needing
    // invalidate().
    bus_.subscribe(EventKind::WifiConnected,
                   [this] { wifi_card_.invalidate(); });
    bus_.subscribe(EventKind::WifiDisconnected,
                   [this] { wifi_card_.invalidate(); });
    bus_.subscribe(EventKind::SettingsChanged,
                   [this] { rebuildStack(); });

    last_activity_ms_ = millis();

    auto bump_activity = [this] {
        last_activity_ms_ = millis();
    };
    bus_.subscribe(EventKind::StatusTransitioned, bump_activity);
    bus_.subscribe(EventKind::PromptArrived,      bump_activity);
    // tokens_today ticks during every Claude turn that consumes tokens, so
    // subscribing TokensChanged here would prevent the screen from ever
    // dimming while a session is active. The event is still published by
    // BleLink for telemetry/UI consumers; it's just not a wake source.
    // WifiConnected/WifiDisconnected are already subscribed for invalidate;
    // also bump activity from them.
    bus_.subscribe(EventKind::WifiConnected,    bump_activity);
    bus_.subscribe(EventKind::WifiDisconnected, bump_activity);
}

namespace {
Card* cardForIdLegacy(uint8_t id, StatusCard& s, EyesCard& e, WifiCard& w) {
    switch (id) {
        case settings::CARD_STATUS: return &s;
        case settings::CARD_EYES:   return &e;
        case settings::CARD_WIFI:   return &w;
    }
    return nullptr;
}
}  // namespace

void CardController::rebuildStack() {
    const settings::Settings& d = settings_.data();

    if (last_cards_mask_         == d.cards_enabled_mask &&
        last_cards_order_count_  == d.cards_order_count &&
        last_boot_card_          == d.boot_card_id &&
        memcmp(last_cards_order_, d.cards_order, sizeof(last_cards_order_)) == 0 &&
        applied_boot_card_) {
        return;
    }

    auto cardFor = [this, &d](uint8_t id) -> Card* {
        Card* legacy = cardForIdLegacy(id, status_card_, eyes_card_,
                                        wifi_card_);
        if (legacy) return legacy;
        if (id >= settings::CARD_BUS_1 && id <= settings::CARD_BUS_4) {
            uint8_t slot = id - settings::CARD_BUS_1;
            // Hide the card if its slot is empty even when the mask bit
            // happens to be set (defence-in-depth alongside settings
            // validation).
            if (d.bus_stops[slot].code[0] == '\0') return nullptr;
            switch (slot) {
                case 0: return &bus_card_0_;
                case 1: return &bus_card_1_;
                case 2: return &bus_card_2_;
                case 3: return &bus_card_3_;
            }
        }
        return nullptr;
    };

    Card* prev_active = stack_.active();
    uint8_t prev_id = 0xFF;
    for (uint8_t i = 0; i < last_cards_order_count_; ++i) {
        Card* c = cardFor(last_cards_order_[i]);
        if (c == prev_active) { prev_id = last_cards_order_[i]; break; }
    }

    stack_.clear();
    for (uint8_t i = 0; i < d.cards_order_count; ++i) {
        Card* c = cardFor(d.cards_order[i]);
        if (c) stack_.addCard(c);
    }

    uint8_t target_id = applied_boot_card_
        ? (prev_id != 0xFF ? prev_id : d.boot_card_id)
        : d.boot_card_id;
    size_t target_index = 0;
    for (uint8_t i = 0; i < d.cards_order_count; ++i) {
        if (d.cards_order[i] == target_id) { target_index = i; break; }
    }
    stack_.setIndex(target_index);

    last_cards_mask_         = d.cards_enabled_mask;
    last_cards_order_count_  = d.cards_order_count;
    memcpy(last_cards_order_, d.cards_order, sizeof(last_cards_order_));
    last_boot_card_          = d.boot_card_id;
    applied_boot_card_       = true;
}

void CardController::runBacklightManager(uint32_t now_ms, Display& display) {
    if (!input_) return;

    // Fold the latest input timestamp into our local clock so that input
    // events that occurred since the last tick count as activity.
    uint32_t last_input = input_->lastInputMs();
    if (last_input > last_activity_ms_) last_activity_ms_ = last_input;

    uint32_t idle_ms = now_ms - last_activity_ms_;
    uint8_t  pct     = backlight_compute_duty(idle_ms, settings_.data());

    bool was_off = display.isAsleep();
    display.setBacklight(pct);
    if (was_off && pct != 0) {
        if (Card* a = stack_.active()) a->invalidate();
    }
}

void CardController::preloadNeighbour(size_t carousel_index) {
    const settings::Settings& d = settings_.data();
    if (carousel_index >= d.cards_order_count) return;
    uint8_t card_id = d.cards_order[carousel_index];
    if (card_id < settings::CARD_BUS_1 || card_id > settings::CARD_BUS_4) return;
    uint8_t slot = (uint8_t)(card_id - settings::CARD_BUS_1);
    const char* code = d.bus_stops[slot].code;
    if (code[0] == '\0') return;
    service_.request(slot, code, bus_fetch_logic::FetchPriority::LOW_PRIO);
}

void CardController::tick(uint32_t now_ms, Display& display) {
    runBacklightManager(now_ms, display);

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

    eyes_card_.setFooter(app_.deviceName(), app_.isLive(now_ms));

    char outBuf[96];
    if (prompt_ui_take_outgoing(&prompt_, outBuf, sizeof(outBuf))) {
        ble_.writeLine(outBuf);
    }

    // Force-show overlays pre-empt the carousel. Wake the display so
    // they're visible even if we'd otherwise be asleep. Order: factory
    // reset highest priority (destructive), then OTA install.

    factory_reset_card_.tick(now_ms);
    if (factory_reset_card_.isActive()) {
        if (display.isAsleep()) display.setBacklight(true);
        if (factory_reset_card_.isDirty()) {
            factory_reset_card_.render(display);
        }
        last_activity_ms_ = now_ms;
        return;
    }

    updating_card_.tick(now_ms);
    if (updating_card_.isActive()) {
        if (display.isAsleep()) display.setBacklight(true);
        if (updating_card_.isDirty()) {
            updating_card_.render(display);
        }
        last_activity_ms_ = now_ms;   // prevent sleep while installing
        return;
    }

    // Don't bother rendering while asleep — saves SPI traffic.
    if (!display.isAsleep()) {
        stack_.tick(now_ms, display);
    }

    // ---- Neighbour preload ----------------------------------------------
    // Track carousel index changes; ~750 ms after the user settles on a
    // card, prefetch the up/down neighbours if they're bus cards. This
    // keeps the worker out of the way during rapid scrolling.
    size_t cur_index = stack_.index();
    if (cur_index != last_seen_index_) {
        last_seen_index_      = cur_index;
        last_card_change_ms_  = now_ms;
        preload_done_for_index_ = (size_t)-1;
    }
    if (cur_index != preload_done_for_index_ &&
        (now_ms - last_card_change_ms_) >= kPreloadDebounceMs &&
        stack_.size() > 1) {
        preloadNeighbour((cur_index + stack_.size() - 1) % stack_.size());
        preloadNeighbour((cur_index + 1) % stack_.size());
        preload_done_for_index_ = cur_index;
    }
}
