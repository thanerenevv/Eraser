#pragma once

#include "nav.h"

typedef void (*input_done_cb_t)(const char *text, void *user);

AppView *input_view_create(const char *title, const char *hint, int max_len,
                            input_done_cb_t on_done, void *user);
