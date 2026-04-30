#pragma once

#include <stddef.h>
#include <stdint.h>

class AppState;
class EventBus;

// Wraps the C-style ble_bridge module: drains incoming bytes, accumulates
// them into a 4KB line buffer (per REFERENCE.md), parses each line into
// AppState, and exposes writeLine() for outgoing decisions.
class BleLink {
public:
    explicit BleLink(AppState& app);

    void begin(const char* device_name);

    // Optional: publishes EventKind::SnapshotReceived once per successful
    // line parse.
    void setEventBus(EventBus* bus) { bus_ = bus; }

    // Drain available bytes; for each completed JSON line, parse into
    // appState and stamp markSnapshot(now_ms).
    void tick(uint32_t now_ms);

    // Send a single newline-terminated line. Returns true on success.
    bool writeLine(const char* line);

    bool isConnected() const;

private:
    AppState& app_;
    EventBus* bus_ = nullptr;

    // Snapshot lines can carry an entries[] transcript; REFERENCE.md caps
    // event payloads at 4KB. 4096 + 1 trailing null = max wire size.
    static constexpr size_t LINE_BUF_LEN = 4097;
    char    line_buf_[LINE_BUF_LEN];
    size_t  line_len_;
    bool    line_overflow_;
};
