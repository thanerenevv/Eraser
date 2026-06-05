#include "settings_screen.h"
#include "views.h"
#include "screen_base.h"
#include "menu_list.h"
#include "theme.h"
#include "../config.h"
#include "../settings_store.h"
#include <lvgl.h>
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BRIGHT_STEP 5

typedef struct {
    lv_obj_t *bar;
    lv_obj_t *pct;
} BrightCtx;

static void bright_refresh(BrightCtx *c) {
    lv_bar_set_value(c->bar, settings_brightness(), LV_ANIM_ON);
    lv_label_set_text_fmt(c->pct, "%d%%", (int)settings_brightness());
}

static void bright_on_up(AppView *v) {
    int p = settings_brightness() + BRIGHT_STEP;
    settings_set_brightness((uint8_t)(p > 100 ? 100 : p));
    bright_refresh((BrightCtx *)v->ctx);
}

static void bright_on_down(AppView *v) {
    int p = settings_brightness() - BRIGHT_STEP;
    settings_set_brightness((uint8_t)(p < 5 ? 5 : p));
    bright_refresh((BrightCtx *)v->ctx);
}

static void bright_on_destroy(AppView *v) { free(v->ctx); }

static AppView *brightness_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("BRIGHTNESS", true);
    BrightCtx *c = (BrightCtx *)calloc(1, sizeof(BrightCtx));

    lv_obj_t *icon = lv_label_create(v->root);
    lv_label_set_text(icon, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(icon, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(icon, FONT_LARGE, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 6);

    c->pct = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->pct, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->pct, FONT_LARGE, 0);
    lv_obj_align(c->pct, LV_ALIGN_CENTER, 0, -8);

    c->bar = lv_bar_create(v->root);
    lv_obj_set_size(c->bar, LCD_WIDTH - 60, 14);
    lv_obj_align(c->bar, LV_ALIGN_CENTER, 0, 22);
    lv_bar_set_range(c->bar, 0, 100);
    lv_obj_set_style_bg_color(c->bar, theme_clr(CLR_BG_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_radius(c->bar, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c->bar, theme_clr(CLR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(c->bar, 7, LV_PART_INDICATOR);

    lv_obj_t *hint = lv_label_create(v->root);
    lv_label_set_text(hint, LV_SYMBOL_UP " / " LV_SYMBOL_DOWN "  adjust");
    lv_obj_set_style_text_color(hint, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    v->ctx        = c;
    v->on_up      = bright_on_up;
    v->on_down    = bright_on_down;
    v->on_destroy = bright_on_destroy;
    bright_refresh(c);
    return v;
}

static const uint8_t k_timeout_vals[]   = { 0, 1, 2, 3, 5, 10 };
static const char   *k_timeout_labels[] = { "Never", "1 min", "2 min", "3 min", "5 min", "10 min" };
#define TIMEOUT_COUNT 6

typedef struct {
    lv_obj_t *val_lbl;
    lv_obj_t *bar;
    int        sel;
} TimeoutCtx;

static int timeout_find_sel(void) {
    uint8_t cur = settings_display_timeout();
    for (int i = 0; i < TIMEOUT_COUNT; i++)
        if (k_timeout_vals[i] == cur) return i;
    return 3;
}

static void timeout_refresh(TimeoutCtx *c) {
    lv_label_set_text(c->val_lbl, k_timeout_labels[c->sel]);
    lv_bar_set_value(c->bar, c->sel, LV_ANIM_ON);
    settings_set_display_timeout(k_timeout_vals[c->sel]);
}

static void timeout_on_up(AppView *v) {
    TimeoutCtx *c = (TimeoutCtx *)v->ctx;
    c->sel = (c->sel + 1) % TIMEOUT_COUNT;
    timeout_refresh(c);
}

static void timeout_on_down(AppView *v) {
    TimeoutCtx *c = (TimeoutCtx *)v->ctx;
    c->sel = (c->sel - 1 + TIMEOUT_COUNT) % TIMEOUT_COUNT;
    timeout_refresh(c);
}

static void timeout_on_destroy(AppView *v) { free(v->ctx); }

static AppView *timeout_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("DISPLAY TIMEOUT", true);
    TimeoutCtx *c = (TimeoutCtx *)calloc(1, sizeof(TimeoutCtx));
    c->sel = timeout_find_sel();

    lv_obj_t *icon = lv_label_create(v->root);
    lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(icon, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(icon, FONT_LARGE, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 6);

    c->val_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->val_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->val_lbl, FONT_LARGE, 0);
    lv_obj_align(c->val_lbl, LV_ALIGN_CENTER, 0, -8);

    c->bar = lv_bar_create(v->root);
    lv_obj_set_size(c->bar, LCD_WIDTH - 60, 14);
    lv_obj_align(c->bar, LV_ALIGN_CENTER, 0, 22);
    lv_bar_set_range(c->bar, 0, TIMEOUT_COUNT - 1);
    lv_obj_set_style_bg_color(c->bar, theme_clr(CLR_BG_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_radius(c->bar, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c->bar, theme_clr(CLR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(c->bar, 7, LV_PART_INDICATOR);

    lv_obj_t *hint = lv_label_create(v->root);
    lv_label_set_text(hint, LV_SYMBOL_UP " / " LV_SYMBOL_DOWN "  cycle");
    lv_obj_set_style_text_color(hint, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    v->ctx        = c;
    v->on_up      = timeout_on_up;
    v->on_down    = timeout_on_down;
    v->on_destroy = timeout_on_destroy;
    timeout_refresh(c);
    return v;
}

typedef struct {
    MenuList   *list;
    lv_obj_t   *status_lbl;
} SavedWifiCtx;

static SavedWifiCtx *g_saved_wifi_ctx = NULL;

static void saved_wifi_rebuild(SavedWifiCtx *c);

static void saved_wifi_delete(void *u) {
    int idx = (int)(intptr_t)u;
    settings_wifi_del(idx);
    if (g_saved_wifi_ctx) saved_wifi_rebuild(g_saved_wifi_ctx);
}

static void saved_wifi_rebuild(SavedWifiCtx *c) {
    int n = settings_wifi_count();
    menu_list_clear(c->list);

    if (n == 0) {
        lv_label_set_text(c->status_lbl, "No saved networks");
        MenuItem none = { LV_SYMBOL_WARNING, "No saved networks", NULL, NULL, NULL };
        menu_list_add(c->list, &none);
        return;
    }

    lv_label_set_text_fmt(c->status_lbl, "%d network(s)", n);
    static char sw_lbl[WIFI_CRED_MAX][WIFI_SSID_LEN];
    for (int i = 0; i < n; i++) {
        WifiCred cred;
        settings_wifi_get(i, &cred);
        strncpy(sw_lbl[i], cred.ssid, WIFI_SSID_LEN - 1);
        sw_lbl[i][WIFI_SSID_LEN - 1] = '\0';
        MenuItem it = { LV_SYMBOL_WIFI,  sw_lbl[i], "del OK", saved_wifi_delete, (void *)(intptr_t)i };
        menu_list_add(c->list, &it);
    }
}

static void swifi_on_up(AppView *v)      { menu_list_up(((SavedWifiCtx *)v->ctx)->list); }
static void swifi_on_down(AppView *v)    { menu_list_down(((SavedWifiCtx *)v->ctx)->list); }
static void swifi_on_ok(AppView *v)      { menu_list_activate(((SavedWifiCtx *)v->ctx)->list); }
static void swifi_on_destroy(AppView *v) {
    SavedWifiCtx *c = (SavedWifiCtx *)v->ctx;
    if (c == g_saved_wifi_ctx) g_saved_wifi_ctx = NULL;
    menu_list_destroy(c->list);
    free(c);
}

static AppView *saved_wifi_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("SAVED WI-FI", true);

    SavedWifiCtx *c = (SavedWifiCtx *)calloc(1, sizeof(SavedWifiCtx));
    g_saved_wifi_ctx = c;

    c->status_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->status_lbl, 8, SCREEN_CONTENT_TOP + 2);

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 18, SCREEN_CONTENT_H - 18);

    saved_wifi_rebuild(c);

    v->ctx        = c;
    v->on_up      = swifi_on_up;
    v->on_down    = swifi_on_down;
    v->on_ok      = swifi_on_ok;
    v->on_destroy = swifi_on_destroy;
    return v;
}

static AppView *sysinfo_view_create(void) {
    char buf[220];
    uint32_t s   = millis() / 1000;
    uint32_t h   = s / 3600;
    uint32_t m   = (s % 3600) / 60;
    uint32_t sec = s % 60;
    snprintf(buf, sizeof(buf),
             "Chip: %s rev%d\n"
             "Cores: %d @ %lu MHz\n"
             "Flash: %lu MB\n"
             "PSRAM: %lu MB\n"
             "Heap free: %lu KB\n"
             "Uptime: %02lu:%02lu:%02lu\n"
             "MAC: %s",
             ESP.getChipModel(),
             (int)ESP.getChipRevision(),
             (int)ESP.getChipCores(),
             (unsigned long)ESP.getCpuFreqMHz(),
             (unsigned long)(ESP.getFlashChipSize() / (1024UL * 1024UL)),
             (unsigned long)(ESP.getPsramSize()    / (1024UL * 1024UL)),
             (unsigned long)(ESP.getFreeHeap()     / 1024UL),
             (unsigned long)h, (unsigned long)m, (unsigned long)sec,
             WiFi.macAddress().c_str());
    return info_view_create("SYSTEM INFO", buf);
}

static AppView *display_info_view_create(void) {
    return info_view_create("DISPLAY",
        "ST7789 IPS LCD\n"
        "320 x 170 @ 16-bit color\n"
        "SPI @ 40 MHz, DMA buffered\n"
        "Renderer: LVGL 9.x partial\n"
        "Backlight: PWM (NVS-persisted)");
}

static AppView *about_view_create(void) {
    return info_view_create("ABOUT",
        "Eraser v0.0.1\n"
        "Platform: ESP32-S3 N16R8\n"
        "Flash: 16MB  PSRAM: 8MB\n");
}

static void open_brightness(void *u)   { (void)u; nav_push(brightness_view_create()); }
static void open_timeout(void *u)      { (void)u; nav_push(timeout_view_create()); }
static void open_saved_wifi(void *u)   { (void)u; nav_push(saved_wifi_view_create()); }
static void open_sysinfo(void *u)      { (void)u; nav_push(sysinfo_view_create()); }
static void open_display_info(void *u) { (void)u; nav_push(display_info_view_create()); }
static void open_about(void *u)        { (void)u; nav_push(about_view_create()); }

AppView *settings_view_create(void) {
    static const MenuItem items[] = {
        { LV_SYMBOL_IMAGE,    "Brightness",      NULL, open_brightness,  NULL },
        { LV_SYMBOL_SETTINGS, "Display Timeout", NULL, open_timeout,     NULL },
        { LV_SYMBOL_WIFI,     "Saved Wi-Fi",     NULL, open_saved_wifi,  NULL },
        { LV_SYMBOL_LIST,     "System Info",     NULL, open_sysinfo,     NULL },
        { LV_SYMBOL_IMAGE,    "Display Info",    NULL, open_display_info,NULL },
        { LV_SYMBOL_SETTINGS, "About",           NULL, open_about,       NULL },
    };
    return list_view_create("SETTINGS", items, 6);
}
