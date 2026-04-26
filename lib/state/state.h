#pragma once
#include <stdint.h>
#include "protocol.h"

enum BuddyState : uint8_t {
    STATE_DISCONNECTED = 0,
    STATE_IDLE,
    STATE_WORKING,
    STATE_WAITING,
};

const char* state_name(BuddyState s);
BuddyState  state_derive(const ClaudeStatus& status, bool connected);
