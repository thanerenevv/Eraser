#include "screen_base.h"
#include <Arduino.h>
#include <stdlib.h>

typedef struct {
    lv_obj_t   *scr;
    lv_obj_t   *clock_lbl;
    lv_obj_t   *batt_lbl;
    lv_timer_t *timer;
} StatusBar;

static const char *battery_symbol(int pct) {
    if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (pct >= 65) return LV_SYMBOL_BATTERY_3;
    if (pct >= 40) return LV_SYMBOL_BATTERY_2;
    if (pct >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void status_update(StatusBar *sb) {
    // Uptime clock.
    uint32_t s = millis() / 1000;
    uint32_t h = s / 3600;
    uint32_t m = (s % 3600) / 60;
    uint32_t sec = s % 60;
    if (h > 0) lv_label_set_text_fmt(sb->clock_lbl, "%lu:%02lu", (unsigned long)h, (unsigned long)m);
    else       lv_label_set_text_fmt(sb->clock_lbl, "%02lu:%02lu", (unsigned long)m, (unsigned long)sec);

#if BATT_ADC_PIN >= 0
    uint32_t mv = analogReadMilliVolts(BATT_ADC_PIN);
    float v = (mv / 1000.0f) * BATT_DIV_RATIO;
    int pct = (int)((v - 3.3f) / (4.2f - 3.3f) * 100.0f);
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    lv_label_set_text(sb->batt_lbl, battery_symbol(pct));
#else
    lv_label_set_text(sb->batt_lbl, LV_SYMBOL_CHARGE);
    (void)battery_symbol;
#endif
}

static void status_timer_cb(lv_timer_t *t) {
    StatusBar *sb = (StatusBar *)lv_timer_get_user_data(t);
    if (sb->scr == lv_screen_active()) status_update(sb);
}

static void status_delete_cb(lv_event_t *e) {
    StatusBar *sb = (StatusBar *)lv_event_get_user_data(e);
    if (sb->timer) lv_timer_delete(sb->timer);
    free(sb);
}

lv_obj_t *screen_base_create(const char *title, bool show_back) {
    lv_obj_t *scr = lv_obj_create(NULL);
    theme_apply_screen(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LCD_WIDTH, STATUS_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, theme_clr(CLR_STATUSBAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    if (show_back) {
        lv_obj_t *back = lv_label_create(bar);
        lv_label_set_text(back, LV_SYMBOL_LEFT " BACK");
        lv_obj_set_style_text_color(back, theme_clr(CLR_TEXT_DIM), 0);
        lv_obj_set_style_text_font(back, FONT_SMALL, 0);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 6, 0);

        lv_obj_t *t = lv_label_create(bar);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_color(t, theme_clr(CLR_ACCENT), 0);
        lv_obj_set_style_text_font(t, FONT_SMALL, 0);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_t *t = lv_label_create(bar);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_color(t, theme_clr(CLR_ACCENT), 0);
        lv_obj_set_style_text_font(t, FONT_SMALL, 0);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 8, 0);
    }

    StatusBar *sb = (StatusBar *)calloc(1, sizeof(StatusBar));
    sb->scr = scr;

    sb->batt_lbl = lv_label_create(bar);
    lv_obj_set_style_text_color(sb->batt_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(sb->batt_lbl, FONT_SMALL, 0);
    lv_obj_align(sb->batt_lbl, LV_ALIGN_RIGHT_MID, -8, 0);

    sb->clock_lbl = lv_label_create(bar);
    lv_obj_set_style_text_color(sb->clock_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(sb->clock_lbl, FONT_SMALL, 0);
    lv_obj_align(sb->clock_lbl, LV_ALIGN_RIGHT_MID, -28, 0);

    status_update(sb);
    sb->timer = lv_timer_create(status_timer_cb, 1000, sb);
    lv_obj_add_event_cb(bar, status_delete_cb, LV_EVENT_DELETE, sb);

    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_size(sep, LCD_WIDTH, 1);
    lv_obj_set_pos(sep, 0, STATUS_BAR_H);
    lv_obj_set_style_bg_color(sep, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    return scr;
}
