#include "BusCard.h"

#include <Arduino.h>
#include <string.h>

#include "../../display/Display.h"
#include "bus_fetch_logic.h"

#include <stdio.h>

namespace {

constexpr int kHeaderY     = 0;
constexpr int kHeaderH     = 14;
constexpr int kDividerY    = 14;
constexpr int kBodyTopY    = 18;
// 6 rows × 19 px pitch fills 114 px of the 117 px body, leaving 3 px at
// the bottom. Each row exceeds the 16 px size-2 character height by 3 px,
// giving ~1 px above and 2 px below for visual breathing room.
constexpr int kRowH        = 19;
constexpr int kBodyW       = 240;
constexpr int kBodyH       = 135 - kBodyTopY;

// Size-2 font is 10 px per char; a 5-char ServiceNo at kCol_Service=8 ends
// at x=58. Push the dot past that with a small gap so the longest service
// numbers don't bump into the load indicator.
constexpr int kCol_Service = 8;
constexpr int kCol_Dot     = 72;
constexpr int kCol_Eta     = 110;
constexpr int kEtaW        = 80;
constexpr int kCol_Type    = 210;   // size-2 "DD" is 20 px wide; ends at x=230, before scroll bar at x=234

constexpr int kScrollX     = 234;
constexpr int kScrollW     = 4;
constexpr int kScrollY     = kBodyTopY + 2;
constexpr int kScrollH     = kBodyH - 4;

constexpr uint16_t kColBg       = ST77XX_BLACK;
constexpr uint16_t kColFg       = ST77XX_WHITE;
constexpr uint16_t kColDim      = 0x7BEF;
constexpr uint16_t kColDivider  = 0x39E7;
constexpr uint16_t kColLoadSEA  = ST77XX_GREEN;
constexpr uint16_t kColLoadSDA  = 0xFD20;            // amber
constexpr uint16_t kColLoadLSD  = ST77XX_RED;
constexpr uint16_t kColEtaArr   = ST77XX_YELLOW;
constexpr uint16_t kColType_DD  = ST77XX_CYAN;
constexpr uint16_t kColType_SD  = 0xC618;            // light grey
constexpr uint16_t kColType_BD  = ST77XX_MAGENTA;
constexpr uint16_t kColWarn     = 0xFD20;

void formatHeaderLabel(const settings::BusStopSlot& slot,
                       char* out, size_t out_len) {
    if (slot.label[0] != '\0') {
        snprintf(out, out_len, "%s", slot.label);
    } else {
        snprintf(out, out_len, "Stop %s", slot.code);
    }
}

uint16_t loadColor(bus_arrivals::BusLoad l) {
    switch (l) {
        case bus_arrivals::LOAD_SEA: return kColLoadSEA;
        case bus_arrivals::LOAD_SDA: return kColLoadSDA;
        case bus_arrivals::LOAD_LSD: return kColLoadLSD;
        default:                     return kColDim;
    }
}

uint16_t typeColor(bus_arrivals::BusType t) {
    switch (t) {
        case bus_arrivals::TYPE_DD: return kColType_DD;
        case bus_arrivals::TYPE_SD: return kColType_SD;
        case bus_arrivals::TYPE_BD: return kColType_BD;
        default:                    return kColDim;
    }
}

const char* typeLabel(bus_arrivals::BusType t) {
    switch (t) {
        case bus_arrivals::TYPE_DD: return "DD";
        case bus_arrivals::TYPE_SD: return "SD";
        case bus_arrivals::TYPE_BD: return "BD";
        default:                    return "--";
    }
}

}  // namespace

BusCard::BusCard(uint8_t slot_index,
                 const Settings& settings,
                 const WifiManager& wifi,
                 net::BusFetchService& service)
    : slot_(slot_index),
      settings_(settings),
      wifi_(wifi),
      service_(service),
      data_{},
      last_fetch_attempt_ms_(0),
      shown_at_ms_(0),
      last_tick_minute_(0),
      first_visible_(0),
      ever_drawn_(false),
      visible_(false),
      dirty_(true),
      last_drawn_first_visible_(0),
      last_drawn_label_{0},
      last_drawn_code_{0},
      last_drawn_wifi_up_(false),
      last_drawn_state_(DisplayState::Loading) {
    for (uint8_t i = 0; i < kViewportRows; ++i) {
        last_drawn_[i] = {};
        last_drawn_minute_[i] = INT32_MIN;
    }
}

void BusCard::invalidate() {
    ever_drawn_ = false;
    dirty_      = true;
    last_drawn_first_visible_ = 0xFF;
    last_drawn_state_         = DisplayState::Loading;
    for (uint8_t i = 0; i < kViewportRows; ++i) {
        last_drawn_[i] = {};
        last_drawn_minute_[i] = INT32_MIN;
    }
    last_drawn_label_[0] = '\xFF';
    last_drawn_code_[0]  = '\xFF';
    last_drawn_wifi_up_  = !wifi_.isConnected();   // force flip
}

bool BusCard::isDirty() const {
    return dirty_ || !ever_drawn_;
}

void BusCard::onShow() {
    visible_ = true;
    shown_at_ms_ = millis();
    dirty_ = true;

    // Pull any preloaded staged result into data_ before the first render
    // so the card lands on real data instead of "Loading...".
    if (service_.takeResult(slot_, data_)) {
        // Preload counts as the most recent fetch attempt — preserve the
        // 30 s refresh schedule instead of immediately re-fetching.
        last_fetch_attempt_ms_ = data_.last_fetch_success_ms;
        last_tick_minute_ = 0;
    } else {
        // No preload — schedule an immediate HIGH request on next tick.
        last_fetch_attempt_ms_ = 0;
    }
}

void BusCard::onHide() {
    visible_ = false;
}

void BusCard::tick(uint32_t now_ms) {
    if (!visible_) return;

    if (!wifi_.isConnected()) {
        if (last_drawn_wifi_up_) dirty_ = true;
        return;
    }

    // Always poll the service — picks up a 30 s refresh or a late-arriving
    // result for a previously-submitted request.
    if (service_.takeResult(slot_, data_)) {
        dirty_ = true;
        last_tick_minute_ = 0;
        return;
    }

    if (shouldFetch(now_ms)) {
        const char* code = settings_.data().bus_stops[slot_].code;
        if (code[0] != '\0') {
            service_.request(slot_, code,
                             bus_fetch_logic::FetchPriority::HIGH_PRIO);
        }
        last_fetch_attempt_ms_ = now_ms;
        // Don't mark dirty yet — display state hasn't changed. The next
        // tick that picks up the staged result will mark dirty.
        return;
    }

    if (data_.valid) {
        uint32_t minute = (now_ms - data_.fetched_at_ms) / 60000;
        if (minute != last_tick_minute_) {
            last_tick_minute_ = minute;
            dirty_ = true;
        }
    }
}

bool BusCard::shouldFetch(uint32_t now_ms) const {
    if (last_fetch_attempt_ms_ == 0) return true;
    return (now_ms - last_fetch_attempt_ms_) >= kFetchPeriodMs;
}

bool BusCard::handleButton(ButtonEvent ev, uint32_t now_ms) {
    (void)now_ms;
    if (ev == BTN_CENTER && data_.service_count > kViewportRows) {
        first_visible_ = (uint8_t)((first_visible_ + 1) % data_.service_count);
        dirty_ = true;
        return true;
    }
    return false;
}

int BusCard::displayedMinute(uint32_t now_ms,
                             const bus_arrivals::BusServiceArrival& svc) const {
    if (svc.eta_seconds_at_fetch == INT32_MIN) return INT32_MIN;
    int32_t elapsed = (int32_t)((now_ms - data_.fetched_at_ms) / 1000);
    int32_t remaining = svc.eta_seconds_at_fetch - elapsed;
    if (remaining <= 0) return 0;
    return (int)(remaining / 60);
}

BusCard::DisplayState BusCard::computeState(uint32_t now_ms) const {
    if (!wifi_.isConnected()) return DisplayState::NoWifi;
    if (!data_.valid) {
        if (data_.last_error[0] == '\0') return DisplayState::Loading;
        return DisplayState::FetchError;
    }
    // valid == true.
    if (data_.last_error[0] != '\0') {
        if ((now_ms - data_.last_fetch_success_ms) > kStaleAfterMs) {
            return DisplayState::Stale;
        }
    }
    if (data_.service_count == 0) return DisplayState::Empty;
    return DisplayState::Normal;
}

void BusCard::renderHeader(Adafruit_ST7789& tft, DisplayState state) {
    char label[32];
    formatHeaderLabel(settings_.data().bus_stops[slot_], label, sizeof(label));

    tft.fillRect(0, kHeaderY, 240, kHeaderH, kColBg);
    tft.setTextColor(kColFg, kColBg);
    tft.setTextSize(1);
    tft.setCursor(8, 4);
    tft.print(label);

    if (state == DisplayState::Stale) {
        // Draw a warning glyph on the right side.
        tft.setTextColor(kColWarn, kColBg);
        tft.setCursor(220, 4);
        tft.print("!");
    }

    tft.drawFastHLine(0, kDividerY, 240, kColDivider);
}

void BusCard::clearBody(Adafruit_ST7789& tft) {
    tft.fillRect(0, kBodyTopY, kBodyW, kBodyH, kColBg);
}

void BusCard::renderCenterMessage(Adafruit_ST7789& tft,
                                  const char* line1, const char* line2) {
    clearBody(tft);
    tft.setTextSize(2);
    tft.setTextColor(kColFg, kColBg);
    int16_t x1, y1; uint16_t w1, h1;
    tft.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    int x = (240 - (int)w1) / 2;
    int y = kBodyTopY + (kBodyH / 2) - (int)h1 - 2;
    tft.setCursor(x, y);
    tft.print(line1);
    if (line2 && line2[0]) {
        tft.setTextSize(1);
        tft.setTextColor(kColDim, kColBg);
        int16_t x2, y2; uint16_t w2, h2;
        tft.getTextBounds(line2, 0, 0, &x2, &y2, &w2, &h2);
        int x_l2 = (240 - (int)w2) / 2;
        tft.setCursor(x_l2, y + (int)h1 + 6);
        tft.print(line2);
    }
}

void BusCard::renderRows(Adafruit_ST7789& tft, uint32_t now_ms) {
    const bool stale = (data_.last_error[0] != '\0');
    const uint16_t fg = stale ? kColDim : kColFg;

    uint8_t total = data_.service_count;
    if (total == 0) { clearBody(tft); return; }

    bool full_rows_repaint =
        first_visible_ != last_drawn_first_visible_ ||
        (last_drawn_state_ != DisplayState::Normal &&
         last_drawn_state_ != DisplayState::Stale);

    if (full_rows_repaint) {
        clearBody(tft);
    }

    for (uint8_t i = 0; i < kViewportRows; ++i) {
        int row_y = kBodyTopY + (int)i * kRowH;
        uint8_t idx = (uint8_t)((first_visible_ + i) % (total > 0 ? total : 1));
        if (i >= total) {
            // Empty viewport slot: repaint to background.
            tft.fillRect(0, row_y, kBodyW - kScrollW - 4, kRowH, kColBg);
            last_drawn_[i] = {};
            last_drawn_minute_[i] = INT32_MIN;
            continue;
        }

        const auto& svc = data_.services[idx];
        int minute = displayedMinute(now_ms, svc);

        bool row_changed =
            full_rows_repaint ||
            strncmp(svc.service_no, last_drawn_[i].service_no,
                    sizeof(svc.service_no)) != 0 ||
            svc.load != last_drawn_[i].load ||
            svc.type != last_drawn_[i].type;

        bool minute_changed = minute != last_drawn_minute_[i];

        // Size-2 text (16 px tall) inside a 19 px row needs +1 px y offset
        // to leave roughly equal padding above and below.
        const int text_y = row_y + 1;

        if (row_changed) {
            // Repaint the whole row band.
            tft.fillRect(0, row_y, kBodyW - kScrollW - 4, kRowH, kColBg);

            // Service number — size 2.
            tft.setTextSize(2);
            tft.setTextColor(fg, kColBg);
            tft.setCursor(kCol_Service, text_y);
            tft.print(svc.service_no);

            // Load dot — colour conveys the SEA/SDA/LSD meaning on its own;
            // no text label needed.
            tft.fillCircle(kCol_Dot + 5, row_y + 9, 5, loadColor(svc.load));

            // Type tag — size 2 to match the service number / ETA. The
            // size-1 version was hard to read at arm's length.
            tft.setTextSize(2);
            tft.setTextColor(stale ? kColDim : typeColor(svc.type), kColBg);
            tft.setCursor(kCol_Type, text_y);
            tft.print(typeLabel(svc.type));

            last_drawn_[i] = svc;
        }

        if (row_changed || minute_changed) {
            // ETA cell only.
            tft.fillRect(kCol_Eta, row_y, kEtaW, kRowH, kColBg);
            tft.setTextSize(2);
            if (minute == INT32_MIN) {
                tft.setTextColor(kColDim, kColBg);
                tft.setCursor(kCol_Eta, text_y);
                tft.print("--");
            } else if (minute <= 0) {
                tft.setTextColor(kColEtaArr, kColBg);
                tft.setCursor(kCol_Eta, text_y);
                tft.print("Arr");
            } else if (minute >= 60) {
                tft.setTextColor(kColDim, kColBg);
                tft.setCursor(kCol_Eta, text_y);
                tft.print("60+");
            } else {
                tft.setTextColor(fg, kColBg);
                char m[8];
                snprintf(m, sizeof(m), "%dm", minute);
                tft.setCursor(kCol_Eta, text_y);
                tft.print(m);
            }
            last_drawn_minute_[i] = minute;
        }
    }

    // Scroll bar.
    if (total > kViewportRows) {
        // Track.
        tft.fillRect(kScrollX, kScrollY, kScrollW, kScrollH, kColBg);
        tft.drawRect(kScrollX, kScrollY, kScrollW, kScrollH, kColDivider);
        // Thumb.
        int thumb_h = (kScrollH * (int)kViewportRows) / (int)total;
        if (thumb_h < 4) thumb_h = 4;
        int thumb_y = kScrollY + (kScrollH * (int)first_visible_) / (int)total;
        tft.fillRect(kScrollX + 1, thumb_y + 1,
                     kScrollW - 2, thumb_h - 2, kColFg);
    } else {
        tft.fillRect(kScrollX, kScrollY, kScrollW, kScrollH, kColBg);
    }

    last_drawn_first_visible_ = first_visible_;
}

void BusCard::renderScrollHint(Adafruit_ST7789& tft, uint32_t now_ms) {
    if (data_.service_count <= kViewportRows) return;
    if (first_visible_ != 0) return;
    uint32_t shown_for = now_ms - shown_at_ms_;
    if (shown_for >= kHintShowMs) {
        // Erase the hint band on the bottom row if we previously drew it.
        // (Safe to fillRect even if nothing was there — paints bg over bg.)
        tft.fillRect(0, 135 - 10, 240, 10, kColBg);
        return;
    }
    tft.setTextSize(1);
    tft.setTextColor(kColDim, kColBg);
    const char* hint = "press [O] to scroll";
    int16_t x1, y1; uint16_t w1, h1;
    tft.getTextBounds(hint, 0, 0, &x1, &y1, &w1, &h1);
    int x = (240 - (int)w1) / 2;
    tft.setCursor(x, 135 - 9);
    tft.print(hint);
    // Stay dirty so we re-render and erase the hint after kHintShowMs.
    dirty_ = true;
}

void BusCard::render(Display& display) {
    Adafruit_ST7789& tft = display.tft();
    uint32_t now_ms = millis();

    DisplayState state = computeState(now_ms);

    char curr_label[32];
    formatHeaderLabel(settings_.data().bus_stops[slot_], curr_label, sizeof(curr_label));

    bool full_clear = !ever_drawn_
        || state != last_drawn_state_
        || strncmp(curr_label, last_drawn_label_, sizeof(last_drawn_label_)) != 0
        || strncmp(settings_.data().bus_stops[slot_].code,
                   last_drawn_code_, sizeof(last_drawn_code_)) != 0
        || wifi_.isConnected() != last_drawn_wifi_up_;

    if (full_clear) {
        tft.fillScreen(kColBg);
    }

    renderHeader(tft, state);

    char codebuf[16];
    snprintf(codebuf, sizeof(codebuf), "%s",
             settings_.data().bus_stops[slot_].code);

    switch (state) {
        case DisplayState::Loading: {
            char ip_line[32];
            snprintf(ip_line, sizeof(ip_line), "Wi-Fi %s",
                     wifi_.ip().toString().c_str());
            renderCenterMessage(tft, "Loading...", ip_line);
            break;
        }
        case DisplayState::NoWifi:
            renderCenterMessage(tft, "No Wi-Fi", "waiting to reconnect");
            break;
        case DisplayState::Empty: {
            char l[32];
            snprintf(l, sizeof(l), "for stop %s", codebuf);
            renderCenterMessage(tft, "No services found", l);
            break;
        }
        case DisplayState::BadCode: {
            char l[40];
            snprintf(l, sizeof(l), "Stop %s unknown", codebuf);
            renderCenterMessage(tft, l, "check the web UI");
            break;
        }
        case DisplayState::FetchError:
            renderCenterMessage(tft, "Bus times unavailable",
                                data_.last_error);
            break;
        case DisplayState::Normal:
        case DisplayState::Stale:
            renderRows(tft, now_ms);
            renderScrollHint(tft, now_ms);
            break;
    }

    // Snapshot for next-render diff.
    strncpy(last_drawn_label_, curr_label, sizeof(last_drawn_label_) - 1);
    last_drawn_label_[sizeof(last_drawn_label_) - 1] = '\0';
    strncpy(last_drawn_code_, codebuf, sizeof(last_drawn_code_) - 1);
    last_drawn_code_[sizeof(last_drawn_code_) - 1] = '\0';
    last_drawn_wifi_up_ = wifi_.isConnected();
    last_drawn_state_   = state;

    dirty_      = false;
    ever_drawn_ = true;
}
