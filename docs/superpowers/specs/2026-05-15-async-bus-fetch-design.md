# Async Bus Arrivals Fetch & Neighbour Preload — Design

**Status:** approved 2026-05-15
**Owner:** Lino

## Summary

Replace the synchronous main-loop HTTP fetch in `BusCard` with a single shared, persistent FreeRTOS worker task that fetches off the main thread. The worker's per-slot result staging buffer doubles as a preload cache, so `CardController` can prefetch the up/down neighbours of the visible card and a flip to a bus card lands on instantly-rendered data instead of a ~1 second freeze.

## Goals

- Flipping to a bus card never blocks the UI on a network fetch — even on the first flip with no preload, the main loop keeps running while the worker fetches in the background.
- A bus card adjacent to the visible card preloads automatically (debounced) when the user settles on a card, so the typical user-facing experience is "flip → instant".
- Bounded, predictable RAM cost and a clean concurrency boundary that's small enough to reason about (this is the first FreeRTOS task in the codebase).
- A failure path (task creation fails, fetch errors, Wi-Fi drops) cannot brick the device or change the existing display states.

## Non-goals

- No change to the on-device card rendering, display states, or the existing `bus_arrivals` parse library.
- No change to `BusArrivalsFetcher`'s public surface — its existing synchronous `fetch()` becomes the worker's blocking call, used by no other caller directly.
- No queue depth >1 per slot — staged results overwrite (latest wins). No history, no retry buffer.
- No preload of non-bus cards (Status/Eyes/Wifi don't fetch network data).
- No decoupling beyond the worker task — only one fetch runs at a time (one radio, one HTTPS handshake at a time is correct).

## Reference

Prior spec: `docs/superpowers/specs/2026-05-13-bus-arrivals-card-design.md`. The original design's "Approach A" (4 sibling cards, each owning its fetcher) stays for the cards themselves, but **fetch ownership moves to a new shared service** because async + one radio naturally pull toward a single worker.

## Architecture

A new class `net::BusFetchService` (`src/net/BusFetchService.{h,cpp}`) owns:

- One persistent FreeRTOS worker task, stack 10 KB, created in `begin()`.
- A 4-entry **request table** (`requests_[MAX_BUS_STOPS]`), one slot per bus stop.
- A 4-entry **staging buffer** (`staging_[MAX_BUS_STOPS]`) of `bus_arrivals::BusStopArrivals` plus a `staged_ready_[MAX_BUS_STOPS]` flag. **The staging buffer doubles as the preload cache.**
- The existing `BusArrivalsFetcher fetcher_` (stateless) — used only on the worker thread.
- Two FreeRTOS primitives: a mutex (`mutex_`) guarding the tables, and a binary "wake" semaphore (`wake_`).

```cpp
namespace net {

enum class FetchPriority : uint8_t { LOW = 0, HIGH = 1 };

class BusFetchService {
public:
    bool begin();   // creates the worker; returns false if task create failed
    bool isAsync() const;   // false iff falling back to synchronous mode

    // All public methods are called from the main thread only.

    // Submit (or refresh) a fetch for slot. Non-blocking. code is copied
    // immediately under the mutex (no Settings race). Empty code = no-op.
    // If a request already exists for this slot at LOW and a HIGH submission
    // arrives, priority is upgraded in place (visible card never starves
    // behind its own earlier preload).
    void request(uint8_t slot, const char* code, FetchPriority priority);

    // If a fresh result is staged for slot, copy it into out, clear the
    // staged flag, return true. Else return false. Idempotent.
    bool takeResult(uint8_t slot, bus_arrivals::BusStopArrivals& out);

private:
    static void workerEntry(void* arg);
    void workerLoop();
    bool runFetchSync(uint8_t slot);   // synchronous fallback path

    struct SlotRequest {
        char          code[settings::BUS_STOP_CODE_LEN + 1];
        FetchPriority prio;
        bool          wanted;
    };

    SlotRequest                   requests_[settings::MAX_BUS_STOPS];
    bus_arrivals::BusStopArrivals staging_[settings::MAX_BUS_STOPS];
    bool                          staged_ready_[settings::MAX_BUS_STOPS];

    BusArrivalsFetcher            fetcher_;
    SemaphoreHandle_t             mutex_   = nullptr;
    SemaphoreHandle_t             wake_    = nullptr;
    TaskHandle_t                  task_    = nullptr;
    bool                          async_   = false;   // false ⇒ synchronous fallback
};

}  // namespace net
```

### Wiring

- `main.cpp` creates `static net::BusFetchService busFetchService;`, calls `busFetchService.begin()` after Wi-Fi setup, and passes `&busFetchService` into `CardController`'s constructor (one extra parameter).
- `CardController` passes the service reference to each of the 4 `BusCard`s at construction (one extra parameter).
- `BusCard` **drops its `BusArrivalsFetcher fetcher_` member** (the field is removed from `BusCard.h`); fetching now goes through the shared service.
- `CardController` gains preload responsibility (see "Preload trigger" below).

### Boundary of responsibilities

- **Service** owns *fetching and concurrency*.
- **`BusCard`** owns *display* and pulling its own data.
- **`CardController`** owns *when neighbour preloads fire*.

## Concurrency model

This is the first background task in the codebase, so the boundary is explicit and narrow.

| Data | Written by | Read by | Guard |
|---|---|---|---|
| `requests_[]` | main thread (`request()`) | worker (picks one) | mutex |
| `staging_[]` + `staged_ready_[]` | worker (after fetch) | main thread (`takeResult()`) | mutex |
| `BusCard::data_` | **main thread only** | main thread (render) | none needed |

**The worker never touches `BusCard::data_`.** `data_` is updated exclusively on the main thread when a `BusCard` calls `takeResult()`.

**The mutex is held only for fixed-size copies, never across a fetch.** Worker loop:

1. `xSemaphoreTake(wake_, portMAX_DELAY)` — block until a new request arrives.
2. Take mutex → `pickHighestPriority(requests_)` → if a slot is `wanted`, copy its code into a local, clear `wanted` → release mutex.
3. If nothing was wanted, loop back to (1).
4. **Run blocking `fetcher_.fetch(code, ms, local)` — no lock held.** Main loop runs freely the whole time.
5. Take mutex → `staging_[slot] = local`; `staged_ready_[slot] = true` → release mutex.
6. Loop back to (2) (re-scan; may pick up another request that arrived during the fetch).

The wake semaphore is binary; a missed give is harmless because the worker re-scans the entire 4-entry table each iteration.

### Slot-pick rule

Pure function, host-testable:

```cpp
// Returns -1 if no slot is wanted; else the index of the highest-priority
// wanted slot. Among equal priorities, lowest slot index wins (deterministic).
int pickHighestPriority(const SlotRequest (&requests)[settings::MAX_BUS_STOPS]);
```

### Priority upgrade rule

Inside `request()` (under mutex):

```
if (requests_[slot].wanted && requests_[slot].prio == LOW && new_prio == HIGH) {
    requests_[slot].prio = HIGH;
}
```

This guarantees that a visible card's HIGH request is never blocked behind its own earlier LOW preload.

## Request lifecycle & data flow

A typical sequence — user flips to a bus card with no preload yet:

1. `CardStack::next()` → new active card → `BusCard::onShow()`.
2. `BusCard::onShow()` calls `service_.takeResult(slot_, data_)` — returns false (nothing staged yet). Sets `visible_ = true`, `last_fetch_attempt_ms_ = 0`, `dirty_ = true`.
3. First render after `onShow()` — `computeState()` returns `Loading` (data_.valid == false). Card renders "Loading…".
4. Next `tick()`: `service_.takeResult` still false. `shouldFetch` true → `service_.request(slot, code, HIGH)` (non-blocking, returns immediately) → `last_fetch_attempt_ms_` = now.
5. Worker picks up the HIGH request, runs `fetcher_.fetch()` (~0.5–1.5 s). Main loop runs throughout — eyes animate, buttons respond, etc.
6. Worker stages the result.
7. Next `tick()` after staging: `service_.takeResult(slot_, data_)` returns true. `data_.valid` is now true. Render shows real data.

A typical sequence — user flips to a bus card that was preloaded:

1. `CardStack::next()` → `BusCard::onShow()`.
2. `BusCard::onShow()` calls `service_.takeResult(slot_, data_)` — **returns true** (preload staged this earlier). `data_.valid` is true.
3. First render shows real data — no "Loading" flash.
4. `onShow()` itself records `last_fetch_attempt_ms_ = data_.last_fetch_success_ms` (the preload counts as the most recent attempt), so the next `shouldFetch` check waits the remaining time to the 30 s mark instead of firing immediately.

A 30 s background refresh while visible is just another `request(HIGH)` → worker → stage → next `tick()`'s `takeResult` updates `data_`. No display state changes; it just appears.

## Preload trigger

`CardController` gains two pieces of state:

- `last_card_change_ms_` — set whenever `stack_.index()` differs from the value seen on the previous tick (or on the first tick after `rebuildStack`).
- `preload_done_for_index_` — the carousel index we last fired preloads for; `0xFF` until first fired.

Each `tick()`, after the existing logic:

```
if (current_index != preload_done_for_index_ &&
    (now_ms - last_card_change_ms_) >= kPreloadDebounceMs /* 750ms */) {
    preloadNeighbour(current_index - 1);   // wraps mod cards_order_count
    preloadNeighbour(current_index + 1);
    preload_done_for_index_ = current_index;
}
```

`preloadNeighbour(i)`:

- Look up `cards_order[i]` → `CardId`.
- If it's `CARD_BUS_1..4` and `bus_stops[slot].code` is non-empty, call `service_.request(slot, code, LOW)`.
- Otherwise skip (Status/Eyes/Wifi or empty bus slot).

The 750 ms debounce stops rapid scrolling (next/next/next) from queuing useless preloads — only the card you actually settle on triggers neighbour preloads.

## `BusCard` changes (compact)

- **Constructor:** drop the implicit `BusArrivalsFetcher fetcher_`; take `BusFetchService& service` as a new parameter.
- **`onShow()`:** in addition to the existing flag setup, call `service_.takeResult(slot_, data_)` immediately so any preloaded data lands in `data_` before the first render. If that returns true, set `last_fetch_attempt_ms_ = data_.last_fetch_success_ms` so the 30 s refresh schedule is preserved across the preload (don't re-fetch immediately if the preload is fresh). If it returns false, leave `last_fetch_attempt_ms_ = 0` to force an immediate `HIGH` request on the next tick.
- **`tick()`:** synchronous `doFetch()` is removed. Replaced with:
  1. Always (when visible & wifi up): `if (service_.takeResult(slot_, data_)) { dirty_ = true; last_tick_minute_ = 0; return; }`
  2. If `shouldFetch(now_ms)`: `service_.request(slot_, code, HIGH)` and record `last_fetch_attempt_ms_ = now_ms`. Returns immediately.
  3. Per-minute tick — unchanged.
- **`handleButton`, `render`, `computeState`, all six display states — unchanged.**
- **`onHide()`:** unchanged. Does NOT cancel any in-flight fetch — letting it complete and stage the result is exactly what makes the data ready when the user returns.

## Error handling

| Failure | Behaviour |
|---|---|
| Worker `fetcher_.fetch()` returns false (HTTP error, timeout, parse error) | Worker stages the result with `last_error` populated; `valid` left as it was. `BusCard::computeState()` already maps that to `FetchError` / `Stale`. No display-state changes. |
| `request()` called with empty `code` | Service silently no-ops. (Cleared bus slots are also removed from the carousel by `rebuildStack`, so this is defensive.) |
| `xTaskCreate` fails at boot | `begin()` returns false. `async_` stays false. `request()` falls back to running the fetch inline on the caller thread; `takeResult()` returns whatever's in `staging_`. Behaviour reverts to today's laggy-but-working. Logged loudly. |
| Worker stack overflow | Caught by FreeRTOS' `configCHECK_FOR_STACK_OVERFLOW`; clean reboot. The 10 KB stack + high-water-mark log give margin and visibility before this happens. |
| Wi-Fi down at submit time | `BusCard` already shows `NoWifi` and won't request, but the worker is robust if a request races a disconnect — `fetcher_.fetch` returns "http begin failed" and stages an error. |

## Memory budget

| Item | RAM |
|---|---|
| Worker task stack | 10 240 B |
| `requests_[4]` | ~32 B |
| `staging_[4]` (`BusStopArrivals` × 4) | ~1.4 KB |
| Mutex + binary semaphore | ~0.2 KB |
| **New total** | **~11.7 KB** |
| Removed: `BusArrivalsFetcher` member × 4 | 0 (was stateless) |

~14 % of the current ~84 KB free heap. Predictable, steady-state. Does not recreate the transient large-allocation problem fixed by the earlier `getString()` change — the worker uses the same fetch code path.

Flash: ~1–2 KB of new code, negligible against the ~390 KB free.

## Stack-size monitoring

After the first successful fetch on the worker, log:

```
Serial.printf("[bus] worker stack high-water mark = %u bytes free\n",
              uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t));
```

Logged once (gated by a one-shot bool inside the worker). Confirms the 10 KB sizing in the wild. If high-water-mark drops below ~1 KB free we bump the stack.

## Testing

| What | How |
|---|---|
| `pickHighestPriority` (slot-pick) | Pure function; unit-tested in `test/test_bus_fetch_service/` with Unity on the native env. Covers: empty table; single LOW; single HIGH; HIGH wins over LOW for *different* slots; HIGH wins over LOW for *same* slot via in-place upgrade in `request()`; all `wanted` flags cleared after pick. |
| Priority upgrade (`request()` invariant) | Same suite: submitting LOW then HIGH for the same slot leaves `prio == HIGH` and `wanted == true`; submitting HIGH then LOW leaves `prio == HIGH`. |
| Fetch + parse | Unchanged — covered by the existing `test_bus_arrivals` suite. |
| Concurrency wiring | Not host-testable. Verified on-device via the manual checklist below. |
| Stack high-water mark | Logged once after the first successful fetch. |

### On-device manual checklist

1. **Cold flip to bus card.** Configure two bus stops; from boot, immediately flip to the first bus card. Expected: brief "Loading"; data arrives within 1–2 s; **the eyes / status card animation never freezes** during the fetch (verifies the fetch is off the main loop).
2. **Preloaded flip is instant.** Sit on the Status card next to a bus card for ~3 s, then flip to the bus card. Expected: bus data is on screen immediately, no "Loading" flash.
3. **Rapid scrolling doesn't pile up.** Press next 5× rapidly across the carousel. Expected: serial log shows preloads only firing for the card you settled on, not each transient.
4. **30 s background refresh.** Sit on a bus card for >30 s. Expected: per-minute ETA tick continues without a freeze; serial log shows a refresh fetch that completes without main-loop hitch; no `fillScreen` strobe (per CLAUDE.md rule).
5. **Wi-Fi drop mid-fetch.** Disconnect Wi-Fi while a fetch is in flight. Expected: card flips to `NoWifi`; worker's fetch fails and stages an error; on Wi-Fi restore, fetches resume; no crash.
6. **Stack monitoring.** Confirm the `[bus] worker stack high-water mark = N bytes free` log appears once after the first fetch and reports ≥ 1 KB free. If lower, bump the stack and re-flash.
7. **Fallback path.** (Optional, hard to trigger naturally.) Force `xTaskCreate` to fail (e.g., a temporary deliberate stack-too-large in dev) and verify the device still works synchronously.

## Open questions

None at design time.
