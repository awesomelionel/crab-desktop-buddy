#pragma once

#include <stdint.h>
#include <Adafruit_ST7789.h>

// 240x135 ST7789 backed by SPI on the Reverse-TFT Feather.
class Display {
public:
    static constexpr int W = 240;
    static constexpr int H = 135;

    Display();
    void begin();

    // Set the backlight brightness as a 0..100 percent. Values above 100
    // are clamped. 0 means off (digital low). No-op if the requested duty
    // matches the currently applied duty.
    void setBacklight(uint8_t pct);

    // Convenience wrapper: true → 100, false → 0. Kept so existing
    // callers compile unchanged.
    void setBacklight(bool on) { setBacklight(static_cast<uint8_t>(on ? 100 : 0)); }

    bool    isAsleep()      const { return current_pct_ == 0; }
    uint8_t backlightPct()  const { return current_pct_; }

    Adafruit_ST7789& tft() { return tft_; }
    int width()  const { return W; }
    int height() const { return H; }

private:
    static constexpr int kBacklightFreqHz  = 5000;
    static constexpr int kBacklightBits    = 8;

    Adafruit_ST7789 tft_;
    bool    asleep_       = false;  // legacy field, kept for API parity
    uint8_t current_pct_  = 0;
};
