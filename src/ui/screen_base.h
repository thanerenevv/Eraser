#pragma once

#include <lvgl.h>
#include "theme.h"
#include "../config.h"

#define SCREEN_CONTENT_TOP (STATUS_BAR_H + 3)
#define SCREEN_CONTENT_H   (LCD_HEIGHT - SCREEN_CONTENT_TOP)

lv_obj_t *screen_base_create(const char *title, bool show_back);
