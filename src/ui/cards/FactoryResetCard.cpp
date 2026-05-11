#include "FactoryResetCard.h"

#include "../../core/FactoryResetCoordinator.h"
#include "../../display/Display.h"
#include "factory_reset_state.h"

#include <Adafruit_ST7789.h>

namespace {
constexpr uint32_t kMinRedrawIntervalMs = 100;
}

void FactoryResetCard::tick(uint32_t now_ms) {
    now_ms_ = now_ms;
}

void FactoryResetCard::invalidate() {
    full_clear_     = true;
    last_pct_drawn_ = 255;
    last_phase_     = 255;
}

bool FactoryResetCard::isActive() const {
    auto p = coord_.phase();
    return p == factory_reset_state::Phase::AwaitingHold
        || p == factory_reset_state::Phase::Resetting;
}

bool FactoryResetCard::isDirty() const {
    if (!isActive()) return false;
    uint32_t held = coord_.holdMs();
    uint8_t pct = (held >= factory_reset_state::kHoldRequiredMs)
                  ? 100
                  : (uint8_t)((held * 100u)
                              / factory_reset_state::kHoldRequiredMs);
    uint8_t phase_id = (uint8_t)coord_.phase();
    return full_clear_
        || pct != last_pct_drawn_
        || phase_id != last_phase_
        || (now_ms_ - last_render_ms_) >= kMinRedrawIntervalMs;
}

void FactoryResetCard::render(Display& display) {
    auto& tft  = display.tft();
    auto  phase = coord_.phase();
    auto  held  = coord_.holdMs();
    uint8_t pct = (held >= factory_reset_state::kHoldRequiredMs)
                  ? 100
                  : (uint8_t)((held * 100u)
                              / factory_reset_state::kHoldRequiredMs);

    if (full_clear_ || (uint8_t)phase != last_phase_) {
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextWrap(false);

        if (phase == factory_reset_state::Phase::Resetting) {
            tft.setTextSize(2);
            tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
            tft.setCursor(8, 20);
            tft.print("RESET DONE");

            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 60);
            tft.print("Rebooting...");
        } else {
            tft.setTextSize(2);
            tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
            tft.setCursor(8, 6);
            tft.print("FACTORY RESET");

            tft.setTextSize(1);
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 32);
            tft.print("Hold center 3s to confirm.");
            tft.setCursor(8, 46);
            tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
            tft.print("Release to cancel.");

            // Progress bar outline (only drawn once per phase).
            tft.drawRect(8, 80, 224, 20, ST77XX_WHITE);
        }
        full_clear_ = false;
        last_phase_ = (uint8_t)phase;
    }

    if (phase == factory_reset_state::Phase::AwaitingHold) {
        // Erase + redraw only the bar interior.
        tft.fillRect(9, 81, 222, 18, ST77XX_BLACK);
        uint16_t fill = (uint16_t)((222u * pct) / 100u);
        if (fill > 0) tft.fillRect(9, 81, fill, 18, ST77XX_RED);
    }

    last_pct_drawn_ = pct;
    last_render_ms_ = now_ms_;
}
