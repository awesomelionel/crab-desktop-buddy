#include "WifiManager.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "../core/EventBus.h"

namespace {
constexpr uint32_t INITIAL_RECONNECT_MS = 2000;
constexpr uint32_t MAX_RECONNECT_MS     = 30000;
constexpr const char* AP_SSID_PREFIX    = "claude-buddy-";
// Fixed mDNS hostname: the device is reachable at claude.local once on
// Wi-Fi. Kept fixed (not derived from the user-settable device name) so it
// needs no sanitising, never goes stale, and matches the docs.
constexpr const char* MDNS_HOSTNAME     = "claude";

void buildApSsid(char* out, size_t out_len) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, out_len, "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
}
}  // namespace

WifiManager::WifiManager(ConfigStore& store)
    : store_(store),
      state_(WifiState::BOOT),
      ssid_{0},
      password_{0},
      reconnect_at_ms_(0),
      reconnect_delay_ms_(INITIAL_RECONNECT_MS),
      reconnect_attempts_(0) {}

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
    state_ = WifiState::AP_PROVISIONING;

    // Tear down the mDNS responder — its STA-side hostname/IP is no longer
    // valid. AP-mode access goes through the captive portal by IP anyway.
    MDNS.end();

    char ap_ssid[32];
    buildApSsid(ap_ssid, sizeof(ap_ssid));
    strncpy(ssid_, ap_ssid, sizeof(ssid_) - 1);
    ssid_[sizeof(ssid_) - 1] = 0;

    // WIFI_AP_STA (instead of WIFI_AP) lets us scan for nearby networks
    // while the AP is up — the captive portal needs the SSID list.
    WiFi.mode(WIFI_AP_STA);
    // Open AP: passphrase=nullptr removes the WPA2 requirement so first-time
    // users can join with one tap. The captive portal is the only thing
    // reachable on the AP, and the AP only exists until creds are saved.
    if (!WiFi.softAP(ap_ssid, nullptr)) {
        Serial.println("[wifi] softAP() failed");
        return;
    }
    Serial.printf("[wifi] AP_PROVISIONING ssid=%s (open) ip=%s\n",
                  ap_ssid, WiFi.softAPIP().toString().c_str());

    // Kick off an initial scan so the dropdown is populated by the time
    // the user joins the AP and loads the portal.
    startScan();
}

void WifiManager::startScan() {
    int s = WiFi.scanComplete();
    if (s == WIFI_SCAN_RUNNING) return;
    // Free the previous result block before starting a new scan.
    if (s >= 0) WiFi.scanDelete();
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    Serial.println("[wifi] scan started");
}

bool WifiManager::scanRunning() const {
    return WiFi.scanComplete() == WIFI_SCAN_RUNNING;
}

int WifiManager::scanResultCount() const {
    int s = WiFi.scanComplete();
    return s >= 0 ? s : -1;
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
    reconnect_attempts_++;
    if (reconnect_attempts_ >= MAX_RECONNECT_ATTEMPTS) {
        Serial.printf("[wifi] %u failed reconnects → AP_PROVISIONING\n",
                      (unsigned)reconnect_attempts_);
        reconnect_attempts_ = 0;
        reconnect_delay_ms_ = INITIAL_RECONNECT_MS;
        WiFi.disconnect(false, false);
        if (was_connected && bus_) bus_->publish(EventKind::WifiDisconnected);
        enterApProvisioning();
        return;
    }

    state_ = WifiState::STA_RECONNECT;
    reconnect_at_ms_ = now_ms + reconnect_delay_ms_;
    Serial.printf("[wifi] STA_RECONNECT in %u ms (attempt %u/%u)\n",
                  (unsigned)reconnect_delay_ms_,
                  (unsigned)reconnect_attempts_,
                  (unsigned)MAX_RECONNECT_ATTEMPTS);
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
    reconnect_attempts_ = 0;
    Serial.printf("[wifi] STA_CONNECTED ip=%s\n", WiFi.localIP().toString().c_str());

    // (Re)start the mDNS responder so the web UI is reachable at
    // <hostname>.local. end()+begin() handles the IP changing across
    // reconnects; only the one HTTP service is advertised.
    MDNS.end();
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[wifi] mDNS up at %s.local\n", MDNS_HOSTNAME);
    } else {
        Serial.println("[wifi] mDNS begin failed");
    }

    if (!was_connected && bus_) bus_->publish(EventKind::WifiConnected);
}
