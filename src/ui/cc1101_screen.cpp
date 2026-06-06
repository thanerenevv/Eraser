#include "cc1101_screen.h"
#include "views.h"
#include "screen_base.h"
#include "theme.h"
#include "menu_list.h"
#include "nav.h"
#include "../config.h"
#include "../rf_hal.h"
#include <Arduino.h>
#include <SmartRC_CC1101.h>
#include <lvgl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ── Shared frequency/modulation presets ──────────────────────────────────────

static const struct { float mhz; const char *label; } k_freqs[] = {
    { 433.92f, "433.92" },
    { 315.00f, "315.00" },
    { 868.30f, "868.30" },
    { 915.00f, "915.00" },
};
static const struct { uint8_t val; const char *name; } k_mods[] = {
    { 0, "OOK"   },
    { 2, "2-FSK" },
    { 3, "GFSK"  },
};
#define FREQ_COUNT  ((int)(sizeof(k_freqs)/sizeof(k_freqs[0])))
#define MOD_COUNT   ((int)(sizeof(k_mods)/sizeof(k_mods[0])))

static void cc1101_hw_init(float freq_mhz, uint8_t mod) {
    SmartRC_cc1101.setSpiPin(CC1101_SCK_PIN, CC1101_MISO_PIN, CC1101_MOSI_PIN, CC1101_CS_PIN);
    SmartRC_cc1101.setGDO0(CC1101_GDO0_PIN);
    SmartRC_cc1101.Init();
    SmartRC_cc1101.setModulation(mod);
    SmartRC_cc1101.SetRx(freq_mhz);
}

// ── CC1101 Sniffer ────────────────────────────────────────────────────────────

#define SNIFFER_MAX_PKTS 16

typedef struct {
    lv_obj_t   *status_lbl;
    lv_obj_t   *freq_lbl;
    lv_obj_t   *count_lbl;
    MenuList   *list;
    lv_timer_t *timer;
    int         pkt_count;
    int         freq_idx;
    int         mod_idx;
    bool        running;
    char        lbl_buf[SNIFFER_MAX_PKTS][28];
    char        val_buf[SNIFFER_MAX_PKTS][10];
} SniffCtx;

static void sniff_update_freq_label(SniffCtx *c) {
    lv_label_set_text_fmt(c->freq_lbl, "%s MHz  %s",
                          k_freqs[c->freq_idx].label, k_mods[c->mod_idx].name);
}

static void sniff_hw_start(SniffCtx *c) {
    cc1101_hw_init(k_freqs[c->freq_idx].mhz, k_mods[c->mod_idx].val);
}

static void sniff_tick(lv_timer_t *t) {
    SniffCtx *c = (SniffCtx *)lv_timer_get_user_data(t);
    if (!c->running) return;

    uint8_t rxbytes = SmartRC_cc1101.SpiReadStatus(CC1101_RXBYTES) & 0x7F;
    if (rxbytes == 0) return;

    uint8_t buf[64] = {0};
    uint8_t len = SmartRC_cc1101.ReceiveData(buf);
    if (len == 0) return;

    int rssi = SmartRC_cc1101.getRssi();
    int idx  = c->pkt_count % SNIFFER_MAX_PKTS;

    char hex[20] = "";
    int  show    = len < 5 ? len : 5;
    for (int i = 0; i < show; i++) {
        char tmp[4];
        snprintf(tmp, sizeof(tmp), i ? " %02X" : "%02X", buf[i]);
        strncat(hex, tmp, sizeof(hex) - strlen(hex) - 1);
    }
    if (len > 5) strncat(hex, "..", sizeof(hex) - strlen(hex) - 1);

    snprintf(c->lbl_buf[idx], sizeof(c->lbl_buf[idx]), "%s %s",
             k_freqs[c->freq_idx].label, hex);
    snprintf(c->val_buf[idx], sizeof(c->val_buf[idx]), "%ddBm", rssi);

    c->pkt_count++;
    int total = c->pkt_count < SNIFFER_MAX_PKTS ? c->pkt_count : SNIFFER_MAX_PKTS;

    menu_list_clear(c->list);
    for (int i = 0; i < total; i++) {
        int j = (c->pkt_count - 1 - i + SNIFFER_MAX_PKTS) % SNIFFER_MAX_PKTS;
        MenuItem it = { LV_SYMBOL_AUDIO, c->lbl_buf[j], c->val_buf[j], NULL, NULL };
        menu_list_add(c->list, &it);
    }

    char countbuf[20];
    snprintf(countbuf, sizeof(countbuf), "Pkts: %d", c->pkt_count);
    lv_label_set_text(c->count_lbl, countbuf);
}

static void sniff_on_ok(AppView *v) {
    SniffCtx *c = (SniffCtx *)v->ctx;
    c->running = !c->running;
    if (c->running) sniff_hw_start(c);
    lv_label_set_text(c->status_lbl,
        c->running ? LV_SYMBOL_PAUSE " RX" : LV_SYMBOL_PLAY " PAUSED");
    lv_obj_set_style_text_color(c->status_lbl,
        theme_clr(c->running ? CLR_SUCCESS : CLR_WARNING), 0);
}

static void sniff_on_up(AppView *v)   { menu_list_up(((SniffCtx *)v->ctx)->list); }
static void sniff_on_down(AppView *v) { menu_list_down(((SniffCtx *)v->ctx)->list); }

static void sniff_on_extra(AppView *v) {
    SniffCtx *c = (SniffCtx *)v->ctx;
    c->freq_idx = (c->freq_idx + 1) % FREQ_COUNT;
    sniff_update_freq_label(c);
    if (c->running) sniff_hw_start(c);
}

static void sniff_on_destroy(AppView *v) {
    SniffCtx *c = (SniffCtx *)v->ctx;
    if (c->timer) lv_timer_delete(c->timer);
    menu_list_destroy(c->list);
    free(c);
}

AppView *cc1101_sniffer_view_create(void) {
    if (!rf_hal_present()) {
        return info_view_create("CC1101 SNIFFER",
            "No CC1101 hardware\ndetected.\n\nConnect a CC1101\nmodule to use\nthis feature.");
    }

    AppView *v  = app_view_new();
    v->root     = screen_base_create("CC1101 SNIFFER", true);
    SniffCtx *c = (SniffCtx *)calloc(1, sizeof(SniffCtx));
    c->freq_idx = 0;
    c->mod_idx  = 1;   // 2-FSK default
    c->running  = true;

    c->freq_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->freq_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(c->freq_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->freq_lbl, 6, SCREEN_CONTENT_TOP + 2);
    sniff_update_freq_label(c);

    c->status_lbl = lv_label_create(v->root);
    lv_label_set_text(c->status_lbl, LV_SYMBOL_PAUSE " RX");
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_SUCCESS), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    lv_obj_align(c->status_lbl, LV_ALIGN_TOP_RIGHT, -6, SCREEN_CONTENT_TOP + 2);

    c->count_lbl = lv_label_create(v->root);
    lv_label_set_text(c->count_lbl, "Pkts: 0");
    lv_obj_set_style_text_color(c->count_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->count_lbl, FONT_SMALL, 0);
    lv_obj_align(c->count_lbl, LV_ALIGN_TOP_RIGHT, -6, SCREEN_CONTENT_TOP + 14);

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 26, SCREEN_CONTENT_H - 26);
    MenuItem ph = { LV_SYMBOL_AUDIO, "Waiting for packets...", NULL, NULL, NULL };
    menu_list_add(c->list, &ph);

    sniff_hw_start(c);

    v->ctx        = c;
    v->on_up      = sniff_on_up;
    v->on_down    = sniff_on_down;
    v->on_ok      = sniff_on_ok;
    v->on_extra   = sniff_on_extra;
    v->on_destroy = sniff_on_destroy;
    c->timer = lv_timer_create(sniff_tick, 50, c);
    return v;
}

// ── CC1101 Signal Replay ──────────────────────────────────────────────────────

typedef enum {
    REPLAY_IDLE,
    REPLAY_RECORDING,
    REPLAY_CAPTURED,
    REPLAY_REPLAYING
} ReplayState;

typedef struct {
    lv_obj_t   *freq_lbl;
    lv_obj_t   *state_lbl;
    lv_obj_t   *info_lbl;
    lv_obj_t   *bytes_lbl;
    lv_timer_t *timer;
    ReplayState rstate;
    uint8_t     pkt[64];
    uint8_t     pkt_len;
    float       freq_mhz;
    int         rssi;
    int         tx_count;
    int         freq_idx;
} ReplayCtx;

static void replay_set_state(ReplayCtx *c, ReplayState s) {
    c->rstate = s;
    switch (s) {
        case REPLAY_IDLE:
            lv_label_set_text(c->state_lbl, "IDLE");
            lv_obj_set_style_text_color(c->state_lbl, theme_clr(CLR_TEXT_DIM), 0);
            lv_label_set_text(c->info_lbl,  "OK: record  EXTRA: cycle freq");
            if (c->pkt_len == 0) lv_label_set_text(c->bytes_lbl, "");
            break;
        case REPLAY_RECORDING:
            lv_label_set_text(c->state_lbl, LV_SYMBOL_EYE_OPEN " REC");
            lv_obj_set_style_text_color(c->state_lbl, theme_clr(CLR_ACCENT), 0);
            lv_label_set_text(c->info_lbl,  "Listening...  OK: cancel");
            lv_label_set_text(c->bytes_lbl, "");
            break;
        case REPLAY_CAPTURED:
            lv_label_set_text(c->state_lbl, LV_SYMBOL_OK " CAPTURED");
            lv_obj_set_style_text_color(c->state_lbl, theme_clr(CLR_SUCCESS), 0);
            lv_label_set_text(c->info_lbl,  "OK: replay  EXTRA: clear");
            break;
        case REPLAY_REPLAYING:
            lv_label_set_text(c->state_lbl, LV_SYMBOL_LOOP " TRANSMIT");
            lv_obj_set_style_text_color(c->state_lbl, theme_clr(CLR_WARNING), 0);
            lv_label_set_text(c->info_lbl,  "Transmitting...");
            break;
    }
}

static void replay_update_freq_lbl(ReplayCtx *c) {
    lv_label_set_text_fmt(c->freq_lbl, "%s MHz  2-FSK",
                          k_freqs[c->freq_idx].label);
}

static void replay_tick(lv_timer_t *t) {
    ReplayCtx *c = (ReplayCtx *)lv_timer_get_user_data(t);

    if (c->rstate == REPLAY_RECORDING) {
        uint8_t rxb = SmartRC_cc1101.SpiReadStatus(CC1101_RXBYTES) & 0x7F;
        if (rxb == 0) return;

        uint8_t len = SmartRC_cc1101.ReceiveData(c->pkt);
        if (len == 0) return;

        c->pkt_len  = len;
        c->freq_mhz = k_freqs[c->freq_idx].mhz;
        c->rssi     = SmartRC_cc1101.getRssi();

        char hex[52] = "";
        int  show = len < 10 ? len : 10;
        for (int i = 0; i < show; i++) {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), i ? " %02X" : "%02X", c->pkt[i]);
            strncat(hex, tmp, sizeof(hex) - strlen(hex) - 1);
        }
        if (len > 10) strncat(hex, "...", sizeof(hex) - strlen(hex) - 1);

        char detail[80];
        snprintf(detail, sizeof(detail), "%.2f MHz  %ddBm  %db\n%s",
                 c->freq_mhz, c->rssi, (int)len, hex);
        lv_label_set_text(c->bytes_lbl, detail);
        replay_set_state(c, REPLAY_CAPTURED);

    } else if (c->rstate == REPLAY_REPLAYING) {
        SmartRC_cc1101.setSidle();
        SmartRC_cc1101.setMHZ(c->freq_mhz);
        SmartRC_cc1101.SendData(c->pkt, c->pkt_len);
        c->tx_count++;

        char info[32];
        snprintf(info, sizeof(info), "Sent %d time(s)  OK: more", c->tx_count);
        lv_label_set_text(c->info_lbl, info);

        SmartRC_cc1101.SetRx(c->freq_mhz);
        replay_set_state(c, REPLAY_CAPTURED);
        lv_label_set_text(c->info_lbl, info);
    }
}

static void replay_on_ok(AppView *v) {
    ReplayCtx *c = (ReplayCtx *)v->ctx;
    switch (c->rstate) {
        case REPLAY_IDLE:
            cc1101_hw_init(k_freqs[c->freq_idx].mhz, 2);
            replay_set_state(c, REPLAY_RECORDING);
            break;
        case REPLAY_RECORDING:
            SmartRC_cc1101.setSidle();
            replay_set_state(c, REPLAY_IDLE);
            break;
        case REPLAY_CAPTURED:
            replay_set_state(c, REPLAY_REPLAYING);
            break;
        case REPLAY_REPLAYING:
            break;
    }
}

static void replay_on_extra(AppView *v) {
    ReplayCtx *c = (ReplayCtx *)v->ctx;
    if (c->rstate == REPLAY_REPLAYING) return;

    if (c->rstate == REPLAY_CAPTURED) {
        c->pkt_len  = 0;
        c->tx_count = 0;
        lv_label_set_text(c->bytes_lbl, "");
        replay_set_state(c, REPLAY_IDLE);
        return;
    }

    // IDLE or RECORDING: cycle frequency
    c->freq_idx = (c->freq_idx + 1) % FREQ_COUNT;
    replay_update_freq_lbl(c);
    if (c->rstate == REPLAY_RECORDING) {
        SmartRC_cc1101.SetRx(k_freqs[c->freq_idx].mhz);
    }
}

static void replay_on_destroy(AppView *v) {
    ReplayCtx *c = (ReplayCtx *)v->ctx;
    if (c->timer) lv_timer_delete(c->timer);
    free(c);
}

AppView *cc1101_replay_view_create(void) {
    if (!rf_hal_present()) {
        return info_view_create("CC1101 REPLAY",
            "No CC1101 hardware\ndetected.\n\nConnect a CC1101\nmodule to use\nthis feature.");
    }

    AppView *v   = app_view_new();
    v->root      = screen_base_create("CC1101 REPLAY", true);
    ReplayCtx *c = (ReplayCtx *)calloc(1, sizeof(ReplayCtx));
    c->freq_idx  = 0;
    c->rstate    = REPLAY_IDLE;

    int y = SCREEN_CONTENT_TOP + 4;

    c->freq_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->freq_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(c->freq_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->freq_lbl, 8, y);
    replay_update_freq_lbl(c);

    y += 18;
    c->state_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_font(c->state_lbl, FONT_TITLE, 0);
    lv_obj_set_pos(c->state_lbl, 8, y);

    y += 24;
    c->info_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->info_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->info_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->info_lbl, 8, y);

    y += 28;
    c->bytes_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->bytes_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->bytes_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->bytes_lbl, 8, y);

    replay_set_state(c, REPLAY_IDLE);

    v->ctx        = c;
    v->on_ok      = replay_on_ok;
    v->on_extra   = replay_on_extra;
    v->on_destroy = replay_on_destroy;
    c->timer = lv_timer_create(replay_tick, 50, c);
    return v;
}
