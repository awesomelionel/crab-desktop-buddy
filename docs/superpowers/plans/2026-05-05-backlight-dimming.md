# Backlight dimming — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce battery draw from the TFT backlight by introducing PWM-controlled brightness and a three-state idle manager (FULL → DIM → OFF) that wakes on local input or meaningful Claude-side events.

**Architecture:** PWM the existing `TFT_BACKLITE` GPIO with the ESP32 LEDC peripheral. Replace the existing on/off `runSleepManager` with `runBacklightManager`, driven by a single `last_activity_ms_` field bumped from `InputRouter::lastInputMs()` and from new EventBus kinds (`StatusTransitioned`, `PromptArrived`, `TokensChanged`) plus existing `WifiConnected`/`WifiDisconnected`. Three new persisted settings (`dim_timeout_s`, `dim_level_pct`, `full_level_pct`) configure thresholds and brightness levels.

**Tech Stack:** ESP32-S3 (Arduino-ESP32 core), Adafruit ST7789 TFT, ESP32 LEDC PWM, Unity native tests (`pio test -e native`), existing `lib/settings` model, existing `core/EventBus.h` pub/sub.

**Spec:** [docs/superpowers/specs/2026-05-05-backlight-dimming-design.md](../specs/2026-05-05-backlight-dimming-design.md)

---

## File map

**Modified:**
- `lib/settings/settings_model.h` — three new fields + constants
- `lib/settings/settings_model.cpp` — defaults, validators, JSON output, new apply function
- `src/core/Settings.h` / `.cpp` — wrapper method `applyBacklight`
- `src/core/EventBus.h` — three new `EventKind` entries
- `src/net/BleLink.h` / `.cpp` — track previous prompt id and `tokens_today`; publish on delta
- `src/main.cpp` — publish `StatusTransitioned` when `state_derive` returns a new `BuddyState`
- `src/display/Display.h` / `.cpp` — PWM backlight via LEDC; `setBacklight(uint8_t pct)`
- `src/ui/CardController.h` / `.cpp` — rename `runSleepManager` → `runBacklightManager`, add `last_activity_ms_`, subscribe to wake events
- `src/net/HttpServer.cpp` — extend Device form with three new fields, route them through `applyBacklight`

**Created:**
- `lib/backlight/backlight.h` / `.cpp` — pure helper `compute_duty(idle_ms, settings) → uint8_t` (testable off-device)
- `lib/backlight/library.json` — PlatformIO library descriptor
- `test/test_backlight/test_backlight.cpp` — Unity tests for `compute_duty`
- `test/test_settings/test_settings_backlight.cpp` — extra Unity tests for new validation rules and JSON output (or extend `test_settings.cpp`)

**Spec coverage note — test scope tradeoff:** the spec sketched a third native test (`test_event_wake`) that would publish each wake EventKind and assert `last_activity_ms_` was bumped. Implementing it requires a test fixture for `CardController`, which depends on `Display`, `BleLink`, `WifiManager`, `Settings`, and `InputRouter` — all Arduino-coupled. The cost of stubbing those exceeds the value of the test, since the wake path is one-line lambdas and the integration is verified end-to-end in Task 12 step 4. We deliberately ship without `test_event_wake`; if regressions appear here in the future, the right fix is to extract a small `BacklightActivityTracker` seam and unit-test it directly.

> **Note for the engineer:** PlatformIO's Unity runner picks up every `test_*` directory; you can either add a second file in the existing `test_settings/` directory or extend the existing `test_settings.cpp`. Steps below add a separate file for clarity.

---

## Task 1: Settings model — add three fields + constants

**Files:**
- Modify: `lib/settings/settings_model.h`
- Modify: `lib/settings/settings_model.cpp` (`setDefaults`)
- Test: `test/test_settings/test_settings_backlight.cpp` (new)

- [ ] **Step 1: Write the failing test for defaults**

Create `test/test_settings/test_settings_backlight.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "settings_model.h"

using namespace settings;

static Settings make_defaults() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

static void test_backlight_defaults(void) {
    Settings s = make_defaults();
    TEST_ASSERT_EQUAL_UINT16(30, s.dim_timeout_s);
    TEST_ASSERT_EQUAL_UINT8(40, s.dim_level_pct);
    TEST_ASSERT_EQUAL_UINT8(100, s.full_level_pct);
}

extern "C" int run_backlight_settings_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_backlight_defaults);
    return UNITY_END();
}
```

Then append to the existing `test/test_settings/test_settings.cpp` `main()` (or its `setup()` — open it and confirm) a call to `run_backlight_settings_tests()`. If the existing file already has its own `UNITY_BEGIN/END` block, add the new `RUN_TEST(test_backlight_defaults)` line directly into its block instead, and skip the `extern "C"` wrapper above. Pick whichever pattern the existing file uses — do not introduce a second `UNITY_BEGIN/END`.

- [ ] **Step 2: Run the test, confirm it fails**

```
pio test -e native -f test_settings
```

Expected: compile error — `dim_timeout_s`, `dim_level_pct`, `full_level_pct` are not members of `Settings`.

- [ ] **Step 3: Add fields and constants to `settings_model.h`**

Open `lib/settings/settings_model.h`. Below the existing `SLEEP_TIMEOUT_MAX_S` line, add:

```cpp
constexpr uint16_t DIM_TIMEOUT_MIN_S = 5;       // 0 also valid (= disabled)
constexpr uint16_t DIM_TIMEOUT_MAX_S = 3600;
constexpr uint8_t  DIM_LEVEL_MIN_PCT  = 1;
constexpr uint8_t  DIM_LEVEL_MAX_PCT  = 99;
constexpr uint8_t  FULL_LEVEL_MIN_PCT = 1;
constexpr uint8_t  FULL_LEVEL_MAX_PCT = 100;
```

Inside `struct Settings`, after `sleep_timeout_s`, add:

```cpp
    uint16_t dim_timeout_s;       // 0 = never dim
    uint8_t  dim_level_pct;       // 1..99
    uint8_t  full_level_pct;      // 1..100
```

- [ ] **Step 4: Set defaults in `settings_model.cpp::setDefaults`**

Open `lib/settings/settings_model.cpp`. Inside `setDefaults`, after the `s.sleep_timeout_s = 0;` line, add:

```cpp
    s.dim_timeout_s   = 30;
    s.dim_level_pct   = 40;
    s.full_level_pct  = 100;
```

- [ ] **Step 5: Run the test, confirm it passes**

```
pio test -e native -f test_settings
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/settings/settings_model.h lib/settings/settings_model.cpp \
        test/test_settings/test_settings_backlight.cpp \
        test/test_settings/test_settings.cpp   # if you modified its main()
git commit -m "feat(settings): add dim_timeout_s / dim_level_pct / full_level_pct fields"
```

---

## Task 2: Settings model — validators

**Files:**
- Modify: `lib/settings/settings_model.cpp` (anonymous namespace + `validate`)
- Test: `test/test_settings/test_settings_backlight.cpp`

- [ ] **Step 1: Write failing tests for validation rules**

Append to `test/test_settings/test_settings_backlight.cpp`:

```cpp
static void test_dim_timeout_zero_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s = 0;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

static void test_dim_timeout_too_low_rejected(void) {
    Settings s = make_defaults();
    s.dim_timeout_s = 4;  // below DIM_TIMEOUT_MIN_S
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "dim_timeout_s") != nullptr);
}

static void test_dim_level_zero_rejected(void) {
    Settings s = make_defaults();
    s.dim_level_pct = 0;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "dim_level_pct") != nullptr);
}

static void test_full_level_over_100_rejected(void) {
    Settings s = make_defaults();
    s.full_level_pct = 101;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "full_level_pct") != nullptr);
}

static void test_dim_must_precede_sleep(void) {
    Settings s = make_defaults();
    s.dim_timeout_s   = 60;
    s.sleep_timeout_s = 60;     // both non-zero, dim NOT < sleep
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "before") != nullptr);
}

static void test_dim_zero_with_sleep_set_accepted(void) {
    Settings s = make_defaults();
    s.dim_timeout_s   = 0;       // dim disabled
    s.sleep_timeout_s = 60;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}
```

Add `RUN_TEST` lines for each new test in the same place you added `test_backlight_defaults`.

- [ ] **Step 2: Run, confirm they fail**

```
pio test -e native -f test_settings
```

Expected: tests fail because the new fields are not validated.

- [ ] **Step 3: Add validators**

In `lib/settings/settings_model.cpp` anonymous namespace (alongside `isValidSleepTimeout`), add:

```cpp
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
```

- [ ] **Step 4: Wire validators into `validate`**

In the same file's `validate` function, after `isValidSleepTimeout(...)` and before the cards block:

```cpp
    if (!isValidDimTimeout(s.dim_timeout_s, error, error_len)) return false;
    if (!isValidDimLevelPct(s.dim_level_pct, error, error_len)) return false;
    if (!isValidFullLevelPct(s.full_level_pct, error, error_len)) return false;
    if (!isValidDimVsSleep(s.dim_timeout_s, s.sleep_timeout_s,
                           error, error_len)) return false;
```

- [ ] **Step 5: Run, confirm they pass**

```
pio test -e native -f test_settings
```

Expected: all backlight tests PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/settings/settings_model.cpp \
        test/test_settings/test_settings_backlight.cpp
git commit -m "feat(settings): validate dim/full-level pct + dim<sleep ordering"
```

---

## Task 3: Settings model — `applyBacklightFields` + JSON output

**Files:**
- Modify: `lib/settings/settings_model.h` (declaration)
- Modify: `lib/settings/settings_model.cpp` (apply function + `toJson` extension)
- Test: `test/test_settings/test_settings_backlight.cpp`

- [ ] **Step 1: Write failing test for `applyBacklightFields`**

Append to `test/test_settings/test_settings_backlight.cpp`:

```cpp
static void test_apply_backlight_ok(void) {
    Settings s = make_defaults();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyBacklightFields(s, /*dim_s*/15, /*dim_pct*/25,
                                           /*full_pct*/90, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT16(15, s.dim_timeout_s);
    TEST_ASSERT_EQUAL_UINT8(25, s.dim_level_pct);
    TEST_ASSERT_EQUAL_UINT8(90, s.full_level_pct);
}

static void test_apply_backlight_bad_pct_rejected(void) {
    Settings s = make_defaults();
    char err[64] = {};
    TEST_ASSERT_FALSE(applyBacklightFields(s, 30, 0 /*invalid*/, 100,
                                            err, sizeof(err)));
    // Ensure the original was not mutated.
    TEST_ASSERT_EQUAL_UINT8(40, s.dim_level_pct);
}

static void test_tojson_includes_backlight(void) {
    Settings s = make_defaults();
    char buf[1024] = {};
    size_t n = toJson(s, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_TRUE(strstr(buf, "\"dim_timeout_s\":30") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"dim_level_pct\":40") != nullptr);
    TEST_ASSERT_TRUE(strstr(buf, "\"full_level_pct\":100") != nullptr);
}
```

Add `RUN_TEST` lines.

- [ ] **Step 2: Run, confirm fail**

```
pio test -e native -f test_settings
```

Expected: compile error (function not declared) and JSON test fails.

- [ ] **Step 3: Declare `applyBacklightFields` in `settings_model.h`**

After `applyCardsFields(...)`, add:

```cpp
// Patch with new backlight fields. Validates per the rules in validate().
// Returns true on success; false leaves s unchanged and writes the reason.
bool applyBacklightFields(Settings& s,
                          uint16_t dim_timeout_s,
                          uint8_t  dim_level_pct,
                          uint8_t  full_level_pct,
                          char* error, size_t error_len);
```

- [ ] **Step 4: Implement in `settings_model.cpp`**

Add (after `applyCardsFields` body):

```cpp
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
```

- [ ] **Step 5: Extend `toJson`**

Open `toJson` and locate the `snprintf(buf, buf_len, "{\"device_name\":..."` block. Extend the format string and arguments so the parent object also serializes the three new fields. The simplest patch: insert three more `"%s_name":N,` pairs into the same `snprintf`.

Replace the existing parent-object snprintf with:

```cpp
    int written = snprintf(buf, buf_len,
        "{\"device_name\":\"%s\","
        "\"live_timeout_s\":%u,"
        "\"sleep_timeout_s\":%u,"
        "\"dim_timeout_s\":%u,"
        "\"dim_level_pct\":%u,"
        "\"full_level_pct\":%u,"
        "\"boot_card_id\":%u,"
        "\"cards\":[",
        s.device_name,
        (unsigned)s.live_timeout_s,
        (unsigned)s.sleep_timeout_s,
        (unsigned)s.dim_timeout_s,
        (unsigned)s.dim_level_pct,
        (unsigned)s.full_level_pct,
        (unsigned)s.boot_card_id);
```

(No other changes needed in `toJson` — the cards array logic continues unchanged.)

- [ ] **Step 6: Run, confirm pass**

```
pio test -e native -f test_settings
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add lib/settings/settings_model.h lib/settings/settings_model.cpp \
        test/test_settings/test_settings_backlight.cpp
git commit -m "feat(settings): applyBacklightFields + toJson includes backlight fields"
```

---

## Task 4: Settings wrapper — `applyBacklight`

**Files:**
- Modify: `src/core/Settings.h` / `Settings.cpp`

> **Note:** This wrapper is exercised end-to-end by Task 11 (HTTP route). No unit test added here because the wrapper is a thin pass-through: validation already lives in `applyBacklightFields`, persistence is shared with the existing `applyDevice`/`applyCards` paths, and the EventBus publish is a one-line forward. Adding a test would mean stubbing NVS persistence on native, which is more wiring than the test would catch.

- [ ] **Step 1: Add declaration**

In `src/core/Settings.h`, after `applyCards(...)` declaration, add:

```cpp
    // Applies a backlight-section update.
    bool applyBacklight(uint16_t dim_timeout_s,
                        uint8_t  dim_level_pct,
                        uint8_t  full_level_pct,
                        char* err, size_t err_len);
```

- [ ] **Step 2: Add implementation**

In `src/core/Settings.cpp`, after `applyCards` body, add:

```cpp
bool Settings::applyBacklight(uint16_t dim_timeout_s,
                              uint8_t  dim_level_pct,
                              uint8_t  full_level_pct,
                              char* err, size_t err_len) {
    settings::Settings next = data_;
    if (!settings::applyBacklightFields(next, dim_timeout_s, dim_level_pct,
                                        full_level_pct, err, err_len)) {
        return false;
    }
    data_ = next;
    persist();
    if (bus_) bus_->publish(EventKind::SettingsChanged);
    return true;
}
```

- [ ] **Step 3: Build firmware to confirm it compiles**

```
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build success.

- [ ] **Step 4: Commit**

```bash
git add src/core/Settings.h src/core/Settings.cpp
git commit -m "feat(settings): wrapper Settings::applyBacklight"
```

---

## Task 5: Backlight duty helper + tests

**Files:**
- Create: `lib/backlight/backlight.h`
- Create: `lib/backlight/backlight.cpp`
- Create: `lib/backlight/library.json`
- Create: `test/test_backlight/test_backlight.cpp`

- [ ] **Step 1: Create the library descriptor**

Create `lib/backlight/library.json`:

```json
{
  "name": "backlight",
  "version": "0.1.0",
  "description": "Backlight duty computation (idle-driven state machine)",
  "build": { "flags": ["-std=gnu++17"] }
}
```

- [ ] **Step 2: Write the failing test**

Create `test/test_backlight/test_backlight.cpp`:

```cpp
#include <unity.h>
#include "backlight.h"
#include "settings_model.h"

using namespace settings;

static Settings defaults_with(uint16_t dim_s, uint8_t dim_pct,
                              uint8_t full_pct, uint16_t sleep_s) {
    Settings s = {};
    setDefaults(s, "Test");
    s.dim_timeout_s   = dim_s;
    s.dim_level_pct   = dim_pct;
    s.full_level_pct  = full_pct;
    s.sleep_timeout_s = sleep_s;
    return s;
}

void setUp(void) {}
void tearDown(void) {}

static void test_zero_idle_returns_full(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(0, s));
}

static void test_just_below_dim_threshold_returns_full(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(29'999, s));
}

static void test_at_dim_threshold_returns_dim(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(40, backlight_compute_duty(30'000, s));
}

static void test_just_below_sleep_returns_dim(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(40, backlight_compute_duty(299'999, s));
}

static void test_at_sleep_returns_off(void) {
    Settings s = defaults_with(30, 40, 100, 300);
    TEST_ASSERT_EQUAL_UINT8(0, backlight_compute_duty(300'000, s));
}

static void test_dim_disabled_skips_to_off(void) {
    Settings s = defaults_with(/*dim_s*/0, 40, 100, /*sleep_s*/60);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(0,      s));
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(59'999, s));
    TEST_ASSERT_EQUAL_UINT8(0,   backlight_compute_duty(60'000, s));
}

static void test_sleep_disabled_caps_at_dim(void) {
    Settings s = defaults_with(/*dim_s*/30, 40, 100, /*sleep_s*/0);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(0,        s));
    TEST_ASSERT_EQUAL_UINT8(40,  backlight_compute_duty(30'000,   s));
    TEST_ASSERT_EQUAL_UINT8(40,  backlight_compute_duty(3'600'000, s));
}

static void test_both_disabled_always_full(void) {
    Settings s = defaults_with(/*dim_s*/0, 40, 100, /*sleep_s*/0);
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(0,         s));
    TEST_ASSERT_EQUAL_UINT8(100, backlight_compute_duty(86'400'000, s));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_idle_returns_full);
    RUN_TEST(test_just_below_dim_threshold_returns_full);
    RUN_TEST(test_at_dim_threshold_returns_dim);
    RUN_TEST(test_just_below_sleep_returns_dim);
    RUN_TEST(test_at_sleep_returns_off);
    RUN_TEST(test_dim_disabled_skips_to_off);
    RUN_TEST(test_sleep_disabled_caps_at_dim);
    RUN_TEST(test_both_disabled_always_full);
    return UNITY_END();
}
```

- [ ] **Step 3: Run, confirm fail**

```
pio test -e native -f test_backlight
```

Expected: compile error — `backlight.h` does not exist.

- [ ] **Step 4: Implement the helper**

Create `lib/backlight/backlight.h`:

```cpp
#pragma once

#include <stdint.h>
#include "settings_model.h"

// Returns the desired backlight duty (0..100) for the given idle duration
// and settings. Pure function; no side effects.
uint8_t backlight_compute_duty(uint32_t idle_ms,
                               const settings::Settings& s);
```

Create `lib/backlight/backlight.cpp`:

```cpp
#include "backlight.h"

uint8_t backlight_compute_duty(uint32_t idle_ms,
                               const settings::Settings& s) {
    if (s.sleep_timeout_s != 0 &&
        idle_ms >= static_cast<uint32_t>(s.sleep_timeout_s) * 1000UL) {
        return 0;
    }
    if (s.dim_timeout_s != 0 &&
        idle_ms >= static_cast<uint32_t>(s.dim_timeout_s) * 1000UL) {
        return s.dim_level_pct;
    }
    return s.full_level_pct;
}
```

- [ ] **Step 5: Run, confirm pass**

```
pio test -e native -f test_backlight
```

Expected: 8/8 PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/backlight/ test/test_backlight/
git commit -m "feat(backlight): pure compute_duty helper + native tests"
```

---

## Task 6: Display PWM via LEDC

**Files:**
- Modify: `src/display/Display.h`
- Modify: `src/display/Display.cpp`

> **No native test** — depends on Arduino LEDC API. Verified by build and the manual smoke test in Task 12.

> **API status (2026-05-09):** the v2.x LEDC functions used in earlier drafts
> of this task (`ledcSetup`, `ledcAttachPin`, channel-indexed `ledcWrite`)
> were removed in Arduino-ESP32 v3.x, which the project now targets. The
> snippets below have been updated to the v3.x API (single-call
> `ledcAttach(pin, freq, res)` and pin-indexed `ledcWrite(pin, duty)`); the
> `kBacklightChannel` constant is gone (v3.x assigns the channel
> automatically). If your `Display.cpp` / `Display.h` already match the
> snippets below, skip to Task 7.

- [ ] **Step 1: Update `Display.h`**

Replace the existing class body in `src/display/Display.h` so it reads:

```cpp
#pragma once

#include <stdint.h>
#include <Adafruit_ST7789.h>

// 240x135 ST7789 backed by SPI on the Reverse-TFT Feather.
class Display {
public:
    Display();
    void begin();

    // Set the backlight brightness as a 0..100 percent. Values above 100
    // are clamped. 0 means off (digital low). No-op if the requested duty
    // matches the currently applied duty.
    void setBacklight(uint8_t pct);

    // Convenience wrapper: true → 100, false → 0. Kept so existing
    // callers compile unchanged.
    void setBacklight(bool on) { setBacklight(static_cast<uint8_t>(on ? 100 : 0)); }

    bool    isAsleep()      const { return current_pct_ == 0; }
    uint8_t backlightPct()  const { return current_pct_; }

    Adafruit_ST7789& tft() { return tft_; }

private:
    // Arduino-ESP32 v3.x: ledcAttach() picks the channel automatically,
    // so we no longer track one. Frequency / resolution still need to
    // match the duty math in setBacklight().
    static constexpr int kBacklightFreqHz  = 5000;
    static constexpr int kBacklightBits    = 8;

    Adafruit_ST7789 tft_;
    bool    asleep_       = false;  // legacy field, kept for API parity
    uint8_t current_pct_  = 0;
};
```

> **Compatibility check before saving:** open the existing `Display.h` first and copy across any other public methods/fields you find (e.g. additional getters, friend declarations) — only the backlight-related sections should change. The snippet above lists what we know is present (`tft()`, `isAsleep`, `begin`).

- [ ] **Step 2: Update `Display.cpp`**

Open `src/display/Display.cpp`. Replace the body so it reads:

```cpp
#include "Display.h"

#include <Arduino.h>
#include <algorithm>

Display::Display() : tft_(TFT_CS, TFT_DC, TFT_RST) {}

void Display::begin() {
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);

    // Configure LEDC for the backlight before driving the pin.
    // Arduino-ESP32 v3.x: ledcAttach() replaces ledcSetup + ledcAttachPin
    // and picks the channel automatically.
    ledcAttach(TFT_BACKLITE, kBacklightFreqHz, kBacklightBits);
    current_pct_ = 0;
    setBacklight(static_cast<uint8_t>(100));

    tft_.init(135, 240);
    tft_.setRotation(1);
    tft_.fillScreen(ST77XX_BLACK);
    tft_.setTextWrap(false);
}

void Display::setBacklight(uint8_t pct) {
    if (pct > 100) pct = 100;
    if (pct == current_pct_) return;
    uint32_t duty = (static_cast<uint32_t>(pct) * 255UL) / 100UL;
    // v3.x: ledcWrite is keyed on the pin, not a channel.
    ledcWrite(TFT_BACKLITE, duty);
    current_pct_ = pct;
    asleep_      = (pct == 0);
}
```

- [ ] **Step 3: Build firmware**

```
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build success. If a caller passes `setBacklight(true/false)`, the inline wrapper handles it — no further edits needed at this point.

- [ ] **Step 4: Commit**

```bash
git add src/display/Display.h src/display/Display.cpp
git commit -m "feat(display): PWM backlight via LEDC, percent-based API"
```

---

## Task 7: New EventBus kinds

**Files:**
- Modify: `src/core/EventBus.h`

- [ ] **Step 1: Add the new kinds**

Open `src/core/EventBus.h`. Inside `enum class EventKind`, before `Count_`, add:

```cpp
    StatusTransitioned, // BuddyState transitioned (publisher: main loop)
    PromptArrived,      // new non-empty prompt id received
    TokensChanged,      // tokens_today changed value
```

- [ ] **Step 2: Build firmware**

```
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build success (the `vector<>[Count_]` array auto-resizes).

- [ ] **Step 3: Commit**

```bash
git add src/core/EventBus.h
git commit -m "feat(eventbus): StatusTransitioned / PromptArrived / TokensChanged kinds"
```

---

## Task 8: BleLink — publish PromptArrived + TokensChanged on delta

**Files:**
- Modify: `src/net/BleLink.h`
- Modify: `src/net/BleLink.cpp`

- [ ] **Step 1: Add tracking fields to `BleLink.h`**

In `class BleLink`'s `private:` section, add:

```cpp
    char     prev_prompt_id_[40] = {0};
    uint32_t prev_tokens_today_  = 0;
```

(Match the size of `ClaudeStatus::ClaudePrompt::id` from `lib/protocol/protocol.h` — currently 40.)

- [ ] **Step 2: Publish on snapshot success**

In `src/net/BleLink.cpp::tick`, locate the existing block:

```cpp
if (protocol_parse_line(line_buf_, &app_.mutableStatus())) {
    app_.markSnapshot(now_ms);
    Serial.printf("[rx] %s\n", line_buf_);
    if (bus_) bus_->publish(EventKind::SnapshotReceived);
}
```

Replace with:

```cpp
if (protocol_parse_line(line_buf_, &app_.mutableStatus())) {
    app_.markSnapshot(now_ms);
    Serial.printf("[rx] %s\n", line_buf_);

    const ClaudeStatus& st = app_.status();

    // PromptArrived: present, has an id, and id differs from previous.
    if (st.prompt.present && st.prompt.id[0] != 0 &&
        strncmp(prev_prompt_id_, st.prompt.id, sizeof(prev_prompt_id_)) != 0) {
        strncpy(prev_prompt_id_, st.prompt.id, sizeof(prev_prompt_id_) - 1);
        prev_prompt_id_[sizeof(prev_prompt_id_) - 1] = 0;
        if (bus_) bus_->publish(EventKind::PromptArrived);
    }

    // TokensChanged: any change to tokens_today.
    if (st.tokens_today != prev_tokens_today_) {
        prev_tokens_today_ = st.tokens_today;
        if (bus_) bus_->publish(EventKind::TokensChanged);
    }

    if (bus_) bus_->publish(EventKind::SnapshotReceived);
}
```

`<string.h>` is already included near the top of `BleLink.cpp` (`#include <string.h>`); no new include needed.

> **Why id-based prompt comparison:** the prompt struct holds a request id from the bridge. Comparing by id covers "new tool request" cleanly without reasoning about empty-vs-full text edits. If `prompt.present == false`, no publish — the prompt was retracted, not arrived.

- [ ] **Step 3: Build firmware**

```
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build success.

- [ ] **Step 4: Commit**

```bash
git add src/net/BleLink.h src/net/BleLink.cpp
git commit -m "feat(ble): publish PromptArrived and TokensChanged on snapshot delta"
```

---

## Task 9: main.cpp — publish StatusTransitioned

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add bus member to publish call**

`main.cpp` already has `eventBus` as a static. In `loop()`, locate:

```cpp
    appState.setBuddyState(state_derive(appState.status(), appState.isLive(now)));
```

Replace with:

```cpp
    {
        BuddyState prev = appState.buddyState();
        BuddyState next = state_derive(appState.status(), appState.isLive(now));
        if (next != prev) {
            appState.setBuddyState(next);
            eventBus.publish(EventKind::StatusTransitioned);
        }
    }
```

> **Note:** `setup()` also calls `setBuddyState(state_derive(...))` once. Leave that call alone — there is no "previous" state at boot, so we deliberately do not publish there.

- [ ] **Step 2: Build firmware**

```
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build success.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat(main): publish StatusTransitioned on BuddyState change"
```

---

## Task 10: CardController — backlight manager + wake subscriptions

**Files:**
- Modify: `src/ui/CardController.h`
- Modify: `src/ui/CardController.cpp`

- [ ] **Step 1: Replace `runSleepManager` with `runBacklightManager` in the header**

In `src/ui/CardController.h`, change the private method declaration from

```cpp
    void runSleepManager(uint32_t now_ms, Display& display);
```

to

```cpp
    void runBacklightManager(uint32_t now_ms, Display& display);
```

In the same private section, add a new field:

```cpp
    uint32_t last_activity_ms_ = 0;
```

- [ ] **Step 2: Update the implementation**

Open `src/ui/CardController.cpp`. Add at the top with the other `#include`s:

```cpp
#include "backlight.h"
```

> **PlatformIO note:** `lib_ldf_mode = deep` in `platformio.ini` means PlatformIO auto-adds every `lib/<name>/` directory to the include path. Use the bare header name — the same pattern used elsewhere (`#include "settings_model.h"`, `#include "protocol.h"`).

In `CardController::begin()`, after the existing `bus_.subscribe(...)` calls, add:

```cpp
    last_activity_ms_ = millis();

    auto bump_activity = [this] {
        last_activity_ms_ = millis();
    };
    bus_.subscribe(EventKind::StatusTransitioned, bump_activity);
    bus_.subscribe(EventKind::PromptArrived,      bump_activity);
    // NOTE: TokensChanged is intentionally NOT a wake source. tokens_today
    // ticks during every Claude turn that consumes tokens, so subscribing
    // here would prevent the screen from ever dimming while a session is
    // active. The event is still published by BleLink so future telemetry
    // / UI features can consume it; it just doesn't bump activity.
    // WifiConnected/WifiDisconnected are already subscribed for invalidate;
    // also bump activity from them.
    bus_.subscribe(EventKind::WifiConnected,    bump_activity);
    bus_.subscribe(EventKind::WifiDisconnected, bump_activity);
```

> **Follow-up (deferred):** `StatusTransitioned` fires on every
> `BuddyState` change, including `WORKING → IDLE` after each Claude turn.
> During a chatty session this also fights the dim timeout, though less
> aggressively than `TokensChanged` would. If hardware testing in Task 12
> shows the screen rarely dims during real usage, the right next move is
> to filter the wake at subscribe time — read `appState.buddyState()` in
> the lambda and bump only on transitions into attention-worthy states
> (e.g. `PROMPT`, `ERROR`, `WORKING` start). We don't pre-empt that here
> because it depends on the actual `BuddyState` enum members and on the
> behavior the user wants confirmed in practice.

Replace the entire `runSleepManager` body with `runBacklightManager`:

```cpp
void CardController::runBacklightManager(uint32_t now_ms, Display& display) {
    if (!input_) return;

    // Fold the latest input timestamp into our local clock so that input
    // events that occurred since the last tick count as activity.
    uint32_t last_input = input_->lastInputMs();
    if (last_input > last_activity_ms_) last_activity_ms_ = last_input;

    uint32_t idle_ms = now_ms - last_activity_ms_;
    uint8_t  pct     = backlight_compute_duty(idle_ms, settings_.data());

    bool was_off = display.isAsleep();
    display.setBacklight(pct);
    if (was_off && pct != 0) {
        if (Card* a = stack_.active()) a->invalidate();
    }
}
```

In `CardController::tick(...)`, change the first line from

```cpp
    runSleepManager(now_ms, display);
```

to

```cpp
    runBacklightManager(now_ms, display);
```

- [ ] **Step 3: Update the header comment**

In `src/ui/CardController.h`, change the class doc comment from

```
// (backlight off after settings.sleep_timeout_s of input idleness).
```

to

```
// (FULL → DIM → OFF backlight management driven by input idleness +
// meaningful EventBus wakes).
```

- [ ] **Step 4: Build firmware**

```
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build success. The previous `display.setBacklight(true/false)` calls in this file still work via the bool-overload.

- [ ] **Step 5: Commit**

```bash
git add src/ui/CardController.h src/ui/CardController.cpp
git commit -m "feat(card-controller): backlight manager driven by activity + wake events"
```

---

## Task 11: HttpServer — expose backlight settings to the web UI

**Files:**
- Modify: `src/net/HttpServer.cpp`

- [ ] **Step 1: Extend the Device form HTML**

Open `src/net/HttpServer.cpp`. Locate the `<div class=section><h2>Device</h2>` block (around line 257). After the existing `Sleep timeout (s)` `<div class=form-group>`, insert:

```cpp
                "<div class=form-group>"
                  "<label for=dt>Dim timeout (s)</label>"
                  "<input id=dt name=dim_timeout_s type=number min=0 max=3600 required>"
                  "<p class=tip>Backlight dims after N seconds idle. 0 disables. Otherwise 5..3600. Must be less than sleep timeout.</p>"
                "</div>"
                "<div class=form-group>"
                  "<label for=dl>Dim level (%)</label>"
                  "<input id=dl name=dim_level_pct type=number min=1 max=99 required>"
                  "<p class=tip>Brightness while dimmed. 1..99.</p>"
                "</div>"
                "<div class=form-group>"
                  "<label for=fl>Full level (%)</label>"
                  "<input id=fl name=full_level_pct type=number min=1 max=100 required>"
                  "<p class=tip>Brightness while active. 1..100.</p>"
                "</div>"
```

- [ ] **Step 2: Pre-fill the new fields in `loadSettings`**

Find the `async function loadSettings` block (around line 360). After the existing

```cpp
              "$('st').value=s.sleep_timeout_s;"
```

insert:

```cpp
              "$('dt').value=s.dim_timeout_s;"
              "$('dl').value=s.dim_level_pct;"
              "$('fl').value=s.full_level_pct;"
```

- [ ] **Step 3: Submit the new fields**

Find the existing `saveDevice` JS handler (search for `device-form`). Make sure the form submission posts every field name listed in the form. The existing pattern does `new FormData(form)`, so new inputs are picked up automatically — but verify by reading the current JS. If it constructs the body manually, append `&dim_timeout_s=...&dim_level_pct=...&full_level_pct=...` in the same style.

If a manual body is constructed, e.g.:

```js
const body = `name=${...}&live_timeout_s=${$('lt').value}&sleep_timeout_s=${$('st').value}`;
```

extend it to:

```js
const body = `name=${...}&live_timeout_s=${$('lt').value}&sleep_timeout_s=${$('st').value}`+
             `&dim_timeout_s=${$('dt').value}&dim_level_pct=${$('dl').value}&full_level_pct=${$('fl').value}`;
```

- [ ] **Step 4: Server-side handler — extend `/api/settings/device`**

Find `server_->on("/api/settings/device", HTTP_POST, [this]() {` (around line 501). Replace its body with:

```cpp
        if (!server_->hasArg("name") || !server_->hasArg("live_timeout_s") ||
            !server_->hasArg("sleep_timeout_s") ||
            !server_->hasArg("dim_timeout_s") ||
            !server_->hasArg("dim_level_pct") ||
            !server_->hasArg("full_level_pct")) {
            sendJsonError(server_, 400, "missing field");
            return;
        }
        String name = server_->arg("name");
        long lt = server_->arg("live_timeout_s").toInt();
        long st = server_->arg("sleep_timeout_s").toInt();
        long dt = server_->arg("dim_timeout_s").toInt();
        long dl = server_->arg("dim_level_pct").toInt();
        long fl = server_->arg("full_level_pct").toInt();
        if (lt < 0 || lt > 0xFFFF || st < 0 || st > 0xFFFF ||
            dt < 0 || dt > 0xFFFF ||
            dl < 0 || dl > 0xFF ||
            fl < 0 || fl > 0xFF) {
            sendJsonError(server_, 400, "field out of range");
            return;
        }
        char err[64] = {};
        if (!settings_.applyDevice(name.c_str(),
                                   static_cast<uint16_t>(lt),
                                   static_cast<uint16_t>(st),
                                   err, sizeof(err))) {
            sendJsonError(server_, 400, err);
            return;
        }
        if (!settings_.applyBacklight(static_cast<uint16_t>(dt),
                                      static_cast<uint8_t>(dl),
                                      static_cast<uint8_t>(fl),
                                      err, sizeof(err))) {
            sendJsonError(server_, 400, err);
            return;
        }
        sendJsonOk(server_);
```

> **Note on atomicity:** if `applyBacklight` fails after `applyDevice` already succeeded, we keep the device-side change but reject the request with the backlight error. The user's form will still show their attempted backlight values from the prior `loadSettings`, and a re-submit will push both. This is acceptable — better than rolling back via a third "transactional" path. The form-level validation makes both sides failing simultaneously rare in practice.

- [ ] **Step 5: Build firmware, flash to device**

```
pio run -e adafruit_feather_esp32s3_reversetft
pio run -e adafruit_feather_esp32s3_reversetft -t upload
```

Expected: build + upload success.

- [ ] **Step 6: Smoke-test the form in a browser**

- Open `http://<device-ip>/` (or AP fallback).
- Verify the Device section now lists Dim timeout, Dim level, Full level, pre-filled with `30`, `40`, `100`.
- Save the form unchanged → `Device saved` (or equivalent green status).
- Save with `dim_timeout_s = 60` and `sleep_timeout_s = 30` → expect a 400 with the "before" error message.

- [ ] **Step 7: Commit**

```bash
git add src/net/HttpServer.cpp
git commit -m "feat(http): expose dim_timeout / dim_level / full_level on Device form"
```

---

## Task 12: Hardware smoke test

**No code changes; this is the acceptance gate.** Run on the physical Feather.

- [ ] **Step 1: Flash and connect to a Claude Code session**

```
pio run -e adafruit_feather_esp32s3_reversetft -t upload
pio device monitor
```

Pair the device over BLE with the Claude Code desktop app's device-buddy bridge.

- [ ] **Step 2: Verify FULL → DIM → OFF**

- Set defaults via the web UI: dim_timeout=30, dim_level=40, full_level=100, sleep_timeout=300.
- Stop interacting. Watch the screen.
- At ~30 s of idleness the backlight visibly dims. Pixels should remain visible — verify the eyes/status card is still showing through dim.
- At ~5 min the backlight turns off entirely.

- [ ] **Step 3: Verify wake from local input**

While OFF, press D0, D1, or D2. Backlight should snap to FULL within one frame (~16 ms). The active card should repaint correctly (no stale pixels from before sleep).

- [ ] **Step 4: Verify wake from Claude-side events**

While DIM (in the 30 s..300 s window), verify each of:

- A `BuddyState` transition **wakes**: start a long Claude run from the Claude Code app, then stop it. Each transition (IDLE→WORKING and back) should restore FULL.
- A new prompt **wakes**: have Claude ask a tool-use question with a Permission prompt. Restore should be immediate.
- A `tokens_today` change **does NOT wake on its own**: while the screen is dim, run a Claude command that consumes tokens but does *not* change `BuddyState` or surface a new prompt (e.g. a short follow-up while already WORKING). `tokens_today` will tick up but the backlight should stay dim. If it wakes, `TokensChanged` is still subscribed in `CardController::begin()` — remove that subscription.

While DIM, *no* wake should trigger from idle keepalive snapshots. Leave the device alone for >2 minutes inside the dim window with Claude connected but idle — backlight should stay dim, then transition to off at the sleep threshold.

- [ ] **Step 5: Verify settings persistence**

Power-cycle the device. After reboot, open the web UI. The values you set in Step 2 must round-trip — defaults should not have replaced them.

- [ ] **Step 6: Manual measurement (optional but recommended)**

If you have a USB power meter, log the device current at FULL, DIM, and OFF for ~30 s each. Note the values in the PR description — it is the only direct evidence that the change actually saves power.

- [ ] **Step 7: Commit a hardware-test note**

If everything passes, no commit needed. If anything fails, do not paper over it — open a follow-up issue and revert / amend the offending task before merging.
