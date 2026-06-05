#include "gpio_screen.h"
#include "eth_screen.h"
#include "views.h"
#include "screen_base.h"
#include "menu_list.h"
#include "theme.h"
#include "nav.h"
#include "../config.h"
#include <lvgl.h>
#include <Arduino.h>
#include <DHTesp.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const int k_gpio_pins[GPIO_EXPO_COUNT] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
    GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6,
};

typedef enum {
    GM_INPUT = 0,
    GM_OUTPUT,
    GM_DHT11,
    GM_DHT22,
    GM_ULTRASONIC,
    GM_DS18B20,
    GM_PIR,
    GM_TONE,
    GM_SERVO,
    GM_COUNT,
} GpioMode;

static const char *k_mode_names[GM_COUNT] = {
    "Input", "Output", "DHT11", "DHT22",
    "HC-SR04", "DS18B20", "PIR", "Tone", "Servo",
};

typedef struct {
    GpioMode mode;
    bool     out_state;
    int      servo_angle;
    int      tone_idx;
} GpioPinState;

static GpioPinState g_pin_state[GPIO_EXPO_COUNT];
static bool         g_state_inited = false;

static void state_init(void) {
    if (g_state_inited) return;
    for (int i = 0; i < GPIO_EXPO_COUNT; i++) {
        g_pin_state[i].mode        = GM_INPUT;
        g_pin_state[i].out_state   = false;
        g_pin_state[i].servo_angle = 90;
        g_pin_state[i].tone_idx    = 0;
    }
    g_state_inited = true;
}

static volatile unsigned long g_echo_start_us = 0;
static volatile unsigned long g_echo_end_us   = 0;
static volatile bool          g_echo_ready    = false;
static int                    g_echo_gpio     = -1;

static void IRAM_ATTR echo_isr(void) {
    if (digitalRead(g_echo_gpio) == HIGH) {
        g_echo_start_us = micros();
        g_echo_ready    = false;
    } else {
        g_echo_end_us = micros();
        g_echo_ready  = true;
    }
}

static void servo_write_angle(int gpio, int angle) {
    uint32_t duty = 1638UL + (uint32_t)((uint32_t)angle * 6554UL / 180UL);
    ledcWrite(gpio, duty);
}

static const uint32_t k_tone_freqs[4] = { 262, 440, 523, 880 };
static const char    *k_tone_names[4] = { "C4  262 Hz", "A4  440 Hz", "C5  523 Hz", "A5  880 Hz" };

static bool is_adc_pin(int gpio) {
    return gpio >= 1 && gpio <= 10;
}

typedef struct {
    int               idx;
    lv_obj_t         *mode_lbl;
    lv_obj_t         *data_lbl;
    lv_obj_t         *hint_lbl;
    lv_timer_t       *poll;
    DHTesp           *dht;
    OneWire          *ds_ow;
    DallasTemperature *ds;
} PinCtx;

static void pin_cleanup_mode(PinCtx *c, GpioMode mode) {
    int gpio = k_gpio_pins[c->idx];
    switch (mode) {
        case GM_ULTRASONIC: {
            int echo_gpio = k_gpio_pins[(c->idx + 1) % GPIO_EXPO_COUNT];
            detachInterrupt(digitalPinToInterrupt(echo_gpio));
            if (g_echo_gpio == echo_gpio) g_echo_gpio = -1;
            break;
        }
        case GM_TONE:
            noTone(gpio);
            break;
        case GM_SERVO:
            ledcDetach(gpio);
            break;
        default:
            break;
    }
}

static void pin_apply(PinCtx *c) {
    if (c->dht)   { delete c->dht;   c->dht   = nullptr; }
    if (c->ds)    { delete c->ds;    c->ds    = nullptr; }
    if (c->ds_ow) { delete c->ds_ow; c->ds_ow = nullptr; }

    int gpio        = k_gpio_pins[c->idx];
    GpioPinState *s = &g_pin_state[c->idx];
    uint32_t ms     = 500;

    switch (s->mode) {
        case GM_INPUT:
            pinMode(gpio, INPUT);
            lv_label_set_text(c->hint_lbl,
                LV_SYMBOL_UP " / " LV_SYMBOL_DOWN "  change mode");
            ms = 300;
            break;

        case GM_OUTPUT:
            pinMode(gpio, OUTPUT);
            digitalWrite(gpio, s->out_state ? HIGH : LOW);
            lv_label_set_text(c->hint_lbl,
                "OK: toggle  " LV_SYMBOL_UP "/" LV_SYMBOL_DOWN " mode");
            ms = 300;
            break;

        case GM_DHT11:
        case GM_DHT22:
            c->dht = new DHTesp();
            if (c->dht)
                c->dht->setup(gpio,
                    s->mode == GM_DHT11 ? DHTesp::DHT11 : DHTesp::DHT22);
            lv_label_set_text(c->hint_lbl,
                LV_SYMBOL_UP " / " LV_SYMBOL_DOWN "  change mode");
            ms = 2000;
            break;

        case GM_ULTRASONIC: {
            int echo_idx  = (c->idx + 1) % GPIO_EXPO_COUNT;
            int echo_gpio = k_gpio_pins[echo_idx];
            g_echo_gpio   = echo_gpio;
            g_echo_ready  = false;
            pinMode(gpio, OUTPUT);
            digitalWrite(gpio, LOW);
            pinMode(echo_gpio, INPUT);
            attachInterrupt(digitalPinToInterrupt(echo_gpio), echo_isr, CHANGE);
            char hint[48];
            snprintf(hint, sizeof(hint), "ECHO: GPIO %d  " LV_SYMBOL_UP "/" LV_SYMBOL_DOWN " mode", echo_gpio);
            lv_label_set_text(c->hint_lbl, hint);
            ms = 500;
            break;
        }

        case GM_DS18B20:
            c->ds_ow = new OneWire(gpio);
            if (c->ds_ow) {
                c->ds = new DallasTemperature(c->ds_ow);
                if (c->ds) {
                    c->ds->begin();
                    c->ds->setWaitForConversion(false);
                    c->ds->requestTemperatures();
                }
            }
            lv_label_set_text(c->hint_lbl,
                "1-Wire  " LV_SYMBOL_UP "/" LV_SYMBOL_DOWN " mode");
            ms = 1500;
            break;

        case GM_PIR:
            pinMode(gpio, INPUT);
            lv_label_set_text(c->hint_lbl,
                LV_SYMBOL_UP " / " LV_SYMBOL_DOWN "  change mode");
            ms = 200;
            break;

        case GM_TONE:
            tone(gpio, k_tone_freqs[s->tone_idx]);
            lv_label_set_text(c->hint_lbl,
                "OK: cycle freq  " LV_SYMBOL_UP "/" LV_SYMBOL_DOWN " mode");
            ms = 500;
            break;

        case GM_SERVO:
            ledcAttach(gpio, 50, 16);
            servo_write_angle(gpio, s->servo_angle);
            lv_label_set_text(c->hint_lbl,
                "OK: +10°  " LV_SYMBOL_UP "/" LV_SYMBOL_DOWN " mode");
            ms = 200;
            break;

        default:
            break;
    }

    lv_label_set_text(c->mode_lbl, k_mode_names[s->mode]);
    lv_label_set_text(c->data_lbl, "---");
    if (c->poll) lv_timer_set_period(c->poll, ms);
}

static void pin_poll(lv_timer_t *t) {
    PinCtx *c       = (PinCtx *)lv_timer_get_user_data(t);
    int gpio        = k_gpio_pins[c->idx];
    GpioPinState *s = &g_pin_state[c->idx];
    char buf[64];

    switch (s->mode) {
        case GM_INPUT:
            if (is_adc_pin(gpio)) {
                snprintf(buf, sizeof(buf), "%s\nADC: %d",
                    digitalRead(gpio) ? "HIGH" : "LOW", analogRead(gpio));
            } else {
                snprintf(buf, sizeof(buf), "%s", digitalRead(gpio) ? "HIGH" : "LOW");
            }
            lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
            lv_label_set_text(c->data_lbl, buf);
            break;

        case GM_OUTPUT:
            lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
            lv_label_set_text(c->data_lbl, s->out_state ? "HIGH" : "LOW");
            break;

        case GM_DHT11:
        case GM_DHT22:
            if (c->dht) {
                TempAndHumidity r = c->dht->getTempAndHumidity();
                if (!isnan(r.temperature) && !isnan(r.humidity)) {
                    snprintf(buf, sizeof(buf), "%.1f °C\n%.1f %%RH",
                        r.temperature, r.humidity);
                } else {
                    snprintf(buf, sizeof(buf), "Sensor error\nCheck wiring");
                }
                lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
                lv_label_set_text(c->data_lbl, buf);
            }
            break;

        case GM_ULTRASONIC:
            digitalWrite(gpio, HIGH);
            delayMicroseconds(10);
            digitalWrite(gpio, LOW);
            if (g_echo_ready) {
                float cm = (float)(g_echo_end_us - g_echo_start_us) * 0.01715f;
                snprintf(buf, sizeof(buf), "%.1f cm\n%.1f in", cm, cm / 2.54f);
            } else {
                snprintf(buf, sizeof(buf), "Measuring...");
            }
            lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
            lv_label_set_text(c->data_lbl, buf);
            break;

        case GM_DS18B20:
            if (c->ds) {
                float temp = c->ds->getTempCByIndex(0);
                c->ds->requestTemperatures();
                if (temp > -100.0f) {
                    snprintf(buf, sizeof(buf), "%.2f °C\n%.2f °F",
                        temp, temp * 1.8f + 32.0f);
                    lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
                } else {
                    snprintf(buf, sizeof(buf), "No sensor\nCheck wiring");
                    lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT_DIM), 0);
                }
                lv_label_set_text(c->data_lbl, buf);
            }
            break;

        case GM_PIR: {
            bool motion = (digitalRead(gpio) == HIGH);
            lv_obj_set_style_text_color(c->data_lbl,
                theme_clr(motion ? CLR_WARNING : CLR_TEXT_DIM), 0);
            lv_label_set_text(c->data_lbl,
                motion ? LV_SYMBOL_BELL " MOTION!" : "Idle");
            break;
        }

        case GM_TONE:
            lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_ACCENT), 0);
            lv_label_set_text(c->data_lbl, k_tone_names[s->tone_idx]);
            break;

        case GM_SERVO:
            snprintf(buf, sizeof(buf), "%d°", s->servo_angle);
            lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
            lv_label_set_text(c->data_lbl, buf);
            break;

        default:
            break;
    }
}

static void pin_on_up(AppView *v) {
    PinCtx *c       = (PinCtx *)v->ctx;
    GpioPinState *s = &g_pin_state[c->idx];
    pin_cleanup_mode(c, s->mode);
    s->mode = (GpioMode)((s->mode + GM_COUNT - 1) % GM_COUNT);
    pin_apply(c);
}

static void pin_on_down(AppView *v) {
    PinCtx *c       = (PinCtx *)v->ctx;
    GpioPinState *s = &g_pin_state[c->idx];
    pin_cleanup_mode(c, s->mode);
    s->mode = (GpioMode)((s->mode + 1) % GM_COUNT);
    pin_apply(c);
}

static void pin_on_ok(AppView *v) {
    PinCtx *c       = (PinCtx *)v->ctx;
    GpioPinState *s = &g_pin_state[c->idx];
    int gpio        = k_gpio_pins[c->idx];
    char buf[16];

    switch (s->mode) {
        case GM_OUTPUT:
            s->out_state = !s->out_state;
            digitalWrite(gpio, s->out_state ? HIGH : LOW);
            lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
            lv_label_set_text(c->data_lbl, s->out_state ? "HIGH" : "LOW");
            break;

        case GM_TONE:
            s->tone_idx = (s->tone_idx + 1) % 4;
            tone(gpio, k_tone_freqs[s->tone_idx]);
            lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_ACCENT), 0);
            lv_label_set_text(c->data_lbl, k_tone_names[s->tone_idx]);
            break;

        case GM_SERVO:
            s->servo_angle = (s->servo_angle + 10) % 190;
            servo_write_angle(gpio, s->servo_angle);
            snprintf(buf, sizeof(buf), "%d°", s->servo_angle);
            lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
            lv_label_set_text(c->data_lbl, buf);
            break;

        default:
            break;
    }
}

static void pin_on_destroy(AppView *v) {
    PinCtx *c       = (PinCtx *)v->ctx;
    GpioPinState *s = &g_pin_state[c->idx];
    pin_cleanup_mode(c, s->mode);
    if (c->poll)  lv_timer_delete(c->poll);
    if (c->dht)   { delete c->dht;   c->dht   = nullptr; }
    if (c->ds)    { delete c->ds;    c->ds    = nullptr; }
    if (c->ds_ow) { delete c->ds_ow; c->ds_ow = nullptr; }
    free(c);
}

static AppView *pin_view_create(int idx) {
    state_init();
    char title[12];
    snprintf(title, sizeof(title), "GPIO %d", k_gpio_pins[idx]);

    AppView *v  = app_view_new();
    v->root     = screen_base_create(title, true);
    PinCtx *c   = (PinCtx *)calloc(1, sizeof(PinCtx));
    c->idx      = idx;

    lv_obj_t *icon = lv_label_create(v->root);
    lv_label_set_text(icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(icon, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(icon, FONT_LARGE, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 4);

    c->mode_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->mode_lbl, theme_clr(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(c->mode_lbl, FONT_MEDIUM, 0);
    lv_obj_set_style_text_align(c->mode_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(c->mode_lbl, LCD_WIDTH);
    lv_obj_align(c->mode_lbl, LV_ALIGN_TOP_MID, 0, SCREEN_CONTENT_TOP + 30);

    c->data_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->data_lbl, theme_clr(CLR_TEXT), 0);
    lv_obj_set_style_text_font(c->data_lbl, FONT_LARGE, 0);
    lv_obj_set_style_text_align(c->data_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(c->data_lbl, LCD_WIDTH - 20);
    lv_obj_align(c->data_lbl, LV_ALIGN_CENTER, 0, 8);

    c->hint_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->hint_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->hint_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_align(c->hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(c->hint_lbl, LCD_WIDTH);
    lv_obj_align(c->hint_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

    v->ctx        = c;
    v->on_up      = pin_on_up;
    v->on_down    = pin_on_down;
    v->on_ok      = pin_on_ok;
    v->on_destroy = pin_on_destroy;

    c->poll = lv_timer_create(pin_poll, 300, c);
    pin_apply(c);
    return v;
}

static void open_pin(void *u) {
    nav_push(pin_view_create((int)(intptr_t)u));
}

static void open_eth(void *u) {
    (void)u;
    nav_push(eth_view_create());
}

AppView *gpio_view_create(void) {
    state_init();
    static const MenuItem items[] = {
        { LV_SYMBOL_CHARGE, "GPIO 1",        NULL,  open_pin, (void *)(intptr_t)0 },
        { LV_SYMBOL_CHARGE, "GPIO 2",        NULL,  open_pin, (void *)(intptr_t)1 },
        { LV_SYMBOL_CHARGE, "GPIO 3",        NULL,  open_pin, (void *)(intptr_t)2 },
        { LV_SYMBOL_CHARGE, "GPIO 45",       NULL,  open_pin, (void *)(intptr_t)3 },
        { LV_SYMBOL_CHARGE, "GPIO 46",       NULL,  open_pin, (void *)(intptr_t)4 },
        { LV_SYMBOL_CHARGE, "GPIO 47",       NULL,  open_pin, (void *)(intptr_t)5 },
        { LV_SYMBOL_CHARGE, "GPIO 48",       NULL,  open_pin, (void *)(intptr_t)6 },
        { LV_SYMBOL_LIST,   "W5500 Ethernet","SPI",  open_eth, NULL               },
    };
    return list_view_create("GPIO", items, 8);
}
