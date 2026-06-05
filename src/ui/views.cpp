#include "views.h"
#include "screen_base.h"
#include "theme.h"
#include "../config.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void anim_text_opa(void *obj, int32_t v) {
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void list_on_up(AppView *v)      { menu_list_up((MenuList *)v->ctx); }
static void list_on_down(AppView *v)    { menu_list_down((MenuList *)v->ctx); }
static void list_on_ok(AppView *v)      { menu_list_activate((MenuList *)v->ctx); }
static void list_on_destroy(AppView *v) { menu_list_destroy((MenuList *)v->ctx); }

AppView *list_view_create(const char *title, const MenuItem *items, int count) {
    AppView *v = app_view_new();
    v->root = screen_base_create(title, true);

    MenuList *ml = menu_list_create(v->root, SCREEN_CONTENT_TOP, SCREEN_CONTENT_H);
    for (int i = 0; i < count; i++) menu_list_add(ml, &items[i]);

    v->ctx        = ml;
    v->on_up      = list_on_up;
    v->on_down    = list_on_down;
    v->on_ok      = list_on_ok;
    v->on_destroy = list_on_destroy;
    return v;
}

AppView *info_view_create(const char *title, const char *body) {
    AppView *v = app_view_new();
    v->root = screen_base_create(title, true);

    lv_obj_t *panel = lv_obj_create(v->root);
    lv_obj_set_size(panel, LCD_WIDTH - 16, SCREEN_CONTENT_H - 12);
    lv_obj_set_pos(panel, 8, SCREEN_CONTENT_TOP + 4);
    lv_obj_set_style_bg_color(panel, theme_clr(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, theme_clr(CLR_SEPARATOR), 0);
    lv_obj_set_style_radius(panel, 5, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(panel);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LCD_WIDTH - 16 - 20);
    lv_label_set_text(lbl, body);
    lv_obj_set_style_text_color(lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(lbl, FONT_NORMAL, 0);
    lv_obj_set_style_text_line_space(lbl, 3, 0);

    return v;
}

typedef struct {
    const ActivityCfg *cfg;
    lv_obj_t   *icon;
    lv_obj_t   *ring[2];
    lv_obj_t   *status;
    lv_obj_t   *counter;
    lv_obj_t   *mode;
    lv_timer_t *timer;
    uint32_t    count;
    int         cx, cy, max_r;
    int         sel_mode;
    bool        active;
} ActCtx;

static void ring_anim_cb(void *var, int32_t v) {
    lv_obj_t *r = (lv_obj_t *)var;
    ActCtx   *c = (ActCtx *)lv_obj_get_user_data(r);
    float p   = v / 1000.0f;
    int   rad = 8 + (int)(p * (c->max_r - 8));
    lv_obj_set_size(r, rad * 2, rad * 2);
    lv_obj_set_pos(r, c->cx - rad, c->cy - rad);
    lv_obj_set_style_radius(r, rad, 0);
    lv_obj_set_style_border_opa(r, (lv_opa_t)((1.0f - p) * 180.0f), 0);
}

static void act_set_running(ActCtx *c, bool on) {
    c->active = on;
    const ActivityCfg *cfg = c->cfg;

    if (on) {
        lv_label_set_text_fmt(c->status, "%s...", cfg->active_verb);
        lv_obj_set_style_text_color(c->status,
            theme_clr(cfg->warn ? CLR_WARNING : CLR_SUCCESS), 0);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, c->icon);
        lv_anim_set_exec_cb(&a, anim_text_opa);
        lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
        lv_anim_set_duration(&a, 500);
        lv_anim_set_playback_duration(&a, 500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);

        for (int i = 0; i < 2; i++) {
            lv_obj_clear_flag(c->ring[i], LV_OBJ_FLAG_HIDDEN);
            lv_anim_t r;
            lv_anim_init(&r);
            lv_anim_set_var(&r, c->ring[i]);
            lv_anim_set_exec_cb(&r, ring_anim_cb);
            lv_anim_set_values(&r, 0, 1000);
            lv_anim_set_duration(&r, 1100);
            lv_anim_set_delay(&r, i * 550);
            lv_anim_set_repeat_count(&r, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&r);
        }
    } else {
        lv_anim_delete(c->icon, anim_text_opa);
        lv_obj_set_style_text_opa(c->icon, LV_OPA_COVER, 0);
        for (int i = 0; i < 2; i++) {
            lv_anim_delete(c->ring[i], ring_anim_cb);
            lv_obj_add_flag(c->ring[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(c->status, cfg->idle_hint);
        lv_obj_set_style_text_color(c->status, theme_clr(CLR_TEXT_DIM), 0);
    }
}

static void act_tick(lv_timer_t *t) {
    ActCtx *c = (ActCtx *)lv_timer_get_user_data(t);
    if (!c->active || !c->cfg->unit) return;
    int step = c->cfg->per_tick;
    c->count += step + (int)random(0, step + 1);
    lv_label_set_text_fmt(c->counter, "%lu %s",
                          (unsigned long)c->count, c->cfg->unit);
}

static void act_update_mode(ActCtx *c) {
    if (!c->mode) return;
    lv_label_set_text_fmt(c->mode, "%s: %s",
                          c->cfg->mode_label, c->cfg->modes[c->sel_mode]);
}

static void act_on_ok(AppView *v)  { ActCtx *c = (ActCtx *)v->ctx; act_set_running(c, !c->active); }

static void act_on_up(AppView *v) {
    ActCtx *c = (ActCtx *)v->ctx;
    if (c->cfg->mode_count < 2) return;
    c->sel_mode = (c->sel_mode + 1) % c->cfg->mode_count;
    act_update_mode(c);
}

static void act_on_down(AppView *v) {
    ActCtx *c = (ActCtx *)v->ctx;
    if (c->cfg->mode_count < 2) return;
    c->sel_mode = (c->sel_mode - 1 + c->cfg->mode_count) % c->cfg->mode_count;
    act_update_mode(c);
}

static void act_on_destroy(AppView *v) {
    ActCtx *c = (ActCtx *)v->ctx;
    if (c->timer) lv_timer_delete(c->timer);
    lv_anim_delete(c->icon, anim_text_opa);
    for (int i = 0; i < 2; i++) lv_anim_delete(c->ring[i], ring_anim_cb);
    free(c);
}

AppView *activity_view_create(const ActivityCfg *cfg) {
    AppView *v = app_view_new();
    v->root = screen_base_create(cfg->title, true);

    ActCtx *c = (ActCtx *)calloc(1, sizeof(ActCtx));
    c->cfg   = cfg;
    c->cx    = LCD_WIDTH / 2;
    c->cy    = SCREEN_CONTENT_TOP + 38;
    c->max_r = 40;

    for (int i = 0; i < 2; i++) {
        c->ring[i] = lv_obj_create(v->root);
        lv_obj_set_user_data(c->ring[i], c);
        lv_obj_set_style_bg_opa(c->ring[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c->ring[i], 2, 0);
        lv_obj_set_style_border_color(c->ring[i],
            theme_clr(cfg->warn ? CLR_WARNING : CLR_ACCENT), 0);
        lv_obj_clear_flag(c->ring[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(c->ring[i], LV_OBJ_FLAG_HIDDEN);
    }

    c->icon = lv_label_create(v->root);
    lv_label_set_text(c->icon, cfg->icon);
    lv_obj_set_style_text_font(c->icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(c->icon,
        theme_clr(cfg->warn ? CLR_WARNING : CLR_ACCENT), 0);
    lv_obj_align(c->icon, LV_ALIGN_TOP_MID, 0, c->cy - 12);

    c->status = lv_label_create(v->root);
    lv_obj_set_style_text_font(c->status, FONT_MEDIUM, 0);
    lv_obj_align(c->status, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 74);

    if (cfg->unit) {
        c->counter = lv_label_create(v->root);
        lv_label_set_text_fmt(c->counter, "0 %s", cfg->unit);
        lv_obj_set_style_text_color(c->counter, theme_clr(CLR_TEXT), 0);
        lv_obj_set_style_text_font(c->counter, FONT_NORMAL, 0);
        lv_obj_align(c->counter, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 96);
    }

    if (cfg->modes && cfg->mode_count > 0) {
        c->mode = lv_label_create(v->root);
        lv_obj_set_style_text_color(c->mode, theme_clr(CLR_ACCENT), 0);
        lv_obj_set_style_text_font(c->mode, FONT_SMALL, 0);
        lv_obj_align(c->mode, LV_ALIGN_BOTTOM_MID, 0, -6);
        act_update_mode(c);
    }

    v->ctx        = c;
    v->on_ok      = act_on_ok;
    v->on_up      = act_on_up;
    v->on_down    = act_on_down;
    v->on_destroy = act_on_destroy;

    act_set_running(c, false);
    c->timer = lv_timer_create(act_tick, cfg->rate_ms ? cfg->rate_ms : 250, c);
    if (cfg->autostart) act_set_running(c, true);
    return v;
}

#define SCAN_CAP 14

typedef struct {
    MenuList   *list;
    lv_obj_t   *status;
    lv_timer_t *timer;
    sim_gen_fn  gen;
    const char *unit;
    const char *restart;
    int         seq;
    char        label[SCAN_CAP][32];
    char        value[SCAN_CAP][14];
    char        detail[SCAN_CAP][176];
} ScanCtx;

static ScanCtx *g_scan_active;

static void scan_open_detail(void *u) {
    int idx = (int)(intptr_t)u;
    if (g_scan_active && idx >= 0 && idx < g_scan_active->seq)
        nav_push(info_view_create("DETAIL", g_scan_active->detail[idx]));
}

static void scan_restart(void *u);

static void scan_render(ScanCtx *c) {
    menu_list_clear(c->list);
    MenuItem r = { LV_SYMBOL_REFRESH, c->restart, NULL, scan_restart, c };
    menu_list_add(c->list, &r);
    for (int i = 0; i < c->seq && i < SCAN_CAP; i++) {
        MenuItem it = { LV_SYMBOL_RIGHT, c->label[i],
                        c->value[i][0] ? c->value[i] : NULL,
                        scan_open_detail, (void *)(intptr_t)i };
        menu_list_add(c->list, &it);
    }
    lv_label_set_text_fmt(c->status, "%d %s", c->seq, c->unit);
}

static void scan_tick(lv_timer_t *t) {
    ScanCtx *c = (ScanCtx *)lv_timer_get_user_data(t);
    if (c->seq >= SCAN_CAP) {
        lv_label_set_text_fmt(c->status, "%d %s " LV_SYMBOL_OK, c->seq, c->unit);
        lv_timer_pause(t);
        return;
    }
    int i = c->seq;
    c->value[i][0] = '\0';
    c->gen(i, c->label[i], sizeof(c->label[i]),
              c->value[i], sizeof(c->value[i]),
              c->detail[i], sizeof(c->detail[i]));
    c->seq++;
    if (menu_list_selected(c->list) == 0) scan_render(c);
    else lv_label_set_text_fmt(c->status, "%d %s", c->seq, c->unit);
}

static void scan_restart(void *u) {
    ScanCtx *c = (ScanCtx *)u;
    c->seq = 0;
    scan_render(c);
    lv_timer_resume(c->timer);
}

static void scan_on_up(AppView *v)   { menu_list_up(((ScanCtx *)v->ctx)->list); }
static void scan_on_down(AppView *v) { menu_list_down(((ScanCtx *)v->ctx)->list); }
static void scan_on_ok(AppView *v) {
    ScanCtx *c = (ScanCtx *)v->ctx;
    g_scan_active = c;
    menu_list_activate(c->list);
}

static void scan_on_destroy(AppView *v) {
    ScanCtx *c = (ScanCtx *)v->ctx;
    if (g_scan_active == c) g_scan_active = NULL;
    if (c->timer) lv_timer_delete(c->timer);
    menu_list_destroy(c->list);
    free(c);
}

AppView *sim_scanner_create(const char *title, const char *icon,
                            const char *restart_label, const char *unit,
                            uint32_t interval_ms, sim_gen_fn gen) {
    (void)icon;
    AppView *v = app_view_new();
    v->root = screen_base_create(title, true);

    ScanCtx *c = (ScanCtx *)calloc(1, sizeof(ScanCtx));
    c->gen     = gen;
    c->unit    = unit;
    c->restart = restart_label;

    c->status = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->status, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status, FONT_SMALL, 0);
    lv_obj_set_pos(c->status, 8, SCREEN_CONTENT_TOP + 2);

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 18, SCREEN_CONTENT_H - 18);
    scan_render(c);

    v->ctx        = c;
    v->on_up      = scan_on_up;
    v->on_down    = scan_on_down;
    v->on_ok      = scan_on_ok;
    v->on_destroy = scan_on_destroy;

    c->timer = lv_timer_create(scan_tick, interval_ms ? interval_ms : 350, c);
    return v;
}
