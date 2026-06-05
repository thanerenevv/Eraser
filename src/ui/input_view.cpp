#include "input_view.h"
#include "screen_base.h"
#include "theme.h"
#include "../config.h"
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

// ── layout ──────────────────────────────────────────────────────────────────
// Status bar: y=0..21 (22px) + separator at y=22 (1px)
// Text box:   y=24, h=17  →  bottom at y=41
// Gap:        3px          →  keyboard starts at y=44
// 4 rows × 30px + 3 gaps × 2px = 126px  →  bottom at y=170  ✓

#define KB_TEXT_Y    24
#define KB_TEXT_H    17
#define KB_START_Y   44
#define KB_KEY_H     30
#define KB_ROW_GAP    2
#define KB_PITCH     32   // horizontal advance per key slot (320/10 = 32)
#define KB_KEY_W     31   // visual key width; 1-px right gap

#define KB_CHAR_ROWS   3
#define KB_MAX_COLS   10
#define KB_SPEC_KEYS   4
#define KB_TOTAL_ROWS  (KB_CHAR_ROWS + 1)

#define KB_ROW_Y(r)  (KB_START_Y + (r) * (KB_KEY_H + KB_ROW_GAP))
// r=0→44, r=1→76, r=2→108, r=3→140, bottom→170

// Special-key x positions and widths (sum = LCD_WIDTH = 320, 2-px gaps between)
static const int16_t SPK_X[KB_SPEC_KEYS] = {  0,  68, 182, 250 };
static const int16_t SPK_W[KB_SPEC_KEYS] = { 66, 112,  66,  70 };

// ── layers ───────────────────────────────────────────────────────────────────
typedef enum { LAYER_LOWER = 0, LAYER_UPPER = 1, LAYER_NUMS = 2 } KbLayer;

static const char *g_rows[3][KB_CHAR_ROWS] = {
    { "qwertyuiop", "asdfghjkl", "zxcvbnm"   },
    { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"   },
    { "1234567890", "!@#$%^&*()", "-_=+[];'." },
};

static const char *g_tog_lbl[3] = { "SYM", "SYM", "ABC" };

// ── context ───────────────────────────────────────────────────────────────────
typedef struct {
    char             entered[65];
    int              len;
    int              max_len;
    int              cur_row;  // 0-2: char rows, 3: special row
    int              cur_col;
    KbLayer          layer;
    const char      *hint;
    input_done_cb_t  cb;
    void            *user;

    lv_obj_t        *text_lbl;
    lv_obj_t        *char_box[KB_CHAR_ROWS][KB_MAX_COLS];
    lv_obj_t        *char_lbl[KB_CHAR_ROWS][KB_MAX_COLS];
    lv_obj_t        *spec_box[KB_SPEC_KEYS];
    lv_obj_t        *spec_lbl[KB_SPEC_KEYS];

    lv_timer_t      *action_timer;
    bool             submitting;
} InputCtx;

// ── helpers ───────────────────────────────────────────────────────────────────
static int row_len(InputCtx *c, int row) {
    if (row < KB_CHAR_ROWS) return (int)strlen(g_rows[c->layer][row]);
    return KB_SPEC_KEYS;
}

static int cur_len(InputCtx *c) { return row_len(c, c->cur_row); }

static void set_key_style(lv_obj_t *box, lv_obj_t *lbl, bool sel) {
    if (sel) {
        lv_obj_set_style_bg_color(box, theme_clr(CLR_ACCENT), 0);
        lv_obj_set_style_border_color(box, theme_clr(CLR_ACCENT), 0);
        lv_obj_set_style_text_color(lbl, theme_clr(CLR_TEXT_DARK), 0);
    } else {
        lv_obj_set_style_bg_color(box, theme_clr(CLR_BG_LIGHT), 0);
        lv_obj_set_style_border_color(box, theme_clr(CLR_SEPARATOR), 0);
        lv_obj_set_style_text_color(lbl, theme_clr(CLR_TEXT), 0);
    }
}

static void highlight(InputCtx *c, int row, int col, bool sel) {
    if (row < KB_CHAR_ROWS)
        set_key_style(c->char_box[row][col], c->char_lbl[row][col], sel);
    else
        set_key_style(c->spec_box[col], c->spec_lbl[col], sel);
}

static void refresh_text(InputCtx *c) {
    if (c->len == 0 && c->hint) {
        lv_label_set_text(c->text_lbl, c->hint);
        lv_obj_set_style_text_color(c->text_lbl, theme_clr(CLR_TEXT_DIM), 0);
    } else {
        char buf[68];
        int n     = c->len < 24 ? c->len : 24;
        int start = c->len > 24 ? c->len - 24 : 0;
        memcpy(buf, c->entered + start, n);
        buf[n]   = '_';
        buf[n+1] = '\0';
        lv_label_set_text(c->text_lbl, buf);
        lv_obj_set_style_text_color(c->text_lbl, theme_clr(CLR_TEXT), 0);
    }
}

// Reposition, relabel, and show/hide all char keys for the current layer.
// Also restores the highlight state of every key.
static void apply_layer(InputCtx *c) {
    for (int r = 0; r < KB_CHAR_ROWS; r++) {
        const char *s  = g_rows[c->layer][r];
        int          n  = (int)strlen(s);
        int          x0 = (LCD_WIDTH - n * KB_PITCH) / 2;
        for (int k = 0; k < KB_MAX_COLS; k++) {
            if (k < n) {
                lv_obj_set_pos(c->char_box[r][k], x0 + k * KB_PITCH, KB_ROW_Y(r));
                char buf[2] = { s[k], '\0' };
                lv_label_set_text(c->char_lbl[r][k], buf);
                lv_obj_remove_flag(c->char_box[r][k], LV_OBJ_FLAG_HIDDEN);
                bool is_cur = (c->cur_row == r && c->cur_col == k);
                set_key_style(c->char_box[r][k], c->char_lbl[r][k], is_cur);
            } else {
                lv_obj_add_flag(c->char_box[r][k], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    lv_label_set_text(c->spec_lbl[0], g_tog_lbl[c->layer]);
    for (int k = 0; k < KB_SPEC_KEYS; k++) {
        bool is_cur = (c->cur_row == KB_CHAR_ROWS && c->cur_col == k);
        set_key_style(c->spec_box[k], c->spec_lbl[k], is_cur);
    }
}

// ── action ────────────────────────────────────────────────────────────────────
static void action_timer_cb(lv_timer_t *t) {
    InputCtx       *c    = (InputCtx *)lv_timer_get_user_data(t);
    c->action_timer       = NULL;
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
    c->action_timer = lv_timer_create(action_timer_cb, 10, c);
    lv_timer_set_repeat_count(c->action_timer, 1);
}

static void press_key(InputCtx *c) {
    if (c->cur_row < KB_CHAR_ROWS) {
        const char *s = g_rows[c->layer][c->cur_row];
        int          n = (int)strlen(s);
        if (c->cur_col < n && c->len < c->max_len) {
            c->entered[c->len++] = s[c->cur_col];
            c->entered[c->len]   = '\0';
            refresh_text(c);
        }
    } else {
        switch (c->cur_col) {
        case 0:  // Layer toggle (SYM / ABC)
            highlight(c, c->cur_row, c->cur_col, false);
            c->layer = (KbLayer)((c->layer + 1) % 3);
            // Clamp column if the new layer's current char-row is shorter
            if (c->cur_row < KB_CHAR_ROWS) {
                int nl = (int)strlen(g_rows[c->layer][c->cur_row]);
                if (c->cur_col >= nl) c->cur_col = nl - 1;
            }
            apply_layer(c);
            break;
        case 1:  // Space
            if (c->len < c->max_len) {
                c->entered[c->len++] = ' ';
                c->entered[c->len]   = '\0';
                refresh_text(c);
            }
            break;
        case 2:  // Backspace
            if (c->len > 0) {
                c->entered[--c->len] = '\0';
                refresh_text(c);
            }
            break;
        case 3:  // Done — submit
            trigger_action(c, true);
            break;
        }
    }
}

// ── button handlers ───────────────────────────────────────────────────────────
static void input_on_up(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    highlight(c, c->cur_row, c->cur_col, false);
    c->cur_row = (c->cur_row - 1 + KB_TOTAL_ROWS) % KB_TOTAL_ROWS;
    int len = cur_len(c);
    if (c->cur_col >= len) c->cur_col = len - 1;
    highlight(c, c->cur_row, c->cur_col, true);
}

static void input_on_down(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    highlight(c, c->cur_row, c->cur_col, false);
    c->cur_row = (c->cur_row + 1) % KB_TOTAL_ROWS;
    int len = cur_len(c);
    if (c->cur_col >= len) c->cur_col = len - 1;
    highlight(c, c->cur_row, c->cur_col, true);
}

static void input_on_ok(AppView *v) {
    press_key((InputCtx *)v->ctx);
}

// Short BACK = move cursor left (wraps).  Hold BACK = nav_home() (cancel).
static bool input_on_back(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    highlight(c, c->cur_row, c->cur_col, false);
    int len = cur_len(c);
    c->cur_col = (c->cur_col - 1 + len) % len;
    highlight(c, c->cur_row, c->cur_col, true);
    return true;
}

static void input_on_extra(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    highlight(c, c->cur_row, c->cur_col, false);
    int len = cur_len(c);
    c->cur_col = (c->cur_col + 1) % len;
    highlight(c, c->cur_row, c->cur_col, true);
}

static void input_on_destroy(AppView *v) {
    InputCtx *c = (InputCtx *)v->ctx;
    if (c->action_timer) lv_timer_delete(c->action_timer);
    if (c->cb) c->cb(NULL, c->user);
    free(c);
}

// ── key factory ───────────────────────────────────────────────────────────────
static lv_obj_t *make_key(lv_obj_t *parent, int x, int y, int w, int h,
                            const char *label, const lv_font_t *font,
                            lv_obj_t **lbl_out) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, theme_clr(CLR_BG_LIGHT), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, theme_clr(CLR_SEPARATOR), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    if (lbl_out) *lbl_out = lbl;
    return box;
}

// ── create ────────────────────────────────────────────────────────────────────
AppView *input_view_create(const char *title, const char *hint, int max_len,
                            input_done_cb_t on_done, void *user) {
    AppView  *v = app_view_new();
    v->root = screen_base_create(title, true);
    lv_obj_t *scr = v->root;

    if (max_len <= 0 || max_len > 64) max_len = 64;

    InputCtx *c = (InputCtx *)calloc(1, sizeof(InputCtx));
    c->max_len  = max_len;
    c->layer    = LAYER_LOWER;
    c->cur_row  = 0;
    c->cur_col  = 0;
    c->hint     = hint;
    c->cb       = on_done;
    c->user     = user;

    // Text input box
    lv_obj_t *text_box = lv_obj_create(scr);
    lv_obj_set_size(text_box, LCD_WIDTH - 4, KB_TEXT_H);
    lv_obj_set_pos(text_box, 2, KB_TEXT_Y);
    lv_obj_set_style_bg_color(text_box, theme_clr(CLR_BG_LIGHT), 0);
    lv_obj_set_style_bg_opa(text_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(text_box, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_border_width(text_box, 1, 0);
    lv_obj_set_style_radius(text_box, 3, 0);
    lv_obj_set_style_pad_hor(text_box, 4, 0);
    lv_obj_set_style_pad_ver(text_box, 0, 0);
    lv_obj_clear_flag(text_box, LV_OBJ_FLAG_SCROLLABLE);

    c->text_lbl = lv_label_create(text_box);
    lv_obj_set_style_text_font(c->text_lbl, FONT_SMALL, 0);
    lv_obj_align(c->text_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Char keys — create KB_MAX_COLS per row; apply_layer shows/hides extras
    for (int r = 0; r < KB_CHAR_ROWS; r++) {
        for (int k = 0; k < KB_MAX_COLS; k++) {
            c->char_box[r][k] = make_key(scr, 0, KB_ROW_Y(r),
                                          KB_KEY_W, KB_KEY_H,
                                          " ", FONT_NORMAL,
                                          &c->char_lbl[r][k]);
        }
    }

    // Special row (SYM · SPACE · DEL · DONE)
    static const char *spec_init[KB_SPEC_KEYS] = { "SYM", "SPACE", "DEL", "DONE" };
    for (int k = 0; k < KB_SPEC_KEYS; k++) {
        c->spec_box[k] = make_key(scr,
                                   SPK_X[k], KB_ROW_Y(KB_CHAR_ROWS),
                                   SPK_W[k], KB_KEY_H,
                                   spec_init[k], FONT_SMALL,
                                   &c->spec_lbl[k]);
    }

    apply_layer(c);   // positions, labels, shows/hides all char keys + highlights (0,0)
    refresh_text(c);  // hint placeholder

    v->ctx        = c;
    v->on_up      = input_on_up;
    v->on_down    = input_on_down;
    v->on_ok      = input_on_ok;
    v->on_back    = input_on_back;
    v->on_extra   = input_on_extra;
    v->on_destroy = input_on_destroy;
    return v;
}
