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
