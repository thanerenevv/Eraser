#include "menu_list.h"
#include "theme.h"
#include "../config.h"
#include <stdlib.h>
#include <string.h>

#define ML_MAX        MENU_ITEMS_MAX
#define ML_ANIM_MS    160
#define ICON_X        12
#define LABEL_X_ICON  44
#define LABEL_X_PLAIN 14
#define EDGE_PAD      14

struct MenuList {
    lv_obj_t      *viewport;
    lv_obj_t      *content;
    lv_obj_t      *highlight;
    lv_obj_t      *icon[ML_MAX];
    lv_obj_t      *text[ML_MAX];
    lv_obj_t      *value[ML_MAX];
    lv_obj_t      *arrow[ML_MAX];
    menu_item_cb_t cb[ML_MAX];
    void          *user[ML_MAX];
    int            count;
    int            sel;
    int            item_h;
    int            viewport_h;
    int            width;
    int            scroll_y;
};

static void strip_decoration(lv_obj_t *o) {
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void make_highlight(MenuList *ml) {
    ml->highlight = lv_obj_create(ml->content);
    lv_obj_set_size(ml->highlight, ml->width - 4, ml->item_h - 4);
    lv_obj_set_pos(ml->highlight, 2, 2);
    lv_obj_set_style_bg_color(ml->highlight, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_bg_opa(ml->highlight, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ml->highlight, 0, 0);
    lv_obj_set_style_radius(ml->highlight, 4, 0);
    lv_obj_clear_flag(ml->highlight, LV_OBJ_FLAG_SCROLLABLE);
}

MenuList *menu_list_create(lv_obj_t *parent, int top_y, int height) {
    MenuList *ml = (MenuList *)calloc(1, sizeof(MenuList));
    ml->count      = 0;
    ml->sel        = 0;
    ml->item_h     = MENU_ITEM_H;
    ml->viewport_h = height;
    ml->width      = LCD_WIDTH;

    ml->viewport = lv_obj_create(parent);
    lv_obj_set_size(ml->viewport, ml->width, height);
    lv_obj_set_pos(ml->viewport, 0, top_y);
    strip_decoration(ml->viewport);

    ml->content = lv_obj_create(ml->viewport);
    lv_obj_set_size(ml->content, ml->width, height);
    lv_obj_set_pos(ml->content, 0, 0);
    strip_decoration(ml->content);

    make_highlight(ml);
    return ml;
}

void menu_list_clear(MenuList *ml) {
    lv_obj_clean(ml->content);     // deletes the highlight + all item widgets
    memset(ml->icon,  0, sizeof(ml->icon));
    memset(ml->text,  0, sizeof(ml->text));
    memset(ml->value, 0, sizeof(ml->value));
    memset(ml->arrow, 0, sizeof(ml->arrow));
    ml->count    = 0;
    ml->sel      = 0;
    ml->scroll_y = 0;
    lv_obj_set_y(ml->content, 0);
    make_highlight(ml);
}

void menu_list_destroy(MenuList *ml) {
    if (ml) free(ml);
}

static void style_item(MenuList *ml, int i, bool selected) {
    lv_color_t label_c = selected ? theme_clr(CLR_TEXT_DARK) : theme_clr(CLR_TEXT);
    lv_color_t dim_c   = selected ? theme_clr(CLR_TEXT_DARK) : theme_clr(CLR_TEXT_DIM);
    lv_obj_set_style_text_color(ml->text[i], label_c, 0);
    if (ml->icon[i])  lv_obj_set_style_text_color(ml->icon[i],
                          selected ? theme_clr(CLR_TEXT_DARK) : theme_clr(CLR_ACCENT), 0);
    if (ml->value[i]) lv_obj_set_style_text_color(ml->value[i], dim_c, 0);
    if (ml->arrow[i]) lv_obj_set_style_text_color(ml->arrow[i], dim_c, 0);
}

static void refresh(MenuList *ml, bool animate) {
    for (int i = 0; i < ml->count; i++) style_item(ml, i, i == ml->sel);

    int vis = ml->viewport_h / ml->item_h;
    if (vis < 1) vis = 1;
    int first = -ml->scroll_y / ml->item_h;
    if (ml->sel < first)             first = ml->sel;
    else if (ml->sel >= first + vis) first = ml->sel - vis + 1;
    int target_y = -first * ml->item_h;
    int hi_y     = ml->sel * ml->item_h + 2;
    ml->scroll_y = target_y;

    if (animate) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ml->highlight);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_values(&a, lv_obj_get_y(ml->highlight), hi_y);
        lv_anim_set_duration(&a, ML_ANIM_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);

        lv_anim_t s;
        lv_anim_init(&s);
        lv_anim_set_var(&s, ml->content);
        lv_anim_set_exec_cb(&s, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_values(&s, lv_obj_get_y(ml->content), target_y);
        lv_anim_set_duration(&s, ML_ANIM_MS);
        lv_anim_set_path_cb(&s, lv_anim_path_ease_out);
        lv_anim_start(&s);
    } else {
        lv_obj_set_y(ml->highlight, hi_y);
        lv_obj_set_y(ml->content, target_y);
    }
}

void menu_list_add(MenuList *ml, const MenuItem *it) {
    if (ml->count >= ML_MAX) return;
    int i = ml->count;
    int y = i * ml->item_h;

    if (it->icon) {
        ml->icon[i] = lv_label_create(ml->content);
        lv_label_set_text(ml->icon[i], it->icon);
        lv_obj_set_style_text_font(ml->icon[i], FONT_NORMAL, 0);
        lv_obj_set_pos(ml->icon[i], ICON_X, y + (ml->item_h - 16) / 2);
    }

    ml->text[i] = lv_label_create(ml->content);
    lv_label_set_text(ml->text[i], it->label);
    lv_obj_set_style_text_font(ml->text[i], FONT_MEDIUM, 0);
    lv_obj_set_pos(ml->text[i], it->icon ? LABEL_X_ICON : LABEL_X_PLAIN,
                   y + (ml->item_h - 18) / 2);

    if (it->value) {
        ml->value[i] = lv_label_create(ml->content);
        lv_label_set_text(ml->value[i], it->value);
        lv_obj_set_style_text_font(ml->value[i], FONT_SMALL, 0);
        lv_obj_align(ml->value[i], LV_ALIGN_TOP_RIGHT, -EDGE_PAD,
                     y + (ml->item_h - 14) / 2);
    } else if (it->cb) {
        ml->arrow[i] = lv_label_create(ml->content);
        lv_label_set_text(ml->arrow[i], LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(ml->arrow[i], FONT_SMALL, 0);
        lv_obj_align(ml->arrow[i], LV_ALIGN_TOP_RIGHT, -EDGE_PAD,
                     y + (ml->item_h - 12) / 2);
    }

    ml->cb[i]   = it->cb;
    ml->user[i] = it->user;
    ml->count++;

    lv_obj_set_height(ml->content,
                      ml->count * ml->item_h > ml->viewport_h
                          ? ml->count * ml->item_h
                          : ml->viewport_h);
    refresh(ml, false);
}

void menu_list_up(MenuList *ml) {
    if (ml->count == 0) return;
    ml->sel = (ml->sel - 1 + ml->count) % ml->count;  // wrap around
    refresh(ml, true);
}

void menu_list_down(MenuList *ml) {
    if (ml->count == 0) return;
    ml->sel = (ml->sel + 1) % ml->count;
    refresh(ml, true);
}

void menu_list_activate(MenuList *ml) {
    if (ml->count == 0) return;
    int i = ml->sel;
    if (ml->cb[i]) ml->cb[i](ml->user[i]);
}

int menu_list_selected(const MenuList *ml) { return ml->sel; }
int menu_list_count(const MenuList *ml)    { return ml->count; }

void menu_list_set_value(MenuList *ml, int index, const char *value) {
    if (index < 0 || index >= ml->count || !ml->value[index]) return;
    lv_label_set_text(ml->value[index], value);
    int y = index * ml->item_h;
    lv_obj_align(ml->value[index], LV_ALIGN_TOP_RIGHT, -EDGE_PAD,
                 y + (ml->item_h - 14) / 2);
}
