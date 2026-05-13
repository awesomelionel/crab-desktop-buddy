# Usage Remaining on Status Card — Implementation Plan

**Goal:** Replace the Status card's single `tokens today` line with a compact
usage summary that shows a progress bar, usage percentage, and remaining
amount.

Target display:

```text
42% used
[████████░░░░░░░░░░░░] 58K left
```

If the host can provide quota reset timing later, the same model can grow into
the dedicated Usage card concept. This plan keeps the first step narrow: update
the default Status card with one current-usage bar.

## Current State

- The firmware currently parses `tokens_today` from BLE snapshots.
- `StatusCard` renders it as `<count> tokens today` at y=70.
- There is no quota limit or remaining field in the current firmware protocol.
- Therefore, the firmware cannot accurately compute "remaining" from today's
  token count alone unless the host sends either:
  - a remaining count, or
  - a usage limit that can be subtracted from `tokens_today`.

## Protocol Decision

Add an optional `usage` object to BLE snapshots:

```json
{
  "total": 3,
  "running": 1,
  "waiting": 0,
  "tokens_today": 12400,
  "usage": {
    "used": 12400,
    "remaining": 87600,
    "limit": 100000
  }
}
```

Field rules:

- `usage.used` is the authoritative usage value for the display.
- `usage.remaining` is the authoritative remaining value when present.
- `usage.limit` is optional fallback: if `remaining` is absent but `limit >= used`,
  firmware computes `remaining = limit - used`.
- Percentage is computed as `used * 100 / (used + remaining)` when remaining
  is present, or `used * 100 / limit` when only limit is available.
- If `usage` is missing, firmware falls back to the old `tokens_today` display
  for backwards compatibility.
- Missing fields preserve previous values, matching current partial-update
  semantics.
- A snapshot with `usage.used: 0` or `usage.remaining: 0` is valid and must be
  honored.

## Display Decision

Replace the y=70 token line on `StatusCard` with a compact two-line usage
bar. This should be closer to the reference image than plain text, but sized
for the existing Status card layout.

When usage data is available:

```text
42% used
[bar] 58K left
```

When only legacy `tokens_today` is available:

```text
12.4K tokens today
```

Rendering notes:

- Use the vertical space currently allocated to the token line and top of the
  message block:
  - Percentage label at y=70, text size 2.
  - Progress bar at y=89, height 8.
  - Remaining label to the right of or below the bar depending on measured fit.
- Move the message/prompt block down only as much as needed; preserve the
  footer band at y=117.
- Use `format_token_count()` for the remaining amount.
- Progress bar geometry should be stable:
  - fixed x/y/w/h
  - no layout shift as percentage changes
  - erase only the usage strip when usage changes
- Bar color:
  - green/cyan for low to moderate usage
  - orange when usage is high
  - red near exhaustion
- The filled percentage should be clamped to `0..100`.

Suggested compact layout:

```text
        42% used
  [==============        ] 58K left
```

If text does not fit cleanly, use:

```text
42% used
58K left
```

with the bar centered between those lines.

## Files

Modify:

- `lib/protocol/protocol.h`
- `lib/protocol/protocol.cpp`
- `test/test_protocol/test_protocol.cpp`
- `src/ui/cards/StatusCard.h`
- `src/ui/cards/StatusCard.cpp`
- `README.md`

Optional if needed:

- `lib/protocol/format.{h,cpp}` if a second formatting helper is cleaner.
- `test/test_format/test_format.cpp` if formatting behavior changes.

## Task 1: Extend Protocol Model

Add a usage sub-struct:

```cpp
struct ClaudeUsage {
    bool     valid;
    uint32_t used;
    uint32_t remaining;
    uint32_t limit;
    bool     has_remaining;
    bool     has_limit;
};
```

Add it to `ClaudeStatus`:

```cpp
ClaudeUsage usage;
```

Implementation details:

- Zero-initialized `ClaudeStatus` should mean `usage.valid == false`.
- Keep `tokens_today` for backwards compatibility and telemetry events.
- Do not rename `tokens_today` in this task.

## Task 2: Parse `usage`

In `protocol_parse_line()`:

- If `doc["usage"]` is an object, parse numeric `used`, `remaining`, and `limit`.
- If `used` is present, set `usage.used`.
- If `remaining` is present, set `usage.remaining` and `has_remaining = true`.
- If `limit` is present, set `usage.limit` and `has_limit = true`.
- If `used` is present and either `remaining` or usable `limit` is present, set
  `usage.valid = true`.
- If `remaining` is missing but `limit >= used`, compute remaining from limit.
- Leave previous values unchanged when the `usage` object or individual fields
  are absent.
- Ignore malformed string/object fields.

Parser tests:

- Parses `usage.used` + `usage.remaining`.
- Parses `usage.used` + `usage.limit` and computes remaining.
- Honors zero remaining.
- Missing `usage` keeps previous usage values.
- Malformed `usage.remaining` keeps previous remaining.
- Legacy `tokens_today` behavior still passes.

## Task 3: Update StatusCard Dirty Tracking

Track the last drawn usage fields in `StatusCard`:

- `last_drawn_usage_valid_`
- `last_drawn_usage_used_`
- `last_drawn_usage_remaining_`
- `last_drawn_usage_pct_`

`isDirty()` should return true when any displayed usage value changes.

Keep existing `last_drawn_tokens_today_` because the fallback legacy line still
depends on it.

## Task 4: Render Usage Percentage + Bar

Replace the token-line rendering block:

- If `status.usage.valid`, render percentage, progress bar, and remaining.
- Else if `status.valid`, render the legacy `tokens_today` line.
- Else render nothing.

Suggested helper inside `StatusCard.cpp`:

```cpp
uint8_t usagePercent(const ClaudeStatus& status);
uint16_t usageBarColor(uint8_t pct);
void drawUsageBar(Adafruit_ST7789& tft, int x, int y, int w, int h,
                  uint8_t pct, uint16_t fill, uint16_t bg);
```

Example output:

```text
42% used
[bar] 58K left
```

Implementation notes:

- Draw a low-contrast bar background first.
- Draw the filled segment over it.
- Use small radius only if it matches existing display primitives and does not
  create expensive redraws; a rectangular bar is acceptable.
- Avoid floating point; percentage can be integer math.
- `remaining == 0` should produce `100% used` and a full bar.
- Unknown remaining/limit should not render a bar; use the legacy token line.

## Task 5: Update README

Update protocol docs to mention:

- `tokens_today` legacy/display fallback.
- Optional `usage` object.
- Status card shows percentage, usage bar, and remaining when usage data is
  present.

## Task 6: Build + Test

Run:

```sh
pio test -e native
pio run -e adafruit_feather_esp32s3_reversetft
```

Known caveat:

- If the native environment still tries to compile Arduino-only `src/` files,
  use the targeted test env or fix native `src_filter` separately. Do not let
  that unrelated config issue block parser test updates.

## Acceptance Criteria

- Existing snapshots with only `tokens_today` still render the old line.
- Snapshots with `usage.used` and `usage.remaining` render percentage, bar,
  and remaining.
- Snapshots with `usage.used` and `usage.limit` render percentage, bar, and
  computed remaining.
- Partial snapshots preserve previous usage values.
- `0 left` renders as `100% used` with a full bar.
- ESP32 firmware builds.

## Follow-Up

After the Status card version lands, consider a dedicated Usage card with:

- Current and weekly usage rows.
- Reset timers.
- Progress bars.
- Color thresholds near quota exhaustion.
