#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "buttons.h"
#include "core/AppState.h"
#include "core/ConfigStore.h"
#include "core/EventBus.h"
#include "display/Display.h"
#include "input/InputRouter.h"
#include "net/BleLink.h"
#include "net/HttpServer.h"
#include "net/WifiManager.h"
#include "prompt_ui.h"
#include "protocol.h"
#include "state.h"
#include "ui/CardStack.h"
#include "ui/cards/EyesCard.h"
#include "ui/cards/NavTestCard.h"
#include "ui/cards/PromptCard.h"
#include "ui/cards/StatusCard.h"
#include "ui/cards/WifiCard.h"

static Display          display;
static AppState         appState;
static Adafruit_ST7789& tft = display.tft();

static const int      PIN_BTN_NEXT             = 0;
static const int      PIN_BTN_PREV             = 2;
static const int      PIN_BTN_CENTER           = 1;
static const uint32_t FRAME_PACING_MS          = 16;
static const uint8_t  BTN_NEXT_PRESSED_LEVEL   = LOW;   // GPIO0 / BOOT
static const uint8_t  BTN_PREV_PRESSED_LEVEL   = HIGH;  // GPIO2 / D2
static const uint8_t  BTN_CENTER_PRESSED_LEVEL = HIGH;  // GPIO1 / D1

static EventBus     eventBus;
static ConfigStore  configStore;
static WifiManager  wifiManager{configStore};
static HttpServer   httpServer{wifiManager, appState, configStore};
static BleLink      bleLink{appState};

static StatusCard  statusCard{appState};
static EyesCard    eyesCard{appState};
static WifiCard    wifiCard{wifiManager};
static NavTestCard navTestCard{PIN_BTN_NEXT, BTN_NEXT_PRESSED_LEVEL,
                               PIN_BTN_PREV, BTN_PREV_PRESSED_LEVEL};
static CardStack   cardStack;
static InputRouter inputRouter{PIN_BTN_NEXT,   BTN_NEXT_PRESSED_LEVEL,
                               PIN_BTN_PREV,   BTN_PREV_PRESSED_LEVEL,
                               PIN_BTN_CENTER, BTN_CENTER_PRESSED_LEVEL,
                               cardStack};

static PromptUi    promptUi   = {};
static PromptCard  promptCard{promptUi};

void setup() {
    Serial.begin(115200);
    delay(200);

    appState.initDeviceName();
    display.begin();
    inputRouter.begin();
    prompt_ui_init(&promptUi);
    configStore.begin();

    cardStack.addCard(&statusCard);
    cardStack.addCard(&eyesCard);
    cardStack.addCard(&wifiCard);
    cardStack.addCard(&navTestCard);

    // Splash before BLE comes up — BLE init takes ~1s and the screen
    // would otherwise stay black.
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(48, 52);
    tft.print("claude buddy");
    tft.setTextSize(1);
    tft.setCursor(48, 72);
    tft.print(appState.deviceName());

    bleLink.setEventBus(&eventBus);
    wifiManager.setEventBus(&eventBus);
    bleLink.begin(appState.deviceName());

    // StatusCard wants to repaint immediately whenever a snapshot lands.
    eventBus.subscribe(EventKind::SnapshotReceived,
                       [] { statusCard.invalidate(); });
    // Wi-Fi transitions repaint the WifiCard so the IP/SSID strip is
    // current the next time the user navigates to it.
    eventBus.subscribe(EventKind::WifiConnected,
                       [] { wifiCard.invalidate(); });
    eventBus.subscribe(EventKind::WifiDisconnected,
                       [] { wifiCard.invalidate(); });

#ifdef WIFI_DEV_SSID
    if (!configStore.hasCreds()) {
        configStore.setCreds(WIFI_DEV_SSID, WIFI_DEV_PASS);
    }
#endif
    wifiManager.begin();
    httpServer.begin();

    appState.setBuddyState(state_derive(appState.status(), appState.isLive(millis())));
    statusCard.invalidate();
}

void loop() {
    uint32_t loop_start = millis();

    uint32_t now = millis();
    bleLink.tick(now);
    wifiManager.tick(now);
    httpServer.tick(now);
    appState.setBuddyState(state_derive(appState.status(), appState.isLive(now)));

    prompt_ui_update(&promptUi, appState.status().prompt, appState.isLive(now), now);

    // PromptCard becomes the overlay whenever PromptUi is visible. Push/pop
    // is idempotent in CardStack, so calling each tick is fine and ensures
    // we re-overlay if PromptUi flips back to visible after a flash hide.
    PromptView pv = prompt_ui_view(&promptUi);
    if (pv.visible && !cardStack.hasOverlay()) {
        promptCard.setFooter(appState.deviceName(), appState.isLive(now));
        cardStack.pushOverlay(&promptCard);
    } else if (!pv.visible && cardStack.hasOverlay()) {
        cardStack.clearOverlay();
    } else if (pv.visible) {
        // Keep the footer fresh in case live/offln flipped while the
        // overlay was already up.
        promptCard.setFooter(appState.deviceName(), appState.isLive(now));
    }

    inputRouter.tick(now);

    // Drain any decision the PromptCard pushed onto PromptUi.
    char outBuf[96];
    if (prompt_ui_take_outgoing(&promptUi, outBuf, sizeof(outBuf))) {
        bleLink.writeLine(outBuf);
    }

    cardStack.tick(now, display);

    while ((millis() - loop_start) < FRAME_PACING_MS) {
        yield();
    }
}
