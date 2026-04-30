#include "Display.h"

#include <Arduino.h>

Display::Display() : tft_(TFT_CS, TFT_DC, TFT_RST) {}

void Display::begin() {
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);
    tft_.init(135, 240);
    tft_.setRotation(1);
    tft_.fillScreen(ST77XX_BLACK);
    tft_.setTextWrap(false);
}
