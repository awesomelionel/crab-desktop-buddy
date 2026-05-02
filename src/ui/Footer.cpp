// src/ui/Footer.cpp
#include "Footer.h"

#include <Adafruit_ST7789.h>

namespace ui {

void drawFooter(Adafruit_ST7789& tft, const char* device_name, bool live) {
    const int mid_y   = kFooterTopY + kFooterH / 2;
    const int pill_y  = mid_y - kFooterPillH / 2;

    // Pill body
    tft.fillRect(kFooterPillX, pill_y,
                 kFooterPillW, kFooterPillH,
                 live ? ST77XX_GREEN : ST77XX_RED);

    // Label baseline: textSize 1 is 8 px tall ascender, +1 to nudge
    // visually centred against the pill.
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE, live ? ST77XX_GREEN : ST77XX_RED);
    tft.setCursor(kFooterPillX + 4, pill_y + 3);
    tft.print(live ? "LIVE" : "OFFL");

    if (device_name && device_name[0]) {
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(kFooterPillX + kFooterPillW + 6, pill_y + 3);
        tft.print(device_name);
    }
}

}  // namespace ui
