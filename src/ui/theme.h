#pragma once

#include <lvgl.h>

#define CLR_BG          0x111111
#define CLR_BG_LIGHT    0x1E1E1E
#define CLR_PANEL       0x181818
#define CLR_STATUSBAR   0x0D0D0D
#define CLR_ACCENT      0xFF6600
#define CLR_ACCENT_DIM  0x993D00
#define CLR_TEXT        0xFFFFFF
#define CLR_TEXT_DIM    0x888888
#define CLR_TEXT_DARK   0x111111
#define CLR_SEPARATOR   0x2A2A2A
#define CLR_WARNING     0xE53935
#define CLR_SUCCESS     0x43A047
#define CLR_GRID        0x242424

#define FONT_SMALL   (&lv_font_montserrat_12)
#define FONT_NORMAL  (&lv_font_montserrat_14)
#define FONT_MEDIUM  (&lv_font_montserrat_16)
#define FONT_LARGE   (&lv_font_montserrat_20)
#define FONT_TITLE   (&lv_font_montserrat_18)

static inline lv_color_t theme_clr(uint32_t hex) {
    return lv_color_hex(hex);
}

static inline void theme_apply_screen(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, theme_clr(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
}
