#include "WifiManager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "../core/EventBus.h"

namespace {
constexpr uint32_t INITIAL_RECONNECT_MS = 2000;
constexpr uint32_t MAX_RECONNECT_MS     = 30000;
constexpr const char* AP_SSID_PREFIX    = "claude-buddy-";
constexpr const char* AP_PASSWORD       = "claudebuddy";  // 8+ chars; WPA2 minimum
}  // namespace

WifiManager::WifiManager(ConfigStore& store)
    : store_(store),
      state_(WifiState::BOOT),
      ssid_{0},
      password_{0},
      reconnect_at_ms_(0),
      reconnect_delay_ms_(INITIAL_RECONNECT_MS) {}

void WifiManager::begin() {
    WiFi.onEvent([this](WiFiEvent_t ev, WiFiEventInfo_t /*info*/) {
        switch (ev) {
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                enterStaConnected();
                break;
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                if (state_ == WifiState::STA_CONNECTED ||
                    state_ == WifiState::STA_CONNECTING) {
                    enterStaReconnect(millis());
                }
                break;
            default:
                break;
        }
    });

    if (store_.hasCreds()) {
        store_.getSsid(ssid_, sizeof(ssid_));
        store_.getPassword(password_, sizeof(password_));
        enterStaConnecting(millis());
    } else {
        enterApProvisioning();
    }
}

void WifiManager::tick(uint32_t now_ms) {
    switch (state_) {
        case WifiState::STA_RECONNECT:
            if ((int32_t)(now_ms - reconnect_at_ms_) >= 0) {
                enterStaConnecting(now_ms);
            }
            break;
        default:
            break;
    }
}

void WifiManager::restart() {
    WiFi.disconnect(true, true);
    if (store_.hasCreds()) {
        store_.getSsid(ssid_, sizeof(ssid_));
        store_.getPassword(password_, sizeof(password_));
        reconnect_delay_ms_ = INITIAL_RECONNECT_MS;
        enterStaConnecting(millis());
    } else {
        enterApProvisioning();
    }
}

const char* WifiManager::stateName() const {
    switch (state_) {
        case WifiState::BOOT:            return "BOOT";
        case WifiState::AP_PROVISIONING: return "AP_PROVISIONING";
        case WifiState::STA_CONNECTING:  return "STA_CONNECTING";
        case WifiState::STA_CONNECTED:   return "STA_CONNECTED";
        case WifiState::STA_RECONNECT:   return "STA_RECONNECT";
    }
    return "?";
}

IPAddress WifiManager::ip() const {
    if (state_ == WifiState::AP_PROVISIONING) return WiFi.softAPIP();
    return WiFi.localIP();
}

bool WifiManager::isConnected() const {
    return state_ == WifiState::STA_CONNECTED && WiFi.localIP() != IPAddress((uint32_t)0);
}

void WifiManager::enterApProvisioning() {
    // Filled in for real in step 5; here we just mark the state so the
    // WifiCard can render the "no creds" hint and STA-only flows are
    // exercised first.
    state_ = WifiState::AP_PROVISIONING;
    ssid_[0] = 0;
    Serial.println("[wifi] AP_PROVISIONING (no creds, AP not yet started)");
}

void WifiManager::enterStaConnecting(uint32_t now_ms) {
    state_ = WifiState::STA_CONNECTING;
    Serial.printf("[wifi] STA_CONNECTING ssid=%s\n", ssid_);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);  // we manage retries explicitly
    WiFi.begin(ssid_, password_);
    (void)now_ms;
}

void WifiManager::enterStaReconnect(uint32_t now_ms) {
    bool was_connected = (state_ == WifiState::STA_CONNECTED);
    state_ = WifiState::STA_RECONNECT;
    reconnect_at_ms_ = now_ms + reconnect_delay_ms_;
    Serial.printf("[wifi] STA_RECONNECT in %u ms\n", (unsigned)reconnect_delay_ms_);
    reconnect_delay_ms_ = reconnect_delay_ms_ < MAX_RECONNECT_MS
                          ? reconnect_delay_ms_ * 2
                          : MAX_RECONNECT_MS;
    WiFi.disconnect(false, false);
    if (was_connected && bus_) bus_->publish(EventKind::WifiDisconnected);
}

void WifiManager::enterStaConnected() {
    bool was_connected = (state_ == WifiState::STA_CONNECTED);
    state_ = WifiState::STA_CONNECTED;
    reconnect_delay_ms_ = INITIAL_RECONNECT_MS;
    Serial.printf("[wifi] STA_CONNECTED ip=%s\n", WiFi.localIP().toString().c_str());
    if (!was_connected && bus_) bus_->publish(EventKind::WifiConnected);
}
