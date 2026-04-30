#include "StatusCard.h"

#include <Arduino.h>

#include "../../display/Display.h"

StatusCard::StatusCard(const AppState& state)
    : state_(state),
      ever_drawn_(false),
      last_drawn_state_(STATE_DISCONNECTED),
      last_drawn_msg_{0},
      last_drawn_live_(false),
      last_recheck_ms_(0) {}

void StatusCard::invalidate() {
    ever_drawn_ = false;
    last_drawn_msg_[0] = 0;
}

void StatusCard::tick(uint32_t now_ms) {
    if (now_ms - last_recheck_ms_ > 1000) {
        last_recheck_ms_ = now_ms;
        // No-op: the live flag is read at render time. The 1s tick is just
        // here so isDirty() flips true once liveness changes due to timeout.
    }
}

bool StatusCard::isDirty() const {
    if (!ever_drawn_) return true;
    if (state_.buddyState() != last_drawn_state_) return true;
    if (strncmp(last_drawn_msg_, state_.status().msg, sizeof(last_drawn_msg_)) != 0) return true;
    if (state_.isLive(millis()) != last_drawn_live_) return true;
    return false;
}

void StatusCard::render(Display& display) {
    auto&               tft    = display.tft();
    const ClaudeStatus& status = state_.status();
    const BuddyState    bs     = state_.buddyState();
    const bool          live   = state_.isLive(millis());

    tft.fillScreen(ST77XX_BLACK);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    const char* name = state_name(bs);
    int16_t  x1, y1; uint16_t tw, th;
    tft.getTextBounds(name, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor((display.width() - (int)tw) / 2, 20);
    tft.print(name);

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(8, 62);
    tft.printf("total %u  run %u  wait %u",
               status.total, status.running, status.waiting);

    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    if (status.msg[0]) {
        tft.setCursor(8, 80);
        tft.printf("%.34s", status.msg);
        if (strlen(status.msg) > 34) {
            tft.setCursor(8, 92);
            tft.printf("%.34s", status.msg + 34);
        }
    }

    tft.setTextColor(live ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
    tft.setCursor(8, 118);
    tft.print(live ? "LIVE  " : "OFFLN ");
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(state_.deviceName());

    last_drawn_state_ = bs;
    strncpy(last_drawn_msg_, status.msg, sizeof(last_drawn_msg_) - 1);
    last_drawn_msg_[sizeof(last_drawn_msg_) - 1] = 0;
    last_drawn_live_ = live;
    ever_drawn_      = true;
}
