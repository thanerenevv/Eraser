#include "buttons.h"
#include "config.h"
#include <Arduino.h>

static const int btn_pins[BTN_ID_COUNT] = {
    BTN_UP, BTN_DOWN, BTN_BACK, BTN_OK, BTN_EXTRA
};

static struct {
    bool     last_raw;
    bool     current;
    bool     hold_fired;
    uint32_t press_time;
    uint32_t last_change;
    uint32_t next_repeat;
    BtnEvent event;
} btn_state[BTN_ID_COUNT];

void buttons_init(void) {
    for (int i = 0; i < BTN_ID_COUNT; i++) {
        pinMode(btn_pins[i], INPUT_PULLUP);
        btn_state[i].last_raw    = true;
        btn_state[i].current     = false;
        btn_state[i].hold_fired  = false;
        btn_state[i].press_time  = 0;
        btn_state[i].last_change = 0;
        btn_state[i].next_repeat = 0;
        btn_state[i].event       = BTN_EVT_NONE;
    }
}

void buttons_tick(void) {
    uint32_t now = millis();

    for (int i = 0; i < BTN_ID_COUNT; i++) {
        bool raw = !digitalRead(btn_pins[i]);
        btn_state[i].event = BTN_EVT_NONE;

        if (raw != btn_state[i].last_raw) {
            btn_state[i].last_change = now;
            btn_state[i].last_raw    = raw;
        }

        if ((now - btn_state[i].last_change) < BTN_DEBOUNCE_MS) {
            continue;
        }

        if (raw && !btn_state[i].current) {
            btn_state[i].current     = true;
            btn_state[i].hold_fired  = false;
            btn_state[i].press_time  = now;
            btn_state[i].next_repeat = now + BTN_REPEAT_DELAY_MS;
            btn_state[i].event       = BTN_EVT_PRESS;
        } else if (!raw && btn_state[i].current) {
            btn_state[i].current    = false;
            btn_state[i].hold_fired = false;
        } else if (raw && btn_state[i].current) {
            if (!btn_state[i].hold_fired &&
                (now - btn_state[i].press_time) >= BTN_HOLD_MS) {
                btn_state[i].hold_fired = true;
                btn_state[i].event      = BTN_EVT_HOLD;
            } else if ((int32_t)(now - btn_state[i].next_repeat) >= 0) {
                btn_state[i].next_repeat = now + BTN_REPEAT_RATE_MS;
                btn_state[i].event       = BTN_EVT_REPEAT;
            }
        }
    }
}

BtnEvent button_get_event(BtnId id) {
    BtnEvent e = btn_state[id].event;
    btn_state[id].event = BTN_EVT_NONE;
    return e;
}

bool button_is_held(BtnId id) {
    return btn_state[id].current;
}
