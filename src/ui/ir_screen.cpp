#include "ir_screen.h"
#include "views.h"
#include "screen_base.h"
#include "menu_list.h"
#include "theme.h"
#include "../config.h"
#include <lvgl.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static IRsend g_irsend(IR_TX_PIN, false, true);
static IRrecv g_irrecv(IR_RX_PIN, 1024, 15, true);
static bool   g_irsend_ready = false;

static void ensure_irsend(void) {
    if (g_irsend_ready) return;
    g_irsend.begin();
    g_irsend_ready = true;
}

#define IR_SAVED_MAX 5

struct IrSaved {
    decode_type_t proto;
    uint64_t      code;
    uint16_t      bits;
    char          label[24];
    bool          valid;
};

static IrSaved g_ir_saved[IR_SAVED_MAX];
static int     g_ir_saved_count = 0;

struct IrPreset {
    const char   *label;
    decode_type_t proto;
    uint64_t      code;
    uint16_t      bits;
};

static const IrPreset k_presets[] = {
    { "Samsung Power",   SAMSUNG,  0xE0E040BFUL, 32 },
    { "Samsung Vol+",    SAMSUNG,  0xE0E0E01FUL, 32 },
    { "Samsung Vol-",    SAMSUNG,  0xE0E0D02FUL, 32 },
    { "Samsung Mute",    SAMSUNG,  0xE0E0F00FUL, 32 },
    { "Samsung CH+",     SAMSUNG,  0xE0E048B7UL, 32 },
    { "Samsung CH-",     SAMSUNG,  0xE0E008F7UL, 32 },
    { "LG Power",        NEC,      0x20DF10EFUL, 32 },
    { "LG Vol+",         NEC,      0x20DF40BFUL, 32 },
    { "LG Vol-",         NEC,      0x20DFC03FUL, 32 },
    { "LG Mute",         NEC,      0x20DF906FUL, 32 },
    { "Sony Power",      SONY,     0x0A90UL,     12 },
    { "Sony Vol+",       SONY,     0x0490UL,     12 },
    { "Sony Vol-",       SONY,     0x0C90UL,     12 },
    { "Sony Mute",       SONY,     0x0290UL,     12 },
    { "Generic Power",   NEC,      0xFF02FDUL,   32 },
    { "Generic Vol+",    NEC,      0xFF629DUL,   32 },
    { "Generic Vol-",    NEC,      0xFFA857UL,   32 },
    { "Generic Mute",    NEC,      0xFF22DDUL,   32 },
};
static const int k_preset_count = (int)(sizeof(k_presets) / sizeof(k_presets[0]));

typedef struct {
    MenuList   *list;
    lv_obj_t   *status_lbl;
    lv_timer_t *clear_timer;
} IrTxCtx;

static void clear_status(lv_timer_t *t) {
    IrTxCtx *c = (IrTxCtx *)lv_timer_get_user_data(t);
    lv_label_set_text(c->status_lbl, "Select a code, OK to send");
    c->clear_timer = NULL;
}

static IrTxCtx *g_tx_ctx = NULL;

static void send_preset(void *u) {
    int idx = (int)(intptr_t)u;
    if (idx < 0 || idx >= k_preset_count || !g_tx_ctx) return;
    ensure_irsend();
    const IrPreset *p = &k_presets[idx];
    g_irsend.send(p->proto, p->code, p->bits, 3);
    lv_label_set_text_fmt(g_tx_ctx->status_lbl, LV_SYMBOL_OK " Sent: %s", p->label);
    lv_obj_set_style_text_color(g_tx_ctx->status_lbl, theme_clr(CLR_SUCCESS), 0);
    if (g_tx_ctx->clear_timer) lv_timer_delete(g_tx_ctx->clear_timer);
    g_tx_ctx->clear_timer = lv_timer_create(clear_status, 2000, g_tx_ctx);
    lv_timer_set_repeat_count(g_tx_ctx->clear_timer, 1);
}

static void irtx_on_up(AppView *v)   { menu_list_up(((IrTxCtx *)v->ctx)->list); }
static void irtx_on_down(AppView *v) { menu_list_down(((IrTxCtx *)v->ctx)->list); }
static void irtx_on_ok(AppView *v)   { menu_list_activate(((IrTxCtx *)v->ctx)->list); }

static void irtx_on_destroy(AppView *v) {
    IrTxCtx *c = (IrTxCtx *)v->ctx;
    if (c == g_tx_ctx) g_tx_ctx = NULL;
    if (c->clear_timer) lv_timer_delete(c->clear_timer);
    menu_list_destroy(c->list);
    free(c);
}

static AppView *ir_tx_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("IR TRANSMIT", true);

    IrTxCtx *c = (IrTxCtx *)calloc(1, sizeof(IrTxCtx));
    g_tx_ctx = c;

    c->status_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->status_lbl, 8, SCREEN_CONTENT_TOP + 2);
    lv_label_set_text(c->status_lbl, "Select a code, OK to send");

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 18, SCREEN_CONTENT_H - 18);

    static char tx_labels[k_preset_count][24];
    for (int i = 0; i < k_preset_count; i++) {
        strncpy(tx_labels[i], k_presets[i].label, 23);
        tx_labels[i][23] = '\0';
        MenuItem it = { LV_SYMBOL_UPLOAD, tx_labels[i], NULL,
                        send_preset, (void *)(intptr_t)i };
        menu_list_add(c->list, &it);
    }

    v->ctx        = c;
    v->on_up      = irtx_on_up;
    v->on_down    = irtx_on_down;
    v->on_ok      = irtx_on_ok;
    v->on_destroy = irtx_on_destroy;
    return v;
}

typedef struct {
    MenuList   *list;
    lv_obj_t   *status_lbl;
    lv_timer_t *poll;
    int         count;
    bool        receiving;
} IrRxCtx;

static char g_rx_detail[16][120];
static decode_type_t g_rx_proto[16];
static uint64_t      g_rx_code[16];
static uint16_t      g_rx_bits[16];
static int           g_rx_count = 0;
static IrRxCtx      *g_rx_ctx   = NULL;

static void rx_save_signal(void *u) {
    int idx = (int)(intptr_t)u;
    if (idx < 0 || idx >= g_rx_count) return;
    if (g_ir_saved_count >= IR_SAVED_MAX) {
        nav_push(info_view_create("FULL", "Max signals saved.\nDelete one first."));
        return;
    }
    int i = g_ir_saved_count;
    g_ir_saved[i].proto  = g_rx_proto[idx];
    g_ir_saved[i].code   = g_rx_code[idx];
    g_ir_saved[i].bits   = g_rx_bits[idx];
    g_ir_saved[i].valid  = true;
    snprintf(g_ir_saved[i].label, sizeof(g_ir_saved[i].label),
             "%s #%d", typeToString(g_rx_proto[idx]).c_str(), i + 1);
    g_ir_saved_count++;
    nav_push(info_view_create("SAVED", "Signal saved.\nView in Saved Signals."));
}

static void rx_open_detail(void *u) {
    int idx = (int)(intptr_t)u;
    if (idx >= 0 && idx < g_rx_count)
        nav_push(info_view_create("IR DETAIL", g_rx_detail[idx]));
}

static void rescan_rx(void *u) {
    IrRxCtx *c = (IrRxCtx *)u;
    if (!c) return;
    g_rx_count  = 0;
    c->count    = 0;
    menu_list_clear(c->list);
    MenuItem re = { LV_SYMBOL_REFRESH, "Clear / Restart", NULL, rescan_rx, c };
    menu_list_add(c->list, &re);
    lv_label_set_text(c->status_lbl, LV_SYMBOL_EYE_OPEN " Listening...");
    g_irrecv.resume();
}

static void rx_poll(lv_timer_t *t) {
    IrRxCtx *c = (IrRxCtx *)lv_timer_get_user_data(t);
    decode_results results;
    if (!g_irrecv.decode(&results)) return;

    int i = g_rx_count;
    if (i >= 16) { g_irrecv.resume(); return; }

    g_rx_proto[i] = results.decode_type;
    g_rx_code[i]  = results.value;
    g_rx_bits[i]  = results.bits;

    String proto_s = typeToString(results.decode_type);
    static char lbl[16][24];
    snprintf(lbl[i], sizeof(lbl[i]), "%s", proto_s.c_str());
    static char val[16][12];
    snprintf(val[i], sizeof(val[i]), "0x%llX", (unsigned long long)results.value);
    snprintf(g_rx_detail[i], sizeof(g_rx_detail[i]),
             "Protocol: %s\nCode: 0x%llX\nBits: %d\nOK to save",
             proto_s.c_str(),
             (unsigned long long)results.value, results.bits);

    MenuItem it  = { LV_SYMBOL_DOWNLOAD, lbl[i], val[i], rx_open_detail, (void *)(intptr_t)i };
    MenuItem sav = { LV_SYMBOL_SAVE,     "Save",  NULL,   rx_save_signal, (void *)(intptr_t)i };
    menu_list_add(c->list, &it);
    menu_list_add(c->list, &sav);

    g_rx_count++;
    c->count = g_rx_count;
    lv_label_set_text_fmt(c->status_lbl, LV_SYMBOL_EYE_OPEN " %d signal(s)", g_rx_count);

    g_irrecv.resume();
}

static void irrx_on_up(AppView *v)   { menu_list_up(((IrRxCtx *)v->ctx)->list); }
static void irrx_on_down(AppView *v) { menu_list_down(((IrRxCtx *)v->ctx)->list); }
static void irrx_on_ok(AppView *v)   { menu_list_activate(((IrRxCtx *)v->ctx)->list); }

static void irrx_on_destroy(AppView *v) {
    IrRxCtx *c = (IrRxCtx *)v->ctx;
    if (c == g_rx_ctx) g_rx_ctx = NULL;
    if (c->poll) lv_timer_delete(c->poll);
    g_irrecv.disableIRIn();
    menu_list_destroy(c->list);
    free(c);
}

static AppView *ir_rx_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("IR RECEIVE", true);

    IrRxCtx *c = (IrRxCtx *)calloc(1, sizeof(IrRxCtx));
    g_rx_ctx = c;

    c->status_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->status_lbl, 8, SCREEN_CONTENT_TOP + 2);
    lv_label_set_text(c->status_lbl, LV_SYMBOL_EYE_OPEN " Listening...");

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 18, SCREEN_CONTENT_H - 18);

    MenuItem re = { LV_SYMBOL_REFRESH, "Clear / Restart", NULL, rescan_rx, c };
    menu_list_add(c->list, &re);

    g_irrecv.enableIRIn();
    c->poll = lv_timer_create(rx_poll, 100, c);

    v->ctx        = c;
    v->on_up      = irrx_on_up;
    v->on_down    = irrx_on_down;
    v->on_ok      = irrx_on_ok;
    v->on_destroy = irrx_on_destroy;
    return v;
}

typedef struct {
    MenuList   *list;
    lv_obj_t   *status_lbl;
    lv_timer_t *clear_timer;
} IrSavedCtx;

static IrSavedCtx *g_saved_ctx = NULL;

static void replay_saved(void *u) {
    int idx = (int)(intptr_t)u;
    if (idx < 0 || idx >= g_ir_saved_count) return;
    ensure_irsend();
    IrSaved *s = &g_ir_saved[idx];
    g_irsend.send(s->proto, s->code, s->bits, 3);
    if (!g_saved_ctx) return;
    lv_label_set_text_fmt(g_saved_ctx->status_lbl, LV_SYMBOL_OK " Sent: %s", s->label);
    lv_obj_set_style_text_color(g_saved_ctx->status_lbl, theme_clr(CLR_SUCCESS), 0);
}

static void delete_saved(void *u) {
    int idx = (int)(intptr_t)u;
    if (idx < 0 || idx >= g_ir_saved_count) return;
    for (int i = idx; i < g_ir_saved_count - 1; i++)
        g_ir_saved[i] = g_ir_saved[i + 1];
    g_ir_saved_count--;
    if (!g_saved_ctx) return;
    menu_list_clear(g_saved_ctx->list);
    lv_label_set_text_fmt(g_saved_ctx->status_lbl, "%d saved", g_ir_saved_count);
    static char sav_lbl[IR_SAVED_MAX][24];
    for (int i = 0; i < g_ir_saved_count; i++) {
        strncpy(sav_lbl[i], g_ir_saved[i].label, 23);
        MenuItem play = { LV_SYMBOL_PLAY,   sav_lbl[i], "replay", replay_saved, (void *)(intptr_t)i };
        MenuItem del  = { LV_SYMBOL_CLOSE,  "Delete",   NULL,     delete_saved, (void *)(intptr_t)i };
        menu_list_add(g_saved_ctx->list, &play);
        menu_list_add(g_saved_ctx->list, &del);
    }
}

static void saved_on_up(AppView *v)   { menu_list_up(((IrSavedCtx *)v->ctx)->list); }
static void saved_on_down(AppView *v) { menu_list_down(((IrSavedCtx *)v->ctx)->list); }
static void saved_on_ok(AppView *v)   { menu_list_activate(((IrSavedCtx *)v->ctx)->list); }

static void saved_on_destroy(AppView *v) {
    IrSavedCtx *c = (IrSavedCtx *)v->ctx;
    if (c == g_saved_ctx) g_saved_ctx = NULL;
    if (c->clear_timer) lv_timer_delete(c->clear_timer);
    menu_list_destroy(c->list);
    free(c);
}

static AppView *ir_saved_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("SAVED SIGNALS", true);

    IrSavedCtx *c = (IrSavedCtx *)calloc(1, sizeof(IrSavedCtx));
    g_saved_ctx = c;

    c->status_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->status_lbl, 8, SCREEN_CONTENT_TOP + 2);

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 18, SCREEN_CONTENT_H - 18);

    if (g_ir_saved_count == 0) {
        lv_label_set_text(c->status_lbl, "No saved signals");
        MenuItem none = { LV_SYMBOL_WARNING, "No signals saved", NULL, NULL, NULL };
        menu_list_add(c->list, &none);
    } else {
        lv_label_set_text_fmt(c->status_lbl, "%d saved", g_ir_saved_count);
        static char sav_lbl[IR_SAVED_MAX][24];
        for (int i = 0; i < g_ir_saved_count; i++) {
            strncpy(sav_lbl[i], g_ir_saved[i].label, 23);
            sav_lbl[i][23] = '\0';
            MenuItem play = { LV_SYMBOL_PLAY,  sav_lbl[i], "replay", replay_saved, (void *)(intptr_t)i };
            MenuItem del  = { LV_SYMBOL_CLOSE, "Delete",    NULL,    delete_saved, (void *)(intptr_t)i };
            menu_list_add(c->list, &play);
            menu_list_add(c->list, &del);
        }
    }

    v->ctx        = c;
    v->on_up      = saved_on_up;
    v->on_down    = saved_on_down;
    v->on_ok      = saved_on_ok;
    v->on_destroy = saved_on_destroy;
    return v;
}

static void open_tx(void *u)    { (void)u; nav_push(ir_tx_view_create()); }
static void open_rx(void *u)    { (void)u; nav_push(ir_rx_view_create()); }
static void open_saved(void *u) { (void)u; nav_push(ir_saved_view_create()); }

AppView *ir_view_create(void) {
    static const MenuItem items[] = {
        { LV_SYMBOL_UPLOAD,   "IR Transmit",   NULL, open_tx,    NULL },
        { LV_SYMBOL_DOWNLOAD, "IR Receive",    NULL, open_rx,    NULL },
        { LV_SYMBOL_SAVE,     "Saved Signals", NULL, open_saved, NULL },
    };
    return list_view_create("INFRARED", items, 3);
}
