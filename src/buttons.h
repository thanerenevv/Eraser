#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BTN_ID_UP = 0,
    BTN_ID_DOWN,
    BTN_ID_BACK,
    BTN_ID_OK,
    BTN_ID_EXTRA,
    BTN_ID_COUNT
} BtnId;

typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_PRESS,
    BTN_EVT_REPEAT,
    BTN_EVT_HOLD
} BtnEvent;

typedef struct {
    BtnId  id;
    BtnEvent event;
} BtnState;

void     buttons_init(void);
void     buttons_tick(void);
BtnEvent button_get_event(BtnId id);
bool     button_is_held(BtnId id);
