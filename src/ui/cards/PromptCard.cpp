#include "PromptCard.h"

#include <Arduino.h>
#include <string.h>

#include "../../display/Display.h"

PromptCard::PromptCard(PromptUi& ui)
    : ui_(ui),
      ever_drawn_(false),
      last_visible_(false),
      last_highlight_(OPT_APPROVE),
      last_id_{0},
      last_flashing_(false),
      footer_device_{0},
      footer_live_(false),
      has_footer_(false) {}

void PromptCard::invalidate() {
    ever_drawn_  = false;
    last_id_[0]  = 0;
}

bool PromptCard::isDirty() const {
    if (!ever_drawn_) return true;
    PromptView pv = prompt_ui_view(&ui_);
    if (pv.visible != last_visible_) return true;
    if (pv.highlight != last_highlight_) return true;
    if ((pv.flash_text != nullptr) != last_flashing_) return true;
    if (strncmp(last_id_, ui_.current_id, sizeof(last_id_)) != 0) return true;
    return false;
}

void PromptCard::setFooter(const char* device_name, bool live) {
    has_footer_ = true;
    footer_live_ = live;
    if (device_name) {
        strncpy(footer_device_, device_name, sizeof(footer_device_) - 1);
        footer_device_[sizeof(footer_device_) - 1] = 0;
    } else {
        footer_device_[0] = 0;
    }
}

void PromptCard::render(Display& display) {
    auto& tft = display.tft();
    PromptView v = prompt_ui_view(&ui_);

    tft.fillScreen(ST77XX_BLACK);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    int16_t x1, y1; uint16_t tw, th;
    const char* hdr = "PERMISSION?";
    tft.getTextBounds(hdr, 0, 0, &x1, &y1, &tw, &th);
    tft.setCursor((display.width() - (int)tw) / 2, 2);
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
        tft.setCursor((display.width() - (int)tw) / 2, 82);
        tft.print(v.flash_text);
    } else {
        const char* labels[3] = {"Approve", "Deny", "Dismiss"};
        const int ys[3] = {66, 82, 98};
        for (int i = 0; i < 3; i++) {
            bool hi = (i == (int)v.highlight);
            if (hi) {
                tft.fillRect(0, ys[i], display.width(), 16, ST77XX_WHITE);
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

    if (has_footer_) {
        tft.setTextSize(1);
        tft.setTextColor(footer_live_ ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
        tft.setCursor(8, 118);
        tft.print(footer_live_ ? "LIVE  " : "OFFLN ");
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.print(footer_device_);
    }

    last_visible_  = v.visible;
    last_highlight_ = v.highlight;
    last_flashing_ = (v.flash_text != nullptr);
    strncpy(last_id_, ui_.current_id, sizeof(last_id_) - 1);
    last_id_[sizeof(last_id_) - 1] = 0;
    ever_drawn_ = true;
}

bool PromptCard::handleButton(ButtonEvent ev, uint32_t now_ms) {
    if (ev == BTN_NONE) return true;
    prompt_ui_button(&ui_, ev, now_ms);
    return true;  // overlay swallows nav buttons.
}
