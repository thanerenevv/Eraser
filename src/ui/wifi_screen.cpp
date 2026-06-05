#include "wifi_screen.h"
#include "input_view.h"
#include "views.h"
#include "screen_base.h"
#include "menu_list.h"
#include "theme.h"
#include "../config.h"
#include "../settings_store.h"
#include <WiFi.h>
#include <lvgl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SCAN_MAX        14
#define CONNECT_TIMEOUT_MS 15000

typedef struct {
    lv_obj_t   *status_lbl;
    lv_obj_t   *detail_lbl;
    lv_obj_t   *spinner;
    lv_timer_t *poll;
    uint32_t    start_ms;
    bool        done;
    char        ssid[33];
    char        pass[64];
} ConnCtx;

static void conn_on_destroy(AppView *v) {
    ConnCtx *c = (ConnCtx *)v->ctx;
    if (c->poll) lv_timer_delete(c->poll);
    free(c);
}

static bool conn_on_back(AppView *v) {
    ConnCtx *c = (ConnCtx *)v->ctx;
    if (!c->done) {
        WiFi.disconnect(true);
    } else if (WiFi.isConnected()) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    return false;
}

static void conn_poll(lv_timer_t *t) {
    ConnCtx *c = (ConnCtx *)lv_timer_get_user_data(t);
    if (c->done) return;

    wl_status_t st = WiFi.status();

    if (st == WL_CONNECTED) {
        c->done = true;
        lv_timer_delete(c->poll);
        c->poll = NULL;

        lv_obj_add_flag(c->spinner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(c->status_lbl, LV_SYMBOL_WIFI "  Connected!");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_SUCCESS), 0);

        char info[80];
        snprintf(info, sizeof(info), "%s\n%s",
                 c->ssid, WiFi.localIP().toString().c_str());
        lv_label_set_text(c->detail_lbl, info);

        if (c->pass[0] != '\0') {
            settings_wifi_put(c->ssid, c->pass);
        }
        return;
    }

    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL ||
        (millis() - c->start_ms) >= CONNECT_TIMEOUT_MS) {
        c->done = true;
        lv_timer_delete(c->poll);
        c->poll = NULL;
        lv_obj_add_flag(c->spinner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(c->status_lbl, LV_SYMBOL_WARNING "  Failed");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_WARNING), 0);
        const char *reason = (st == WL_NO_SSID_AVAIL) ? "Network not found"
                           : (st == WL_CONNECT_FAILED) ? "Wrong password"
                                                        : "Timed out";
        lv_label_set_text(c->detail_lbl, reason);
    }
}

static AppView *wifi_connect_view_create(const char *ssid, const char *pass) {
    AppView *v = app_view_new();
    v->root = screen_base_create("CONNECTING", true);
    lv_obj_t *scr = v->root;

    ConnCtx *c = (ConnCtx *)calloc(1, sizeof(ConnCtx));
    strncpy(c->ssid, ssid, sizeof(c->ssid) - 1);
    strncpy(c->pass, pass ? pass : "", sizeof(c->pass) - 1);
    c->start_ms = millis();

    lv_obj_t *ssid_lbl = lv_label_create(scr);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ssid_lbl, LCD_WIDTH - 16);
    lv_label_set_text(ssid_lbl, ssid);
    lv_obj_set_style_text_color(ssid_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(ssid_lbl, FONT_MEDIUM, 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 6);

    c->spinner = lv_spinner_create(scr);
    lv_spinner_set_anim_params(c->spinner, 1000, 60);
    lv_obj_set_size(c->spinner, 48, 48);
    lv_obj_align(c->spinner, LV_ALIGN_CENTER, 0, -16);
    lv_obj_set_style_arc_color(c->spinner, theme_clr(CLR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(c->spinner, theme_clr(CLR_BG_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_arc_width(c->spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(c->spinner, 4, LV_PART_MAIN);

    c->status_lbl = lv_label_create(scr);
    lv_label_set_text(c->status_lbl, "Connecting...");
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_NORMAL, 0);
    lv_obj_align(c->status_lbl, LV_ALIGN_CENTER, 0, 24);

    c->detail_lbl = lv_label_create(scr);
    lv_label_set_text(c->detail_lbl, "");
    lv_obj_set_style_text_color(c->detail_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->detail_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_align(c->detail_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(c->detail_lbl, LCD_WIDTH - 16);
    lv_obj_align(c->detail_lbl, LV_ALIGN_CENTER, 0, 44);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    if (pass && pass[0] != '\0') WiFi.begin(ssid, pass);
    else                          WiFi.begin(ssid);

    c->poll = lv_timer_create(conn_poll, 500, c);

    v->ctx        = c;
    v->on_back    = conn_on_back;
    v->on_destroy = conn_on_destroy;
    return v;
}

typedef struct {
    MenuList   *list;
    lv_obj_t   *status_lbl;
    lv_timer_t *poll;
    bool        scanning;
    char        ssid[SCAN_MAX][33];
    wifi_auth_mode_t enc[SCAN_MAX];
    int         result_count;
} ScanCtx;

static char g_scan_detail[SCAN_MAX][140];
static ScanCtx *g_scan_ctx = NULL;

static const char *enc_str(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN:           return "Open";
        case WIFI_AUTH_WEP:            return "WEP";
        case WIFI_AUTH_WPA_PSK:        return "WPA";
        case WIFI_AUTH_WPA2_PSK:       return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:   return "WPA/2";
        case WIFI_AUTH_WPA3_PSK:       return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:  return "WPA2/3";
        default:                       return "?";
    }
}

static void on_network_ok(void *u);
static void start_scan(ScanCtx *c);
static void rescan_cb(void *u) { start_scan((ScanCtx *)u); }

static void populate_results(ScanCtx *c, int n) {
    if (n > SCAN_MAX) n = SCAN_MAX;
    c->result_count = n;

    menu_list_clear(c->list);
    MenuItem rescan = { LV_SYMBOL_REFRESH, "Rescan", NULL, rescan_cb, c };
    menu_list_add(c->list, &rescan);

    static char row_label[SCAN_MAX][34];
    static char row_val[SCAN_MAX][10];

    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        if (s.length() == 0) s = "<hidden>";
        strncpy(c->ssid[i], s.c_str(), 32);
        c->ssid[i][32] = '\0';
        c->enc[i] = WiFi.encryptionType(i);

        snprintf(row_label[i], sizeof(row_label[i]), "%s", c->ssid[i]);
        snprintf(row_val[i],   sizeof(row_val[i]),   "%ddBm", WiFi.RSSI(i));

        uint8_t *b = WiFi.BSSID(i);
        bool saved = settings_wifi_find(c->ssid[i]) >= 0;
        snprintf(g_scan_detail[i], sizeof(g_scan_detail[i]),
                 "SSID: %s\nBSSID: %02X:%02X:%02X:%02X:%02X:%02X\n"
                 "Ch: %d  RSSI: %d dBm\nSecurity: %s%s",
                 c->ssid[i], b[0], b[1], b[2], b[3], b[4], b[5],
                 WiFi.channel(i), WiFi.RSSI(i), enc_str(c->enc[i]),
                 saved ? "\nCredentials: saved" : "");

        const char *icon = (c->enc[i] == WIFI_AUTH_OPEN)
                           ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_OPEN;
        MenuItem it = { icon, row_label[i], row_val[i],
                        on_network_ok, (void *)(intptr_t)i };
        menu_list_add(c->list, &it);
    }

    lv_label_set_text_fmt(c->status_lbl, "%d networks", n);
}

static void on_password_done(const char *pass, void *user) {
    if (!pass) return;
    int idx = (int)(intptr_t)user;
    if (!g_scan_ctx || idx < 0 || idx >= g_scan_ctx->result_count) return;
    nav_push(wifi_connect_view_create(g_scan_ctx->ssid[idx], pass));
}

static void on_network_ok(void *u) {
    int idx = (int)(intptr_t)u;
    if (!g_scan_ctx || idx < 0 || idx >= g_scan_ctx->result_count) return;

    const char *ssid = g_scan_ctx->ssid[idx];
    bool is_open = (g_scan_ctx->enc[idx] == WIFI_AUTH_OPEN);

    if (is_open) {
        nav_push(wifi_connect_view_create(ssid, ""));
        return;
    }

    int saved_idx = settings_wifi_find(ssid);
    if (saved_idx >= 0) {
        WifiCred cred;
        settings_wifi_get(saved_idx, &cred);
        nav_push(wifi_connect_view_create(ssid, cred.pass));
        return;
    }

    nav_push(input_view_create("ENTER PASSWORD", ssid, 63,
                               on_password_done, (void *)(intptr_t)idx));
}

static void scan_poll(lv_timer_t *t) {
    ScanCtx *c = (ScanCtx *)lv_timer_get_user_data(t);
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    c->scanning = false;
    if (n == WIFI_SCAN_FAILED || n < 0) {
        lv_label_set_text(c->status_lbl, "Scan failed");
    } else {
        populate_results(c, n);
    }
}

static void start_scan(ScanCtx *c) {
    if (c->scanning) return;
    c->scanning = true;
    menu_list_clear(c->list);
    lv_label_set_text(c->status_lbl, LV_SYMBOL_REFRESH " Scanning...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
}

static void scan_on_up(AppView *v)   { menu_list_up(((ScanCtx *)v->ctx)->list); }
static void scan_on_down(AppView *v) { menu_list_down(((ScanCtx *)v->ctx)->list); }
static void scan_on_ok(AppView *v)   { menu_list_activate(((ScanCtx *)v->ctx)->list); }

static void scan_on_destroy(AppView *v) {
    ScanCtx *c = (ScanCtx *)v->ctx;
    if (g_scan_ctx == c) g_scan_ctx = NULL;
    if (c->poll) lv_timer_delete(c->poll);
    WiFi.scanDelete();
    menu_list_destroy(c->list);
    free(c);
}

static AppView *wifi_scan_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("WI-FI SCAN", true);

    ScanCtx *c = (ScanCtx *)calloc(1, sizeof(ScanCtx));
    g_scan_ctx = c;

    c->status_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->status_lbl, 8, SCREEN_CONTENT_TOP + 2);

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 18, SCREEN_CONTENT_H - 18);

    v->ctx        = c;
    v->on_up      = scan_on_up;
    v->on_down    = scan_on_down;
    v->on_ok      = scan_on_ok;
    v->on_destroy = scan_on_destroy;

    c->poll = lv_timer_create(scan_poll, 300, c);
    start_scan(c);
    return v;
}

typedef struct {
    lv_obj_t   *status_lbl;
    lv_obj_t   *detail_lbl;
    lv_timer_t *poll;
} StatusCtx;

static void status_refresh(StatusCtx *c) {
    if (WiFi.isConnected()) {
        char buf[80];
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s", WiFi.SSID().c_str());
        lv_label_set_text(c->status_lbl, buf);
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_SUCCESS), 0);
        lv_label_set_text_fmt(c->detail_lbl, "IP: %s\nRSSI: %d dBm\nCh: %d",
                              WiFi.localIP().toString().c_str(),
                              WiFi.RSSI(), WiFi.channel());
    } else {
        lv_label_set_text(c->status_lbl, LV_SYMBOL_WARNING "  Not connected");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
        lv_label_set_text(c->detail_lbl, "Use Scan to connect.");
    }
}

static void status_poll(lv_timer_t *t) {
    status_refresh((StatusCtx *)lv_timer_get_user_data(t));
}

static void status_on_ok(AppView *v) {
    (void)v;
    if (WiFi.isConnected()) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
}

static void status_on_destroy(AppView *v) {
    StatusCtx *c = (StatusCtx *)v->ctx;
    if (c->poll) lv_timer_delete(c->poll);
    free(c);
}

static AppView *wifi_status_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("WI-FI STATUS", true);
    lv_obj_t *scr = v->root;

    StatusCtx *c = (StatusCtx *)calloc(1, sizeof(StatusCtx));

    c->status_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(c->status_lbl, FONT_MEDIUM, 0);
    lv_obj_align(c->status_lbl, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 8);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, LCD_WIDTH - 16, 66);
    lv_obj_set_pos(panel, 8, SCREEN_CONTENT_TOP + 32);
    lv_obj_set_style_bg_color(panel, theme_clr(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, theme_clr(CLR_SEPARATOR), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 5, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    c->detail_lbl = lv_label_create(panel);
    lv_obj_set_style_text_color(c->detail_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->detail_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_line_space(c->detail_lbl, 3, 0);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "OK: Disconnect");
    lv_obj_set_style_text_color(hint, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    status_refresh(c);
    c->poll = lv_timer_create(status_poll, 2000, c);

    v->ctx        = c;
    v->on_ok      = status_on_ok;
    v->on_destroy = status_on_destroy;
    return v;
}

static void open_scan(void *u)   { (void)u; nav_push(wifi_scan_view_create()); }
static void open_status(void *u) { (void)u; nav_push(wifi_status_view_create()); }

AppView *wifi_view_create(void) {
    static const MenuItem items[] = {
        { LV_SYMBOL_WIFI,    "Scan Networks", NULL, open_scan,   NULL },
        { LV_SYMBOL_LIST,    "Status",        NULL, open_status, NULL },
    };
    return list_view_create("WI-FI", items, 2);
}
