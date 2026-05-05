#include "Settings.h"

#include <Preferences.h>
#include <string.h>

#include "EventBus.h"

namespace {
constexpr const char* NS  = "settings";
constexpr const char* KEY = "v1";    // single-blob key — version it for future migrations
}  // namespace

Settings::Settings() {
    settings::setDefaults(data_, "Claude");
}

void Settings::begin(const char* default_name) {
    settings::setDefaults(data_, default_name);

    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) return;

    settings::Settings stored = data_;
    size_t n = p.getBytesLength(KEY);
    if (n == sizeof(settings::Settings)) {
        p.getBytes(KEY, &stored, sizeof(stored));
        // Validate before adopting; if the stored blob is broken (old
        // version, corruption), fall back to defaults silently.
        char err[64] = {};
        if (settings::validate(stored, err, sizeof(err))) {
            data_ = stored;
        }
    }
    p.end();
}

bool Settings::applyDevice(const char* name,
                           uint16_t live_timeout_s,
                           uint16_t sleep_timeout_s,
                           char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyDeviceFields(next, name, live_timeout_s, sleep_timeout_s,
                                     err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}

bool Settings::applyCards(uint8_t enabled_mask,
                          const uint8_t* order, uint8_t count,
                          uint8_t boot_card_id,
                          char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyCardsFields(next, enabled_mask, order, count, boot_card_id,
                                    err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}

bool Settings::applyBacklight(uint16_t dim_timeout_s,
                              uint8_t  dim_level_pct,
                              uint8_t  full_level_pct,
                              char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyBacklightFields(next, dim_timeout_s, dim_level_pct,
                                        full_level_pct, err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}

void Settings::clearToDefaults(const char* default_name) {
    Preferences p;
    if (p.begin(NS, /*readOnly=*/false)) {
        p.remove(KEY);
        p.end();
    }
    settings::setDefaults(data_, default_name);
    if (bus_) bus_->publish(EventKind::SettingsChanged);
}

void Settings::persist() {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false)) return;
    p.putBytes(KEY, &data_, sizeof(data_));
    p.end();
}
