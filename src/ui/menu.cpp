#include "menu.h"
#include "screen_base.h"
#include "theme.h"
#include "../config.h"
#include "wifi_screen.h"
#include "bt_screen.h"
#include "rf_screen.h"
#include "ir_screen.h"
#include "settings_screen.h"
#include "gpio_screen.h"
#include <lvgl.h>
#include <stdlib.h>

#define HOME_ITEMS 6

#define ICON_BOX_W   64
#define ICON_BOX_H   64
#define ICON_BOX_X   ((LCD_WIDTH - ICON_BOX_W) / 2)
#define ICON_BOX_Y   (SCREEN_CONTENT_TOP + 15)
#define NAME_Y       (ICON_BOX_Y + ICON_BOX_H + 7)
#define DOTS_Y       (LCD_HEIGHT - 13)
#define ARROW_CY     (ICON_BOX_Y + ICON_BOX_H / 2)

typedef void (*ItemAction)(void);

typedef struct {
    int        index;
    lv_obj_t  *icon_box;
    lv_obj_t  *icon_lbl;
    lv_obj_t  *name_lbl;
    lv_obj_t  *left_arr;
    lv_obj_t  *right_arr;
    lv_obj_t  *dots[HOME_ITEMS];
} HomeCtx;

static const char *item_icons[HOME_ITEMS] = {
    LV_SYMBOL_WIFI,
    LV_SYMBOL_BLUETOOTH,
    LV_SYMBOL_SHUFFLE,
    LV_SYMBOL_EYE_OPEN,
    LV_SYMBOL_CHARGE,
    LV_SYMBOL_SETTINGS,
};

static const char *item_names[HOME_ITEMS] = {
    "Wi-Fi",
    "Bluetooth",
    "RF Radio",
    "Infrared",
    "GPIO",
    "Settings",
};

static void go_wifi(void)     { nav_push(wifi_view_create()); }
static void go_bt(void)       { nav_push(bt_view_create()); }
static void go_rf(void)       { nav_push(rf_view_create()); }
static void go_ir(void)       { nav_push(ir_view_create()); }
static void go_gpio(void)     { nav_push(gpio_view_create()); }
static void go_settings(void) { nav_push(settings_view_create()); }

static const ItemAction item_actions[HOME_ITEMS] = {
    go_wifi, go_bt, go_rf, go_ir, go_gpio, go_settings,
};

static void home_update(HomeCtx *ctx) {
    int i = ctx->index;
    lv_label_set_text(ctx->icon_lbl, item_icons[i]);
    lv_label_set_text(ctx->name_lbl, item_names[i]);

    for (int d = 0; d < HOME_ITEMS; d++) {
        lv_obj_set_style_bg_color(ctx->dots[d],
            theme_clr(d == i ? CLR_ACCENT : CLR_TEXT_DIM), 0);
        lv_obj_set_size(ctx->dots[d], d == i ? 8 : 6, d == i ? 8 : 6);
    }
}

static void home_prev(AppView *v) {
    HomeCtx *ctx = (HomeCtx *)v->ctx;
    ctx->index = (ctx->index - 1 + HOME_ITEMS) % HOME_ITEMS;
    home_update(ctx);
}

static void home_next(AppView *v) {
    HomeCtx *ctx = (HomeCtx *)v->ctx;
    ctx->index = (ctx->index + 1) % HOME_ITEMS;
    home_update(ctx);
}

static void home_on_up(AppView *v)    { home_prev(v); }
static void home_on_down(AppView *v)  { home_next(v); }
static void home_on_extra(AppView *v) { home_next(v); }

static void home_on_ok(AppView *v) {
    HomeCtx *ctx = (HomeCtx *)v->ctx;
    item_actions[ctx->index]();
}

static bool home_on_back(AppView *v) {
    home_prev(v);
    return true;
}

static void home_on_destroy(AppView *v) {
    free(v->ctx);
}

AppView *home_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("RF MULTITOOL", false);
    lv_obj_t *scr = v->root;

    HomeCtx *ctx = (HomeCtx *)calloc(1, sizeof(HomeCtx));
    ctx->index = 0;

    ctx->icon_box = lv_obj_create(scr);
    lv_obj_set_size(ctx->icon_box, ICON_BOX_W, ICON_BOX_H);
    lv_obj_set_pos(ctx->icon_box, ICON_BOX_X, ICON_BOX_Y);
    lv_obj_set_style_bg_color(ctx->icon_box, theme_clr(CLR_BG_LIGHT), 0);
    lv_obj_set_style_bg_opa(ctx->icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ctx->icon_box, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_border_width(ctx->icon_box, 2, 0);
    lv_obj_set_style_radius(ctx->icon_box, 14, 0);
    lv_obj_set_style_pad_all(ctx->icon_box, 0, 0);
    lv_obj_clear_flag(ctx->icon_box, LV_OBJ_FLAG_SCROLLABLE);

    ctx->icon_lbl = lv_label_create(ctx->icon_box);
    lv_obj_set_style_text_color(ctx->icon_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(ctx->icon_lbl, FONT_LARGE, 0);
    lv_obj_align(ctx->icon_lbl, LV_ALIGN_CENTER, 0, 0);

    ctx->name_lbl = lv_label_create(scr);
    lv_obj_set_width(ctx->name_lbl, LCD_WIDTH);
    lv_obj_set_style_text_color(ctx->name_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(ctx->name_lbl, FONT_MEDIUM, 0);
    lv_obj_set_style_text_align(ctx->name_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ctx->name_lbl, 0, NAME_Y);

    ctx->left_arr = lv_label_create(scr);
    lv_label_set_text(ctx->left_arr, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(ctx->left_arr, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(ctx->left_arr, FONT_LARGE, 0);
    lv_obj_set_pos(ctx->left_arr, 6, ARROW_CY - 10);

    ctx->right_arr = lv_label_create(scr);
    lv_label_set_text(ctx->right_arr, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(ctx->right_arr, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(ctx->right_arr, FONT_LARGE, 0);
    lv_obj_set_pos(ctx->right_arr, LCD_WIDTH - 24, ARROW_CY - 10);

    const int dot_sz  = 6;
    const int dot_gap = 6;
    const int total_w = HOME_ITEMS * dot_sz + (HOME_ITEMS - 1) * dot_gap;
    int dot_x = (LCD_WIDTH - total_w) / 2;
    for (int i = 0; i < HOME_ITEMS; i++) {
        ctx->dots[i] = lv_obj_create(scr);
        lv_obj_set_size(ctx->dots[i], dot_sz, dot_sz);
        lv_obj_set_pos(ctx->dots[i], dot_x, DOTS_Y);
        lv_obj_set_style_radius(ctx->dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ctx->dots[i], 0, 0);
        lv_obj_set_style_pad_all(ctx->dots[i], 0, 0);
        dot_x += dot_sz + dot_gap;
    }

    home_update(ctx);

    v->ctx        = ctx;
    v->on_up      = home_on_up;
    v->on_down    = home_on_down;
    v->on_ok      = home_on_ok;
    v->on_back    = home_on_back;
    v->on_extra   = home_on_extra;
    v->on_destroy = home_on_destroy;
    return v;
}
