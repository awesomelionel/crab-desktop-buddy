#include "idle_policy.h"

namespace idle_policy {

namespace {
constexpr uint32_t kNapFallbackMs = 30'000;
}

uint32_t idleMs(uint32_t now_ms, uint32_t last_activity_ms) {
    return last_activity_ms > now_ms ? 0 : now_ms - last_activity_ms;
}

bool holdsAwake(BuddyState state) {
    return state == STATE_WORKING || state == STATE_WAITING;
}

uint32_t napTimeoutMs(const settings::Settings& s) {
    if (s.dim_timeout_s != 0) {
        return static_cast<uint32_t>(s.dim_timeout_s) * 1000UL;
    }
    return kNapFallbackMs;
}

bool shouldNap(BuddyState state, uint32_t idle_ms,
               const settings::Settings& s) {
    if (state != STATE_IDLE) return false;
    return idle_ms >= napTimeoutMs(s);
}

}  // namespace idle_policy
