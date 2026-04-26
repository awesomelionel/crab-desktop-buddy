#include "state.h"

const char* state_name(BuddyState s) {
    switch (s) {
        case STATE_DISCONNECTED: return "disconnected";
        case STATE_IDLE:         return "idle";
        case STATE_WORKING:      return "working";
        case STATE_WAITING:      return "waiting";
    }
    return "?";
}

BuddyState state_derive(const ClaudeStatus& status, bool connected) {
    if (!connected)            return STATE_DISCONNECTED;
    if (status.waiting > 0)    return STATE_WAITING;
    if (status.running > 0)    return STATE_WORKING;
    return STATE_IDLE;
}
