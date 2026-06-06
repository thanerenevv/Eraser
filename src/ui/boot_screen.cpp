#include "boot_screen.h"
#include "screen_base.h"
#include "theme.h"
#include "menu.h"
#include "nav.h"
#include "../config.h"
#include "../rf_hal.h"
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <lvgl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BOOT_HOLD_MS  5000
#define ROW_H         15
#define ROW_GAP        3

// ── Hardware probes (blocking, called before first render) ────────────────────

static bool probe_nrf24(void) {
    // Send NOP (0xFF) to NRF24 and check STATUS register.
    // Valid STATUS is never 0x00 or 0xFF; reset value is 0x0E.
    SPIClass spi(FSPI);
    spi.begin(NRF24_SCK_PIN, NRF24_MISO_PIN, NRF24_MOSI_PIN, -1);
    spi.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    pinMode(NRF24_CSN_PIN, OUTPUT);
    digitalWrite(NRF24_CSN_PIN, LOW);
    uint8_t status = spi.transfer(0xFF);
    digitalWrite(NRF24_CSN_PIN, HIGH);
    spi.endTransaction();
    spi.end();
    return (status != 0x00 && status != 0xFF);
}

static bool probe_w5500(void) {
    // Ethernet.hardwareStatus() reads the W5500 chip-version register (expects 0x04).
    SPI.begin(ETH_SCK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, -1);
    Ethernet.init(ETH_CS_PIN);
    bool ok = (Ethernet.hardwareStatus() != EthernetNoHardware);
    // Leave SPI.begin() active; eth_screen will reuse the global SPI object.
    return ok;
}

// ── Boot timer ────────────────────────────────────────────────────────────────

typedef struct {
    lv_timer_t *timer;
    uint32_t    start_ms;
    lv_obj_t   *countdown_lbl;
} BootCtx;

static void boot_timer_cb(lv_timer_t *t) {
    BootCtx *ctx = (BootCtx *)lv_timer_get_user_data(t);
    uint32_t elapsed = millis() - ctx->start_ms;

    if (elapsed < BOOT_HOLD_MS) {
        uint32_t secs = (BOOT_HOLD_MS - elapsed + 999) / 1000;
        char buf[28];
        snprintf(buf, sizeof(buf), "Starting in %lus...", (unsigned long)secs);
        lv_label_set_text(ctx->countdown_lbl, buf);
    } else {
        lv_timer_t *tmr = ctx->timer;
        ctx->timer = NULL;
        lv_timer_delete(tmr);
        nav_set_root(home_view_create());
    }
}

static bool boot_on_back(AppView *v) { (void)v; return true; }

static void boot_on_destroy(AppView *v) {
    BootCtx *ctx = (BootCtx *)v->ctx;
    if (ctx->timer) lv_timer_delete(ctx->timer);
    free(ctx);
}

// ── Row builder ───────────────────────────────────────────────────────────────

static void add_row(lv_obj_t *parent, int y,
                    const char *name, const char *status_text, bool ok) {
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_pos(icon, 6, y + 1);
    lv_obj_set_style_text_font(icon, FONT_SMALL, 0);
    lv_obj_set_style_text_color(icon,
        ok ? theme_clr(CLR_SUCCESS) : theme_clr(CLR_WARNING), 0);

    lv_obj_t *name_lbl = lv_label_create(parent);
    lv_label_set_text(name_lbl, name);
    lv_obj_set_pos(name_lbl, 26, y);
    lv_obj_set_style_text_font(name_lbl, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(name_lbl, theme_clr(CLR_TEXT), 0);

    lv_obj_t *val_lbl = lv_label_create(parent);
    lv_label_set_text(val_lbl, status_text);
    lv_obj_set_style_text_font(val_lbl, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(val_lbl,
        ok ? theme_clr(CLR_SUCCESS) : theme_clr(CLR_WARNING), 0);
    // Right-align the value label
    lv_obj_set_width(val_lbl, 110);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(val_lbl, LCD_WIDTH - 116, y);
}

// ── Entry point ───────────────────────────────────────────────────────────────

AppView *boot_screen_create(void) {
    bool nrf24_ok = probe_nrf24();
    bool eth_ok   = probe_w5500();
    bool cc1101   = rf_hal_present();

    AppView *v    = app_view_new();
    v->root       = screen_base_create("Eraser", false);
    v->on_back    = boot_on_back;
    v->on_destroy = boot_on_destroy;

    BootCtx *ctx  = (BootCtx *)calloc(1, sizeof(BootCtx));
    v->ctx        = ctx;
    ctx->start_ms = millis();

    lv_obj_t *root = v->root;

    // Title
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "BOOTING UP...");
    lv_obj_set_width(title, LCD_WIDTH);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_pos(title, 0, SCREEN_CONTENT_TOP + 3);

    // Separator below title
    lv_obj_t *sep = lv_obj_create(root);
    lv_obj_set_pos(sep, 0, SCREEN_CONTENT_TOP + 22);
    lv_obj_set_size(sep, LCD_WIDTH, 1);
    lv_obj_set_style_bg_color(sep, theme_clr(CLR_SEPARATOR), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // Module rows
    int y = SCREEN_CONTENT_TOP + 27;
    add_row(root, y, "Display",    "OK",                          true);
    y += ROW_H + ROW_GAP;
    add_row(root, y, "Wi-Fi",      "Built-in",                    true);
    y += ROW_H + ROW_GAP;
    add_row(root, y, "Bluetooth",  "Built-in",                    true);
    y += ROW_H + ROW_GAP;
    add_row(root, y, "CC1101",     cc1101 ? "OK" : "Simulation",  cc1101);
    y += ROW_H + ROW_GAP;
    add_row(root, y, "NRF24L01+",  nrf24_ok ? "Connected" : "Not found", nrf24_ok);
    y += ROW_H + ROW_GAP;
    add_row(root, y, "Ethernet",   eth_ok   ? "Connected" : "Not found", eth_ok);

    // Countdown label at bottom
    ctx->countdown_lbl = lv_label_create(root);
    lv_label_set_text(ctx->countdown_lbl, "Starting in 5s...");
    lv_obj_set_width(ctx->countdown_lbl, LCD_WIDTH);
    lv_obj_set_style_text_align(ctx->countdown_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ctx->countdown_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(ctx->countdown_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_pos(ctx->countdown_lbl, 0, LCD_HEIGHT - 16);

    // Start 250ms countdown tick
    ctx->timer = lv_timer_create(boot_timer_cb, 250, ctx);

    return v;
}
