#pragma once

#include <stdint.h>

#include "settings_model.h"
#include "state.h"

// Connected-idle nap vs backlight-off. Backlight duty stays in
// backlight_compute_duty; this decides when the face should look asleep
// and which BuddyStates keep the idle clock at zero.
namespace idle_policy {

// now_ms - last_activity_ms, clamped at 0. Event handlers stamp activity
// with millis() after the loop captured now_ms, so last can be ahead.
uint32_t idleMs(uint32_t now_ms, uint32_t last_activity_ms);

// WORKING and WAITING hold the backlight awake and never nap.
bool holdsAwake(BuddyState state);

// Connected-idle nap threshold. Uses dim_timeout_s when set, otherwise 30 s.
uint32_t napTimeoutMs(const settings::Settings& s);

// True only for STATE_IDLE after napTimeoutMs of quiet. DISCONNECTED
// already has its own sleep look; WORKING/WAITING never nap.
bool shouldNap(BuddyState state, uint32_t idle_ms,
               const settings::Settings& s);

}  // namespace idle_policy
