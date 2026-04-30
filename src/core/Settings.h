#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings_model.h"

class EventBus;

// Arduino-side wrapper around lib/settings. Persists to NVS (namespace
// "settings", separate from "claude-buddy" used for Wi-Fi creds), publishes
// EventKind::SettingsChanged on every successful mutation.
class Settings {
public:
    Settings();

    void setEventBus(EventBus* bus) { bus_ = bus; }

    // Loads from NVS; if no record is present, applies defaults seeded with
    // default_name (typically the MAC-derived "Claude-XXXX"). Safe to call
    // once at setup time.
    void begin(const char* default_name);

    const settings::Settings& data() const { return data_; }

    // Applies a device-section update. Returns true on success; on failure,
    // err is populated and in-memory state is unchanged.
    bool applyDevice(const char* name,
                     uint16_t live_timeout_s,
                     uint16_t sleep_timeout_s,
                     char* err, size_t err_len);

    // Applies a cards-section update.
    bool applyCards(uint8_t enabled_mask,
                    const uint8_t* order, uint8_t count,
                    uint8_t boot_card_id,
                    char* err, size_t err_len);

    // Wipe NVS settings namespace and reset to defaults seeded with
    // default_name. Wi-Fi creds (in a separate namespace) are untouched.
    void clearToDefaults(const char* default_name);

private:
    void persist();

    settings::Settings data_;
    EventBus*          bus_ = nullptr;
};
