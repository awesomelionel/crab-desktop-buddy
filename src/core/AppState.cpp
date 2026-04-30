#include "AppState.h"

#include <Arduino.h>
#include <esp_mac.h>
#include <stdio.h>
#include <string.h>

AppState::AppState()
    : status_{}, buddy_state_(STATE_DISCONNECTED), last_snapshot_ms_(0) {
    strncpy(device_name_, "Claude", sizeof(device_name_) - 1);
    device_name_[sizeof(device_name_) - 1] = 0;
}

void AppState::initDeviceName() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(device_name_, sizeof(device_name_), "Claude-%02X%02X", mac[4], mac[5]);
}
