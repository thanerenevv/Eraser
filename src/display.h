#pragma once

#include <lvgl.h>

void display_init(void);
lv_display_t *display_get(void);

void display_set_brightness(uint8_t pct);
uint8_t display_get_brightness(void);
