#pragma once

#include <stdint.h>

#include "../Card.h"
#include "../../core/Settings.h"
#include "../../net/BusFetchService.h"
#include "../../net/WifiManager.h"
#include "bus_arrivals.h"
#include "settings_model.h"

class BusCard : public Card {
public:
    BusCard(uint8_t slot_index,
            const Settings& settings,
            const WifiManager& wifi,
            net::BusFetchService& service);

    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;
    void tick(uint32_t now_ms) override;
    bool handleButton(ButtonEvent ev, uint32_t now_ms) override;
    void onShow() override;
    void onHide() override;

    static constexpr uint8_t  kViewportRows  = 6;
    static constexpr uint32_t kFetchPeriodMs = 30000;
    static constexpr uint32_t kStaleAfterMs  = 90000;
    static constexpr uint32_t kHintShowMs    = 3000;

private:
    enum class DisplayState : uint8_t {
        Loading,
        NoWifi,
        Stale,
        Empty,
        BadCode,
        FetchError,
        Normal,
    };

    DisplayState computeState(uint32_t now_ms) const;
    bool         shouldFetch(uint32_t now_ms) const;
    int          displayedMinute(uint32_t now_ms,
                                 const bus_arrivals::BusServiceArrival& svc) const;

    void renderHeader(class Adafruit_ST7789& tft, DisplayState state);
    void renderRows(class Adafruit_ST7789& tft, uint32_t now_ms);
    void renderCenterMessage(class Adafruit_ST7789& tft,
                             const char* line1, const char* line2);
    void renderScrollHint(class Adafruit_ST7789& tft, uint32_t now_ms);
    void clearBody(class Adafruit_ST7789& tft);

    uint8_t                          slot_;
    const Settings&                  settings_;
    const WifiManager&               wifi_;
    net::BusFetchService&            service_;
    bus_arrivals::BusStopArrivals    data_;
    uint32_t                         last_fetch_attempt_ms_;
    uint32_t                         shown_at_ms_;
    uint32_t                         last_tick_minute_;
    uint8_t                          first_visible_;
    bool                             ever_drawn_;
    bool                             visible_;
    bool                             dirty_;

    // Snapshots for diff-based row repaint.
    bus_arrivals::BusServiceArrival  last_drawn_[kViewportRows];
    int                              last_drawn_minute_[kViewportRows];
    uint8_t                          last_drawn_first_visible_;
    char                             last_drawn_label_[settings::MAX_BUS_LABEL_LEN + 1];
    char                             last_drawn_code_[settings::BUS_STOP_CODE_LEN + 1];
    bool                             last_drawn_wifi_up_;
    DisplayState                     last_drawn_state_;
};
