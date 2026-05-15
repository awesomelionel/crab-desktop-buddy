#pragma once

#include <stdint.h>

#include "CardStack.h"
#include "../core/AppState.h"
#include "../core/EventBus.h"
#include "../core/Settings.h"
#include "../input/InputRouter.h"
#include "../net/BusFetchService.h"
#include "../net/WifiManager.h"
#include "prompt_ui.h"
#include "settings_model.h"
#include "cards/BusCard.h"
#include "cards/EyesCard.h"
#include "cards/FactoryResetCard.h"
#include "cards/PromptCard.h"
#include "cards/StatusCard.h"
#include "cards/UpdatingCard.h"
#include "cards/WifiCard.h"

class Display;
class BleLink;
class FactoryResetCoordinator;
class UpdateManager;

// Owns the card carousel + prompt overlay, listens to EventBus, drains
// outgoing PromptUi decisions to BleLink, and runs the backlight manager
// (FULL → DIM → OFF backlight management driven by input idleness +
// meaningful EventBus wakes).
class CardController {
public:
    CardController(AppState& app, EventBus& bus, WifiManager& wifi,
                   PromptUi& prompt, BleLink& ble, Settings& settings,
                   UpdateManager& um, FactoryResetCoordinator& fr,
                   net::BusFetchService& service);

    // The sleep manager queries last-input time through the router; bind
    // after construction so the InputRouter (which needs stack()) can be
    // constructed first. Without it, sleep is effectively disabled.
    void setInputRouter(InputRouter* router) { input_ = router; }

    void begin();
    void tick(uint32_t now_ms, Display& display);

    CardStack& stack() { return stack_; }

private:
    void rebuildStack();
    void runBacklightManager(uint32_t now_ms, Display& display);

    AppState&    app_;
    EventBus&    bus_;
    WifiManager& wifi_;
    PromptUi&    prompt_;
    BleLink&     ble_;
    Settings&    settings_;
    InputRouter* input_ = nullptr;
    uint32_t     last_activity_ms_ = 0;

    UpdateManager&           um_;
    FactoryResetCoordinator& fr_;
    net::BusFetchService&    service_;

    StatusCard       status_card_;
    EyesCard         eyes_card_;
    WifiCard         wifi_card_;
    PromptCard       prompt_card_;
    UpdatingCard     updating_card_;
    FactoryResetCard factory_reset_card_;
    BusCard          bus_card_0_;
    BusCard          bus_card_1_;
    BusCard          bus_card_2_;
    BusCard          bus_card_3_;
    CardStack        stack_;

    bool         prompt_visible_;
    // Cached settings snapshot so rebuildStack only fires on actual change.
    uint8_t      last_cards_mask_;
    uint8_t      last_cards_order_count_;
    uint8_t      last_cards_order_[settings::CARD_COUNT];
    uint8_t      last_boot_card_;
    bool         applied_boot_card_;
};
