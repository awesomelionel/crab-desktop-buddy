#include "protocol.h"
#include <ArduinoJson.h>
#include <string.h>

// Sanitize bytes from `src` into `dst` (size n incl. NUL): replace any
// non-printable byte (including '\n', '\t', '\r') with '?', stop at NUL or
// when the buffer is full. Always NUL-terminates.
static void sanitize_copy(char* dst, size_t n, const char* src) {
    if (n == 0) return;
    size_t i = 0;
    if (src) {
        for (; i < n - 1 && src[i]; i++) {
            unsigned char c = (unsigned char)src[i];
            dst[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
        }
    }
    dst[i] = 0;
}

static void recompute_usage(ClaudeUsage* u) {
    if (!u) return;
    if (!u->has_remaining && u->has_limit && u->limit >= u->used) {
        u->remaining = u->limit - u->used;
    }
    u->valid = u->has_remaining || (u->has_limit && u->limit >= u->used);
}

bool protocol_parse_line(const char* line, ClaudeStatus* out) {
    if (!line || !out) return false;
    if (line[0] != '{') return false;

    JsonDocument doc;
    if (deserializeJson(doc, line)) return false;
    if (!doc.is<JsonObject>()) return false;

    // We treat any object containing `total` as a snapshot. Other line
    // shapes (acks, time-sync, owner) leave existing prompt state alone
    // so an ack reply can't accidentally clear an active prompt.
    bool isSnapshot = doc["total"].is<unsigned int>();

    out->total   = doc["total"]   | out->total;
    out->running = doc["running"] | out->running;
    out->waiting = doc["waiting"] | out->waiting;

    if (doc["tokens_today"].is<uint32_t>()) {
        out->tokens_today = doc["tokens_today"].as<uint32_t>();
    }
    // else: leave previous value unchanged (matches partial-update semantics)

    JsonObject usage = doc["usage"];
    if (!usage.isNull()) {
        if (usage["used"].is<uint32_t>()) {
            out->usage.used = usage["used"].as<uint32_t>();
        }
        if (usage["remaining"].is<uint32_t>()) {
            out->usage.remaining = usage["remaining"].as<uint32_t>();
            out->usage.has_remaining = true;
        }
        if (usage["limit"].is<uint32_t>()) {
            out->usage.limit = usage["limit"].as<uint32_t>();
            out->usage.has_limit = true;
        }
        recompute_usage(&out->usage);
    }
    // else: leave previous value unchanged (matches partial-update semantics)

    const char* m = doc["msg"];
    if (m) {
        strncpy(out->msg, m, sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = 0;
    }

    if (isSnapshot) {
        // Default the prompt to "absent" for this snapshot, then
        // overwrite if a valid one is present.
        out->prompt.present = false;

        JsonObject p = doc["prompt"];
        if (!p.isNull()) {
            const char* id   = p["id"];
            const char* tool = p["tool"];
            const char* hint = p["hint"];
            // Echoing back a truncated id would silently no-op on the
            // desktop, so refuse a prompt we can't faithfully respond to.
            if (id && strlen(id) < sizeof(out->prompt.id)) {
                strncpy(out->prompt.id, id, sizeof(out->prompt.id) - 1);
                out->prompt.id[sizeof(out->prompt.id) - 1] = 0;
                sanitize_copy(out->prompt.tool, sizeof(out->prompt.tool), tool);
                sanitize_copy(out->prompt.hint, sizeof(out->prompt.hint), hint);
                out->prompt.present = true;
            }
        }
    }

    out->valid = true;
    return true;
}
