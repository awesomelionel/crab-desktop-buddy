#pragma once

#include <stdint.h>

#include "protocol.h"
#include "state.h"

// Owns the snapshot-derived state: latest ClaudeStatus, derived BuddyState,
// time of last received snapshot, and the BLE device name. Render code reads
// it via const reference; the input/networking layer mutates via setters.
class AppState {
public:
    static constexpr uint32_t LIVE_TIMEOUT_MS = 30000;

    AppState();

    // Populate device_name_ from the BT MAC. Safe to call once setup-time.
    void initDeviceName();

    const char*         deviceName() const { return device_name_; }
    const ClaudeStatus& status()     const { return status_; }
    BuddyState          buddyState() const { return buddy_state_; }
    uint32_t            lastSnapshotMs() const { return last_snapshot_ms_; }

    bool isLive(uint32_t now_ms) const {
        return last_snapshot_ms_ != 0 && (now_ms - last_snapshot_ms_) <= LIVE_TIMEOUT_MS;
    }

    // Mutators used by the BLE/input layers.
    ClaudeStatus& mutableStatus() { return status_; }
    void          markSnapshot(uint32_t now_ms) { last_snapshot_ms_ = now_ms; }
    void          setBuddyState(BuddyState s) { buddy_state_ = s; }

private:
    char         device_name_[16];
    ClaudeStatus status_;
    BuddyState   buddy_state_;
    uint32_t     last_snapshot_ms_;
};
