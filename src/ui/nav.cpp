#include "nav.h"
#include "../config.h"
#include <stdlib.h>

#define NAV_ANIM_MS 240

static AppView *stack[NAV_STACK_MAX];
static int      top = -1;

AppView *app_view_new(void) {
    return (AppView *)calloc(1, sizeof(AppView));
}

static void view_free(AppView *v) {
    if (!v) return;
    if (v->on_destroy) v->on_destroy(v);
    free(v);
}

void nav_init(void) {
    for (int i = 0; i <= top; i++) view_free(stack[i]);
    top = -1;
}

void nav_set_root(AppView *root) {
    nav_init();
    stack[++top] = root;
    lv_scr_load(root->root);
}

void nav_push(AppView *v) {
    if (!v) return;
    if (top >= NAV_STACK_MAX - 1) { view_free(v); return; }
    stack[++top] = v;
    lv_scr_load_anim(v->root, LV_SCR_LOAD_ANIM_MOVE_LEFT, NAV_ANIM_MS, 0, false);
}

void nav_pop(void) {
    if (top <= 0) return;
    AppView *leaving = stack[top--];
    AppView *prev    = stack[top];
    lv_scr_load_anim(prev->root, LV_SCR_LOAD_ANIM_MOVE_RIGHT, NAV_ANIM_MS, 0, true);
    if (leaving->on_destroy) leaving->on_destroy(leaving);
    free(leaving);
}

void nav_home(void) {
    if (top <= 0) return;
    AppView *active = stack[top];
    for (int i = top - 1; i >= 1; i--) {
        if (stack[i]->on_destroy) stack[i]->on_destroy(stack[i]);
        lv_obj_delete(stack[i]->root);
        free(stack[i]);
    }
    if (active->on_destroy) active->on_destroy(active);
    lv_scr_load_anim(stack[0]->root, LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                     NAV_ANIM_MS, 0, true);
    free(active);
    top = 0;
}

int nav_depth(void) { return top + 1; }

AppView *nav_top(void) { return (top >= 0) ? stack[top] : nullptr; }

void nav_dispatch_up(void) {
    AppView *v = nav_top();
    if (v && v->on_up) v->on_up(v);
}

void nav_dispatch_down(void) {
    AppView *v = nav_top();
    if (v && v->on_down) v->on_down(v);
}

void nav_dispatch_ok(void) {
    AppView *v = nav_top();
    if (v && v->on_ok) v->on_ok(v);
}

void nav_dispatch_back(void) {
    AppView *v = nav_top();
    if (!v) return;
    if (v->on_back && v->on_back(v)) return;
    nav_pop();
}

void nav_dispatch_extra(void) {
    AppView *v = nav_top();
    if (v && v->on_extra) v->on_extra(v);
}
