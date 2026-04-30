#include <Arduino.h>

#include "core/AppState.h"
#include "core/ConfigStore.h"
#include "core/EventBus.h"
#include "display/Display.h"
#include "input/InputRouter.h"
#include "net/BleLink.h"
#include "net/HttpServer.h"
#include "net/WifiManager.h"
#include "prompt_ui.h"
#include "state.h"
#include "ui/CardController.h"

static const int      PIN_BTN_NEXT             = 0;       // GPIO0 / BOOT
static const int      PIN_BTN_PREV             = 2;       // GPIO2 / D2
static const int      PIN_BTN_CENTER           = 1;       // GPIO1 / D1
static const uint8_t  BTN_NEXT_PRESSED_LEVEL   = LOW;
static const uint8_t  BTN_PREV_PRESSED_LEVEL   = HIGH;
static const uint8_t  BTN_CENTER_PRESSED_LEVEL = HIGH;
static const uint32_t FRAME_PACING_MS          = 16;

static Display      display;
static AppState     appState;
static EventBus     eventBus;
static ConfigStore  configStore;
static WifiManager  wifiManager{configStore};
static HttpServer   httpServer{wifiManager, appState, configStore};
static BleLink      bleLink{appState};

static PromptUi     promptUi = {};
static CardController cardController{appState, eventBus, wifiManager, promptUi, bleLink,
                                     PIN_BTN_NEXT, BTN_NEXT_PRESSED_LEVEL,
                                     PIN_BTN_PREV, BTN_PREV_PRESSED_LEVEL};
static InputRouter  inputRouter{PIN_BTN_NEXT,   BTN_NEXT_PRESSED_LEVEL,
                                PIN_BTN_PREV,   BTN_PREV_PRESSED_LEVEL,
                                PIN_BTN_CENTER, BTN_CENTER_PRESSED_LEVEL,
                                cardController.stack()};

static void drawSplash() {
    auto& tft = display.tft();
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(48, 52);
    tft.print("claude buddy");
    tft.setTextSize(1);
    tft.setCursor(48, 72);
    tft.print(appState.deviceName());
}

void setup() {
    Serial.begin(115200);
    delay(200);

    appState.initDeviceName();
    display.begin();
    inputRouter.begin();
    prompt_ui_init(&promptUi);
    configStore.begin();

    cardController.begin();

    // Hold center 5s to wipe Wi-Fi creds and reboot into the captive
    // portal. Useful when the network changes or the user wants to
    // reprovision without flashing.
    inputRouter.onCenterLongPress(5000, [] {
        Serial.println("[wifi] center long-press: clearing creds and rebooting");
        configStore.clear();
        delay(200);
        ESP.restart();
    });

    drawSplash();

    bleLink.setEventBus(&eventBus);
    wifiManager.setEventBus(&eventBus);
    bleLink.begin(appState.deviceName());

#ifdef WIFI_DEV_SSID
    if (!configStore.hasCreds()) {
        configStore.setCreds(WIFI_DEV_SSID, WIFI_DEV_PASS);
    }
#endif
    wifiManager.begin();
    httpServer.begin();

    appState.setBuddyState(state_derive(appState.status(), appState.isLive(millis())));
}

void loop() {
    uint32_t loop_start = millis();
    uint32_t now        = loop_start;

    bleLink.tick(now);
    wifiManager.tick(now);
    httpServer.tick(now);
    appState.setBuddyState(state_derive(appState.status(), appState.isLive(now)));

    inputRouter.tick(now);
    cardController.tick(now, display);

    while ((millis() - loop_start) < FRAME_PACING_MS) {
        yield();
    }
}
