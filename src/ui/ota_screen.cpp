#include "ota_screen.h"
#include "screen_base.h"
#include "theme.h"
#include "views.h"
#include "menu_list.h"
#include "input_view.h"
#include "nav.h"
#include "../config.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ─── Shared UI helper ────────────────────────────────────────────────────────

static lv_obj_t *make_ota_bar(lv_obj_t *scr) {
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, LCD_WIDTH - 32, 10);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 4);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, theme_clr(CLR_BG_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, theme_clr(CLR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    return bar;
}

// ─── Method 1: AP Web Upload ─────────────────────────────────────────────────

static const char *k_ap_ssid = "Eraser-OTA";
static const char *k_ap_pass = "eraserota";

static const char k_upload_html[] =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{background:#111;color:#eee;font-family:sans-serif;text-align:center;padding:24px}"
    "h2{color:#ff6600;margin-bottom:8px}"
    "p{color:#999;font-size:14px}"
    "input[type=file]{display:block;margin:16px auto;color:#eee}"
    ".btn{background:#ff6600;color:#fff;border:none;padding:12px 32px;"
    "font-size:16px;border-radius:6px;cursor:pointer;margin-top:12px}"
    ".btn:hover{background:#cc5200}"
    "</style></head><body>"
    "<h2>Eraser OTA</h2>"
    "<p>Select firmware .bin to flash</p>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='fw' accept='.bin'>"
    "<br><button class='btn' type='submit'>Flash Firmware</button>"
    "</form></body></html>";

typedef struct {
    lv_obj_t       *status_lbl;
    lv_obj_t       *bar;
    lv_obj_t       *detail_lbl;
    lv_timer_t     *poll;
    WebServer      *server;
    volatile size_t bytes_written;
    volatile bool   uploading;
    volatile bool   done;
    volatile bool   err;
} ApOtaCtx;

static ApOtaCtx *g_ap_ctx = NULL;

static bool ap_ota_on_back(AppView *v) {
    ApOtaCtx *c = (ApOtaCtx *)v->ctx;
    if (c->done) {
        ESP.restart();
        return true;
    }
    return false;
}

static void ap_ota_on_destroy(AppView *v) {
    ApOtaCtx *c = (ApOtaCtx *)v->ctx;
    if (g_ap_ctx == c) g_ap_ctx = NULL;
    if (c->poll) { lv_timer_delete(c->poll); c->poll = NULL; }
    if (c->server) { c->server->stop(); delete c->server; c->server = NULL; }
    WiFi.softAPdisconnect(true);
    free(c);
}

static void ap_ota_poll(lv_timer_t *t) {
    ApOtaCtx *c = (ApOtaCtx *)lv_timer_get_user_data(t);
    c->server->handleClient();

    if (c->done) {
        lv_timer_delete(c->poll); c->poll = NULL;
        lv_label_set_text(c->status_lbl, LV_SYMBOL_OK "  Flash done!");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_SUCCESS), 0);
        lv_bar_set_value(c->bar, 100, LV_ANIM_OFF);
        lv_label_set_text(c->detail_lbl, "Press BACK to reboot");
        return;
    }
    if (c->err) {
        lv_timer_delete(c->poll); c->poll = NULL;
        lv_label_set_text(c->status_lbl, LV_SYMBOL_WARNING "  Flash failed");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_WARNING), 0);
        lv_label_set_text(c->detail_lbl, "Check file and retry");
        return;
    }
    if (c->uploading && c->bytes_written > 0) {
        lv_label_set_text_fmt(c->detail_lbl, "Flashing: %u KB...",
                              (unsigned)(c->bytes_written / 1024));
    }
}

static AppView *ota_ap_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("AP UPLOAD", true);
    lv_obj_t *scr = v->root;

    ApOtaCtx *c = (ApOtaCtx *)calloc(1, sizeof(ApOtaCtx));
    g_ap_ctx = c;

    c->status_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(c->status_lbl, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_align(c->status_lbl, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 8);
    lv_label_set_text(c->status_lbl, "Waiting for upload...");

    c->bar = make_ota_bar(scr);

    c->detail_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(c->detail_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->detail_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_align(c->detail_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(c->detail_lbl, LCD_WIDTH - 16);
    lv_obj_align(c->detail_lbl, LV_ALIGN_CENTER, 0, 28);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(k_ap_ssid, k_ap_pass);
    IPAddress ip = WiFi.softAPIP();

    char detail[96];
    snprintf(detail, sizeof(detail), "SSID: %s\nPass: %s\nVisit: %s",
             k_ap_ssid, k_ap_pass, ip.toString().c_str());
    lv_label_set_text(c->detail_lbl, detail);

    c->server = new WebServer(80);

    c->server->on("/", HTTP_GET, [c]() {
        c->server->send(200, "text/html", k_upload_html);
    });

    c->server->on("/update", HTTP_POST,
        [c]() {
            bool ok = !c->err && Update.isFinished();
            c->server->send(200, "text/plain",
                            ok ? "OK! Device rebooting..." : "FAIL. Try again.");
            if (ok) c->done = true;
        },
        [c]() {
            HTTPUpload &up = c->server->upload();
            if (up.status == UPLOAD_FILE_START) {
                c->bytes_written = 0;
                c->uploading = true;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) c->err = true;
            } else if (up.status == UPLOAD_FILE_WRITE && !c->err) {
                if (Update.write(up.buf, up.currentSize) != up.currentSize)
                    c->err = true;
                else
                    c->bytes_written += up.currentSize;
            } else if (up.status == UPLOAD_FILE_END && !c->err) {
                if (!Update.end(true)) c->err = true;
                c->uploading = false;
            }
        }
    );

    c->server->begin();
    c->poll = lv_timer_create(ap_ota_poll, 50, c);

    v->ctx        = c;
    v->on_back    = ap_ota_on_back;
    v->on_destroy = ap_ota_on_destroy;
    return v;
}

// ─── Method 2: ArduinoOTA (LAN) ─────────────────────────────────────────────

typedef struct {
    lv_obj_t   *status_lbl;
    lv_obj_t   *bar;
    lv_obj_t   *detail_lbl;
    lv_timer_t *poll;
    bool        started;
    bool        done;
    int         pct;
} ArdOtaCtx;

static ArdOtaCtx *g_ard_ctx = NULL;

static void ard_poll(lv_timer_t *t) {
    ArdOtaCtx *c = (ArdOtaCtx *)lv_timer_get_user_data(t);
    if (!c->started) return;
    ArduinoOTA.handle();

    if (c->done) {
        lv_timer_delete(c->poll); c->poll = NULL;
        lv_label_set_text(c->status_lbl, LV_SYMBOL_OK "  Done! Rebooting...");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_SUCCESS), 0);
        lv_bar_set_value(c->bar, 100, LV_ANIM_OFF);
        lv_label_set_text(c->detail_lbl, "");
        return;
    }

    if (c->pct > 0 && c->pct < 100) {
        lv_bar_set_value(c->bar, c->pct, LV_ANIM_OFF);
        lv_label_set_text_fmt(c->detail_lbl, "Flashing... %d%%", c->pct);
    }
}

static void ard_ota_on_destroy(AppView *v) {
    ArdOtaCtx *c = (ArdOtaCtx *)v->ctx;
    if (g_ard_ctx == c) g_ard_ctx = NULL;
    if (c->poll) { lv_timer_delete(c->poll); c->poll = NULL; }
    free(c);
}

static AppView *ota_arduinoota_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("ARDUINO OTA", true);
    lv_obj_t *scr = v->root;

    ArdOtaCtx *c = (ArdOtaCtx *)calloc(1, sizeof(ArdOtaCtx));
    g_ard_ctx = c;

    c->status_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(c->status_lbl, FONT_NORMAL, 0);
    lv_obj_align(c->status_lbl, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 8);

    c->bar = make_ota_bar(scr);

    c->detail_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(c->detail_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->detail_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_align(c->detail_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(c->detail_lbl, LCD_WIDTH - 16);
    lv_obj_align(c->detail_lbl, LV_ALIGN_CENTER, 0, 28);

    if (!WiFi.isConnected()) {
        lv_label_set_text(c->status_lbl, LV_SYMBOL_WARNING "  No WiFi");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_WARNING), 0);
        lv_label_set_text(c->detail_lbl, "Connect to WiFi first\nfrom the Wi-Fi menu");
    } else {
        c->started = true;

        ArduinoOTA.setHostname("eraser");
        ArduinoOTA.onStart([]() {
            if (g_ard_ctx) g_ard_ctx->pct = 1;
        });
        ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
            if (g_ard_ctx && total > 0)
                g_ard_ctx->pct = (int)(done * 100 / total);
        });
        ArduinoOTA.onEnd([]() {
            if (g_ard_ctx) g_ard_ctx->done = true;
        });
        ArduinoOTA.begin();

        lv_label_set_text(c->status_lbl, "Waiting for upload...");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_ACCENT), 0);

        char det[96];
        snprintf(det, sizeof(det), "Hostname: eraser.local\nIP: %s  Port: 3232",
                 WiFi.localIP().toString().c_str());
        lv_label_set_text(c->detail_lbl, det);

        c->poll = lv_timer_create(ard_poll, 100, c);
    }

    v->ctx        = c;
    v->on_destroy = ard_ota_on_destroy;
    return v;
}

// ─── Method 3: HTTP Pull ─────────────────────────────────────────────────────

typedef struct {
    lv_obj_t        *status_lbl;
    lv_obj_t        *bar;
    lv_obj_t        *detail_lbl;
    lv_timer_t      *poll;
    TaskHandle_t     task;
    volatile int     pct;
    volatile bool    done;
    volatile bool    err;
    char             err_msg[64];
    char             url[128];
} HttpOtaCtx;

static void http_ota_task(void *pvParam) {
    HttpOtaCtx *c = (HttpOtaCtx *)pvParam;
    WiFiClient client;
    HTTPClient http;

    if (!http.begin(client, c->url)) {
        strlcpy(c->err_msg, "Bad URL", sizeof(c->err_msg));
        c->err  = true;
        c->task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int code = http.GET();
    if (code != 200) {
        snprintf(c->err_msg, sizeof(c->err_msg), "HTTP error %d", code);
        c->err = true;
        http.end();
        c->task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int total = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) {
        strlcpy(c->err_msg, "No OTA partition", sizeof(c->err_msg));
        c->err = true;
        http.end();
        c->task = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[1024];
    int written = 0;

    while (http.connected() && (total <= 0 || written < total)) {
        int avail = stream->available();
        if (!avail) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        int n = (avail < (int)sizeof(buf)) ? avail : (int)sizeof(buf);
        int r = stream->readBytes(buf, n);
        if (r <= 0) break;
        if (Update.write(buf, r) != (size_t)r) {
            strlcpy(c->err_msg, "Write failed", sizeof(c->err_msg));
            c->err = true;
            break;
        }
        written += r;
        if (total > 0) c->pct = written * 100 / total;
    }

    if (!c->err) {
        if (Update.end(true)) {
            c->pct  = 100;
            c->done = true;
        } else {
            strlcpy(c->err_msg, "Finalize failed", sizeof(c->err_msg));
            c->err = true;
        }
    }

    http.end();
    c->task = NULL;
    vTaskDelete(NULL);
}

// Suppress BACK while download is in progress; reboot on completion.
static bool http_ota_on_back(AppView *v) {
    HttpOtaCtx *c = (HttpOtaCtx *)v->ctx;
    if (c->done) { ESP.restart(); return true; }
    if (c->task)  return true;   // block navigation during download
    return false;
}

static void http_ota_on_destroy(AppView *v) {
    HttpOtaCtx *c = (HttpOtaCtx *)v->ctx;
    if (c->poll) { lv_timer_delete(c->poll); c->poll = NULL; }
    // task is guaranteed NULL here (on_back blocks pop while task runs)
    free(c);
}

static void http_ota_poll(lv_timer_t *t) {
    HttpOtaCtx *c = (HttpOtaCtx *)lv_timer_get_user_data(t);

    if (c->done) {
        lv_timer_delete(c->poll); c->poll = NULL;
        lv_label_set_text(c->status_lbl, LV_SYMBOL_OK "  Done!");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_SUCCESS), 0);
        lv_bar_set_value(c->bar, 100, LV_ANIM_OFF);
        lv_label_set_text(c->detail_lbl, "Press BACK to reboot");
        return;
    }
    if (c->err) {
        lv_timer_delete(c->poll); c->poll = NULL;
        lv_label_set_text(c->status_lbl, LV_SYMBOL_WARNING "  Failed");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_WARNING), 0);
        lv_label_set_text(c->detail_lbl, c->err_msg);
        return;
    }
    int pct = c->pct;
    lv_bar_set_value(c->bar, pct, LV_ANIM_OFF);
    if (pct > 0)
        lv_label_set_text_fmt(c->detail_lbl, "Downloading... %d%%", pct);
}

static AppView *ota_http_dl_view_create(const char *url) {
    AppView *v = app_view_new();
    v->root = screen_base_create("HTTP PULL", true);
    lv_obj_t *scr = v->root;

    HttpOtaCtx *c = (HttpOtaCtx *)calloc(1, sizeof(HttpOtaCtx));
    strlcpy(c->url, url, sizeof(c->url));

    c->status_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(c->status_lbl, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_align(c->status_lbl, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 8);

    c->bar = make_ota_bar(scr);

    c->detail_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(c->detail_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->detail_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_align(c->detail_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(c->detail_lbl, LCD_WIDTH - 16);
    lv_obj_align(c->detail_lbl, LV_ALIGN_CENTER, 0, 28);

    if (!WiFi.isConnected()) {
        lv_label_set_text(c->status_lbl, LV_SYMBOL_WARNING "  No WiFi");
        lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_WARNING), 0);
        lv_label_set_text(c->detail_lbl, "Connect to WiFi first");
    } else {
        char disp[80];
        snprintf(disp, sizeof(disp), "%.79s", url);
        lv_label_set_text(c->detail_lbl, disp);
        lv_label_set_text(c->status_lbl, "Connecting...");

        c->poll = lv_timer_create(http_ota_poll, 250, c);
        xTaskCreatePinnedToCore(http_ota_task, "http_ota", 16384, c,
                                5, &c->task, 0);
    }

    v->ctx        = c;
    v->on_back    = http_ota_on_back;
    v->on_destroy = http_ota_on_destroy;
    return v;
}

static void on_http_url_done(const char *url, void *user) {
    (void)user;
    if (!url || !url[0]) return;
    nav_push(ota_http_dl_view_create(url));
}

static void open_http_ota(void *u) {
    (void)u;
    nav_push(input_view_create("HTTP OTA URL", "http://192.168.1.x/fw.bin",
                                127, on_http_url_done, NULL));
}

// ─── Top-level OTA menu ──────────────────────────────────────────────────────

static void open_ap_ota(void *u)  { (void)u; nav_push(ota_ap_view_create()); }
static void open_ard_ota(void *u) { (void)u; nav_push(ota_arduinoota_view_create()); }

AppView *ota_view_create(void) {
    static const MenuItem items[] = {
        { LV_SYMBOL_UPLOAD,   "AP Web Flash", "Browser", open_ap_ota,   NULL },
        { LV_SYMBOL_WIFI,     "ArduinoOTA",   "LAN",     open_ard_ota,  NULL },
        { LV_SYMBOL_DOWNLOAD, "HTTP Pull",    "URL",     open_http_ota, NULL },
    };
    return list_view_create("OTA UPDATE", items, 3);
}
