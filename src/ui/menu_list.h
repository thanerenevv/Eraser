#pragma once

#include <lvgl.h>

typedef void (*menu_item_cb_t)(void *user);

typedef struct {
    const char     *icon;   // LV_SYMBOL_* or NULL
    const char     *label;
    const char     *value;  // right-aligned value text, or NULL
    menu_item_cb_t  cb;      // invoked when the item is activated, or NULL
    void           *user;    // passed to cb
} MenuItem;

typedef struct MenuList MenuList;

MenuList *menu_list_create(lv_obj_t *parent, int top_y, int height);
void      menu_list_destroy(MenuList *ml);

void menu_list_add(MenuList *ml, const MenuItem *item);
void menu_list_clear(MenuList *ml);

void menu_list_up(MenuList *ml);
void menu_list_down(MenuList *ml);
void menu_list_activate(MenuList *ml);
int  menu_list_selected(const MenuList *ml);
int  menu_list_count(const MenuList *ml);

void menu_list_set_value(MenuList *ml, int index, const char *value);
