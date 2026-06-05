#include "eth_screen.h"
#include "screen_base.h"
#include "theme.h"
#include "nav.h"
#include "../config.h"
#include <lvgl.h>
#include <SPI.h>
#include <Ethernet.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// LCD uses SPIClass(HSPI); W5500 uses the global SPI object (FSPI/SPI2) — no conflict.
// SPI.begin() on ESP32 is a no-op if already begun, so custom pins are preserved
// even if the Ethernet library calls SPI.begin() internally.

static uint8_t g_eth_mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
static bool    g_eth_spi_ready = false;

typedef struct {
    lv_obj_t     *status_lbl;
    lv_obj_t     *info_lbl;
    lv_obj_t     *spinner;
    lv_timer_t   *poll;
    TaskHandle_t  task;
    volatile bool done;
    volatile bool dhcp_ok;
    volatile bool no_link;
} EthCtx;

// ─── DHCP task (runs on core 0 so LVGL loop on core 1 stays unblocked) ──────

static void eth_dhcp_task(void *pvParam) {
    EthCtx *c = (EthCtx *)pvParam;

    int result = Ethernet.begin(g_eth_mac, 15000, 4000);

    if (result) {
        c->dhcp_ok = true;
    } else {
        c->dhcp_ok  = false;
        c->no_link  = (Ethernet.linkStatus() == LinkOFF);
    }

    c->done = true;
    c->task = NULL;
    vTaskDelete(NULL);
}

// ─── Poll timer (reads volatile flags, touches LVGL only from Arduino loop) ─

static void eth_poll(lv_timer_t *t) {
    EthCtx *c = (EthCtx *)lv_timer_get_user_data(t);
    if (!c->done) return;

    lv_timer_delete(c->poll); c->poll = NULL;
    lv_obj_add_flag(c->spinner, LV_OBJ_FLAG_HIDDEN);

    if (c->dhcp_ok) {
        lv_label_set_text(c->status_lbl,
                          LV_SYMBOL_WIFI "  Connected  " LV_SYMBOL_OK);
        lv_obj_set_style_text_color(c->status_lbl,
                                    theme_clr(CLR_SUCCESS), 0);

        char info[200];
        snprintf(info, sizeof(info),
                 "IP:   %s\n"
                 "Mask: %s\n"
                 "GW:   %s\n"
                 "DNS:  %s\n"
                 "MAC: DE:AD:BE:EF:FE:ED",
                 Ethernet.localIP().toString().c_str(),
                 Ethernet.subnetMask().toString().c_str(),
                 Ethernet.gatewayIP().toString().c_str(),
                 Ethernet.dnsServerIP().toString().c_str());
        lv_label_set_text(c->info_lbl, info);

    } else if (c->no_link) {
        lv_label_set_text(c->status_lbl,
                          LV_SYMBOL_WARNING "  No Link Detected");
        lv_obj_set_style_text_color(c->status_lbl,
                                    theme_clr(CLR_WARNING), 0);

        char info[128];
        snprintf(info, sizeof(info),
                 "W5500 not found or no cable.\n"
                 "Check wiring:\n"
                 "  SCK  \xe2\x86\x92 GPIO %d\n"
                 "  MISO \xe2\x86\x92 GPIO %d\n"
                 "  MOSI \xe2\x86\x92 GPIO %d\n"
                 "  CS   \xe2\x86\x92 GPIO %d",
                 ETH_SCK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);
        lv_label_set_text(c->info_lbl, info);

    } else {
        lv_label_set_text(c->status_lbl,
                          LV_SYMBOL_WARNING "  DHCP Failed");
        lv_obj_set_style_text_color(c->status_lbl,
                                    theme_clr(CLR_WARNING), 0);
        lv_label_set_text(c->info_lbl,
                          "Link up but no DHCP reply.\n"
                          "Check if a DHCP server\nis on the network.\n\n"
                          "Press OK to retry.");
    }
}

// ─── Start / restart DHCP ───────────────────────────────────────────────────

static void eth_start(EthCtx *c) {
    if (!g_eth_spi_ready) {
        SPI.begin(ETH_SCK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);
        Ethernet.init(ETH_CS_PIN);
        g_eth_spi_ready = true;
    }

    c->done    = false;
    c->dhcp_ok = false;
    c->no_link = false;

    lv_obj_clear_flag(c->spinner, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(c->status_lbl, "Negotiating DHCP...");
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_label_set_text(c->info_lbl, "Please wait (~15 s max)");

    if (c->poll) { lv_timer_delete(c->poll); c->poll = NULL; }
    c->poll = lv_timer_create(eth_poll, 300, c);

    xTaskCreatePinnedToCore(eth_dhcp_task, "eth_dhcp", 8192, c,
                            4, &c->task, 0);
}

// ─── AppView callbacks ───────────────────────────────────────────────────────

static void eth_on_ok(AppView *v) {
    EthCtx *c = (EthCtx *)v->ctx;
    if (c->task) return;   // already running
    eth_start(c);
}

static bool eth_on_back(AppView *v) {
    EthCtx *c = (EthCtx *)v->ctx;
    return (c->task != NULL);  // block back while task is in flight
}

static void eth_on_destroy(AppView *v) {
    EthCtx *c = (EthCtx *)v->ctx;
    if (c->poll) { lv_timer_delete(c->poll); c->poll = NULL; }
    free(c);
}

// ─── View factory ────────────────────────────────────────────────────────────

AppView *eth_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("W5500 ETH", true);
    lv_obj_t *scr = v->root;

    EthCtx *c = (EthCtx *)calloc(1, sizeof(EthCtx));

    // Spinner (hidden until DHCP starts)
    c->spinner = lv_spinner_create(scr);
    lv_spinner_set_anim_params(c->spinner, 1000, 60);
    lv_obj_set_size(c->spinner, 24, 24);
    lv_obj_set_pos(c->spinner, 6, SCREEN_CONTENT_TOP + 6);
    lv_obj_set_style_arc_color(c->spinner, theme_clr(CLR_ACCENT),   LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(c->spinner, theme_clr(CLR_BG_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_arc_width(c->spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(c->spinner, 3, LV_PART_MAIN);
    lv_obj_add_flag(c->spinner, LV_OBJ_FLAG_HIDDEN);

    // Status label
    c->status_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(c->status_lbl, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_align(c->status_lbl, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 7);
    lv_label_set_text(c->status_lbl, "Press OK to connect");

    // Info panel
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, LCD_WIDTH - 16, 92);
    lv_obj_set_pos(panel, 8, SCREEN_CONTENT_TOP + 28);
    lv_obj_set_style_bg_color(panel, theme_clr(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, theme_clr(CLR_SEPARATOR), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 5, 0);
    lv_obj_set_style_pad_all(panel, 7, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    c->info_lbl = lv_label_create(panel);
    lv_obj_set_style_text_color(c->info_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->info_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_line_space(c->info_lbl, 3, 0);

    // Default info shows pin wiring guide
    char pins[128];
    snprintf(pins, sizeof(pins),
             "Connect W5500 to:\n"
             "  SCK  \xe2\x86\x92 GPIO %d\n"
             "  MISO \xe2\x86\x92 GPIO %d\n"
             "  MOSI \xe2\x86\x92 GPIO %d\n"
             "  CS   \xe2\x86\x92 GPIO %d",
             ETH_SCK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);
    lv_label_set_text(c->info_lbl, pins);

    // Bottom hint
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "OK: Connect   BACK: exit");
    lv_obj_set_style_text_color(hint, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    v->ctx        = c;
    v->on_ok      = eth_on_ok;
    v->on_back    = eth_on_back;
    v->on_destroy = eth_on_destroy;
    return v;
}
