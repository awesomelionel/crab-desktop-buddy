#include "settings_model.h"

#include <stdio.h>
#include <string.h>

namespace settings {

namespace {

void writeError(char* error, size_t error_len, const char* msg) {
    if (!error || error_len == 0) return;
    size_t n = strlen(msg);
    if (n >= error_len) n = error_len - 1;
    memcpy(error, msg, n);
    error[n] = 0;
}

bool isPrintableAscii(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return u >= 32 && u <= 126;
}

bool isAsciiDigit(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return u >= '0' && u <= '9';
}

bool isValidBusStopCode(const char* code, char* error, size_t error_len) {
    if (!code) { writeError(error, error_len, "bus_stops code missing"); return false; }
    size_t n = strlen(code);
    if (n == 0) return true;
    if (n != BUS_STOP_CODE_LEN) {
        writeError(error, error_len, "bus_stops code must be 5 digits or empty");
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!isAsciiDigit(code[i])) {
            writeError(error, error_len, "bus_stops code must be 5 digits or empty");
            return false;
        }
    }
    return true;
}

bool isValidBusStopLabel(const char* label, char* error, size_t error_len) {
    if (!label) { writeError(error, error_len, "bus_stops label missing"); return false; }
    size_t n = strlen(label);
    if (n > MAX_BUS_LABEL_LEN) {
        writeError(error, error_len, "bus_stops label too long (max 12)");
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!isPrintableAscii(label[i])) {
            writeError(error, error_len, "bus_stops label has non-printable chars");
            return false;
        }
    }
    return true;
}

uint8_t busSlotForCardId(uint8_t id) {
    return (uint8_t)(id - CARD_BUS_1);
}

bool isValidDeviceName(const char* name, char* error, size_t error_len) {
    if (!name) { writeError(error, error_len, "name missing"); return false; }
    size_t n = strlen(name);
    if (n == 0) { writeError(error, error_len, "name empty"); return false; }
    if (n > MAX_DEVICE_NAME_LEN) {
        writeError(error, error_len, "name too long (max 15)");
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!isPrintableAscii(name[i])) {
            writeError(error, error_len, "name has non-printable chars");
            return false;
        }
    }
    return true;
}

bool isValidLiveTimeout(uint16_t v, char* error, size_t error_len) {
    if (v < LIVE_TIMEOUT_MIN_S || v > LIVE_TIMEOUT_MAX_S) {
        writeError(error, error_len, "live_timeout_s out of range (5..300)");
        return false;
    }
    return true;
}

bool isValidSleepTimeout(uint16_t v, char* error, size_t error_len) {
    if (v == 0) return true;
    if (v < SLEEP_TIMEOUT_MIN_S || v > SLEEP_TIMEOUT_MAX_S) {
        writeError(error, error_len, "sleep_timeout_s out of range (0 or 30..3600)");
        return false;
    }
    return true;
}

bool isValidDimTimeout(uint16_t v, char* error, size_t error_len) {
    if (v == 0) return true;
    if (v < DIM_TIMEOUT_MIN_S || v > DIM_TIMEOUT_MAX_S) {
        writeError(error, error_len,
                   "dim_timeout_s out of range (0 or 5..3600)");
        return false;
    }
    return true;
}

bool isValidDimLevelPct(uint8_t v, char* error, size_t error_len) {
    if (v < DIM_LEVEL_MIN_PCT || v > DIM_LEVEL_MAX_PCT) {
        writeError(error, error_len, "dim_level_pct out of range (1..99)");
        return false;
    }
    return true;
}

bool isValidFullLevelPct(uint8_t v, char* error, size_t error_len) {
    if (v < FULL_LEVEL_MIN_PCT || v > FULL_LEVEL_MAX_PCT) {
        writeError(error, error_len, "full_level_pct out of range (1..100)");
        return false;
    }
    return true;
}

bool isValidDailyTokenCap(uint32_t v, char* error, size_t error_len) {
    if (v > DAILY_TOKEN_CAP_MAX) {
        writeError(error, error_len,
                   "daily_token_cap out of range (0..100000000)");
        return false;
    }
    return true;
}

bool isValidDimVsSleep(uint16_t dim_s, uint16_t sleep_s,
                       char* error, size_t error_len) {
    // Constraint applies only when both are non-zero.
    if (dim_s != 0 && sleep_s != 0 && dim_s >= sleep_s) {
        writeError(error, error_len,
                   "dim_timeout_s must be before sleep_timeout_s");
        return false;
    }
    return true;
}

bool isValidCards(uint8_t enabled_mask, const uint8_t* order, uint8_t count,
                  char* error, size_t error_len) {
    if (enabled_mask == 0) {
        writeError(error, error_len, "at least one card must be enabled");
        return false;
    }
    // Count bits in enabled_mask
    uint8_t expected = 0;
    for (uint8_t i = 0; i < CARD_COUNT; ++i) {
        if (enabled_mask & (1u << i)) expected++;
    }
    if (count != expected) {
        writeError(error, error_len, "order length must match enabled count");
        return false;
    }
    if (count > CARD_COUNT) {
        writeError(error, error_len, "order length exceeds CARD_COUNT");
        return false;
    }
    // Each order entry must be a valid CardId, in enabled_mask, no dups.
    uint8_t seen = 0;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t id = order[i];
        if (id >= CARD_COUNT) {
            writeError(error, error_len, "order has unknown card id");
            return false;
        }
        uint8_t bit = 1u << id;
        if ((enabled_mask & bit) == 0) {
            writeError(error, error_len, "order has card not in enabled mask");
            return false;
        }
        if (seen & bit) {
            writeError(error, error_len, "order has duplicate card id");
            return false;
        }
        seen |= bit;
    }
    return true;
}

}  // namespace

void setDefaults(Settings& s, const char* default_name) {
    if (default_name) {
        size_t n = strlen(default_name);
        if (n > MAX_DEVICE_NAME_LEN) n = MAX_DEVICE_NAME_LEN;
        memcpy(s.device_name, default_name, n);
        s.device_name[n] = 0;
    } else {
        strcpy(s.device_name, "Claude");
    }
    s.live_timeout_s    = 30;
    s.sleep_timeout_s   = 0;
    s.dim_timeout_s     = 30;
    s.dim_level_pct     = 40;
    s.full_level_pct    = 100;
    s.daily_token_cap   = 0;
    // All except NavTest enabled by default.
    s.cards_enabled_mask = (1u << CARD_STATUS) | (1u << CARD_EYES) | (1u << CARD_WIFI);
    s.cards_order[0]    = CARD_STATUS;
    s.cards_order[1]    = CARD_EYES;
    s.cards_order[2]    = CARD_WIFI;
    s.cards_order[3]    = 0;
    s.cards_order_count = 3;
    s.boot_card_id      = CARD_STATUS;
    for (uint8_t i = 0; i < MAX_BUS_STOPS; ++i) {
        s.bus_stops[i].code[0]  = '\0';
        s.bus_stops[i].label[0] = '\0';
    }
}

bool validate(const Settings& s, char* error, size_t error_len) {
    if (!isValidDeviceName(s.device_name, error, error_len)) return false;
    if (!isValidLiveTimeout(s.live_timeout_s, error, error_len)) return false;
    if (!isValidSleepTimeout(s.sleep_timeout_s, error, error_len)) return false;
    if (!isValidDimTimeout(s.dim_timeout_s, error, error_len)) return false;
    if (!isValidDimLevelPct(s.dim_level_pct, error, error_len)) return false;
    if (!isValidFullLevelPct(s.full_level_pct, error, error_len)) return false;
    if (!isValidDailyTokenCap(s.daily_token_cap, error, error_len)) return false;
    if (!isValidDimVsSleep(s.dim_timeout_s, s.sleep_timeout_s,
                           error, error_len)) return false;
    if (!isValidCards(s.cards_enabled_mask, s.cards_order, s.cards_order_count,
                      error, error_len)) return false;
    if (s.boot_card_id >= CARD_COUNT) {
        writeError(error, error_len, "boot_card_id is not a valid card");
        return false;
    }
    if ((s.cards_enabled_mask & (1u << s.boot_card_id)) == 0) {
        writeError(error, error_len, "boot_card_id is not enabled");
        return false;
    }
    for (uint8_t i = 0; i < MAX_BUS_STOPS; ++i) {
        if (!isValidBusStopCode(s.bus_stops[i].code,   error, error_len)) return false;
        if (!isValidBusStopLabel(s.bus_stops[i].label, error, error_len)) return false;
    }
    for (uint8_t id = CARD_BUS_1; id <= CARD_BUS_4; ++id) {
        if ((s.cards_enabled_mask & (1u << id)) == 0) continue;
        if (s.bus_stops[busSlotForCardId(id)].code[0] == '\0') {
            writeError(error, error_len, "bus card enabled with empty code");
            return false;
        }
    }
    return true;
}

bool applyDeviceFields(Settings& s,
                       const char* name,
                       uint16_t live_timeout_s,
                       uint16_t sleep_timeout_s,
                       char* error, size_t error_len) {
    if (!isValidDeviceName(name, error, error_len)) return false;
    if (!isValidLiveTimeout(live_timeout_s, error, error_len)) return false;
    if (!isValidSleepTimeout(sleep_timeout_s, error, error_len)) return false;

    size_t n = strlen(name);
    memcpy(s.device_name, name, n);
    s.device_name[n] = 0;
    s.live_timeout_s  = live_timeout_s;
    s.sleep_timeout_s = sleep_timeout_s;
    return true;
}

bool applyCardsFields(Settings& s,
                      uint8_t enabled_mask,
                      const uint8_t* order, uint8_t count,
                      uint8_t boot_card_id,
                      char* error, size_t error_len) {
    if (!isValidCards(enabled_mask, order, count, error, error_len)) return false;
    if (boot_card_id >= CARD_COUNT) {
        writeError(error, error_len, "boot_card_id is not a valid card");
        return false;
    }

    s.cards_enabled_mask = enabled_mask;
    for (uint8_t i = 0; i < count; ++i) s.cards_order[i] = order[i];
    for (uint8_t i = count; i < CARD_COUNT; ++i) s.cards_order[i] = 0;
    s.cards_order_count = count;

    if ((enabled_mask & (1u << boot_card_id)) == 0) {
        // Silent default per spec: pick the first enabled card.
        for (uint8_t i = 0; i < CARD_COUNT; ++i) {
            if (enabled_mask & (1u << i)) {
                s.boot_card_id = i;
                break;
            }
        }
    } else {
        s.boot_card_id = boot_card_id;
    }
    return true;
}

bool applyBacklightFields(Settings& s,
                          uint16_t dim_timeout_s,
                          uint8_t  dim_level_pct,
                          uint8_t  full_level_pct,
                          char* error, size_t error_len) {
    if (!isValidDimTimeout(dim_timeout_s, error, error_len)) return false;
    if (!isValidDimLevelPct(dim_level_pct, error, error_len)) return false;
    if (!isValidFullLevelPct(full_level_pct, error, error_len)) return false;
    if (!isValidDimVsSleep(dim_timeout_s, s.sleep_timeout_s,
                           error, error_len)) return false;
    s.dim_timeout_s   = dim_timeout_s;
    s.dim_level_pct   = dim_level_pct;
    s.full_level_pct  = full_level_pct;
    return true;
}

bool applyDailyCapField(Settings& s,
                        uint32_t daily_token_cap,
                        char* error, size_t error_len) {
    if (!isValidDailyTokenCap(daily_token_cap, error, error_len)) return false;
    s.daily_token_cap = daily_token_cap;
    return true;
}

bool applyBusStopField(Settings& s,
                       uint8_t slot,
                       const char* code,
                       const char* label,
                       char* error, size_t error_len) {
    if (slot >= MAX_BUS_STOPS) {
        writeError(error, error_len, "bus_stops slot out of range");
        return false;
    }
    if (!isValidBusStopCode(code,   error, error_len)) return false;
    const bool clearing = (code[0] == '\0');
    if (!clearing) {
        if (!isValidBusStopLabel(label, error, error_len)) return false;
    }

    if (clearing) {
        const uint8_t card_id = (uint8_t)(CARD_BUS_1 + slot);
        if (s.cards_enabled_mask == (uint8_t)(1u << card_id)) {
            writeError(error, error_len, "at least one card must be enabled");
            return false;
        }
    }

    if (clearing) {
        s.bus_stops[slot].code[0]  = '\0';
        s.bus_stops[slot].label[0] = '\0';
        const uint8_t card_id = (uint8_t)(CARD_BUS_1 + slot);
        if (s.cards_enabled_mask & (1u << card_id)) {
            s.cards_enabled_mask &= (uint8_t)~(1u << card_id);
            uint8_t out = 0;
            for (uint8_t i = 0; i < s.cards_order_count; ++i) {
                if (s.cards_order[i] != card_id) {
                    s.cards_order[out++] = s.cards_order[i];
                }
            }
            for (uint8_t i = out; i < CARD_COUNT; ++i) s.cards_order[i] = 0;
            s.cards_order_count = out;
            if (s.boot_card_id == card_id) {
                for (uint8_t i = 0; i < CARD_COUNT; ++i) {
                    if (s.cards_enabled_mask & (1u << i)) {
                        s.boot_card_id = i;
                        break;
                    }
                }
            }
        }
    } else {
        size_t n = strlen(code);
        memcpy(s.bus_stops[slot].code, code, n);
        s.bus_stops[slot].code[n] = '\0';
        size_t m = strlen(label);
        memcpy(s.bus_stops[slot].label, label, m);
        s.bus_stops[slot].label[m] = '\0';
    }
    return true;
}

const char* cardName(CardId id) {
    switch (id) {
        case CARD_STATUS:  return "Status";
        case CARD_EYES:    return "Eyes";
        case CARD_WIFI:    return "Wifi";
        case CARD_NAVTEST: return "NavTest";
        case CARD_COUNT:   return "?";
    }
    return "?";
}

size_t toJson(const Settings& s, char* buf, size_t buf_len) {
    if (!buf || buf_len == 0) return 0;

    // Build cards array first; then assemble into the parent.
    int written = snprintf(buf, buf_len,
        "{\"device_name\":\"%s\","
        "\"live_timeout_s\":%u,"
        "\"sleep_timeout_s\":%u,"
        "\"dim_timeout_s\":%u,"
        "\"dim_level_pct\":%u,"
        "\"full_level_pct\":%u,"
        "\"daily_token_cap\":%u,"
        "\"boot_card_id\":%u,"
        "\"cards\":[",
        s.device_name,
        (unsigned)s.live_timeout_s,
        (unsigned)s.sleep_timeout_s,
        (unsigned)s.dim_timeout_s,
        (unsigned)s.dim_level_pct,
        (unsigned)s.full_level_pct,
        (unsigned)s.daily_token_cap,
        (unsigned)s.boot_card_id);
    if (written < 0 || (size_t)written >= buf_len) return 0;
    size_t pos = (size_t)written;

    // Walk every CardId so the array is stable order regardless of cards_order.
    // For each, emit { id, name, enabled, order } where order is its index in
    // cards_order if present, else null.
    bool first = true;
    for (uint8_t id = 0; id < CARD_COUNT; ++id) {
        bool enabled = (s.cards_enabled_mask & (1u << id)) != 0;
        // Find position in order, if any.
        int pos_in_order = -1;
        for (uint8_t i = 0; i < s.cards_order_count; ++i) {
            if (s.cards_order[i] == id) { pos_in_order = i; break; }
        }
        const char* sep = first ? "" : ",";
        first = false;

        int n;
        if (pos_in_order >= 0) {
            n = snprintf(buf + pos, buf_len - pos,
                "%s{\"id\":%u,\"name\":\"%s\",\"enabled\":%s,\"order\":%d}",
                sep, (unsigned)id, cardName(static_cast<CardId>(id)),
                enabled ? "true" : "false", pos_in_order);
        } else {
            n = snprintf(buf + pos, buf_len - pos,
                "%s{\"id\":%u,\"name\":\"%s\",\"enabled\":%s,\"order\":null}",
                sep, (unsigned)id, cardName(static_cast<CardId>(id)),
                enabled ? "true" : "false");
        }
        if (n < 0 || (size_t)n >= buf_len - pos) return 0;
        pos += (size_t)n;
    }

    int n = snprintf(buf + pos, buf_len - pos, "]}");
    if (n < 0 || (size_t)n >= buf_len - pos) return 0;
    pos += (size_t)n;
    return pos;
}

}  // namespace settings
