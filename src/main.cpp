#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "ble_bridge.h"
#include "buttons.h"
#include "core/AppState.h"
#include "display/Display.h"
#include "input/InputRouter.h"
#include "prompt_ui.h"
#include "protocol.h"
#include "state.h"
#include "ui/CardStack.h"
#include "ui/cards/EyesCard.h"
#include "ui/cards/NavTestCard.h"
#include "ui/cards/StatusCard.h"

static Display          display;
static AppState         appState;
static Adafruit_ST7789& tft = display.tft();
static constexpr int    W   = Display::W;

static const int      PIN_BTN_NEXT             = 0;
static const int      PIN_BTN_PREV             = 2;
static const int      PIN_BTN_CENTER           = 1;
static const uint32_t FRAME_PACING_MS          = 16;
static const uint8_t  BTN_NEXT_PRESSED_LEVEL   = LOW;   // GPIO0 / BOOT
static const uint8_t  BTN_PREV_PRESSED_LEVEL   = HIGH;  // GPIO2 / D2
static const uint8_t  BTN_CENTER_PRESSED_LEVEL = HIGH;  // GPIO1 / D1

static StatusCard  statusCard{appState};
static EyesCard    eyesCard{appState};
static NavTestCard navTestCard{PIN_BTN_NEXT, BTN_NEXT_PRESSED_LEVEL,
                               PIN_BTN_PREV, BTN_PREV_PRESSED_LEVEL};
static CardStack   cardStack;
static InputRouter inputRouter{PIN_BTN_NEXT,   BTN_NEXT_PRESSED_LEVEL,
                               PIN_BTN_PREV,   BTN_PREV_PRESSED_LEVEL,
                               PIN_BTN_CENTER, BTN_CENTER_PRESSED_LEVEL,
                               cardStack};

static PromptUi     promptUi            = {};
static bool         lastPromptVisible   = false;
static PromptOption lastPromptHighlight = OPT_APPROVE;
static char         lastPromptId[40]    = {};
static bool         lastPromptFlashing  = false;

static void render_prompt(const PromptView& v) {
    tft.fillScreen(ST77XX_BLACK);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    int16_t x1, y1; uint16_t tw, th;
    const char* hdr = "PERMISSION?";
    tft.getTextBounds(hdr, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor((W - (int)tw) / 2, 2);
    tft.print(hdr);

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(8, 28);
    tft.printf("Tool: %.20s", v.tool ? v.tool : "");

    if (v.hint && v.hint[0]) {
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(8, 40);
        tft.printf("%.38s", v.hint);
        if (strlen(v.hint) > 38) {
            tft.setCursor(8, 50);
            tft.printf("%.38s", v.hint + 38);
        }
    }

    tft.setTextSize(2);
    if (v.flash_text) {
        tft.setTextColor(v.flash_color, ST77XX_BLACK);
        tft.getTextBounds(v.flash_text, 0, 0, &x1, &y1, &tw, &th);
        tft.setCursor((W - (int)tw) / 2, 82);
        tft.print(v.flash_text);
    } else {
        const char* labels[3] = {"Approve", "Deny", "Dismiss"};
        const int ys[3] = {66, 82, 98};
        for (int i = 0; i < 3; i++) {
            bool hi = (i == (int)v.highlight);
            if (hi) {
                tft.fillRect(0, ys[i], W, 16, ST77XX_WHITE);
                tft.setTextColor(ST77XX_BLACK, ST77XX_WHITE);
                tft.setCursor(8, ys[i]);
                tft.print("> ");
                tft.print(labels[i]);
            } else {
                tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
                tft.setCursor(24, ys[i]);
                tft.print(labels[i]);
            }
        }
    }

    tft.setTextSize(1);
    const bool live = appState.isLive(millis());
    tft.setTextColor(live ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
    tft.setCursor(8, 118);
    tft.print(live ? "LIVE  " : "OFFLN ");
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(appState.deviceName());
}

void setup() {
    Serial.begin(115200);
    delay(200);

    appState.initDeviceName();
    display.begin();
    inputRouter.begin();
    prompt_ui_init(&promptUi);

    cardStack.addCard(&statusCard);
    cardStack.addCard(&eyesCard);
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

    ble_init(appState.deviceName());

    appState.setBuddyState(state_derive(appState.status(), appState.isLive(millis())));
    statusCard.invalidate();
}

void loop() {
    uint32_t loop_start = millis();

    // Snapshot lines can carry an `entries[]` transcript array; REFERENCE.md
    // caps event payloads at 4KB. 4096 + 1 for the null terminator gives us
    // exactly the max wire size with no headroom games.
    static char   lineBuf[4097];
    static size_t lineLen      = 0;
    static bool   lineOverflow = false;

    while (ble_available()) {
        int c = ble_read();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (lineOverflow) {
                Serial.printf("[rx] line overflow (>%u bytes), dropped\n",
                              (unsigned)sizeof(lineBuf) - 1);
                lineOverflow = false;
            } else if (lineLen > 0) {
                lineBuf[lineLen] = 0;
                if (lineBuf[0] == '{') {
                    if (protocol_parse_line(lineBuf, &appState.mutableStatus())) {
                        appState.markSnapshot(millis());
                        Serial.printf("[rx] %s\n", lineBuf);
                    }
                }
            }
            lineLen = 0;
        } else if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = (char)c;
        } else {
            lineOverflow = true;
        }
    }

    uint32_t now = millis();
    appState.setBuddyState(state_derive(appState.status(), appState.isLive(now)));

    prompt_ui_update(&promptUi, appState.status().prompt, appState.isLive(now), now);
    ButtonEvent ev = inputRouter.update(now);
    if (ev != BTN_NONE && prompt_ui_view(&promptUi).visible) {
        prompt_ui_button(&promptUi, ev, now);
    }

    char outBuf[96];
    if (prompt_ui_take_outgoing(&promptUi, outBuf, sizeof(outBuf))) {
        if (!ble_write_line(outBuf)) {
            Serial.printf("[tx] dropped (not connected): %s\n", outBuf);
        } else {
            Serial.printf("[tx] %s\n", outBuf);
        }
    }

    PromptView pv = prompt_ui_view(&promptUi);
    if (!pv.visible) {
        inputRouter.dispatch(ev, now);
    }

    if (pv.visible) {
        bool promptViewChanged =
            pv.visible != lastPromptVisible
            || pv.highlight != lastPromptHighlight
            || (pv.flash_text != nullptr) != lastPromptFlashing
            || strcmp(lastPromptId, promptUi.current_id) != 0;
        if (promptViewChanged) {
            render_prompt(pv);
            lastPromptVisible   = true;
            lastPromptHighlight = pv.highlight;
            lastPromptFlashing  = (pv.flash_text != nullptr);
            strncpy(lastPromptId, promptUi.current_id, sizeof(lastPromptId) - 1);
            lastPromptId[sizeof(lastPromptId) - 1] = 0;
            // Force the underlying card to repaint when the prompt clears.
            Card* a = cardStack.active();
            if (a) a->invalidate();
        }
    } else {
        if (lastPromptVisible) {
            // Prompt just closed — make sure the underlying card repaints.
            Card* a = cardStack.active();
            if (a) a->invalidate();
        }
        lastPromptVisible = false;
        cardStack.tick(now, display);
    }

    while ((millis() - loop_start) < FRAME_PACING_MS) {
        yield();
    }
}
