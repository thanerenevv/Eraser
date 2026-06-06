#include "rf_screen.h"
#include "views.h"
#include "screen_base.h"
#include "theme.h"
#include "mousejack_screen.h"
#include "../config.h"
#include "../rf_hal.h"
#include <Arduino.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BINS        RF_SPECTRUM_BINS
#define SIDE_MARGIN 6
#define HEADER_Y    (SCREEN_CONTENT_TOP + 1)
#define BASELINE    (LCD_HEIGHT - 20)
#define SPECT_H     (BASELINE - (SCREEN_CONTENT_TOP + 18))
#define RSSI_MIN    (-100)
#define RSSI_MAX    (-30)
#define PEAK_DECAY  2
#define EMA_ATTACK  0.40f
#define EMA_RELEASE 0.14f

typedef struct {
    RfBand      band;
    bool        running;
    bool        band_locked;
    int         slot;
    int         barw;
    lv_obj_t   *bar[BINS];
    lv_obj_t   *peak[BINS];
    int         peak_h[BINS];
    float       h_smooth[BINS];
    lv_obj_t   *range_lbl;
    lv_obj_t   *state_lbl;
    lv_obj_t   *band_lbl;
    lv_obj_t   *peak_lbl;
    lv_timer_t *timer;
} SpecCtx;

static void anim_opa(void *obj, int32_t v) {
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void state_set_pulse(SpecCtx *c, bool on) {
    lv_anim_delete(c->state_lbl, anim_opa);
    if (!on) { lv_obj_set_style_text_opa(c->state_lbl, LV_OPA_COVER, 0); return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c->state_lbl);
    lv_anim_set_exec_cb(&a, anim_opa);
    lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_duration(&a, 650);
    lv_anim_set_playback_duration(&a, 650);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static int rssi_to_h(int rssi) {
    if (rssi < RSSI_MIN) rssi = RSSI_MIN;
    if (rssi > RSSI_MAX) rssi = RSSI_MAX;
    return (rssi - RSSI_MIN) * SPECT_H / (RSSI_MAX - RSSI_MIN);
}

static void spec_apply_band_labels(SpecCtx *c) {
    if (c->band == RF_BAND_24) {
        lv_label_set_text(c->range_lbl, "2400-2484 MHz (CH 0-83)");
        lv_label_set_text_fmt(c->band_lbl, "NRF24L01+  2.4 GHz");
    } else {
        lv_label_set_text_fmt(c->range_lbl, "%.2f-%.2f MHz",
                              rf_band_lo_mhz(c->band), rf_band_hi_mhz(c->band));
        lv_label_set_text_fmt(c->band_lbl, "%s  %s",
                              rf_band_name(c->band), rf_hal_chip(c->band));
    }
    for (int i = 0; i < BINS; i++) { c->peak_h[i] = 0; c->h_smooth[i] = 0.0f; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c->band_lbl);
    lv_anim_set_exec_cb(&a, anim_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void spec_tick(lv_timer_t *t) {
    SpecCtx *c = (SpecCtx *)lv_timer_get_user_data(t);
    if (!c->running) return;
    rf_hal_tick();

    float lo = rf_band_lo_mhz(c->band), hi = rf_band_hi_mhz(c->band);
    int   best_rssi = -127;
    float best_freq = lo;

    for (int i = 0; i < BINS; i++) {
        float f    = lo + (i + 0.5f) / BINS * (hi - lo);
        int   rssi = rf_hal_rssi(c->band, f);
        int   raw  = rssi_to_h(rssi);

        float sm = c->h_smooth[i];
        sm += (raw - sm) * (raw > sm ? EMA_ATTACK : EMA_RELEASE);
        c->h_smooth[i] = sm;
        int h = (int)(sm + 0.5f);

        lv_obj_set_y(c->bar[i], BASELINE - h);
        lv_obj_set_height(c->bar[i], h < 1 ? 1 : h);
        uint32_t col = h > SPECT_H * 6 / 10 ? CLR_ACCENT
                     : h > SPECT_H * 3 / 10 ? CLR_ACCENT_DIM : CLR_GRID;
        lv_obj_set_style_bg_color(c->bar[i], theme_clr(col), 0);

        if (h > c->peak_h[i]) c->peak_h[i] = h;
        else                  c->peak_h[i] -= PEAK_DECAY;
        if (c->peak_h[i] < 0) c->peak_h[i] = 0;
        lv_obj_set_y(c->peak[i], BASELINE - c->peak_h[i] - 1);

        if (rssi > best_rssi) { best_rssi = rssi; best_freq = f; }
    }

    if (c->band == RF_BAND_24) {
        int ch = (int)(best_freq - 2400.0f + 0.5f);
        lv_label_set_text_fmt(c->peak_lbl, "CH %d  %d dBm", ch, best_rssi);
    } else {
        lv_label_set_text_fmt(c->peak_lbl, "%.2f MHz  %d dBm", best_freq, best_rssi);
    }
}

static void spec_on_ok(AppView *v) {
    SpecCtx *c = (SpecCtx *)v->ctx;
    c->running = !c->running;
    lv_label_set_text(c->state_lbl, c->running ? LV_SYMBOL_PAUSE " RUN"
                                               : LV_SYMBOL_PLAY " HOLD");
    lv_obj_set_style_text_color(c->state_lbl,
        theme_clr(c->running ? CLR_SUCCESS : CLR_WARNING), 0);
    state_set_pulse(c, c->running);
}

static void spec_on_up(AppView *v) {
    SpecCtx *c = (SpecCtx *)v->ctx;
    if (c->band_locked) return;
    c->band = (RfBand)((c->band + 1) % RF_BAND_COUNT);
    spec_apply_band_labels(c);
}

static void spec_on_down(AppView *v) {
    SpecCtx *c = (SpecCtx *)v->ctx;
    if (c->band_locked) return;
    c->band = (RfBand)((c->band + RF_BAND_COUNT - 1) % RF_BAND_COUNT);
    spec_apply_band_labels(c);
}

static void spec_on_destroy(AppView *v) {
    SpecCtx *c = (SpecCtx *)v->ctx;
    if (c->timer) lv_timer_delete(c->timer);
    lv_anim_delete(c->state_lbl, anim_opa);
    lv_anim_delete(c->band_lbl,  anim_opa);
    free(c);
}

static void add_grid_line(lv_obj_t *parent, int area_w, int dbm) {
    int y = BASELINE - rssi_to_h(dbm);
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, area_w, 1);
    lv_obj_set_pos(line, SIDE_MARGIN, y);
    lv_obj_set_style_bg_color(line, theme_clr(CLR_GRID), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text_fmt(lbl, "%d", dbm);
    lv_obj_set_style_text_color(lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(lbl, FONT_SMALL, 0);
    lv_obj_set_pos(lbl, SIDE_MARGIN + 1, y - 13);
}

static AppView *spectrum_view_create_ex(const char *title, RfBand start_band, bool locked) {
    AppView *v = app_view_new();
    v->root = screen_base_create(title, true);
    SpecCtx *c = (SpecCtx *)calloc(1, sizeof(SpecCtx));
    c->running     = true;
    c->band        = start_band;
    c->band_locked = locked;

    int area_w = LCD_WIDTH - 2 * SIDE_MARGIN;
    c->slot = area_w / BINS;
    c->barw = c->slot > 3 ? c->slot - 2 : 2;

    lv_obj_t *base = lv_obj_create(v->root);
    lv_obj_set_size(base, area_w, 1);
    lv_obj_set_pos(base, SIDE_MARGIN, BASELINE);
    lv_obj_set_style_bg_color(base, theme_clr(CLR_GRID), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(base, 0, 0);
    lv_obj_set_style_radius(base, 0, 0);

    add_grid_line(v->root, area_w, -40);
    add_grid_line(v->root, area_w, -60);
    add_grid_line(v->root, area_w, -80);

    for (int i = 0; i < BINS; i++) {
        int x = SIDE_MARGIN + i * c->slot;
        c->bar[i] = lv_obj_create(v->root);
        lv_obj_set_size(c->bar[i], c->barw, 1);
        lv_obj_set_pos(c->bar[i], x, BASELINE - 1);
        lv_obj_set_style_bg_color(c->bar[i], theme_clr(CLR_ACCENT_DIM), 0);
        lv_obj_set_style_bg_opa(c->bar[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c->bar[i], 0, 0);
        lv_obj_set_style_radius(c->bar[i], 1, 0);
        lv_obj_clear_flag(c->bar[i], LV_OBJ_FLAG_SCROLLABLE);

        c->peak[i] = lv_obj_create(v->root);
        lv_obj_set_size(c->peak[i], c->barw, 2);
        lv_obj_set_pos(c->peak[i], x, BASELINE - 1);
        lv_obj_set_style_bg_color(c->peak[i], theme_clr(CLR_TEXT), 0);
        lv_obj_set_style_bg_opa(c->peak[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c->peak[i], 0, 0);
        lv_obj_set_style_radius(c->peak[i], 0, 0);
        lv_obj_clear_flag(c->peak[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    c->range_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->range_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->range_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->range_lbl, SIDE_MARGIN, HEADER_Y);

    c->state_lbl = lv_label_create(v->root);
    lv_label_set_text(c->state_lbl, LV_SYMBOL_PAUSE " RUN");
    lv_obj_set_style_text_color(c->state_lbl, theme_clr(CLR_SUCCESS), 0);
    lv_obj_set_style_text_font(c->state_lbl, FONT_SMALL, 0);
    lv_obj_align(c->state_lbl, LV_ALIGN_TOP_RIGHT, -SIDE_MARGIN, HEADER_Y);

    c->band_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->band_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(c->band_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->band_lbl, SIDE_MARGIN, BASELINE + 4);

    c->peak_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->peak_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->peak_lbl, FONT_SMALL, 0);
    lv_obj_align(c->peak_lbl, LV_ALIGN_TOP_RIGHT, -SIDE_MARGIN, BASELINE + 4);

    spec_apply_band_labels(c);

    v->ctx        = c;
    v->on_up      = spec_on_up;
    v->on_down    = spec_on_down;
    v->on_ok      = spec_on_ok;
    v->on_destroy = spec_on_destroy;
    state_set_pulse(c, true);
    c->timer = lv_timer_create(spec_tick, 50, c);
    return v;
}

static const char *k_cc1101_protos[] = { "OOK", "ASK", "FSK", "2-FSK", "GFSK", "MSK" };
static const char *k_cc1101_freqs[]  = {
    "315.00", "433.92", "434.15", "868.00", "868.30", "869.85", "915.00", "916.50"
};

static void cc1101_pkt_gen(int seq, char *label, int lsz,
                            char *value, int vsz, char *detail, int dsz) {
    (void)seq;
    const char *proto = k_cc1101_protos[random(0, 6)];
    const char *freq  = k_cc1101_freqs[random(0, 8)];
    int rssi = -(int)random(40, 90);
    uint8_t d[8];
    for (int i = 0; i < 8; i++) d[i] = (uint8_t)random(0, 256);

    snprintf(label, lsz, "%s  %s MHz", proto, freq);
    snprintf(value, vsz, "%ddBm", rssi);
    snprintf(detail, dsz,
             "Protocol: %s\nFreq: %s MHz\nRSSI: %d dBm\n"
             "Data: %02X %02X %02X %02X %02X %02X %02X %02X\n"
             "HW: %s",
             proto, freq, rssi,
             d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
             rf_hal_present() ? "CC1101 detected" : "Simulation");
}

#define FREQSCAN_BINS 40

typedef struct {
    MenuList   *list;
    lv_obj_t   *status_lbl;
    lv_timer_t *scan_timer;
    bool        scanning;
    int         pass;
} FreqScanCtx;

typedef struct { float freq; int rssi; } FreqEntry;

static int freq_cmp(const void *a, const void *b) {
    return ((FreqEntry *)b)->rssi - ((FreqEntry *)a)->rssi;
}

static FreqScanCtx *g_fscan = NULL;

static void fscan_rescan(void *u);

static void fscan_run(FreqScanCtx *c) {
    FreqEntry entries[FREQSCAN_BINS * 3];
    int n = 0;

    rf_hal_tick();
    for (int b = 0; b < 3; b++) {
        RfBand band = (RfBand)b;
        float lo = rf_band_lo_mhz(band), hi = rf_band_hi_mhz(band);
        for (int i = 0; i < FREQSCAN_BINS; i++) {
            float f = lo + (i + 0.5f) / FREQSCAN_BINS * (hi - lo);
            entries[n++] = { f, rf_hal_rssi(band, f) };
        }
    }

    qsort(entries, n, sizeof(FreqEntry), freq_cmp);

    menu_list_clear(c->list);
    MenuItem re = { LV_SYMBOL_REFRESH, "Rescan", NULL, fscan_rescan, c };
    menu_list_add(c->list, &re);

    static char fs_lbl[12][24];
    static char fs_val[12][10];
    int shown = n < 12 ? n : 12;
    for (int i = 0; i < shown; i++) {
        snprintf(fs_lbl[i], sizeof(fs_lbl[i]), "%.3f MHz", entries[i].freq);
        snprintf(fs_val[i], sizeof(fs_val[i]), "%d dBm", entries[i].rssi);
        MenuItem it = { LV_SYMBOL_EYE_OPEN, fs_lbl[i], fs_val[i], NULL, NULL };
        menu_list_add(c->list, &it);
    }

    lv_label_set_text_fmt(c->status_lbl,
                          "Pass %d  %s", c->pass++,
                          rf_hal_present() ? "live" : "sim");
}

static void fscan_tick(lv_timer_t *t) {
    FreqScanCtx *c = (FreqScanCtx *)lv_timer_get_user_data(t);
    if (!c->scanning) return;
    fscan_run(c);
}

static void fscan_rescan(void *u) {
    FreqScanCtx *c = (FreqScanCtx *)u;
    c->scanning = true;
    c->pass = 0;
    fscan_run(c);
}

static void fscan_on_up(AppView *v)   { menu_list_up(((FreqScanCtx *)v->ctx)->list); }
static void fscan_on_down(AppView *v) { menu_list_down(((FreqScanCtx *)v->ctx)->list); }
static void fscan_on_ok(AppView *v)   { menu_list_activate(((FreqScanCtx *)v->ctx)->list); }

static void fscan_on_destroy(AppView *v) {
    FreqScanCtx *c = (FreqScanCtx *)v->ctx;
    if (c == g_fscan) g_fscan = NULL;
    if (c->scan_timer) lv_timer_delete(c->scan_timer);
    menu_list_destroy(c->list);
    free(c);
}

static AppView *cc1101_freqscan_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("FREQ SCANNER", true);

    FreqScanCtx *c = (FreqScanCtx *)calloc(1, sizeof(FreqScanCtx));
    g_fscan = c;
    c->scanning = true;

    c->status_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->status_lbl, 8, SCREEN_CONTENT_TOP + 2);

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 18, SCREEN_CONTENT_H - 18);

    fscan_run(c);
    c->scan_timer = lv_timer_create(fscan_tick, 1500, c);

    v->ctx        = c;
    v->on_up      = fscan_on_up;
    v->on_down    = fscan_on_down;
    v->on_ok      = fscan_on_ok;
    v->on_destroy = fscan_on_destroy;
    return v;
}

static const char *k_cc1101_sigs[] = {
    "433.92 MHz  OOK", "868.30 MHz  FSK",
    "315.00 MHz  ASK", "915.00 MHz  2-FSK",
};

static const ActivityCfg k_cc1101_replay_cfg = {
    LV_SYMBOL_LOOP, "SIGNAL REPLAY", "Select signal, OK to send",
    "Replaying", "pulses", 6, 90,
    k_cc1101_sigs, 4, "Signal", false, false,
};

static void cc1101_open_spectrum(void *u) { (void)u; nav_push(spectrum_view_create_ex("CC1101 SPECTRUM", RF_BAND_433, false)); }
static void cc1101_open_sniffer(void *u)  { (void)u; nav_push(sim_scanner_create("CC1101 SNIFFER", LV_SYMBOL_EYE_OPEN, "Rescan", "packets", 600, cc1101_pkt_gen)); }
static void cc1101_open_freqscan(void *u) { (void)u; nav_push(cc1101_freqscan_view_create()); }
static void cc1101_open_replay(void *u)   { (void)u; nav_push(activity_view_create(&k_cc1101_replay_cfg)); }

AppView *cc1101_view_create(void) {
    static const MenuItem items[] = {
        { LV_SYMBOL_EYE_OPEN, "Spectrum Analyzer", NULL, cc1101_open_spectrum, NULL },
        { LV_SYMBOL_AUDIO,    "Packet Sniffer",    NULL, cc1101_open_sniffer,  NULL },
        { LV_SYMBOL_SHUFFLE,  "Freq Scanner",      NULL, cc1101_open_freqscan, NULL },
        { LV_SYMBOL_LOOP,     "Signal Replay",     NULL, cc1101_open_replay,   NULL },
    };
    return list_view_create("CC1101", items, 4);
}

#define NRF24_CH_COUNT   84
#define OCC_SLOT          3
#define OCC_BAR_W         2
#define OCC_THRESH_DBM  (-88)
#define OCC_RESET_SCANS  250

typedef struct {
    bool        running;
    int         scans;
    int         hits[NRF24_CH_COUNT];
    int         max_hits;
    int         busiest;
    lv_obj_t   *bar[NRF24_CH_COUNT];
    lv_obj_t   *stats_lbl;
    lv_obj_t   *busy_lbl;
    lv_obj_t   *state_lbl;
    lv_timer_t *timer;
} OccCtx;

static void occ_tick(lv_timer_t *t) {
    OccCtx *c = (OccCtx *)lv_timer_get_user_data(t);
    if (!c->running) return;

    rf_hal_tick();
    c->scans++;

    if (c->scans > 0 && c->scans % OCC_RESET_SCANS == 0) {
        memset(c->hits, 0, sizeof(c->hits));
        c->max_hits = 1;
    }

    for (int ch = 0; ch < NRF24_CH_COUNT; ch++) {
        if (rf_hal_rssi(RF_BAND_24, 2400.0f + ch) > OCC_THRESH_DBM)
            c->hits[ch]++;
    }

    c->max_hits = 1;
    c->busiest  = 0;
    for (int ch = 0; ch < NRF24_CH_COUNT; ch++) {
        if (c->hits[ch] > c->max_hits) {
            c->max_hits = c->hits[ch];
            c->busiest  = ch;
        }
    }

    for (int ch = 0; ch < NRF24_CH_COUNT; ch++) {
        int h = c->hits[ch] * SPECT_H / c->max_hits;
        if (h < 1) h = 1;
        lv_obj_set_y(c->bar[ch], BASELINE - h);
        lv_obj_set_height(c->bar[ch], h);
        float p = (float)c->hits[ch] / c->max_hits;
        uint32_t col = p > 0.7f ? CLR_WARNING
                     : p > 0.3f ? CLR_ACCENT : CLR_ACCENT_DIM;
        lv_obj_set_style_bg_color(c->bar[ch], theme_clr(col), 0);
    }

    lv_label_set_text_fmt(c->stats_lbl, "Scan %d  OK:pause", c->scans);
    lv_label_set_text_fmt(c->busy_lbl,  "Top: CH%d %dMHz",
                          c->busiest, 2400 + c->busiest);
}

static void occ_on_ok(AppView *v) {
    OccCtx *c = (OccCtx *)v->ctx;
    c->running = !c->running;
    lv_label_set_text(c->state_lbl,
        c->running ? LV_SYMBOL_PAUSE " RUN" : LV_SYMBOL_PLAY " HOLD");
    lv_obj_set_style_text_color(c->state_lbl,
        theme_clr(c->running ? CLR_SUCCESS : CLR_WARNING), 0);
}

static void occ_on_destroy(AppView *v) {
    OccCtx *c = (OccCtx *)v->ctx;
    if (c->timer) lv_timer_delete(c->timer);
    free(c);
}

static void occ_wifi_marker(lv_obj_t *root, int ch, const char *name) {
    int x = SIDE_MARGIN + ch * OCC_SLOT;
    lv_obj_t *line = lv_obj_create(root);
    lv_obj_set_size(line, 1, SPECT_H);
    lv_obj_set_pos(line, x, SCREEN_CONTENT_TOP + 18);
    lv_obj_set_style_bg_color(line, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_40, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(root);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(lbl, FONT_SMALL, 0);
    lv_obj_set_pos(lbl, x - 4, SCREEN_CONTENT_TOP + 19);
}

static AppView *nrf24_occ_view_create(void) {
    AppView *v  = app_view_new();
    v->root     = screen_base_create("CHANNEL OCC MAP", true);
    OccCtx *c   = (OccCtx *)calloc(1, sizeof(OccCtx));
    c->running  = true;
    c->max_hits = 1;

    lv_obj_t *base = lv_obj_create(v->root);
    lv_obj_set_size(base, LCD_WIDTH - 2 * SIDE_MARGIN, 1);
    lv_obj_set_pos(base, SIDE_MARGIN, BASELINE);
    lv_obj_set_style_bg_color(base, theme_clr(CLR_GRID), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(base, 0, 0);
    lv_obj_set_style_radius(base, 0, 0);

    occ_wifi_marker(v->root, 12, "W1");
    occ_wifi_marker(v->root, 37, "W6");
    occ_wifi_marker(v->root, 62, "W11");

    for (int ch = 0; ch < NRF24_CH_COUNT; ch++) {
        int x = SIDE_MARGIN + ch * OCC_SLOT;
        c->bar[ch] = lv_obj_create(v->root);
        lv_obj_set_size(c->bar[ch], OCC_BAR_W, 1);
        lv_obj_set_pos(c->bar[ch], x, BASELINE - 1);
        lv_obj_set_style_bg_color(c->bar[ch], theme_clr(CLR_ACCENT_DIM), 0);
        lv_obj_set_style_bg_opa(c->bar[ch], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c->bar[ch], 0, 0);
        lv_obj_set_style_radius(c->bar[ch], 0, 0);
        lv_obj_clear_flag(c->bar[ch], LV_OBJ_FLAG_SCROLLABLE);
    }

    c->stats_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->stats_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->stats_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->stats_lbl, SIDE_MARGIN, BASELINE + 4);

    c->busy_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->busy_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->busy_lbl, FONT_SMALL, 0);
    lv_obj_align(c->busy_lbl, LV_ALIGN_TOP_RIGHT, -SIDE_MARGIN, BASELINE + 4);

    c->state_lbl = lv_label_create(v->root);
    lv_label_set_text(c->state_lbl, LV_SYMBOL_PAUSE " RUN");
    lv_obj_set_style_text_color(c->state_lbl, theme_clr(CLR_SUCCESS), 0);
    lv_obj_set_style_text_font(c->state_lbl, FONT_SMALL, 0);
    lv_obj_align(c->state_lbl, LV_ALIGN_TOP_RIGHT, -SIDE_MARGIN, HEADER_Y);

    v->ctx        = c;
    v->on_ok      = occ_on_ok;
    v->on_destroy = occ_on_destroy;
    c->timer = lv_timer_create(occ_tick, 60, c);
    return v;
}

static void nrf24_open_chscan(void *u) {
    (void)u;
    nav_push(spectrum_view_create_ex("2.4G CHANNELS", RF_BAND_24, true));
}

static void nrf24_open_occ(void *u) {
    (void)u;
    nav_push(nrf24_occ_view_create());
}

static void nrf24_open_mousejack(void *u) {
    (void)u;
    nav_push(mousejack_view_create());
}

AppView *nrf24_view_create(void) {
    static const MenuItem items[] = {
        { LV_SYMBOL_EYE_OPEN, "Channel Scanner", NULL, nrf24_open_chscan,    NULL },
        { LV_SYMBOL_BARS,     "Channel Occ Map", NULL, nrf24_open_occ,       NULL },
        { LV_SYMBOL_WARNING,  "MouseJack",        NULL, nrf24_open_mousejack, NULL },
    };
    return list_view_create("NRF24L01+", items, 3);
}

static void open_cc1101(void *u) { (void)u; nav_push(cc1101_view_create()); }
static void open_nrf24(void *u)  { (void)u; nav_push(nrf24_view_create()); }

AppView *rf_view_create(void) {
    static const MenuItem items[] = {
        { LV_SYMBOL_AUDIO,   "CC1101",     NULL, open_cc1101, NULL },
        { LV_SYMBOL_SHUFFLE, "NRF24L01+",  NULL, open_nrf24,  NULL },
    };
    return list_view_create("RF RADIO", items, 2);
}
