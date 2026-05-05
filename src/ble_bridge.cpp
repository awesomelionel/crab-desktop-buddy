#include "ble_bridge.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <string>

// Nordic UART Service UUIDs — every BLE serial example uses these.
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// 4KB matches the desktop's max event payload (REFERENCE.md). Under burst
// the ring needs to hold one full line in flight while loop() drains it.
static const size_t RX_CAP = 4096;
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
        // ESP32 BLE Arduino >= 2.0 returns std::string from getValue();
        // earlier versions returned Arduino String. Use std::string here so
        // we are explicit about which API we depend on.
        std::string v = c->getValue();
        if (!v.empty())
            rxPush(reinterpret_cast<const uint8_t*>(v.data()), v.size());
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

void ble_set_device_name(const char* device_name) {
    if (!device_name || !device_name[0]) return;
    // The Bluedroid BLEDevice API doesn't expose a clean live-rename;
    // calling esp_ble_gap_set_device_name directly requires headers that
    // arent on the standard PlatformIO include path. The new name is
    // already in NVS — the next boot will advertise under it. Trigger
    // a reboot from the HTTP layer if the rename should take effect now.
    Serial.printf("[ble] rename to '%s' pending reboot\n", device_name);
}

bool   ble_connected() { return connected; }
size_t ble_available() { return (rxHead + RX_CAP - rxTail) % RX_CAP; }
int    ble_read() {
    if (rxHead == rxTail) return -1;
    int b = rxBuf[rxTail];
    rxTail = (rxTail + 1) % RX_CAP;
    return b;
}

bool ble_write_line(const char* line) {
    if (!connected || !txChar || !line) return false;

    size_t n = strlen(line);
    // Out-of-line guard: keep payloads small. NUS over notify with
    // setMTU(517) gives ~514 bytes per packet — our outgoing lines
    // are well under 100 bytes.
    if (n > 256) return false;

    // Build "<line>\n" in a small stack buffer and notify in one shot.
    char buf[260];
    memcpy(buf, line, n);
    buf[n]     = '\n';
    buf[n + 1] = 0;

    txChar->setValue((uint8_t*)buf, n + 1);
    txChar->notify();
    return true;
}
