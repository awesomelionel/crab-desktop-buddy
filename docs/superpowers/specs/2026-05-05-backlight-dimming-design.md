# Backlight dimming — design

Date: 2026-05-05
Status: Draft for review
Related: `src/display/Display.{h,cpp}` (PWM backlight),
`src/ui/CardController.{h,cpp}` (sleep manager → backlight manager),
`lib/settings/settings_model.{h,cpp}` (new fields),
`src/net/HttpServer.cpp` (settings form),
`src/core/EventBus.h` (new event kinds),
`src/net/BleLink.cpp` and `src/main.cpp` (publishers).

## Goal

Reduce the largest single battery draw on the device — the TFT backlight
LED — by dimming the screen during input idleness, then turning it off
entirely after a longer idle. Today `TFT_BACKLITE` is driven with
`digitalWrite` (full on or full off) and `CardController::runSleepManager`
already cuts it off after `settings.sleep_timeout_s`. There is no middle
state.

UX constraint: the first button press, or any meaningful Claude-side
event (status transition, new prompt, daily-token change, Wi-Fi
connect/disconnect), must restore full brightness with no perceivable
lag. Continuous BLE keepalive snapshots and ongoing eye animations do
**not** count as activity — they would defeat the dimming.

## Summary of what changes

1. **PWM backlight.** `Display::setBacklight` becomes percent-based,
   driven by the ESP32 LEDC peripheral on the existing `TFT_BACKLITE`
   pin (channel 0, 5 kHz, 8-bit). A `bool` overload is kept as a thin
   wrapper for any code path that wants on/off semantics.

2. **Three-state backlight manager.** `CardController::runSleepManager`
   is renamed to `runBacklightManager`. It computes one of three duty
   levels — FULL, DIM, OFF — from a single `last_activity_ms_` field
   and the new settings. Each tick it calls
   `display.setBacklight(pct)`, which is a no-op when `pct` matches the
   currently applied duty.

3. **Three new settings.** `dim_timeout_s`, `dim_level_pct`,
   `full_level_pct` join the existing `sleep_timeout_s` in
   `settings::Settings`. Defaults: dim after 30 s to 40 %, full level
   100 %. Validated, persisted, exposed on the existing HTTP settings
   page next to `sleep_timeout_s`.

4. **Event-driven wake.** `last_activity_ms_` is bumped from local
   input (already plumbed via `InputRouter::lastInputMs`) and from a
   small set of meaningful EventBus kinds:
   `StatusTransitioned`, `PromptArrived`, `TokensChanged`,
   `WifiConnected`, `WifiDisconnected`. The first three are new and
   are published by `state_derive` callers and the BLE snapshot
   parser.

5. **Out of scope** — explicit:
   - Soft PWM ramps between brightness levels (animation, not energy).
   - CPU clock scaling.
   - Render quiescence (skipping ticks when nothing dirty).
   - BLE / Wi-Fi radio duty tuning.

   Each of these can be a follow-up spec; bundling them here would
   bloat scope and increase regression risk.

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│ CardController::tick(now, display)                       │
│                                                          │
│  ┌─ runBacklightManager(now, display) ────────────────┐  │
│  │   reads:                                           │  │
│  │     last_activity_ms_   (own field)                │  │
│  │     input_->lastInputMs() (folded in)              │  │
│  │     settings.{dim,sleep}_timeout_s,                │  │
│  │              {dim,full}_level_pct                  │  │
│  │   writes:                                          │  │
│  │     display.setBacklight(pct 0..100)               │  │
│  │     stack_.active()->invalidate() on OFF→non-OFF   │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
       ▲                                ▲
       │ InputRouter bumps              │ EventBus subscribers bump
       │ lastInputMs() on any           │ last_activity_ms_:
       │ button event                   │   StatusTransitioned
                                        │   PromptArrived
                                        │   TokensChanged
                                        │   WifiConnected
                                        │   WifiDisconnected
```

State machine, derived purely from `now - last_activity_ms_`:

```
                 idle ≥ dim_timeout_s
       FULL ────────────────────────────► DIM
        ▲                                  │
        │                                  │ idle ≥ sleep_timeout_s
        │ activity                         ▼
        └──────────────── OFF ◄────────────┘
                          (any activity → FULL)
```

## Components

### `Display` — PWM backlight

```cpp
class Display {
public:
    void begin();
    void setBacklight(uint8_t pct);   // 0..100, primary API
    void setBacklight(bool on);       // wrapper: true→100, false→0
    bool isAsleep() const { return current_pct_ == 0; }
    uint8_t backlightPct() const { return current_pct_; }

private:
    static constexpr int kBacklightChannel = 0;
    static constexpr int kBacklightFreqHz  = 5000;
    static constexpr int kBacklightBits    = 8;
    uint8_t current_pct_ = 0;
};
```

`begin()` configures and attaches the LEDC channel, then sets 100 %.
`setBacklight(uint8_t)` clamps to ≤ 100, no-ops on unchanged value,
otherwise writes `pct * 255 / 100` to the channel and stores
`current_pct_`. `setBacklight(bool)` forwards `100` or `0`.

### `CardController` — backlight manager

`runSleepManager` is renamed `runBacklightManager`. Pseudocode:

```cpp
void CardController::runBacklightManager(uint32_t now_ms, Display& d) {
    if (!input_) return;
    last_activity_ms_ = std::max(last_activity_ms_, input_->lastInputMs());

    const settings::Settings& s = settings_.data();
    uint32_t idle_ms = now_ms - last_activity_ms_;

    uint8_t pct = s.full_level_pct;
    if (s.sleep_timeout_s != 0 &&
        idle_ms >= (uint32_t)s.sleep_timeout_s * 1000UL) {
        pct = 0;
    } else if (s.dim_timeout_s != 0 &&
               idle_ms >= (uint32_t)s.dim_timeout_s * 1000UL) {
        pct = s.dim_level_pct;
    }

    bool was_off = d.isAsleep();
    d.setBacklight(pct);
    if (was_off && pct != 0) {
        if (Card* a = stack_.active()) a->invalidate();
    }
}
```

`last_activity_ms_` is initialised to `0` at construction and set to
`millis()` inside `CardController::begin()` (i.e. after the Arduino
runtime has started ticking). This keeps the device in FULL for the
first `dim_timeout_s` seconds after boot, regardless of static-init
order.

EventBus subscriptions are added in `CardController::begin()` alongside
the existing ones:

```cpp
auto bump = [this] {
    last_activity_ms_ = millis();
};
bus_.subscribe(EventKind::StatusTransitioned, bump);
bus_.subscribe(EventKind::PromptArrived,      bump);
bus_.subscribe(EventKind::TokensChanged,      bump);
bus_.subscribe(EventKind::WifiConnected,      bump);
bus_.subscribe(EventKind::WifiDisconnected,   bump);
```

### `settings::Settings` — new fields

```cpp
uint16_t dim_timeout_s;   // 0 = never dim, otherwise 5..3600
uint8_t  dim_level_pct;   // 1..99
uint8_t  full_level_pct;  // 1..100
```

Defaults: `dim_timeout_s=30`, `dim_level_pct=40`, `full_level_pct=100`.

Validation in `settings_model.cpp`:
- `dim_timeout_s` is 0 or in `[5, 3600]`.
- `dim_level_pct` is in `[1, 99]`.
- `full_level_pct` is in `[1, 100]`.
- If `dim_timeout_s != 0` *and* `sleep_timeout_s != 0` then
  `dim_timeout_s < sleep_timeout_s`. (Dim must come before off; if
  either is 0 the constraint does not apply.)

JSON serializer/parser extended to round-trip the three new keys.

### HTTP settings form

`HttpServer.cpp` gets three rows next to the existing `sleep_timeout_s`
input:
- `dim_timeout_s` — number, with tip "Backlight dims after N seconds
  idle. 0 disables dimming. Otherwise 5..3600."
- `dim_level_pct` — number, with tip "Brightness while dimmed,
  1..99 %."
- `full_level_pct` — number, with tip "Brightness while active,
  1..100 %."

The validator returns the existing-style error string on bad input.

### `EventBus` — new kinds

Three new entries in `EventKind`:
- `StatusTransitioned` — published from `main.cpp` when
  `state_derive(...)` returns a different `BuddyState` than the
  previous tick. Cheapest place to detect transitions; the derived
  state already lives there.
- `PromptArrived` — published from `BleLink` when the parsed
  snapshot's `prompt` field is non-empty *and* differs from the
  previous snapshot's `prompt`.
- `TokensChanged` — published from `BleLink` when
  `tokens_today` differs from the previous snapshot.

Plain BLE keepalive snapshots that change none of these fields publish
nothing (existing snapshot path is unchanged).

## Data flow — wake examples

**User presses a button while OFF.** `InputRouter` updates
`lastInputMs()` synchronously. Next loop tick:
`runBacklightManager` folds the new input timestamp into
`last_activity_ms_`, idle_ms returns to 0, pct → 100, OFF→FULL
transition triggers `stack_.active()->invalidate()` so the card
repaints stale pixels.

**Claude sends a status transition while DIM.** `BleLink` publishes
the snapshot, `state_derive` in `main.cpp` notices a `BuddyState`
change and publishes `StatusTransitioned`. The subscriber bumps
`last_activity_ms_ = millis()`. Next tick: idle 0, pct → 100, no
invalidate (pixels were intact).

**BLE keepalive arrives, nothing changed.** No new event published.
`last_activity_ms_` unchanged. Backlight stays at its current level.

## Error handling

- LEDC channel attach is assumed to succeed — single channel, no other
  callers in the firmware. No runtime fallback path.
- `setBacklight` clamps `pct > 100` to 100 instead of rejecting.
- Invalid setting payloads are rejected by the model validator with a
  user-visible error string, same path as today.

## Testing

Native env (`test/`), Unity:

1. **`test_settings_dim`** — extend the existing settings_model tests:
   - Round-trip JSON with the three new fields.
   - Defaults are applied when keys are absent.
   - Out-of-range values are rejected with a clear error.
   - `dim_timeout_s >= sleep_timeout_s` (both non-zero) is rejected.
   - `dim_timeout_s = 0` with any `sleep_timeout_s` is accepted.

2. **`test_backlight_manager`** — new test, pure logic. Inject a fake
   `Settings` and feed `(idle_ms)` to a small computeDuty helper
   extracted from `runBacklightManager` so it's testable off-device.
   Cases:
   - idle 0 → `full_level_pct`.
   - idle just below `dim_timeout_s` → `full_level_pct`.
   - idle ≥ `dim_timeout_s` and < `sleep_timeout_s` → `dim_level_pct`.
   - idle ≥ `sleep_timeout_s` → 0.
   - `dim_timeout_s = 0` disables DIM (jumps straight to OFF).
   - `sleep_timeout_s = 0` disables OFF (caps at DIM).
   - both 0 → always `full_level_pct`.

3. **`test_event_wake`** — fake EventBus + `CardController` interaction.
   For each subscribed `EventKind`, publish, then assert
   `last_activity_ms_` is bumped and the next backlight computation
   returns to FULL.

`Display::setBacklight` itself is not unit-tested (depends on Arduino
LEDC). Manual hardware smoke test:
- Build the firmware, leave the device idle.
- Verify backlight dims at `dim_timeout_s` and turns off at
  `sleep_timeout_s`.
- Press any button → restores full instantly, screen repaints.
- From Claude: trigger a `BuddyState` transition (start/stop a
  command) → verify wake.

## Risks and open questions

- **PWM frequency interaction with the backlight LED.** 5 kHz is the
  Adafruit-recommended default for ESP32 LEDC backlighting and well
  above the visible-flicker threshold. If we see flicker on the
  prototype hardware, raise to 20 kHz (still within 8-bit duty
  resolution).
- **`StatusTransitioned` publish site.** Publishing from `main.cpp`
  keeps the event close to the only call site of `state_derive`. If
  another caller of `state_derive` appears later, the publish needs to
  move. Acceptable for now.
- **`PromptArrived` definition.** "Non-empty and different from
  previous" treats prompt-text edits as new arrivals. That is
  intentional — any prompt change is a user-attention event.
- **Backlight power numbers** are not measured here. The design
  assumes "dimming saves power"; absolute mA savings will be confirmed
  on hardware after implementation. Not a gating factor for accepting
  this design.
