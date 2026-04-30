#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

// Tiny synchronous pub/sub. Subscribers run inline on publish(). No
// queueing, no thread safety — intentional, since we are a single
// Arduino loop.
//
// Add new event kinds by appending to EventKind. Payload-bearing events
// pass their data through the std::function args.

enum class EventKind : uint8_t {
    SnapshotReceived,   // a parsed snapshot landed in AppState
    LinkLive,           // BLE / live transitioned false → true
    LinkDead,           // BLE / live transitioned true → false
    PromptShow,         // PromptUi went visible
    PromptHide,         // PromptUi went hidden
    WifiConnected,      // WifiManager reached STA_CONNECTED
    WifiDisconnected,   // WifiManager left STA_CONNECTED
    Count_,
};

class EventBus {
public:
    using Listener = std::function<void()>;

    void subscribe(EventKind k, Listener l) {
        listeners_[static_cast<size_t>(k)].push_back(std::move(l));
    }

    void publish(EventKind k) {
        const auto& list = listeners_[static_cast<size_t>(k)];
        for (const auto& l : list) {
            if (l) l();
        }
    }

private:
    std::vector<Listener> listeners_[static_cast<size_t>(EventKind::Count_)];
};
