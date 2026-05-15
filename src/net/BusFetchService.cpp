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
        requests_[i].prio     = bus_fetch_logic::FetchPriority::LOW_PRIO;
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
