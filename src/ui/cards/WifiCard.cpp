#include "WifiCard.h"

#include <Arduino.h>
#include <qrcode.h>
#include <stdio.h>
#include <string.h>

#include "../../display/Display.h"

namespace {
constexpr uint32_t SPINNER_TICK_MS = 400;

// QR layout for the AP_PROVISIONING screen. Version 3 (29x29 modules)
// at scale 3 fits the 240x135 display with text on the left.
constexpr uint8_t  QR_VERSION = 3;
constexpr uint8_t  QR_SCALE   = 3;
constexpr int      QR_X       = 138;  // top-left of the QR (modules)
constexpr int      QR_Y       = 24;
constexpr int      QR_QUIET   = 4;    // module-wide quiet zone

void renderWifiJoinQr(Adafruit_ST7789& tft, const char* ssid) {
    // Standard "Wi-Fi network configuration" URI; T:nopass means open AP.
    // Format: WIFI:T:nopass;S:<ssid>;;
    char payload[80];
    snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;",
             (ssid && ssid[0]) ? ssid : "claude-buddy");

    QRCode qr;
    uint8_t buf[qrcode_getBufferSize(QR_VERSION)];
    qrcode_initText(&qr, buf, QR_VERSION, ECC_LOW, payload);

    // Scanners need a quiet zone of light modules around the QR. Paint a
    // white background that's bigger than the QR by QR_QUIET modules per
    // side, then draw black modules on top.
    int side  = qr.size * QR_SCALE;
    int quiet = QR_QUIET * QR_SCALE;
    tft.fillRect(QR_X - quiet, QR_Y - quiet,
                 side + 2 * quiet, side + 2 * quiet,
                 ST77XX_WHITE);

    for (uint8_t y = 0; y < qr.size; y++) {
        for (uint8_t x = 0; x < qr.size; x++) {
            if (qrcode_getModule(&qr, x, y)) {
                tft.fillRect(QR_X + x * QR_SCALE,
                             QR_Y + y * QR_SCALE,
                             QR_SCALE, QR_SCALE,
                             ST77XX_BLACK);
            }
        }
    }
}
}  // namespace

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
            const char* ap = wifi_.ssid();
            const char* ap_label = (ap && ap[0]) ? ap : "claude-buddy";

            tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
            tft.setCursor(8, 32);
            tft.print("scan to join");

            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 56);
            tft.print("or AP:");
            tft.setCursor(8, 70);
            tft.print(ap_label);

            tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
            tft.setCursor(8, 96);
            tft.print("then go to:");
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 110);
            tft.print("http://192.168.4.1");

            renderWifiJoinQr(tft, ap_label);
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
            tft.print("http://");
            tft.print(wifi_.ip().toString());
            break;
        }
    }

    last_state_ = wifi_.state();
    last_ip_    = (uint32_t)wifi_.ip();
    strncpy(last_ssid_, wifi_.ssid(), sizeof(last_ssid_) - 1);
    last_ssid_[sizeof(last_ssid_) - 1] = 0;
    ever_drawn_ = true;
}
