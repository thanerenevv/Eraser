#include "input_view.h"
#include "screen_base.h"
#include "theme.h"
#include "../config.h"
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

static const char k_chars[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    " !@#$%&*()-_=+[];':\",./?\\";
static const int k_char_count = (int)(sizeof(k_chars) - 1);

#define INPUT_CHAR_BOX_W   52
#define INPUT_CHAR_BOX_H   44
#define INPUT_CHAR_BOX_X   ((LCD_WIDTH - INPUT_CHAR_BOX_W) / 2)
#define INPUT_CHAR_BOX_Y   80

typedef struct {
    char             entered[65];
    int              len;
    int              max_len;
    int              char_idx;
    input_done_cb_t  cb;
    void            *user;
    lv_obj_t        *text_lbl;
    lv_obj_t        *char_lbl;
    lv_timer_t      *action_timer;
    bool             submitting;
} InputCtx;

static void input_refresh_text(InputCtx *c) {
    char buf[68];
    int n = c->len < 20 ? c->len : 20;
    int start = c->len > 20 ? c->len - 20 : 0;
    memcpy(buf, c->entered + start, n);
    buf[n]     = '_';
    buf[n + 1] = '\0';
    lv_label_set_text(c->text_lbl, buf);
}

static void input_refresh_char(InputCtx *c) {
    char buf[2] = { k_chars[c->char_idx], '\0' };
    lv_label_set_text(c->char_lbl, buf);
}

static void input_action_cb(lv_timer_t *t) {
    InputCtx *c = (InputCtx *)lv_timer_get_user_data(t);
    c->action_timer = NULL;

    input_done_cb_t cb   = c->cb;
    void           *user = c->user;
    bool            sub  = c->submitting;
    char            text[65];
    memcpy(text, c->entered, c->len + 1);

    c->cb = NULL;
    nav_pop();
    if (cb) cb(sub ? text : NULL, user);
}

static void trigger_action(InputCtx *c, bool submit) {
    if (c->action_timer) return;
    c->submitting   = submit;
    c->action_timer = lv_timer_create(input_action_cb, 10, c);
    lv_timer_set_repeat_count(c->action_timer, 1);
}

static void input_on_up(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    c->char_idx = (c->char_idx + 1) % k_char_count;
    input_refresh_char(c);
}

static void input_on_down(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    c->char_idx = (c->char_idx - 1 + k_char_count) % k_char_count;
    input_refresh_char(c);
}

static void input_on_ok(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    if (c->len >= c->max_len) return;
    c->entered[c->len++] = k_chars[c->char_idx];
    c->entered[c->len]   = '\0';
    input_refresh_text(c);
}

static void input_on_extra(AppView *v) {
    trigger_action((InputCtx *)v->ctx, true);
}

static bool input_on_back(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    if (c->len > 0) {
        c->entered[--c->len] = '\0';
        input_refresh_text(c);
        return true;
    }
    trigger_action(c, false);
    return true;
}

static void input_on_destroy(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    if (c->action_timer) lv_timer_delete(c->action_timer);
    if (c->cb) c->cb(NULL, c->user);
    free(c);
}

AppView *input_view_create(const char *title, const char *hint, int max_len,
                            input_done_cb_t on_done, void *user) {
    AppView  *v = app_view_new();
    v->root = screen_base_create(title, true);
    lv_obj_t *scr = v->root;

    if (max_len <= 0 || max_len > 64) max_len = 64;

    InputCtx *c = (InputCtx *)calloc(1, sizeof(InputCtx));
    c->max_len  = max_len;
    c->char_idx = 0;
    c->cb       = on_done;
    c->user     = user;

    lv_obj_t *hint_lbl = lv_label_create(scr);
    lv_label_set_text(hint_lbl, hint ? hint : "");
    lv_obj_set_style_text_color(hint_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(hint_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(hint_lbl, 8, SCREEN_CONTENT_TOP + 3);

    lv_obj_t *text_box = lv_obj_create(scr);
    lv_obj_set_size(text_box, LCD_WIDTH - 16, 22);
    lv_obj_set_pos(text_box, 8, SCREEN_CONTENT_TOP + 20);
    lv_obj_set_style_bg_color(text_box, theme_clr(CLR_BG_LIGHT), 0);
    lv_obj_set_style_bg_opa(text_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(text_box, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_border_width(text_box, 1, 0);
    lv_obj_set_style_radius(text_box, 4, 0);
    lv_obj_set_style_pad_all(text_box, 4, 0);
    lv_obj_clear_flag(text_box, LV_OBJ_FLAG_SCROLLABLE);

    c->text_lbl = lv_label_create(text_box);
    lv_obj_set_style_text_color(c->text_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->text_lbl, FONT_SMALL, 0);
    lv_obj_align(c->text_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *up_arr = lv_label_create(scr);
    lv_label_set_text(up_arr, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(up_arr, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(up_arr, FONT_NORMAL, 0);
    lv_obj_set_pos(up_arr, INPUT_CHAR_BOX_X + INPUT_CHAR_BOX_W / 2 - 6, INPUT_CHAR_BOX_Y - 18);

    lv_obj_t *char_box = lv_obj_create(scr);
    lv_obj_set_size(char_box, INPUT_CHAR_BOX_W, INPUT_CHAR_BOX_H);
    lv_obj_set_pos(char_box, INPUT_CHAR_BOX_X, INPUT_CHAR_BOX_Y);
    lv_obj_set_style_bg_color(char_box, theme_clr(CLR_BG_LIGHT), 0);
    lv_obj_set_style_bg_opa(char_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(char_box, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_border_width(char_box, 2, 0);
    lv_obj_set_style_radius(char_box, 8, 0);
    lv_obj_set_style_pad_all(char_box, 0, 0);
    lv_obj_clear_flag(char_box, LV_OBJ_FLAG_SCROLLABLE);

    c->char_lbl = lv_label_create(char_box);
    lv_obj_set_style_text_color(c->char_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(c->char_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(c->char_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *dn_arr = lv_label_create(scr);
    lv_label_set_text(dn_arr, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(dn_arr, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(dn_arr, FONT_NORMAL, 0);
    lv_obj_set_pos(dn_arr, INPUT_CHAR_BOX_X + INPUT_CHAR_BOX_W / 2 - 6,
                   INPUT_CHAR_BOX_Y + INPUT_CHAR_BOX_H + 3);

    lv_obj_t *help = lv_label_create(scr);
    lv_label_set_text(help, LV_SYMBOL_OK " accept  R: done  BACK: del");
    lv_obj_set_style_text_color(help, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(help, FONT_SMALL, 0);
    lv_obj_align(help, LV_ALIGN_BOTTOM_MID, 0, -4);

    input_refresh_text(c);
    input_refresh_char(c);

    v->ctx        = c;
    v->on_up      = input_on_up;
    v->on_down    = input_on_down;
    v->on_ok      = input_on_ok;
    v->on_back    = input_on_back;
    v->on_extra   = input_on_extra;
    v->on_destroy = input_on_destroy;
    return v;
}
