#pragma once
#include <stdint.h>
#include <stddef.h>

struct ClaudePrompt {
    bool present;
    char id[40];    // real Anthropic request IDs are ~30 chars; reference impl uses 40
    char tool[16];
    char hint[64];
};

struct ClaudeStatus {
    uint8_t      total;
    uint8_t      running;
    uint8_t      waiting;
    char         msg[32];
    bool         valid;        // true once at least one snapshot has parsed
    ClaudePrompt prompt;
    uint32_t     tokens_today; // output tokens since local midnight (from bridge)
};

// Parse one newline-stripped JSON object from the bridge into `out`.
// Missing fields are left at whatever they already were in `out` so that
// successive partial snapshots accumulate. Returns false if the line isn't
// a JSON object, json fails to parse, or `line`/`out` is null.
bool protocol_parse_line(const char* line, ClaudeStatus* out);
