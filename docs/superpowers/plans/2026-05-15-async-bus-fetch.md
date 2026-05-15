# Async Bus Fetch & Neighbour Preload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the bus arrival HTTP fetch off the main loop into a single shared persistent FreeRTOS worker, and have `CardController` preload the immediate up/down neighbours of the visible card so flips between cards never freeze the UI.

**Architecture:** A new `net::BusFetchService` owns one persistent worker task (10 KB stack), a 4-entry request table, and a 4-entry result staging buffer that doubles as the preload cache. The pure slot-pick and priority-upgrade logic lives in a host-testable `lib/bus_fetch_logic/` library. `BusCard` drops its private `BusArrivalsFetcher` member and instead pulls results from the service in `tick()` and `onShow()`. `CardController` triggers neighbour preloads ~750 ms after the carousel settles. A synchronous fallback engages if `xTaskCreate` fails so the device never bricks.

**Tech Stack:** Arduino-ESP32 v3.x (pioarduino fork), FreeRTOS (xTaskCreate, SemaphoreHandle_t mutex + binary semaphore), the existing `BusArrivalsFetcher` (HTTPClient + NetworkClientSecure), Unity (PlatformIO native env) for the pure-logic tests.

**Reference spec:** `docs/superpowers/specs/2026-05-15-async-bus-fetch-design.md` (commit `178b921`).

---

## File Structure

| Path | Status | Responsibility |
|---|---|---|
| `lib/bus_fetch_logic/library.json` | create | PlatformIO library manifest |
| `lib/bus_fetch_logic/bus_fetch_logic.h` | create | `FetchPriority` enum, `SlotRequest` struct, `pickHighestPriority`, `applyRequest` declarations |
| `lib/bus_fetch_logic/bus_fetch_logic.cpp` | create | Pure C++ implementations of the two functions |
| `test/test_bus_fetch_logic/test_bus_fetch_logic.cpp` | create | Unity tests for the pure logic |
| `src/net/BusFetchService.h` | create | `BusFetchService` class (worker task + tables + sync fallback) |
| `src/net/BusFetchService.cpp` | create | Worker loop, request/takeResult, sync fallback path, stack-watermark log |
| `src/ui/cards/BusCard.h` | modify | Drop `BusArrivalsFetcher fetcher_`; constructor takes `BusFetchService& service` |
| `src/ui/cards/BusCard.cpp` | modify | Drop `doFetch`; `tick()` polls `takeResult` + submits async; `onShow()` pulls preload |
| `src/ui/CardController.h` | modify | Constructor takes `BusFetchService&`; new preload-debounce members |
| `src/ui/CardController.cpp` | modify | Pass service to BusCards; preload trigger in `tick()` |
| `src/main.cpp` | modify | Create `BusFetchService`, call `begin()`, pass to `CardController` |

No `platformio.ini` change required (`bus_fetch_logic` is auto-discovered from `lib/` per `lib_ldf_mode = deep`).

---

## Conventions used in this plan

- **Build firmware:** `pio run -e adafruit_feather_esp32s3_reversetft`. To upload, append `-t upload`.
- **Run all native tests:** `pio test -e native`.
- **Run a single native test suite:** `pio test -e native -f test_<name>`.
- **Each task ends with a commit.** Use the suggested message verbatim or adjust for accuracy.
- **TDD where testable** (the pure logic). The worker, FreeRTOS wiring, and on-card behaviour are verified by build + on-device manual checklist (Task 6).

---

## Task 1: Pure logic library + tests (TDD)

**Files:**
- Create: `lib/bus_fetch_logic/library.json`
- Create: `lib/bus_fetch_logic/bus_fetch_logic.h`
- Create: `lib/bus_fetch_logic/bus_fetch_logic.cpp`
- Create: `test/test_bus_fetch_logic/test_bus_fetch_logic.cpp`

- [ ] **Step 1: Create the PlatformIO library manifest**

Create `lib/bus_fetch_logic/library.json`:

```json
{
  "name": "bus_fetch_logic",
  "version": "0.1.0",
  "description": "Pure-C++ slot-request table logic for the bus fetch service.",
  "frameworks": "*",
  "platforms": "*"
}
```

- [ ] **Step 2: Write the public header**

Create `lib/bus_fetch_logic/bus_fetch_logic.h`:

```cpp
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings_model.h"   // settings::MAX_BUS_STOPS, settings::BUS_STOP_CODE_LEN

namespace bus_fetch_logic {

enum class FetchPriority : uint8_t { LOW = 0, HIGH = 1 };

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
```

- [ ] **Step 3: Stub the implementation so the link succeeds**

Create `lib/bus_fetch_logic/bus_fetch_logic.cpp`:

```cpp
#include "bus_fetch_logic.h"

#include <string.h>

namespace bus_fetch_logic {

int pickHighestPriority(const SlotRequest (&/*table*/)[settings::MAX_BUS_STOPS]) {
    return -1;   // stub — Step 6 fills this in
}

void applyRequest(SlotRequest& /*entry*/, const char* /*code*/,
                  FetchPriority /*new_prio*/) {
    // stub — Step 7 fills this in
}

}  // namespace bus_fetch_logic
```

- [ ] **Step 4: Write the failing tests**

Create `test/test_bus_fetch_logic/test_bus_fetch_logic.cpp`:

```cpp
#include <unity.h>
#include <stdint.h>
#include <string.h>

#include "bus_fetch_logic.h"

void setUp(void) {}
void tearDown(void) {}

using bus_fetch_logic::FetchPriority;
using bus_fetch_logic::SlotRequest;
using bus_fetch_logic::pickHighestPriority;
using bus_fetch_logic::applyRequest;

static SlotRequest blank_table[settings::MAX_BUS_STOPS];

static void clearTable() {
    for (size_t i = 0; i < settings::MAX_BUS_STOPS; ++i) {
        blank_table[i].code[0] = '\0';
        blank_table[i].prio    = FetchPriority::LOW;
        blank_table[i].wanted  = false;
    }
}

// Populate a slot directly (no applyRequest) so pick tests are independent
// of applyRequest correctness.
static void setSlot(size_t idx, FetchPriority prio, bool wanted,
                    const char* code = "00000") {
    strncpy(blank_table[idx].code, code, settings::BUS_STOP_CODE_LEN);
    blank_table[idx].code[settings::BUS_STOP_CODE_LEN] = '\0';
    blank_table[idx].prio   = prio;
    blank_table[idx].wanted = wanted;
}

static void test_pick_empty_returns_minus_one(void) {
    clearTable();
    TEST_ASSERT_EQUAL_INT(-1, pickHighestPriority(blank_table));
}

static void test_pick_single_low_returns_that_slot(void) {
    clearTable();
    setSlot(2, FetchPriority::LOW, true);
    TEST_ASSERT_EQUAL_INT(2, pickHighestPriority(blank_table));
}

static void test_pick_single_high_returns_that_slot(void) {
    clearTable();
    setSlot(1, FetchPriority::HIGH, true);
    TEST_ASSERT_EQUAL_INT(1, pickHighestPriority(blank_table));
}

static void test_pick_high_wins_over_low_in_different_slots(void) {
    clearTable();
    setSlot(0, FetchPriority::LOW,  true);
    setSlot(3, FetchPriority::HIGH, true);
    TEST_ASSERT_EQUAL_INT(3, pickHighestPriority(blank_table));
}

static void test_pick_equal_priority_lowest_index_wins(void) {
    clearTable();
    setSlot(2, FetchPriority::LOW, true);
    setSlot(3, FetchPriority::LOW, true);
    TEST_ASSERT_EQUAL_INT(2, pickHighestPriority(blank_table));
}

static void test_pick_ignores_not_wanted(void) {
    clearTable();
    setSlot(0, FetchPriority::HIGH, false);   // present but not wanted
    setSlot(2, FetchPriority::LOW,  true);
    TEST_ASSERT_EQUAL_INT(2, pickHighestPriority(blank_table));
}

static void test_apply_to_empty_entry_sets_low(void) {
    clearTable();
    applyRequest(blank_table[1], "50171", FetchPriority::LOW);
    TEST_ASSERT_TRUE(blank_table[1].wanted);
    TEST_ASSERT_EQUAL_INT((int)FetchPriority::LOW, (int)blank_table[1].prio);
    TEST_ASSERT_EQUAL_STRING("50171", blank_table[1].code);
}

static void test_apply_low_then_high_upgrades_in_place(void) {
    clearTable();
    applyRequest(blank_table[1], "50171", FetchPriority::LOW);
    applyRequest(blank_table[1], "50171", FetchPriority::HIGH);
    TEST_ASSERT_TRUE(blank_table[1].wanted);
    TEST_ASSERT_EQUAL_INT((int)FetchPriority::HIGH, (int)blank_table[1].prio);
}

static void test_apply_high_then_low_does_not_downgrade(void) {
    clearTable();
    applyRequest(blank_table[1], "50171", FetchPriority::HIGH);
    applyRequest(blank_table[1], "50171", FetchPriority::LOW);
    TEST_ASSERT_TRUE(blank_table[1].wanted);
    TEST_ASSERT_EQUAL_INT((int)FetchPriority::HIGH, (int)blank_table[1].prio);
}

static void test_apply_refreshes_code_each_call(void) {
    clearTable();
    applyRequest(blank_table[0], "50171", FetchPriority::LOW);
    applyRequest(blank_table[0], "54321", FetchPriority::LOW);
    TEST_ASSERT_EQUAL_STRING("54321", blank_table[0].code);
}

static void test_apply_truncates_overlong_code_safely(void) {
    clearTable();
    applyRequest(blank_table[0], "1234567", FetchPriority::LOW);
    TEST_ASSERT_EQUAL_size_t(settings::BUS_STOP_CODE_LEN,
                             strlen(blank_table[0].code));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_pick_empty_returns_minus_one);
    RUN_TEST(test_pick_single_low_returns_that_slot);
    RUN_TEST(test_pick_single_high_returns_that_slot);
    RUN_TEST(test_pick_high_wins_over_low_in_different_slots);
    RUN_TEST(test_pick_equal_priority_lowest_index_wins);
    RUN_TEST(test_pick_ignores_not_wanted);
    RUN_TEST(test_apply_to_empty_entry_sets_low);
    RUN_TEST(test_apply_low_then_high_upgrades_in_place);
    RUN_TEST(test_apply_high_then_low_does_not_downgrade);
    RUN_TEST(test_apply_refreshes_code_each_call);
    RUN_TEST(test_apply_truncates_overlong_code_safely);
    return UNITY_END();
}
```

- [ ] **Step 5: Run, expect failure**

Run: `pio test -e native -f test_bus_fetch_logic 2>&1 | tail -20`

Expected: most tests fail (stub `pickHighestPriority` always returns -1; stub `applyRequest` never sets `wanted`).

- [ ] **Step 6: Implement `pickHighestPriority`**

Replace the stub in `lib/bus_fetch_logic/bus_fetch_logic.cpp`:

```cpp
int pickHighestPriority(const SlotRequest (&table)[settings::MAX_BUS_STOPS]) {
    int best = -1;
    for (size_t i = 0; i < settings::MAX_BUS_STOPS; ++i) {
        if (!table[i].wanted) continue;
        // Strict > on prio means equal priorities keep the earlier (lower-
        // index) slot we already chose, which is the deterministic tie rule.
        if (best < 0 || table[i].prio > table[best].prio) {
            best = (int)i;
        }
    }
    return best;
}
```

- [ ] **Step 7: Implement `applyRequest`**

Replace the stub in the same file:

```cpp
void applyRequest(SlotRequest& entry, const char* code,
                  FetchPriority new_prio) {
    // Always refresh the code. strnlen + bounded memcpy + explicit null term
    // — defensive even though caller is expected to pass a valid stop code.
    size_t n = strnlen(code, settings::BUS_STOP_CODE_LEN);
    memcpy(entry.code, code, n);
    entry.code[n] = '\0';

    // Priority upgrades but never downgrades while still wanted.
    if (!entry.wanted || new_prio > entry.prio) {
        entry.prio = new_prio;
    }
    entry.wanted = true;
}
```

- [ ] **Step 8: Run, expect pass**

Run: `pio test -e native -f test_bus_fetch_logic 2>&1 | tail -20`

Expected: 11/11 tests pass.

- [ ] **Step 9: Confirm firmware build still succeeds**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -5`

Expected: SUCCESS. (The new library is unreferenced; linker garbage-collects.)

- [ ] **Step 10: Commit**

```bash
git add lib/bus_fetch_logic/ test/test_bus_fetch_logic/
git commit -m "feat(bus_fetch_logic): pure slot-pick + priority-upgrade with unit tests"
```

---

## Task 2: `BusFetchService` class (worker task + sync fallback)

**Files:**
- Create: `src/net/BusFetchService.h`
- Create: `src/net/BusFetchService.cpp`

No native test possible (FreeRTOS isn't available off-target). Verified by build now and on-device in Task 6.

- [ ] **Step 1: Create the header**

Create `src/net/BusFetchService.h`:

```cpp
#pragma once

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "BusArrivalsFetcher.h"
#include "bus_arrivals.h"
#include "bus_fetch_logic.h"
#include "settings_model.h"

namespace net {

// Single shared bus-arrivals fetch worker. One persistent FreeRTOS task
// drains a 4-entry request table (one per bus stop slot) and stages
// results into a 4-entry buffer that BusCard polls via takeResult().
//
// All public methods are called from the main thread only.
class BusFetchService {
public:
    BusFetchService();
    ~BusFetchService();

    // Create the worker task. Returns true on success; false leaves the
    // service in synchronous-fallback mode (request() runs the fetch
    // inline on the caller's thread).
    bool begin();

    // True iff the worker task is running. False means synchronous fallback.
    bool isAsync() const { return async_; }

    // Submit (or refresh) a fetch for slot. Non-blocking in async mode.
    // code is copied immediately under the mutex (no Settings race).
    // Empty code or out-of-range slot is a silent no-op.
    void request(uint8_t slot, const char* code,
                 bus_fetch_logic::FetchPriority priority);

    // If a fresh result is staged for slot, copy it into out, clear the
    // staged flag, return true. Else return false. Idempotent.
    bool takeResult(uint8_t slot, bus_arrivals::BusStopArrivals& out);

private:
    static void workerEntry(void* arg);
    void        workerLoop();
    void        runFetchSync(uint8_t slot, const char* code);

    bus_fetch_logic::SlotRequest  requests_[settings::MAX_BUS_STOPS];
    bus_arrivals::BusStopArrivals staging_[settings::MAX_BUS_STOPS];
    bool                          staged_ready_[settings::MAX_BUS_STOPS];

    BusArrivalsFetcher  fetcher_;       // used only on the worker thread
    SemaphoreHandle_t   mutex_;         // guards requests_ + staging_ + staged_ready_
    SemaphoreHandle_t   wake_;          // binary sem: main signals a new request
    TaskHandle_t        task_;
    bool                async_;
    bool                logged_high_water_;   // one-shot "first fetch" stack log
};

}  // namespace net
```

- [ ] **Step 2: Create the implementation**

Create `src/net/BusFetchService.cpp`:

```cpp
#include "BusFetchService.h"

#include <Arduino.h>
#include <string.h>

namespace net {

namespace {
constexpr uint32_t kWorkerStackBytes = 10 * 1024;
constexpr UBaseType_t kWorkerPriority = 5;   // sits between idle (0) and Arduino loop (1) + sensor work
constexpr const char* kWorkerName = "bus_fetch";
}  // namespace

BusFetchService::BusFetchService()
    : requests_{},
      staging_{},
      staged_ready_{},
      mutex_(nullptr),
      wake_(nullptr),
      task_(nullptr),
      async_(false),
      logged_high_water_(false) {
    for (uint8_t i = 0; i < settings::MAX_BUS_STOPS; ++i) {
        requests_[i].code[0]  = '\0';
        requests_[i].prio     = bus_fetch_logic::FetchPriority::LOW;
        requests_[i].wanted   = false;
        staged_ready_[i]      = false;
    }
}

BusFetchService::~BusFetchService() {
    // Service is a static singleton in main.cpp; ~BusFetchService() runs
    // at program exit which never happens on this device. No teardown.
}

bool BusFetchService::begin() {
    mutex_ = xSemaphoreCreateMutex();
    wake_  = xSemaphoreCreateBinary();
    if (!mutex_ || !wake_) {
        Serial.println("[bus] semaphore alloc failed; sync fallback");
        async_ = false;
        return false;
    }

    BaseType_t ok = xTaskCreate(&BusFetchService::workerEntry,
                                kWorkerName,
                                kWorkerStackBytes / sizeof(StackType_t),
                                this,
                                kWorkerPriority,
                                &task_);
    if (ok != pdPASS) {
        Serial.println("[bus] xTaskCreate failed; sync fallback");
        async_ = false;
        return false;
    }
    async_ = true;
    Serial.printf("[bus] worker task up (stack %u bytes)\n",
                  (unsigned)kWorkerStackBytes);
    return true;
}

void BusFetchService::request(uint8_t slot, const char* code,
                              bus_fetch_logic::FetchPriority priority) {
    if (slot >= settings::MAX_BUS_STOPS) return;
    if (!code || code[0] == '\0') return;

    if (!async_) {
        runFetchSync(slot, code);
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    bus_fetch_logic::applyRequest(requests_[slot], code, priority);
    xSemaphoreGive(mutex_);
    xSemaphoreGive(wake_);   // binary sem: caps at 1, harmless if already given
}

bool BusFetchService::takeResult(uint8_t slot,
                                 bus_arrivals::BusStopArrivals& out) {
    if (slot >= settings::MAX_BUS_STOPS) return false;

    if (!mutex_) {
        // Pure sync mode without semaphores (alloc failure path).
        if (!staged_ready_[slot]) return false;
        out = staging_[slot];
        staged_ready_[slot] = false;
        return true;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool have = staged_ready_[slot];
    if (have) {
        out = staging_[slot];
        staged_ready_[slot] = false;
    }
    xSemaphoreGive(mutex_);
    return have;
}

void BusFetchService::runFetchSync(uint8_t slot, const char* code) {
    // Synchronous fallback path. Blocks the caller (i.e. the main loop)
    // for the duration of the fetch — same behaviour as before this
    // service existed. Only used when xTaskCreate fails at boot.
    bus_arrivals::BusStopArrivals local{};
    fetcher_.fetch(code, millis(), local);
    if (mutex_) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        staging_[slot] = local;
        staged_ready_[slot] = true;
        xSemaphoreGive(mutex_);
    } else {
        staging_[slot] = local;
        staged_ready_[slot] = true;
    }
}

void BusFetchService::workerEntry(void* arg) {
    static_cast<BusFetchService*>(arg)->workerLoop();
}

void BusFetchService::workerLoop() {
    char     code_local[settings::BUS_STOP_CODE_LEN + 1];
    uint8_t  slot_local = 0;

    for (;;) {
        // Pick the highest-priority wanted slot. If none, block on wake.
        bool have_work = false;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        int best = bus_fetch_logic::pickHighestPriority(requests_);
        if (best >= 0) {
            slot_local = (uint8_t)best;
            size_t n = strnlen(requests_[best].code,
                               settings::BUS_STOP_CODE_LEN);
            memcpy(code_local, requests_[best].code, n);
            code_local[n] = '\0';
            requests_[best].wanted = false;   // claimed
            have_work = true;
        }
        xSemaphoreGive(mutex_);

        if (!have_work) {
            xSemaphoreTake(wake_, portMAX_DELAY);
            continue;
        }

        // Blocking fetch — no lock held. Main loop runs freely throughout.
        bus_arrivals::BusStopArrivals local{};
        bool ok = fetcher_.fetch(code_local, millis(), local);

        // Stage the result (success or failure both stage; the BusCard's
        // computeState() decides what to render).
        xSemaphoreTake(mutex_, portMAX_DELAY);
        staging_[slot_local] = local;
        staged_ready_[slot_local] = true;
        xSemaphoreGive(mutex_);

        if (ok && !logged_high_water_) {
            logged_high_water_ = true;
            UBaseType_t free_words = uxTaskGetStackHighWaterMark(nullptr);
            Serial.printf("[bus] worker stack high-water mark = %u bytes free\n",
                          (unsigned)(free_words * sizeof(StackType_t)));
        }
    }
}

}  // namespace net
```

- [ ] **Step 3: Build to confirm**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -8`

Expected: SUCCESS. The class is unreferenced; the linker garbage-collects (or keeps it — either is fine).

- [ ] **Step 4: Commit**

```bash
git add src/net/BusFetchService.h src/net/BusFetchService.cpp
git commit -m "feat(net): BusFetchService — persistent FreeRTOS worker with sync fallback"
```

---

## Task 3: Wire `BusFetchService` into `BusCard`, `CardController`, `main.cpp`

**Files:**
- Modify: `src/ui/cards/BusCard.h`
- Modify: `src/ui/cards/BusCard.cpp`
- Modify: `src/ui/CardController.h`
- Modify: `src/ui/CardController.cpp`
- Modify: `src/main.cpp`

This is the atomic switch from synchronous fetch to async. Must be one task because changing `BusCard`'s constructor signature breaks every caller until they're all updated.

- [ ] **Step 1: Update `BusCard.h`**

In `src/ui/cards/BusCard.h`:

(a) Replace the include `#include "../../net/BusArrivalsFetcher.h"` with `#include "../../net/BusFetchService.h"`.

(b) Change the constructor declaration:

```cpp
    BusCard(uint8_t slot_index,
            const Settings& settings,
            const WifiManager& wifi,
            net::BusFetchService& service);
```

(c) Replace the `net::BusArrivalsFetcher fetcher_;` member with:

```cpp
    net::BusFetchService& service_;
```

- [ ] **Step 2: Update `BusCard.cpp` constructor + onShow + tick**

In `src/ui/cards/BusCard.cpp`:

(a) Update the constructor signature and initializer list. Replace the existing constructor (around lines 60-80) with:

```cpp
BusCard::BusCard(uint8_t slot_index,
                 const Settings& settings,
                 const WifiManager& wifi,
                 net::BusFetchService& service)
    : slot_(slot_index),
      settings_(settings),
      wifi_(wifi),
      service_(service),
      data_{},
      last_fetch_attempt_ms_(0),
      shown_at_ms_(0),
      last_tick_minute_(0),
      first_visible_(0),
      ever_drawn_(false),
      visible_(false),
      dirty_(true),
      last_drawn_first_visible_(0),
      last_drawn_label_{0},
      last_drawn_code_{0},
      last_drawn_wifi_up_(false),
      last_drawn_state_(DisplayState::Loading) {
    for (uint8_t i = 0; i < kViewportRows; ++i) {
        last_drawn_[i] = {};
        last_drawn_minute_[i] = INT32_MIN;
    }
}
```

(b) Replace the `onShow()` body so it pulls preloaded data immediately:

```cpp
void BusCard::onShow() {
    visible_ = true;
    shown_at_ms_ = millis();
    dirty_ = true;

    // Pull any preloaded staged result into data_ before the first render
    // so the card lands on real data instead of "Loading...".
    if (service_.takeResult(slot_, data_)) {
        // Preload counts as the most recent fetch attempt — preserve the
        // 30 s refresh schedule instead of immediately re-fetching.
        last_fetch_attempt_ms_ = data_.last_fetch_success_ms;
        last_tick_minute_ = 0;
    } else {
        // No preload — schedule an immediate HIGH request on next tick.
        last_fetch_attempt_ms_ = 0;
    }
}
```

(c) Replace the `tick()` body with the async version:

```cpp
void BusCard::tick(uint32_t now_ms) {
    if (!visible_) return;

    if (!wifi_.isConnected()) {
        if (last_drawn_wifi_up_) dirty_ = true;
        return;
    }

    // Always poll the service — picks up a 30 s refresh or a late-arriving
    // result for a previously-submitted request.
    if (service_.takeResult(slot_, data_)) {
        dirty_ = true;
        last_tick_minute_ = 0;
        return;
    }

    if (shouldFetch(now_ms)) {
        const char* code = settings_.data().bus_stops[slot_].code;
        if (code[0] != '\0') {
            service_.request(slot_, code,
                             bus_fetch_logic::FetchPriority::HIGH);
        }
        last_fetch_attempt_ms_ = now_ms;
        // Don't mark dirty yet — display state hasn't changed. The next
        // tick that picks up the staged result will mark dirty.
        return;
    }

    if (data_.valid) {
        uint32_t minute = (now_ms - data_.fetched_at_ms) / 60000;
        if (minute != last_tick_minute_) {
            last_tick_minute_ = minute;
            dirty_ = true;
        }
    }
}
```

(d) Delete the `doFetch` member function (the body that called `fetcher_.fetch`). It's no longer used.

(e) Make sure `bus_fetch_logic.h` is included — it's pulled in transitively by `BusFetchService.h`, but add an explicit `#include "bus_fetch_logic.h"` near the other includes at the top of `BusCard.cpp` for clarity (gives access to `bus_fetch_logic::FetchPriority` used in `tick()`).

- [ ] **Step 3: Update `BusCard.h` to drop the `doFetch` declaration**

In `src/ui/cards/BusCard.h`, remove the line:

```cpp
    void         doFetch(uint32_t now_ms);
```

(`shouldFetch` stays — still used by `tick()`.)

- [ ] **Step 4: Update `CardController.h` constructor + member**

In `src/ui/CardController.h`:

(a) Add include `#include "../net/BusFetchService.h"` near the other net includes.

(b) Add `net::BusFetchService& service` as the last parameter of the constructor:

```cpp
    CardController(AppState& app, EventBus& bus, WifiManager& wifi,
                   PromptUi& prompt, BleLink& ble, Settings& settings,
                   UpdateManager& um, FactoryResetCoordinator& fr,
                   net::BusFetchService& service);
```

(c) Add a `net::BusFetchService& service_;` private member, right after `FactoryResetCoordinator& fr_;`. (Stored now even though `BusCard` is what actually fetches; Task 4's preload trigger will use it.)

- [ ] **Step 5: Update `CardController.cpp` constructor**

In `src/ui/CardController.cpp`:

(a) Update the constructor signature to match the header.

(b) In the initializer list, add `service_(service),` right after `fr_(fr),` (matches the header member order).

(c) Pass the service into each `BusCard` constructor. Find the `bus_card_0_(0, settings, wifi),` lines and change them to:

```cpp
      bus_card_0_(0, settings, wifi, service),
      bus_card_1_(1, settings, wifi, service),
      bus_card_2_(2, settings, wifi, service),
      bus_card_3_(3, settings, wifi, service),
```

(`service` is the new constructor parameter.)

- [ ] **Step 6: Update `main.cpp` — create the service and wire it in**

In `src/main.cpp`:

(a) Add include `#include "net/BusFetchService.h"` near the other net includes.

(b) Add a static instance after the `bleLink` declaration but before the `cardController`:

```cpp
static net::BusFetchService busFetchService;
```

(c) Update the `cardController` static initializer to pass `busFetchService` as the new last argument:

```cpp
static CardController cardController{appState, eventBus, wifiManager, promptUi, bleLink,
                                     settingsStore,
                                     UpdateManager::instance(),
                                     factoryReset,
                                     busFetchService};
```

(d) In `setup()`, add a call to `busFetchService.begin()` AFTER `wifiManager.begin()` (the worker task is fine to start before Wi-Fi is up — the first fetch will just fail and stage an error, which is harmless). Concretely, find the line `wifiManager.begin();` and add right after it:

```cpp
    busFetchService.begin();
```

- [ ] **Step 7: Build**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -8`

Expected: SUCCESS. RAM should grow by ~12 KB (worker stack + tables).

- [ ] **Step 8: Run native tests to confirm nothing else broke**

Run: `pio test -e native 2>&1 | tail -8`

Expected: all existing native tests still pass (no host-testable code was touched semantically).

- [ ] **Step 9: Commit**

```bash
git add src/ui/cards/BusCard.h src/ui/cards/BusCard.cpp \
        src/ui/CardController.h src/ui/CardController.cpp \
        src/main.cpp
git commit -m "feat(ui): BusCard fetches via BusFetchService instead of inline blocking"
```

---

## Task 4: Neighbour preload trigger in `CardController`

**Files:**
- Modify: `src/ui/CardController.h`
- Modify: `src/ui/CardController.cpp`

- [ ] **Step 1: Add the debounce + preload-tracking members**

In `src/ui/CardController.h`, after the existing `applied_boot_card_` member (in the private section), add:

```cpp
    // Preload neighbour bus cards ~750 ms after the carousel settles on
    // a card, so flipping to a neighbour lands on instantly-rendered data.
    uint32_t  last_card_change_ms_     = 0;
    size_t    last_seen_index_         = (size_t)-1;
    size_t    preload_done_for_index_  = (size_t)-1;
    static constexpr uint32_t kPreloadDebounceMs = 750;
```

Also declare a private helper:

```cpp
    void preloadNeighbour(size_t carousel_index);
```

(`service_` was added in Task 3 Step 4(c) — no member changes here besides the three above.)

- [ ] **Step 2: (no constructor changes)**

The new tracking members have inline defaults in the header (`= 0`, `= (size_t)-1`, `= (size_t)-1`), so they don't need explicit constructor init. Skip to Step 3.

- [ ] **Step 3: Add the preload trigger to `tick()`**

In `src/ui/CardController.cpp`, find `void CardController::tick(uint32_t now_ms, Display& display) {` and add the preload logic at the END of the function, right before the closing brace:

```cpp
    // ---- Neighbour preload ----------------------------------------------
    // Track carousel index changes; ~750 ms after the user settles on a
    // card, prefetch the up/down neighbours if they're bus cards. This
    // keeps the worker out of the way during rapid scrolling.
    size_t cur_index = stack_.index();
    if (cur_index != last_seen_index_) {
        last_seen_index_      = cur_index;
        last_card_change_ms_  = now_ms;
        preload_done_for_index_ = (size_t)-1;
    }
    if (cur_index != preload_done_for_index_ &&
        (now_ms - last_card_change_ms_) >= kPreloadDebounceMs &&
        stack_.size() > 1) {
        preloadNeighbour((cur_index + stack_.size() - 1) % stack_.size());
        preloadNeighbour((cur_index + 1) % stack_.size());
        preload_done_for_index_ = cur_index;
    }
}
```

- [ ] **Step 4: Implement `preloadNeighbour`**

Add the helper near the other private methods in `src/ui/CardController.cpp`:

```cpp
void CardController::preloadNeighbour(size_t carousel_index) {
    const settings::Settings& d = settings_.data();
    if (carousel_index >= d.cards_order_count) return;
    uint8_t card_id = d.cards_order[carousel_index];
    if (card_id < settings::CARD_BUS_1 || card_id > settings::CARD_BUS_4) return;
    uint8_t slot = (uint8_t)(card_id - settings::CARD_BUS_1);
    const char* code = d.bus_stops[slot].code;
    if (code[0] == '\0') return;
    service_.request(slot, code, bus_fetch_logic::FetchPriority::LOW);
}
```

- [ ] **Step 5: Build**

Run: `pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -5`

Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/ui/CardController.h src/ui/CardController.cpp
git commit -m "feat(ui): debounced neighbour-bus-card preload via BusFetchService"
```

---

## Task 5: Stack high-water-mark logging — already in Task 2

The `[bus] worker stack high-water mark = N bytes free` log line was already added inside `BusFetchService::workerLoop()` in Task 2 Step 2 (gated by `logged_high_water_` so it fires once after the first successful fetch). No additional code change needed.

- [ ] **Step 1: Confirm by grepping**

Run: `grep -n "high-water mark" src/net/BusFetchService.cpp`

Expected: exactly one hit, inside `workerLoop()`.

- [ ] **Step 2: No commit** (no code change). Proceed to Task 6.

---

## Task 6: On-device manual validation

This is the only verification path for the FreeRTOS-side behaviour. Run after flashing.

**Setup:**
1. Build & flash: `pio run -e adafruit_feather_esp32s3_reversetft -t upload && pio device monitor -e adafruit_feather_esp32s3_reversetft`.
2. Configure at least 2 bus stops in the web UI; arrange the carousel so the bus cards are next to a non-bus card (Status or Eyes).

- [ ] **Step 1: Worker task starts**

After boot + Wi-Fi connect, the serial monitor must show:

```
[bus] worker task up (stack 10240 bytes)
```

If instead it shows `[bus] xTaskCreate failed; sync fallback`, stop here — Task 2 needs investigation.

- [ ] **Step 2: Cold flip to bus card — UI does not freeze**

From boot, while the eyes card is animating, flip directly to the first bus card. Expected: the bus card briefly shows "Loading…" while the eyes animation **continues without freezing on the previous card**. Within 1–2 s the bus data appears. The serial log shows a `[bus] GET ...` line that completes asynchronously.

If the previous card's animation visibly freezes during the fetch, the worker isn't running — fall back to the troubleshooting at the bottom.

- [ ] **Step 3: Stack high-water-mark log appears**

After the first successful fetch, the serial monitor must show one line like:

```
[bus] worker stack high-water mark = NNNN bytes free
```

Confirm the value is **≥ 1024 bytes free**. If it's lower, edit `kWorkerStackBytes` in `src/net/BusFetchService.cpp` upward (try 12 KB), rebuild, re-flash, repeat.

- [ ] **Step 4: Preloaded flip is instant**

Sit on the Status (or Eyes) card next to a bus card for ~3 s. Then flip to the bus card. Expected: bus data is on screen on the very first frame — no "Loading…" flash.

Look for the preload in the serial log: a `[bus] GET ...` line for the neighbour stop should appear ~750 ms after you settle on the previous card.

- [ ] **Step 5: Rapid scrolling doesn't pile up**

From the eyes card, press the `next` button 5× rapidly across the carousel. Expected: the serial log shows preloads only firing for the card you settled on, not each transient one. (You may see one or two cancelled preloads if you happen to pause briefly — that's fine.)

- [ ] **Step 6: 30 s background refresh while visible**

Sit on a bus card for >30 s. Expected: the per-minute ETA tick continues without a freeze. The serial log shows a `[bus] GET` line at the 30 s mark; the screen does NOT strobe (no `fillScreen` per CLAUDE.md rule); the data updates a moment later.

- [ ] **Step 7: Wi-Fi drop mid-fetch**

Sit on a bus card. Disconnect the AP (or block the device's Wi-Fi). Expected: the next fetch fails (`http begin failed` or similar in the serial log); the card transitions to `NoWifi` ("waiting to reconnect"); no crash. Reconnect Wi-Fi; the next fetch should succeed and the card returns to Normal.

- [ ] **Step 8: No-bus-stops configuration**

Disable all bus cards via the web UI; confirm boot is clean (the worker task starts but receives no requests; serial log is quiet on the `[bus]` channel).

- [ ] **Step 9: Commit any tweaks**

If you bumped the stack size in Step 3 (or made any other small tuning):

```bash
git add src/net/BusFetchService.cpp
git commit -m "tune(bus): adjust worker stack to keep ~N bytes high-water margin"
```

If no tweaks were needed, no commit.

---

## Self-Review

**Spec coverage:**

| Spec section | Implemented in |
|---|---|
| Goals (no UI freeze on flip; preload neighbours; bounded RAM; safe failure) | Tasks 2 (sync fallback), 3 (async path), 4 (preload), 6 (on-device check) |
| Non-goals (no display change, no API change to fetcher, no >1 deep queue, no cross-card service abstraction) | Honoured by design — Task 3 keeps display states untouched; service has 1-deep staging per slot |
| `BusFetchService` class shape (header sketch with private members, public surface) | Task 2 Step 1 (header), Step 2 (impl) |
| Wiring (main.cpp creates, CardController takes ref, BusCards take ref) | Task 3 Steps 4–6 |
| Concurrency model (mutex held only for fast copies, worker never touches `data_`) | Task 2 Step 2 (workerLoop releases mutex before fetch); Task 3 Step 2 (BusCard.cpp only reads/writes data_ on main thread) |
| Slot-pick rule (highest priority, lowest index ties) | Task 1 Step 6 + tests in Step 4 |
| Priority upgrade rule | Task 1 Step 7 + tests |
| Wake semaphore + re-scan after fetch | Task 2 Step 2 (workerLoop loop structure) |
| Request lifecycle (cold flip vs preloaded flip sequences) | Task 3 Steps 2b + 2c (onShow + tick) |
| Preload trigger (debounce, neighbour lookup) | Task 4 Step 3 + Step 4 |
| BusCard changes (drop fetcher_, change tick/onShow, keep render unchanged) | Task 3 Steps 1–3 |
| Error handling (fetch failure stages error; empty code no-op; xTaskCreate fail → sync; stack overflow rebooted by FreeRTOS) | Task 2 Step 2 (request() filters empty; runFetchSync fallback; stages error result on fetch failure) |
| Memory budget (~12 KB total) | Task 2 (10 KB stack + 1.5 KB tables) |
| Stack high-water-mark logging | Task 2 Step 2 (one-shot log inside workerLoop) |
| Testing (pure-logic unit tests + on-device checklist) | Task 1 (Unity), Task 6 (manual) |

**Placeholder scan:** None. Each step shows the actual code or command.

**Type consistency:** `bus_fetch_logic::FetchPriority`, `bus_fetch_logic::SlotRequest`, `bus_fetch_logic::pickHighestPriority`, `bus_fetch_logic::applyRequest`, `net::BusFetchService`, `service_`, `service` (constructor param) — all names declared once and referenced consistently. The `BusCard` constructor signature (slot, settings, wifi, service) is the same in BusCard.h, BusCard.cpp, and the four `bus_card_N_` initializers in CardController.cpp. The `CardController` constructor signature change adds exactly one parameter and is updated in main.cpp.

**Spec gaps:** None. The "synchronous fallback" path is fully covered by `runFetchSync` in Task 2; the high-water-mark log is in Task 2 (Task 5 is intentionally a no-op confirmation). The `onHide` behaviour (does not cancel in-flight) is unchanged from current code — no edit needed in Task 3, and the spec explicitly says onHide is unchanged.

---
