#pragma once

#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include "state.h"

struct EyesAnim {
    BuddyState prev_state;

    // DISCONNECTED asleep
    uint32_t disc_anim_start_ms;  // millis() at entry to STATE_DISCONNECTED
    uint32_t disc_age_ms;         // cached for render: (now - disc_anim_start_ms)

    // Squish-blink (IDLE, WAITING): index into height table, -1 = idle between blinks
    int8_t   blink_i;
    uint32_t next_blink_ms;
    uint32_t blink_step_deadline_ms;

    // Glance (IDLE only)
    int16_t  glance_x;
    uint32_t next_glance_ms;
    uint32_t glance_return_ms;

    // WORKING scan
    uint32_t scan_epoch_ms;

    // Cached for render (updated in eyes_tick)
    uint8_t  draw_h;
    int16_t  draw_dx;
    int16_t  draw_base_y;
    int8_t   draw_sweat_y;
};

void eyes_reset(EyesAnim& e);
void eyes_tick(EyesAnim& e, BuddyState state, uint32_t now_ms);
void eyes_render(Adafruit_ST7789& tft, const EyesAnim& e, BuddyState state);
