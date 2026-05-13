#pragma once

#include <stdint.h>

#include "protocol.h"
#include "state.h"

class Settings;

// Cached battery snapshot. Populated by BatteryMonitor's tick() in
// main.cpp and read by StatusCard. percent == 0xFF means "not yet
// sampled" / no fuel gauge present, in which case the widget hides.
struct BatteryStatus {
    uint8_t percent  = 0xFF;
    bool    charging = false;
    bool    present  = false;
};

// Owns the snapshot-derived state: latest ClaudeStatus, derived BuddyState,
// time of last received snapshot, and the BLE device name. Render code reads
// it via const reference; the input/networking layer mutates via setters.
//
// Liveness threshold comes from Settings (settable from the web UI). The
// device_name field is the MAC-derived seed; the user-visible name lives on
// Settings and is read via deviceName().
class AppState {
public:
    AppState();

    // Populate macDeviceName_ from the BT MAC ("Claude-XXXX"). Safe to call
    // once at setup time.
    void initMacDeviceName();

    // Bind Settings so isLive() and deviceName() can read live values.
    void setSettings(const Settings* settings) { settings_ = settings; }

    // The MAC-derived seed name; used by Settings::begin() as the default
    // device name on first boot.
    const char* macDeviceName() const { return mac_device_name_; }

    // The user-visible device name. Falls back to the MAC-derived seed if
    // Settings hasn't been bound yet.
    const char* deviceName() const;

    const ClaudeStatus& status()     const { return status_; }
    BuddyState          buddyState() const { return buddy_state_; }
    uint32_t            lastSnapshotMs() const { return last_snapshot_ms_; }
    const BatteryStatus& battery()   const { return battery_; }
    const Settings*     settings()   const { return settings_; }

    bool isLive(uint32_t now_ms) const;

    // Mutators used by the BLE/input layers.
    ClaudeStatus& mutableStatus() { return status_; }
    void          markSnapshot(uint32_t now_ms) { last_snapshot_ms_ = now_ms; }
    void          setBuddyState(BuddyState s) { buddy_state_ = s; }
    void          setBattery(const BatteryStatus& b) { battery_ = b; }

private:
    char            mac_device_name_[16];
    ClaudeStatus    status_;
    BuddyState      buddy_state_;
    uint32_t        last_snapshot_ms_;
    BatteryStatus   battery_;
    const Settings* settings_ = nullptr;
};
