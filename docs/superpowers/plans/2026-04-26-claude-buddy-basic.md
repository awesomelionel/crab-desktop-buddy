# Claude Buddy Basic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a minimal ESP32-S3 firmware (Adafruit Reverse TFT Feather) that connects to Claude Desktop over BLE and renders the live session state ("idle", "working", "waiting", "disconnected") plus the heartbeat `msg` as text on the TFT.

**Architecture:** A small Arduino/PlatformIO project using the standard ESP32 BLE stack to advertise the Nordic UART Service (NUS), receive newline-delimited JSON snapshots from Claude Desktop, parse them with ArduinoJson into a `ClaudeStatus` struct, derive a `BuddyState` enum from running/waiting counts, and render the state name + msg with `Adafruit_ST7789`. Pure parser / state logic lives under `lib/` so it's host-testable with PlatformIO's Unity native env; hardware-only code lives under `src/`.

**Tech Stack:** PlatformIO, Arduino framework for ESP32 (espressif32 platform), ArduinoJson 7, Adafruit ST7735/ST7789 + Adafruit GFX, ESP-IDF BLE (BLEDevice/BLEServer via the Arduino wrapper), Unity test framework on the `native` platform for unit tests.

**Scope decisions** (intentionally not in v1, can be added later):
- No LE Secure Connections / pairing — accept the unencrypted-OK path that REFERENCE.md describes. The link is sniffable; that's fine for a basic local dev build.
- No outbound traffic. We don't send permission decisions, status acks, owner acks, time sync, or folder-push acks. The desktop will keep retrying / time out — that's acceptable.
- No buttons, no NeoPixel, no battery readout, no IMU, no audio, no NVS persistence, no animations. Just a static text screen that updates on each snapshot.
- No partitions.csv override — default Arduino partitions are fine since we don't use a filesystem.

---

## File Structure

```
claude-buddy/
├── platformio.ini                  device + native test envs
├── README.md                       quick build/flash/run instructions
├── src/
│   ├── main.cpp                    setup/loop, line accumulation, TFT render
│   ├── ble_bridge.h                BLE NUS public API
│   └── ble_bridge.cpp              NUS server impl (RX ring buffer, advertise)
├── lib/
│   ├── protocol/
│   │   ├── protocol.h              ClaudeStatus struct + parse function
│   │   └── protocol.cpp            ArduinoJson-based parse implementation
│   └── state/
│       ├── state.h                 BuddyState enum + state_name + state_derive
│       └── state.cpp               state derivation + name table
└── test/
    └── test_native/
        ├── test_protocol.cpp       Unity tests for protocol_parse_line
        └── test_state.cpp          Unity tests for state_derive
```

**Why this split:**
- `lib/protocol/` and `lib/state/` are pure C++ with no Arduino headers, so the same source compiles on both the device target and the `native` host target. PlatformIO auto-discovers them and links them into both envs.
- `src/ble_bridge.{h,cpp}` and `src/main.cpp` use `BLEDevice` / `Adafruit_ST7789` and only build on the device target. PlatformIO automatically excludes `src/` when running tests on `native` (with `test_build_src` left at its default of `false`).
- Tests live under `test/test_native/` so `pio test -e native` picks them up.

---

## Task 1: Project skeleton

**Files:**
- Create: `platformio.ini`
- Create: `README.md`
- Create: `src/.gitkeep`
- Create: `lib/.gitkeep`
- Create: `test/.gitkeep`

- [ ] **Step 1: Create `platformio.ini`**

Write `/Users/lioneltan/code/claude-buddy/platformio.ini`:

```ini
[platformio]
default_envs = adafruit_feather_esp32s3_reversetft

[env]
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0

[env:adafruit_feather_esp32s3_reversetft]
platform = espressif32
board = adafruit_feather_esp32s3_reversetft
framework = arduino
monitor_speed = 115200
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DBOARD_HAS_PSRAM
    -DARDUINOJSON_ENABLE_PSRAM=1
board_build.arduino.memory_type = qio_opi
lib_ldf_mode = deep
lib_deps =
    ${env.lib_deps}
    adafruit/Adafruit ST7735 and ST7789 Library
    adafruit/Adafruit GFX Library

[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -Wall
lib_deps =
    ${env.lib_deps}
```

- [ ] **Step 2: Create empty `.gitkeep` files for tracked-but-empty dirs**

```bash
mkdir -p /Users/lioneltan/code/claude-buddy/src
mkdir -p /Users/lioneltan/code/claude-buddy/lib
mkdir -p /Users/lioneltan/code/claude-buddy/test
touch /Users/lioneltan/code/claude-buddy/src/.gitkeep
touch /Users/lioneltan/code/claude-buddy/lib/.gitkeep
touch /Users/lioneltan/code/claude-buddy/test/.gitkeep
```

- [ ] **Step 3: Create `README.md`**

Write `/Users/lioneltan/code/claude-buddy/README.md`:

```markdown
# claude-buddy

A minimal ESP32-S3 firmware (Adafruit Reverse TFT Feather) that connects to
Claude Desktop over BLE (Nordic UART Service) and shows the current session
state on the TFT as text.

See `cdb/REFERENCE.md` for the wire protocol; this implementation is a
ground-up rewrite, not a fork of `cdb/`.

## Build & flash

```
pio run -e adafruit_feather_esp32s3_reversetft -t upload
pio device monitor
```

## Run host tests

```
pio test -e native
```

## Pair with Claude Desktop

1. Claude → Help → Troubleshooting → Enable Developer Mode
2. Developer → Open Hardware Buddy…
3. Click Connect, pick `Claude-XXXX` from the list
\```
```

- [ ] **Step 4: Verify the env resolves**

Run: `cd /Users/lioneltan/code/claude-buddy && pio project init --board adafruit_feather_esp32s3_reversetft 2>&1 | tail -5`

Expected: no errors. (The `init` command is a no-op on an existing config; we only run it to validate the file.)

If `pio` isn't installed, install with `brew install platformio` first.

- [ ] **Step 5: Commit**

```bash
cd /Users/lioneltan/code/claude-buddy
git add platformio.ini README.md src/.gitkeep lib/.gitkeep test/.gitkeep
git commit -m "scaffold: platformio config + dirs"
```

---

## Task 2: ClaudeStatus + protocol parser (TDD)

**Files:**
- Create: `lib/protocol/protocol.h`
- Create: `lib/protocol/protocol.cpp`
- Create: `test/test_native/test_protocol.cpp`

- [ ] **Step 1: Write the failing test**

Write `/Users/lioneltan/code/claude-buddy/test/test_native/test_protocol.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parse_full_snapshot(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":3,\"running\":1,\"waiting\":2,\"msg\":\"approve: Bash\"}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT8(3, s.total);
    TEST_ASSERT_EQUAL_UINT8(1, s.running);
    TEST_ASSERT_EQUAL_UINT8(2, s.waiting);
    TEST_ASSERT_EQUAL_STRING("approve: Bash", s.msg);
}

static void test_parse_missing_fields_keeps_previous(void) {
    ClaudeStatus s = {};
    s.total = 5; s.running = 2; s.waiting = 1;
    strcpy(s.msg, "old");
    bool ok = protocol_parse_line("{\"running\":4}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(5, s.total);     // preserved
    TEST_ASSERT_EQUAL_UINT8(4, s.running);   // updated
    TEST_ASSERT_EQUAL_UINT8(1, s.waiting);   // preserved
    TEST_ASSERT_EQUAL_STRING("old", s.msg);  // preserved
}

static void test_parse_rejects_non_object(void) {
    ClaudeStatus s = {};
    TEST_ASSERT_FALSE(protocol_parse_line("not json", &s));
    TEST_ASSERT_FALSE(protocol_parse_line("[1,2,3]", &s));
    TEST_ASSERT_FALSE(protocol_parse_line("", &s));
    TEST_ASSERT_FALSE(protocol_parse_line(nullptr, &s));
    TEST_ASSERT_FALSE(s.valid);
}

static void test_parse_rejects_malformed_json(void) {
    ClaudeStatus s = {};
    TEST_ASSERT_FALSE(protocol_parse_line("{not json", &s));
    TEST_ASSERT_FALSE(s.valid);
}

static void test_parse_truncates_long_msg(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"msg\":\"this string is intentionally longer than the buffer fits\"}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(s.msg) - 1, strlen(s.msg));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_full_snapshot);
    RUN_TEST(test_parse_missing_fields_keeps_previous);
    RUN_TEST(test_parse_rejects_non_object);
    RUN_TEST(test_parse_rejects_malformed_json);
    RUN_TEST(test_parse_truncates_long_msg);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `cd /Users/lioneltan/code/claude-buddy && pio test -e native 2>&1 | tail -20`

Expected: build failure — `fatal error: protocol.h: No such file or directory`.

- [ ] **Step 3: Write `protocol.h`**

Write `/Users/lioneltan/code/claude-buddy/lib/protocol/protocol.h`:

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

struct ClaudeStatus {
    uint8_t total;
    uint8_t running;
    uint8_t waiting;
    char    msg[32];
    bool    valid;        // true once at least one snapshot has parsed
};

// Parse one newline-stripped JSON object from the bridge into `out`.
// Missing fields are left at whatever they already were in `out` so that
// successive partial snapshots accumulate. Returns false if the line isn't
// a JSON object, json fails to parse, or `line`/`out` is null.
bool protocol_parse_line(const char* line, ClaudeStatus* out);
```

- [ ] **Step 4: Write `protocol.cpp`**

Write `/Users/lioneltan/code/claude-buddy/lib/protocol/protocol.cpp`:

```cpp
#include "protocol.h"
#include <ArduinoJson.h>
#include <string.h>

bool protocol_parse_line(const char* line, ClaudeStatus* out) {
    if (!line || !out) return false;
    if (line[0] != '{') return false;

    JsonDocument doc;
    if (deserializeJson(doc, line)) return false;
    if (!doc.is<JsonObject>()) return false;

    out->total   = doc["total"]   | out->total;
    out->running = doc["running"] | out->running;
    out->waiting = doc["waiting"] | out->waiting;

    const char* m = doc["msg"];
    if (m) {
        strncpy(out->msg, m, sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = 0;
    }
    out->valid = true;
    return true;
}
```

- [ ] **Step 5: Run tests and confirm they pass**

Run: `cd /Users/lioneltan/code/claude-buddy && pio test -e native 2>&1 | tail -15`

Expected: `5 Tests 0 Failures 0 Ignored`.

- [ ] **Step 6: Commit**

```bash
cd /Users/lioneltan/code/claude-buddy
git add lib/protocol/protocol.h lib/protocol/protocol.cpp test/test_native/test_protocol.cpp
git commit -m "feat: ClaudeStatus + JSON line parser"
```

---

## Task 3: BuddyState derivation (TDD)

**Files:**
- Create: `lib/state/state.h`
- Create: `lib/state/state.cpp`
- Create: `test/test_native/test_state.cpp`

- [ ] **Step 1: Write the failing test**

Write `/Users/lioneltan/code/claude-buddy/test/test_native/test_state.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "state.h"

void setUp(void) {}
void tearDown(void) {}

static ClaudeStatus mk(uint8_t t, uint8_t r, uint8_t w) {
    ClaudeStatus s = {};
    s.total = t; s.running = r; s.waiting = w; s.valid = true;
    return s;
}

static void test_disconnected_when_not_connected(void) {
    ClaudeStatus s = mk(3, 1, 1);
    TEST_ASSERT_EQUAL(STATE_DISCONNECTED, state_derive(s, false));
}

static void test_idle_when_no_running_no_waiting(void) {
    ClaudeStatus s = mk(0, 0, 0);
    TEST_ASSERT_EQUAL(STATE_IDLE, state_derive(s, true));
}

static void test_idle_with_total_but_nothing_active(void) {
    ClaudeStatus s = mk(2, 0, 0);
    TEST_ASSERT_EQUAL(STATE_IDLE, state_derive(s, true));
}

static void test_working_when_running_positive(void) {
    ClaudeStatus s = mk(2, 1, 0);
    TEST_ASSERT_EQUAL(STATE_WORKING, state_derive(s, true));
}

static void test_waiting_takes_priority_over_running(void) {
    ClaudeStatus s = mk(3, 2, 1);
    TEST_ASSERT_EQUAL(STATE_WAITING, state_derive(s, true));
}

static void test_state_name_strings(void) {
    TEST_ASSERT_EQUAL_STRING("disconnected", state_name(STATE_DISCONNECTED));
    TEST_ASSERT_EQUAL_STRING("idle",         state_name(STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("working",      state_name(STATE_WORKING));
    TEST_ASSERT_EQUAL_STRING("waiting",      state_name(STATE_WAITING));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_disconnected_when_not_connected);
    RUN_TEST(test_idle_when_no_running_no_waiting);
    RUN_TEST(test_idle_with_total_but_nothing_active);
    RUN_TEST(test_working_when_running_positive);
    RUN_TEST(test_waiting_takes_priority_over_running);
    RUN_TEST(test_state_name_strings);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `cd /Users/lioneltan/code/claude-buddy && pio test -e native -f test_state 2>&1 | tail -15`

Expected: build failure — `fatal error: state.h: No such file or directory`.

- [ ] **Step 3: Write `state.h`**

Write `/Users/lioneltan/code/claude-buddy/lib/state/state.h`:

```cpp
#pragma once
#include <stdint.h>
#include "protocol.h"

enum BuddyState : uint8_t {
    STATE_DISCONNECTED = 0,
    STATE_IDLE,
    STATE_WORKING,
    STATE_WAITING,
};

const char* state_name(BuddyState s);
BuddyState  state_derive(const ClaudeStatus& status, bool connected);
```

- [ ] **Step 4: Write `state.cpp`**

Write `/Users/lioneltan/code/claude-buddy/lib/state/state.cpp`:

```cpp
#include "state.h"

const char* state_name(BuddyState s) {
    switch (s) {
        case STATE_DISCONNECTED: return "disconnected";
        case STATE_IDLE:         return "idle";
        case STATE_WORKING:      return "working";
        case STATE_WAITING:      return "waiting";
    }
    return "?";
}

BuddyState state_derive(const ClaudeStatus& status, bool connected) {
    if (!connected)            return STATE_DISCONNECTED;
    if (status.waiting > 0)    return STATE_WAITING;
    if (status.running > 0)    return STATE_WORKING;
    return STATE_IDLE;
}
```

- [ ] **Step 5: Run all tests and confirm they pass**

Run: `cd /Users/lioneltan/code/claude-buddy && pio test -e native 2>&1 | tail -20`

Expected: both test files run; `11 Tests 0 Failures 0 Ignored` total.

- [ ] **Step 6: Commit**

```bash
cd /Users/lioneltan/code/claude-buddy
git add lib/state/state.h lib/state/state.cpp test/test_native/test_state.cpp
git commit -m "feat: BuddyState derivation"
```

---

## Task 4: BLE Nordic UART bridge

**Files:**
- Create: `src/ble_bridge.h`
- Create: `src/ble_bridge.cpp`

(No native test — depends on the ESP32 BLE stack. Verified manually in Task 6.)

- [ ] **Step 1: Write `ble_bridge.h`**

Write `/Users/lioneltan/code/claude-buddy/src/ble_bridge.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

// Nordic UART Service-compatible BLE peripheral. Claude Desktop
// (and any nRF/Web Bluetooth client) writes JSON lines into the RX
// characteristic; we drop the bytes into a ring buffer that the main
// loop drains via ble_available()/ble_read(), accumulating until it
// sees '\n' and handing the line to protocol_parse_line.
//
// Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
// RX char: 6e400002-b5a3-f393-e0a9-e50e24dcca9e (write)
// TX char: 6e400003-b5a3-f393-e0a9-e50e24dcca9e (notify, unused in v1)

void   ble_init(const char* device_name);
bool   ble_connected();
size_t ble_available();
int    ble_read();   // -1 if empty
```

- [ ] **Step 2: Write `ble_bridge.cpp`**

Write `/Users/lioneltan/code/claude-buddy/src/ble_bridge.cpp`:

```cpp
#include "ble_bridge.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Nordic UART Service UUIDs — every BLE serial example uses these.
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

static const size_t RX_CAP = 2048;
static uint8_t  rxBuf[RX_CAP];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;

static BLEServer*         server = nullptr;
static BLECharacteristic* rxChar = nullptr;
static BLECharacteristic* txChar = nullptr;
static volatile bool      connected = false;

static void rxPush(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t next = (rxHead + 1) % RX_CAP;
        if (next == rxTail) return;  // full — drop
        rxBuf[rxHead] = p[i];
        rxHead = next;
    }
}

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        String v = c->getValue();
        if (v.length() > 0) rxPush((const uint8_t*)v.c_str(), v.length());
    }
};

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        connected = true;
        Serial.println("[ble] connected");
    }
    void onDisconnect(BLEServer*) override {
        connected = false;
        Serial.println("[ble] disconnected, restarting advertising");
        BLEDevice::startAdvertising();
    }
};

void ble_init(const char* device_name) {
    BLEDevice::init(device_name);
    BLEDevice::setMTU(517);

    server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService* svc = server->createService(NUS_SERVICE_UUID);

    txChar = svc->createCharacteristic(
        NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    txChar->addDescriptor(new BLE2902());

    rxChar = svc->createCharacteristic(
        NUS_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rxChar->setCallbacks(new RxCallbacks());

    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.printf("[ble] advertising as '%s'\n", device_name);
}

bool   ble_connected() { return connected; }
size_t ble_available() { return (rxHead + RX_CAP - rxTail) % RX_CAP; }
int    ble_read() {
    if (rxHead == rxTail) return -1;
    int b = rxBuf[rxTail];
    rxTail = (rxTail + 1) % RX_CAP;
    return b;
}
```

- [ ] **Step 3: Verify the device build still compiles**

Run: `cd /Users/lioneltan/code/claude-buddy && pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -10`

Expected: build fails with "undefined reference" because there's no `setup`/`loop` yet — that's the next task. The fact that ble_bridge.cpp itself compiles cleanly is what we're checking.

(If ble_bridge.cpp itself errors, fix those errors before moving on. Common issue: missing build flag for BLE, but it's enabled by default for `arduino` framework on ESP32.)

- [ ] **Step 4: Commit**

```bash
cd /Users/lioneltan/code/claude-buddy
git add src/ble_bridge.h src/ble_bridge.cpp
git commit -m "feat: NUS BLE bridge (RX-only)"
```

---

## Task 5: TFT render + main loop

**Files:**
- Create: `src/main.cpp`

- [ ] **Step 1: Write `main.cpp`**

Write `/Users/lioneltan/code/claude-buddy/src/main.cpp`:

```cpp
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_mac.h>

#include "ble_bridge.h"
#include "protocol.h"
#include "state.h"

// 1.14" Reverse TFT Feather: 240x135 native, 135x240 in rotation 0 (portrait).
static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
static const int W = 135;
static const int H = 240;

static char         deviceName[16] = "Claude";
static ClaudeStatus status         = {};
static BuddyState   currentState   = STATE_DISCONNECTED;
static BuddyState   lastDrawnState = (BuddyState)0xFF;
static char         lastDrawnMsg[sizeof(status.msg)] = {};
static uint32_t     lastSnapshotMs = 0;

// Treat the link as live if a snapshot arrived within the heartbeat window.
// REFERENCE.md says the desktop sends a keepalive every 10s and to treat
// >30s of silence as dead.
static const uint32_t LIVE_TIMEOUT_MS = 30000;

static bool isLive() {
    return lastSnapshotMs != 0 && (millis() - lastSnapshotMs) <= LIVE_TIMEOUT_MS;
}

static void initDeviceName() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(deviceName, sizeof(deviceName), "Claude-%02X%02X", mac[4], mac[5]);
}

static void initDisplay() {
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);
    tft.init(135, 240);
    tft.setRotation(0);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);
}

static void render() {
    tft.fillScreen(ST77XX_BLACK);

    // State name, big and centered horizontally near the top half.
    tft.setTextSize(3);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    const char* name = state_name(currentState);
    int16_t  x1, y1; uint16_t tw, th;
    tft.getTextBounds(name, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor((W - (int)tw) / 2, 70);
    tft.print(name);

    // Counts row.
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(8, 130);
    tft.printf("total %u  run %u  wait %u",
               status.total, status.running, status.waiting);

    // Last msg, wrapped manually to ~22 chars per line, two lines max.
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    if (status.msg[0]) {
        tft.setCursor(8, 160);
        tft.printf("%.22s", status.msg);
        if (strlen(status.msg) > 22) {
            tft.setCursor(8, 172);
            tft.printf("%.22s", status.msg + 22);
        }
    }

    // Footer: device name + link state.
    tft.setTextColor(isLive() ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
    tft.setCursor(8, 220);
    tft.print(isLive() ? "LIVE  " : "OFFLN ");
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(deviceName);
}

void setup() {
    Serial.begin(115200);
    delay(200);

    initDeviceName();
    initDisplay();

    // Splash before BLE comes up — BLE init takes ~1s and the screen
    // would otherwise stay black.
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(8, 100);
    tft.print("claude buddy");
    tft.setTextSize(1);
    tft.setCursor(8, 130);
    tft.print(deviceName);

    ble_init(deviceName);

    render();  // initial paint with disconnected state
    lastDrawnState = currentState;
    lastDrawnMsg[0] = 0;
}

void loop() {
    static char lineBuf[1024];
    static size_t lineLen = 0;

    while (ble_available()) {
        int c = ble_read();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (lineLen > 0) {
                lineBuf[lineLen] = 0;
                if (lineBuf[0] == '{') {
                    if (protocol_parse_line(lineBuf, &status)) {
                        lastSnapshotMs = millis();
                        Serial.printf("[rx] %s\n", lineBuf);
                    }
                }
                lineLen = 0;
            }
        } else if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = (char)c;
        }
    }

    BuddyState next = state_derive(status, isLive());
    bool stateChanged = (next != lastDrawnState);
    bool msgChanged   = strncmp(lastDrawnMsg, status.msg, sizeof(lastDrawnMsg)) != 0;
    if (stateChanged || msgChanged) {
        currentState = next;
        render();
        lastDrawnState = next;
        strncpy(lastDrawnMsg, status.msg, sizeof(lastDrawnMsg) - 1);
        lastDrawnMsg[sizeof(lastDrawnMsg) - 1] = 0;
    } else {
        // Even with no new data, we need to flip to OFFLN once the
        // 30s timeout elapses. Cheap recheck once a second.
        static uint32_t lastTick = 0;
        if (millis() - lastTick > 1000) {
            lastTick = millis();
            BuddyState recheck = state_derive(status, isLive());
            if (recheck != lastDrawnState) {
                currentState = recheck;
                render();
                lastDrawnState = recheck;
            }
        }
    }

    delay(20);
}
```

- [ ] **Step 2: Build the firmware**

Run: `cd /Users/lioneltan/code/claude-buddy && pio run -e adafruit_feather_esp32s3_reversetft 2>&1 | tail -15`

Expected: `SUCCESS` line at the end with RAM/Flash usage stats. Any compile error: read it carefully and fix the offending file (most likely `TFT_I2C_POWER` undefined on a board variant — if so, replace `pinMode(TFT_I2C_POWER, OUTPUT); digitalWrite(TFT_I2C_POWER, HIGH);` with `// no I2C power rail to enable on this board variant`).

- [ ] **Step 3: Run native tests one more time to confirm they still pass**

Run: `cd /Users/lioneltan/code/claude-buddy && pio test -e native 2>&1 | tail -10`

Expected: `11 Tests 0 Failures 0 Ignored`.

- [ ] **Step 4: Commit**

```bash
cd /Users/lioneltan/code/claude-buddy
git add src/main.cpp
git commit -m "feat: TFT render + BLE line accumulation in main loop"
```

---

## Task 6: Manual end-to-end verification

**Files:** none (hardware test).

This task is the only place the actual board gets exercised. If you don't have the hardware in front of you, stop here and surface that to the user.

- [ ] **Step 1: Flash the firmware**

Connect the Adafruit Reverse TFT Feather over USB-C (the data port, not a power-only cable).

Run: `cd /Users/lioneltan/code/claude-buddy && pio run -e adafruit_feather_esp32s3_reversetft -t upload 2>&1 | tail -10`

Expected: `[SUCCESS]` at the end. If stuck at "Connecting...": double-tap RESET on the board to force bootloader mode, then re-run.

- [ ] **Step 2: Watch the splash and disconnected state**

Run: `pio device monitor -e adafruit_feather_esp32s3_reversetft`

Expected on the TFT: brief "claude buddy" + `Claude-XXXX` splash, then a screen reading:
- big white text: `disconnected`
- cyan: `total 0  run 0  wait 0`
- footer: red `OFFLN  Claude-XXXX`

Expected on the serial monitor: `[ble] advertising as 'Claude-XXXX'`.

- [ ] **Step 3: Pair and confirm `idle`**

In Claude Desktop:
1. Help → Troubleshooting → Enable Developer Mode
2. Developer → Open Hardware Buddy…
3. Click Connect, pick `Claude-XXXX`

On the TFT, expected within ~5s: footer flips to green `LIVE`, big text changes to `idle` (no sessions running).

On the serial monitor, expected: `[ble] connected` then a stream of `[rx] {...}` lines containing `running`, `waiting`, `total`, `msg`.

- [ ] **Step 4: Confirm `working` and `waiting` transitions**

Start a Claude Code session in the terminal and ask it to do something that takes a moment (e.g. `claude "list files in this dir"`).

Expected: TFT flips to `working` while the session generates. If the session asks for a permission, TFT flips to `waiting`. After the session finishes, TFT returns to `idle`.

- [ ] **Step 5: Confirm `disconnected` after timeout**

Quit Claude Desktop (or close the Hardware Buddy window).

Expected within 30s: footer flips back to red `OFFLN`, big text returns to `disconnected`.

- [ ] **Step 6: Commit success**

If everything above worked, this is the basic working version. No code change for this task — just confirmation.

If anything didn't work, capture the symptom (serial log + what's on screen) and fix the offending module before declaring done.

---

## Done

The basic version is complete when Task 6's checks all pass. Future enhancements (in roughly this order, each its own plan):

1. Send permission decisions back over BLE (read `prompt.id` from snapshot, button-A approve / button-B deny, ack the desktop).
2. Implement the `status` ack so the Hardware Buddy stats panel shows something useful.
3. Add LE Secure Connections + bonding so transcript snippets aren't sniffable.
4. Time sync + RTC handling.
5. Folder push / character pack support.
