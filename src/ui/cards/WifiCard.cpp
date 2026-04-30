#include "WifiCard.h"

#include <Arduino.h>
#include <string.h>

#include "../../display/Display.h"

namespace {
constexpr uint32_t SPINNER_TICK_MS = 400;
}

WifiCard::WifiCard(const WifiManager& wifi)
    : wifi_(wifi),
      ever_drawn_(false),
      last_state_(WifiState::BOOT),
      last_ip_(0),
      last_ssid_{0},
      spinner_phase_(0),
      last_spinner_tick_ms_(0),
      now_ms_(0) {}

void WifiCard::invalidate() {
    ever_drawn_ = false;
    last_ssid_[0] = 0;
}

void WifiCard::tick(uint32_t now_ms) {
    now_ms_ = now_ms;
    if (wifi_.state() == WifiState::STA_CONNECTING ||
        wifi_.state() == WifiState::STA_RECONNECT) {
        if (now_ms - last_spinner_tick_ms_ >= SPINNER_TICK_MS) {
            last_spinner_tick_ms_ = now_ms;
            spinner_phase_ = (spinner_phase_ + 1) % 4;
        }
    }
}

bool WifiCard::isDirty() const {
    if (!ever_drawn_) return true;
    if (last_state_ != wifi_.state()) return true;
    if ((uint32_t)wifi_.ip() != last_ip_) return true;
    if (strncmp(last_ssid_, wifi_.ssid(), sizeof(last_ssid_)) != 0) return true;
    if (wifi_.state() == WifiState::STA_CONNECTING ||
        wifi_.state() == WifiState::STA_RECONNECT) {
        // Spinner advance: dirty whenever a tick happened in the last frame.
        if (now_ms_ == last_spinner_tick_ms_) return true;
    }
    return false;
}

void WifiCard::render(Display& display) {
    auto& tft = display.tft();

    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setCursor(8, 6);
    tft.print("Wi-Fi");

    tft.setTextSize(1);

    switch (wifi_.state()) {
        case WifiState::BOOT:
        case WifiState::AP_PROVISIONING: {
            tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
            tft.setCursor(8, 36);
            tft.print("provisioning needed");

            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 56);
            tft.print("AP: ");
            const char* ap = wifi_.ssid();
            tft.print(ap[0] ? ap : "claude-buddy");

            tft.setCursor(8, 70);
            tft.print("URL: http://192.168.4.1");

            tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
            tft.setCursor(8, 100);
            tft.print("connect to AP to setup");
            break;
        }
        case WifiState::STA_CONNECTING:
        case WifiState::STA_RECONNECT: {
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 36);
            tft.print("ssid: ");
            tft.print(wifi_.ssid());

            tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
            tft.setCursor(8, 60);
            tft.print(wifi_.state() == WifiState::STA_RECONNECT ? "reconnecting"
                                                                : "connecting");
            for (uint8_t i = 0; i < spinner_phase_; ++i) tft.print('.');
            break;
        }
        case WifiState::STA_CONNECTED: {
            tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
            tft.setCursor(8, 36);
            tft.print("connected");

            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 56);
            tft.print("ssid: ");
            tft.print(wifi_.ssid());

            tft.setCursor(8, 76);
            tft.print("ip:   ");
            tft.print(wifi_.ip().toString());

            tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
            tft.setCursor(8, 100);
            tft.print("http://claude-buddy.local");
            break;
        }
    }

    last_state_ = wifi_.state();
    last_ip_    = (uint32_t)wifi_.ip();
    strncpy(last_ssid_, wifi_.ssid(), sizeof(last_ssid_) - 1);
    last_ssid_[sizeof(last_ssid_) - 1] = 0;
    ever_drawn_ = true;
}
