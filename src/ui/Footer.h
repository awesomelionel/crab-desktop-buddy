// src/ui/Footer.h
#pragma once

#include <stdint.h>

class Adafruit_ST7789;

namespace ui {

// Shared bottom footer used by every card that wants to surface link
// state. Renders a green LIVE / red OFFLN pill on the left followed by
// the device name. Geometry is fixed and globally consistent: 18 px
// band, 12 px tall pill, 9 px (textSize 1) labels recentred on the
// band midline. Drawn directly to the TFT — no compositor.
//
// The caller is responsible for ensuring the band area (y >= 117) was
// erased to black before this is called. The helper does not erase its
// own background — that lets it composite cleanly on top of partial
// redraws driven by per-card dirty rects.
constexpr int   kFooterH         = 18;
constexpr int   kFooterTopY      = 135 - kFooterH;   // 117
constexpr int   kFooterPillX     = 4;
constexpr int   kFooterPillW     = 34;
constexpr int   kFooterPillH     = 12;

void drawFooter(Adafruit_ST7789& tft, const char* device_name, bool live);

}  // namespace ui
