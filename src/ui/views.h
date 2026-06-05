#pragma once

#include "nav.h"
#include "menu_list.h"

AppView *list_view_create(const char *title, const MenuItem *items, int count);

AppView *info_view_create(const char *title, const char *body);

typedef struct {
    const char        *icon;
    const char        *title;
    const char        *idle_hint;
    const char        *active_verb;
    const char        *unit;
    int                per_tick;
    uint32_t           rate_ms;
    const char *const *modes;
    int                mode_count;
    const char        *mode_label;
    bool               warn;
    bool               autostart;
} ActivityCfg;

AppView *activity_view_create(const ActivityCfg *cfg);

typedef void (*sim_gen_fn)(int seq,
                           char *label, int label_sz,
                           char *value, int value_sz,
                           char *detail, int detail_sz);

AppView *sim_scanner_create(const char *title, const char *icon,
                            const char *restart_label, const char *unit,
                            uint32_t interval_ms, sim_gen_fn gen);
