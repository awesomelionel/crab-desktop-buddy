#pragma once

#include <stddef.h>

// Tiny NVS-backed key/value store for Wi-Fi credentials.
//
// Persists to the "claude-buddy" namespace in the ESP32 Preferences (NVS).
// All getters write into caller-provided buffers and return the number of
// bytes written (excluding the trailing null), or 0 if the key isn't set
// or the buffer is null/zero-length.
class ConfigStore {
public:
    static constexpr size_t SSID_MAX_LEN = 32;     // Wi-Fi SSID max per 802.11
    static constexpr size_t PASS_MAX_LEN = 64;     // WPA2 PSK max

    void begin();

    bool   hasCreds() const;
    size_t getSsid(char* buf, size_t buf_len) const;
    size_t getPassword(char* buf, size_t buf_len) const;
    bool   setCreds(const char* ssid, const char* password);
    void   clear();
};
