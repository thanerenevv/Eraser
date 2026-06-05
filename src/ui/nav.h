#pragma once

#include <lvgl.h>

typedef struct AppView AppView;
struct AppView {
    lv_obj_t *root;
    void (*on_up)(AppView *self);
    void (*on_down)(AppView *self);
    void (*on_ok)(AppView *self);
    bool (*on_back)(AppView *self);
    void (*on_extra)(AppView *self);
    void (*on_destroy)(AppView *self);
    void *ctx;
};

AppView *app_view_new(void);

void nav_init(void);
void nav_set_root(AppView *root);
void nav_push(AppView *v);
void nav_pop(void);
void nav_home(void);
int  nav_depth(void);

AppView *nav_top(void);

void nav_dispatch_up(void);
void nav_dispatch_down(void);
void nav_dispatch_ok(void);
void nav_dispatch_back(void);
void nav_dispatch_extra(void);
