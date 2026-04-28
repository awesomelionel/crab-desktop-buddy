#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_mac.h>

#include "ble_bridge.h"
#include "buttons.h"
#include "eyes.h"
#include "prompt_ui.h"
#include "protocol.h"
#include "state.h"

// 1.14" Reverse TFT Feather: 240x135 native landscape, rotation 1 = 90° clockwise.
static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
static const int W = 240;
static const int H = 135;

enum CardId : uint8_t { CARD_STATUS = 0, CARD_EYES, CARD_NAV_TEST, CARD_COUNT };

static const int           PIN_BTN_NEXT        = 0;
static const int           PIN_BTN_PREV        = 2;
static const uint32_t      BTN_DEBOUNCE_MS     = 50;
static const uint32_t      FRAME_PACING_MS     = 16;
static const uint8_t       BTN_NEXT_PRESSED_LEVEL = LOW;   // GPIO0 / BOOT
static const uint8_t       BTN_PREV_PRESSED_LEVEL = HIGH;  // GPIO2 / D2

static CardId   currentCard = CARD_STATUS;
static EyesAnim eyesAnim    = {};

static char         deviceName[16] = "Claude";
static ClaudeStatus status         = {};
static BuddyState   currentState   = STATE_DISCONNECTED;
static BuddyState   lastDrawnState = (BuddyState)0xFF;
static char         lastDrawnMsg[sizeof(status.msg)] = {};
static uint32_t     lastSnapshotMs = 0;
static bool         eyesFrameValid = false;
static BuddyState   lastEyesState = (BuddyState)0xFF;
static uint8_t      lastEyesH = 0;
static int16_t      lastEyesDx = 0;
static int16_t      lastEyesBaseY = 0;
static uint32_t     lastEyesDiscAge = 0xFFFFFFFFu;
static Buttons      btns           = {};
static PromptUi     promptUi       = {};
static bool         lastPromptVisible    = false;
static PromptOption lastPromptHighlight  = OPT_APPROVE;
static char         lastPromptId[40]     = {};
static bool         lastPromptFlashing   = false;

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
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);
}

static void render_status() {
    tft.fillScreen(ST77XX_BLACK);

    // State name, big and centered horizontally near the top half.
    tft.setTextSize(3);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    const char* name = state_name(currentState);
    int16_t  x1, y1; uint16_t tw, th;
    tft.getTextBounds(name, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor((W - (int)tw) / 2, 20);
    tft.print(name);

    // Counts row.
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(8, 62);
    tft.printf("total %u  run %u  wait %u",
               status.total, status.running, status.waiting);

    // Last msg, wrapped manually to ~34 chars per line, two lines max.
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    if (status.msg[0]) {
        tft.setCursor(8, 80);
        tft.printf("%.34s", status.msg);
        if (strlen(status.msg) > 34) {
            tft.setCursor(8, 92);
            tft.printf("%.34s", status.msg + 34);
        }
    }

    // Footer: device name + link state.
    tft.setTextColor(isLive() ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
    tft.setCursor(8, 118);
    tft.print(isLive() ? "LIVE  " : "OFFLN ");
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(deviceName);
}

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
    tft.setTextColor(isLive() ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
    tft.setCursor(8, 118);
    tft.print(isLive() ? "LIVE  " : "OFFLN ");
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.print(deviceName);
}

static void render_nav_test() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setCursor(18, 20);
    tft.print("card 3: nav test");

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(20, 62);
    tft.print("D0: ");
    tft.print((digitalRead(PIN_BTN_NEXT) == BTN_NEXT_PRESSED_LEVEL) ? "PRESSED " : "released");

    tft.setCursor(20, 90);
    tft.print("D2: ");
    tft.print((digitalRead(PIN_BTN_PREV) == BTN_PREV_PRESSED_LEVEL) ? "PRESSED " : "released");
}

static void paint_current_card() {
    if (currentCard == CARD_STATUS) {
        render_status();
    } else if (currentCard == CARD_EYES) {
        eyes_render(tft, eyesAnim, currentState);
    } else {
        render_nav_test();
    }
}

static void sync_status_meta() {
    lastDrawnState = currentState;
    strncpy(lastDrawnMsg, status.msg, sizeof(lastDrawnMsg) - 1);
    lastDrawnMsg[sizeof(lastDrawnMsg) - 1] = 0;
}

static void invalidate_non_status_cards() {
    eyesFrameValid = false;
}

struct ButtonEdge {
    uint8_t  pressedLevel;
    uint8_t  lastReading;
    uint8_t  stable;
    uint32_t debounceClock;
    bool     consumed;
    bool     initialized;
};

static bool btn_pressed(int pin, ButtonEdge& b, uint32_t now) {
    uint8_t r = digitalRead(pin) == LOW ? LOW : HIGH;
    if (!b.initialized) {
        b.lastReading = r;
        b.stable = r;
        b.debounceClock = now;
        b.consumed = false;
        b.initialized = true;
        return false;
    }
    if (r != b.lastReading) {
        b.debounceClock = now;
    }
    b.lastReading = r;
    if ((now - b.debounceClock) < BTN_DEBOUNCE_MS) {
        return false;
    }
    if (r != b.stable) {
        b.stable = r;
        if (b.stable == b.pressedLevel) {
            if (!b.consumed) {
                b.consumed = true;
                return true;
            }
        } else {
            b.consumed = false;
        }
    } else if (b.stable != b.pressedLevel) {
        b.consumed = false;
    }
    return false;
}

static void poll_nav(uint32_t now) {
    static ButtonEdge nextBtn = {BTN_NEXT_PRESSED_LEVEL, HIGH, HIGH, 0, false, false};
    static ButtonEdge prevBtn = {BTN_PREV_PRESSED_LEVEL, LOW, LOW, 0, false, false};

    if (btn_pressed(PIN_BTN_NEXT, nextBtn, now)) {
        currentCard = static_cast<CardId>((currentCard + 1) % CARD_COUNT);
        if (currentCard == CARD_EYES) {
            eyes_reset(eyesAnim);
        } else {
            sync_status_meta();
        }
        invalidate_non_status_cards();
        paint_current_card();
    }
    if (btn_pressed(PIN_BTN_PREV, prevBtn, now)) {
        currentCard = static_cast<CardId>((currentCard + CARD_COUNT - 1) % CARD_COUNT);
        if (currentCard == CARD_EYES) {
            eyes_reset(eyesAnim);
        } else {
            sync_status_meta();
        }
        invalidate_non_status_cards();
        paint_current_card();
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    initDeviceName();
    initDisplay();

    pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
    // D2 on this hardware is active HIGH in the deskhog setup.
    pinMode(PIN_BTN_PREV, INPUT_PULLDOWN);
    pinMode(1, INPUT_PULLDOWN);  // D1 / center
    buttons_init(&btns);
    prompt_ui_init(&promptUi);

    // Splash before BLE comes up — BLE init takes ~1s and the screen
    // would otherwise stay black.
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(48, 52);
    tft.print("claude buddy");
    tft.setTextSize(1);
    tft.setCursor(48, 72);
    tft.print(deviceName);

    ble_init(deviceName);

    currentState = state_derive(status, isLive());
    paint_current_card();
    lastDrawnState = currentState;
    lastDrawnMsg[0] = 0;
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
                    if (protocol_parse_line(lineBuf, &status)) {
                        lastSnapshotMs = millis();
                        Serial.printf("[rx] %s\n", lineBuf);
                    }
                }
            }
            lineLen = 0;
        } else if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = (char)c;
        } else {
            // Past the buffer — keep eating until we see a newline so the next
            // line starts clean. Don't try to parse a truncated payload.
            lineOverflow = true;
        }
    }

    uint32_t     now  = millis();
    BuddyState   next = state_derive(status, isLive());
    currentState      = next;

    prompt_ui_update(&promptUi, status.prompt, isLive(), now);
    bool up_raw     = (digitalRead(2) == HIGH);
    bool down_raw   = (digitalRead(0) == LOW);
    bool center_raw = (digitalRead(1) == HIGH);
    ButtonEvent ev  = buttons_step(&btns, now, up_raw, down_raw, center_raw);
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
        poll_nav(now);
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
            lastDrawnState = (BuddyState)0xFF;
            lastDrawnMsg[0] = 0;
        }
    } else if (currentCard == CARD_STATUS) {
        lastPromptVisible = false;
        bool stateChanged = (next != lastDrawnState);
        bool msgChanged =
            strncmp(lastDrawnMsg, status.msg, sizeof(lastDrawnMsg)) != 0;
        if (stateChanged || msgChanged) {
            paint_current_card();
            sync_status_meta();
        } else {
            static uint32_t lastTick = 0;
            if (now - lastTick > 1000) {
                lastTick = now;
                BuddyState recheck = state_derive(status, isLive());
                currentState       = recheck;
                if (recheck != lastDrawnState) {
                    paint_current_card();
                    sync_status_meta();
                }
            }
        }
    } else if (currentCard == CARD_EYES) {
        eyes_tick(eyesAnim, currentState, now);
        bool eyesChanged = !eyesFrameValid ||
                           lastEyesState != currentState ||
                           lastEyesH != eyesAnim.draw_h ||
                           lastEyesDx != eyesAnim.draw_dx ||
                           lastEyesBaseY != eyesAnim.draw_base_y ||
                           lastEyesDiscAge != eyesAnim.disc_age_ms;
        if (eyesChanged) {
            paint_current_card();
            lastEyesState = currentState;
            lastEyesH = eyesAnim.draw_h;
            lastEyesDx = eyesAnim.draw_dx;
            lastEyesBaseY = eyesAnim.draw_base_y;
            lastEyesDiscAge = eyesAnim.disc_age_ms;
            eyesFrameValid = true;
        }
    } else {
        static bool navInit = false;
        static uint8_t lastNextRaw = HIGH;
        static uint8_t lastPrevRaw = HIGH;
        uint8_t nextRaw = digitalRead(PIN_BTN_NEXT) == LOW ? LOW : HIGH;
        uint8_t prevRaw = digitalRead(PIN_BTN_PREV) == HIGH ? HIGH : LOW;
        if (!navInit || nextRaw != lastNextRaw || prevRaw != lastPrevRaw) {
            paint_current_card();
            lastNextRaw = nextRaw;
            lastPrevRaw = prevRaw;
            navInit = true;
        }
    }

    while ((millis() - loop_start) < FRAME_PACING_MS) {
        yield();
    }
}
