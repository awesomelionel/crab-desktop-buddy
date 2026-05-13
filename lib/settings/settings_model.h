#pragma once

#include <stddef.h>
#include <stdint.h>

namespace settings {

// Card identity. Stable across versions — adding a card is fine, but
// renumbering existing entries would silently re-map persisted prefs.
enum CardId : uint8_t {
    CARD_STATUS  = 0,
    CARD_EYES    = 1,
    CARD_WIFI    = 2,
    CARD_NAVTEST = 3,
    CARD_BUS_1   = 4,
    CARD_BUS_2   = 5,
    CARD_BUS_3   = 6,
    CARD_BUS_4   = 7,
    CARD_COUNT   = 8,  // sentinel; not a real card
};

constexpr uint8_t  MAX_DEVICE_NAME_LEN = 15;   // not counting null
constexpr uint16_t LIVE_TIMEOUT_MIN_S  = 5;
constexpr uint16_t LIVE_TIMEOUT_MAX_S  = 300;
constexpr uint16_t SLEEP_TIMEOUT_MIN_S = 30;   // 0 also valid (= disabled)
constexpr uint16_t SLEEP_TIMEOUT_MAX_S = 3600;
constexpr uint16_t DIM_TIMEOUT_MIN_S   = 5;    // 0 also valid (= disabled)
constexpr uint16_t DIM_TIMEOUT_MAX_S   = 3600;
constexpr uint8_t  DIM_LEVEL_MIN_PCT   = 1;
constexpr uint8_t  DIM_LEVEL_MAX_PCT   = 99;
constexpr uint8_t  FULL_LEVEL_MIN_PCT  = 1;
constexpr uint8_t  FULL_LEVEL_MAX_PCT  = 100;
constexpr uint32_t DAILY_TOKEN_CAP_MAX = 100000000u;  // 100M tokens, sanity ceiling

constexpr uint8_t  MAX_BUS_STOPS       = 4;
constexpr uint8_t  MAX_BUS_LABEL_LEN   = 12;   // not counting null
constexpr uint8_t  BUS_STOP_CODE_LEN   = 5;    // Singapore stop codes are exactly 5 digits

struct BusStopSlot {
    char code[BUS_STOP_CODE_LEN + 1];   // "" = empty slot, disables card
    char label[MAX_BUS_LABEL_LEN + 1];  // "" = renderer falls back to "Stop NNNNN"
};

struct Settings {
    char     device_name[MAX_DEVICE_NAME_LEN + 1];
    uint16_t live_timeout_s;
    uint16_t sleep_timeout_s;     // 0 = always on
    uint16_t dim_timeout_s;       // 0 = never dim
    uint8_t  dim_level_pct;       // 1..99
    uint8_t  full_level_pct;      // 1..100
    uint32_t daily_token_cap;     // 0 = disabled (legacy tokens_today line shown)
    uint8_t  cards_enabled_mask;  // bit i set = CardId i is enabled
    uint8_t  cards_order[CARD_COUNT];  // permutation; first cards_order_count entries valid
    uint8_t  cards_order_count;
    uint8_t  boot_card_id;
    BusStopSlot bus_stops[MAX_BUS_STOPS];
};

// Populate s with defaults. default_name is copied (caller-supplied "Claude-XXXX").
void setDefaults(Settings& s, const char* default_name);

// Validate. Returns true if every field passes. On failure fills `error`
// (never null on call). Empty error_buf size disables the message.
bool validate(const Settings& s, char* error, size_t error_len);

// Patch a copy of s with new device fields, validate, and overwrite if valid.
// Returns true on success; false leaves s unchanged and writes the reason.
bool applyDeviceFields(Settings& s,
                       const char* name,
                       uint16_t live_timeout_s,
                       uint16_t sleep_timeout_s,
                       char* error, size_t error_len);

// Patch with new cards fields. order[] has count entries; each must be a
// CardId < CARD_COUNT. enabled_mask must include exactly the ids in order[].
// boot_card_id must be enabled; if it isn't, the result silently uses the
// first enabled card (per spec).
bool applyCardsFields(Settings& s,
                      uint8_t enabled_mask,
                      const uint8_t* order, uint8_t count,
                      uint8_t boot_card_id,
                      char* error, size_t error_len);

// Patch with new backlight fields. Validates per the rules in validate().
// Returns true on success; false leaves s unchanged and writes the reason.
bool applyBacklightFields(Settings& s,
                          uint16_t dim_timeout_s,
                          uint8_t  dim_level_pct,
                          uint8_t  full_level_pct,
                          char* error, size_t error_len);

// Patch with a new daily_token_cap value. Validates per the same rule as
// validate(). Returns true on success; false leaves s unchanged and
// writes the reason into error.
bool applyDailyCapField(Settings& s,
                        uint32_t daily_token_cap,
                        char* error, size_t error_len);

// Patch a single bus stop slot. slot must be < MAX_BUS_STOPS.
// code may be "" (clears the slot and auto-disables the corresponding card)
// or exactly BUS_STOP_CODE_LEN ASCII digits. label may be empty or up to
// MAX_BUS_LABEL_LEN printable ASCII chars. Returns true on success;
// false leaves s unchanged and writes the reason into error.
bool applyBusStopField(Settings& s,
                       uint8_t slot,
                       const char* code,
                       const char* label,
                       char* error, size_t error_len);

// Render Settings as a single JSON object into buf. Returns the number of
// chars written (excluding null), or 0 if buf_len is too small.
//
// Shape:
//   {"device_name":"...","live_timeout_s":N,"sleep_timeout_s":N,
//    "dim_timeout_s":N,"dim_level_pct":N,"full_level_pct":N,
//    "daily_token_cap":N,"boot_card_id":N,
//    "cards":[{"id":N,"name":"...","enabled":bool,"order":N}, ...]}
size_t toJson(const Settings& s, char* buf, size_t buf_len);

// Human-readable card name, stable for use in JSON / UI labels.
const char* cardName(CardId id);

}  // namespace settings
