#include <Arduino.h>
#include <lvgl.h>
#include <esp_timer.h>

#include "config.h"
#include "display.h"
#include "buttons.h"
#include "settings_store.h"
#include "rf_hal.h"
#include "ui/nav.h"
#include "ui/menu.h"
#include "ui/boot_screen.h"

static uint32_t g_last_activity_ms = 0;
static bool     g_display_asleep   = false;

static void lvgl_tick_cb(void *arg) {
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_tick_init(void) {
    const esp_timer_create_args_t args = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick"
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000ULL));
}

static void wake_display(void) {
    g_display_asleep   = false;
    g_last_activity_ms = millis();
    display_set_brightness(settings_brightness());
}

static void process_buttons(void) {
    buttons_tick();

    BtnEvent ev_up    = button_get_event(BTN_ID_UP);
    BtnEvent ev_down  = button_get_event(BTN_ID_DOWN);
    BtnEvent ev_ok    = button_get_event(BTN_ID_OK);
    BtnEvent ev_back  = button_get_event(BTN_ID_BACK);
    BtnEvent ev_extra = button_get_event(BTN_ID_EXTRA);

    bool any = ev_up || ev_down || ev_ok || ev_back || ev_extra;

    if (any) {
        g_last_activity_ms = millis();
        if (g_display_asleep) {
            wake_display();
            return;
        }
    }

    if (ev_up    == BTN_EVT_PRESS || ev_up    == BTN_EVT_REPEAT) nav_dispatch_up();
    if (ev_down  == BTN_EVT_PRESS || ev_down  == BTN_EVT_REPEAT) nav_dispatch_down();
    if (ev_extra == BTN_EVT_PRESS || ev_extra == BTN_EVT_REPEAT) nav_dispatch_extra();
    if (ev_ok    == BTN_EVT_PRESS)                                nav_dispatch_ok();
    if (ev_back  == BTN_EVT_PRESS)                                nav_dispatch_back();
    else if (ev_back == BTN_EVT_HOLD)                             nav_home();
}

static void check_display_timeout(void) {
    uint8_t to_min = settings_display_timeout();
    if (to_min == 0 || g_display_asleep) return;
    if ((millis() - g_last_activity_ms) >= (uint32_t)to_min * 60000UL) {
        g_display_asleep = true;
        display_set_brightness(0);
    }
}

void setup(void) {
    Serial.begin(115200);

    lv_init();
    lvgl_tick_init();

    display_init();
    settings_load();
    buttons_init();
    rf_hal_init();

    g_last_activity_ms = millis();

    nav_init();
    nav_set_root(boot_screen_create());
}

void loop(void) {
    process_buttons();
    check_display_timeout();
    uint32_t delay_ms = lv_timer_handler();
    if (delay_ms > 5) delay_ms = 5;
    delay(delay_ms);
}
