#include "BleLink.h"

#include <Arduino.h>

#include "../ble_bridge.h"
#include "../core/AppState.h"
#include "../core/EventBus.h"
#include "protocol.h"

BleLink::BleLink(AppState& app)
    : app_(app), line_buf_{}, line_len_(0), line_overflow_(false) {}

void BleLink::begin(const char* device_name) {
    ble_init(device_name);
}

void BleLink::tick(uint32_t now_ms) {
    while (ble_available()) {
        int c = ble_read();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (line_overflow_) {
                Serial.printf("[rx] line overflow (>%u bytes), dropped\n",
                              (unsigned)(LINE_BUF_LEN - 1));
                line_overflow_ = false;
            } else if (line_len_ > 0) {
                line_buf_[line_len_] = 0;
                if (line_buf_[0] == '{') {
                    if (protocol_parse_line(line_buf_, &app_.mutableStatus())) {
                        app_.markSnapshot(now_ms);
                        Serial.printf("[rx] %s\n", line_buf_);
                        if (bus_) bus_->publish(EventKind::SnapshotReceived);
                    }
                }
            }
            line_len_ = 0;
        } else if (line_len_ < LINE_BUF_LEN - 1) {
            line_buf_[line_len_++] = (char)c;
        } else {
            // Past the buffer — keep eating until newline so the next line
            // starts clean. Don't try to parse a truncated payload.
            line_overflow_ = true;
        }
    }
}

bool BleLink::writeLine(const char* line) {
    if (!line) return false;
    bool ok = ble_write_line(line);
    if (ok) {
        Serial.printf("[tx] %s\n", line);
    } else {
        Serial.printf("[tx] dropped (not connected): %s\n", line);
    }
    return ok;
}

bool BleLink::isConnected() const {
    return ble_connected();
}
