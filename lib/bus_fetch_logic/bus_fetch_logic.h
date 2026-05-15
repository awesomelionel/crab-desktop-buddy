#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings_model.h"   // settings::MAX_BUS_STOPS, settings::BUS_STOP_CODE_LEN

namespace bus_fetch_logic {

// Enumerators are LOW_PRIO/HIGH_PRIO (not LOW/HIGH) because Arduino's
// <esp32-hal-gpio.h> #defines LOW and HIGH as numeric macros, which would
// expand inside FetchPriority::LOW at any caller that includes Arduino.h.
enum class FetchPriority : uint8_t { LOW_PRIO = 0, HIGH_PRIO = 1 };

struct SlotRequest {
    char          code[settings::BUS_STOP_CODE_LEN + 1];
    FetchPriority prio;
    bool          wanted;
};

// Returns the index of the highest-priority `wanted` slot in the table,
// or -1 if no slot is wanted. Among equal priorities the lowest index wins
// (deterministic, no starvation by design).
int pickHighestPriority(const SlotRequest (&table)[settings::MAX_BUS_STOPS]);

// Apply a new request to a single slot's entry. Always copies the latest
// `code` (bounded strncpy + explicit null term). Priority upgrades but
// never downgrades while the slot is still wanted: LOW + HIGH -> HIGH,
// HIGH + LOW -> HIGH (no downgrade). Caller must filter out empty `code`.
void applyRequest(SlotRequest& entry, const char* code, FetchPriority new_prio);

}  // namespace bus_fetch_logic
