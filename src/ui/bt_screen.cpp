#include "bt_screen.h"
#include "views.h"
#include "screen_base.h"
#include "menu_list.h"
#include "theme.h"
#include "../config.h"
#include <lvgl.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BLE_MAX_RESULTS  12
#define BLE_SCAN_SECS    8

static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

struct BleEntry {
    char    name[33];
    char    addr[18];
    int8_t  rssi;
    bool    connectable;
    bool    valid;
};

static BleEntry          g_results[BLE_MAX_RESULTS];
static volatile int      g_result_count = 0;
static volatile bool     g_scan_done    = false;
static volatile bool     g_scan_active  = false;
static bool              g_ble_inited   = false;
static TaskHandle_t      g_scan_task    = NULL;

class BleCallback : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        portENTER_CRITICAL(&g_mux);
        int i = g_result_count;
        bool room = (i < BLE_MAX_RESULTS);
        portEXIT_CRITICAL(&g_mux);

        if (!room) return;

        BleEntry e;
        strncpy(e.name, dev.haveName() ? dev.getName().c_str() : "<unknown>", 32);
        e.name[32] = '\0';
        strncpy(e.addr, dev.getAddress().toString().c_str(), 17);
        e.addr[17] = '\0';
        e.rssi        = (int8_t)dev.getRSSI();
        e.connectable = dev.isConnectable();
        e.valid       = true;

        portENTER_CRITICAL(&g_mux);
        if (g_result_count < BLE_MAX_RESULTS) {
            int idx = g_result_count;
            g_results[idx] = e;
            g_result_count = idx + 1;
        }
        portEXIT_CRITICAL(&g_mux);
    }
};

static BleCallback g_ble_cb;

static void ble_scan_task(void *) {
    BLEDevice::getScan()->start(BLE_SCAN_SECS, false);
    g_scan_done   = true;
    g_scan_active = false;
    g_scan_task   = NULL;
    vTaskDelete(NULL);
}

static void ble_ensure_init(void) {
    if (g_ble_inited) return;
    BLEDevice::init("RF-MULTITOOL");
    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&g_ble_cb);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    g_ble_inited = true;
}

static void ble_start_scan(void) {
    if (g_scan_active) return;
    ble_ensure_init();

    portENTER_CRITICAL(&g_mux);
    g_result_count = 0;
    g_scan_done    = false;
    g_scan_active  = true;
    memset(g_results, 0, sizeof(g_results));
    portEXIT_CRITICAL(&g_mux);

    BLEDevice::getScan()->clearResults();
    xTaskCreatePinnedToCore(ble_scan_task, "ble_scan", 4096, NULL, 1, &g_scan_task, 0);
}

static void ble_stop_scan(void) {
    if (!g_scan_active) return;
    BLEDevice::getScan()->stop();
    portENTER_CRITICAL(&g_mux);
    g_scan_active = false;
    g_scan_task   = NULL;
    portEXIT_CRITICAL(&g_mux);
}

typedef struct {
    MenuList    *list;
    lv_obj_t    *status_lbl;
    lv_timer_t  *poll;
    int          displayed;
    bool         scan_started;
} BleCtx;

static char g_ble_detail[BLE_MAX_RESULTS][128];

static void ble_open_detail(void *u) {
    int idx = (int)(intptr_t)u;
    if (idx >= 0 && idx < BLE_MAX_RESULTS && g_results[idx].valid)
        nav_push(info_view_create("BLE DEVICE", g_ble_detail[idx]));
}

static void ble_rescan(void *u) {
    BleCtx *c = (BleCtx *)u;
    c->displayed  = 0;
    c->scan_started = false;
    menu_list_clear(c->list);
    lv_label_set_text(c->status_lbl, LV_SYMBOL_REFRESH " Scanning...");
    ble_start_scan();
}

static void ble_poll(lv_timer_t *t) {
    BleCtx *c = (BleCtx *)lv_timer_get_user_data(t);

    if (!c->scan_started) {
        c->scan_started = true;
        ble_start_scan();
        lv_label_set_text(c->status_lbl, LV_SYMBOL_BLUETOOTH " Scanning...");
    }

    int count;
    portENTER_CRITICAL(&g_mux);
    count = g_result_count;
    portEXIT_CRITICAL(&g_mux);

    static char lbl[BLE_MAX_RESULTS][33];
    static char val[BLE_MAX_RESULTS][10];

    while (c->displayed < count) {
        int i = c->displayed;
        BleEntry e;
        portENTER_CRITICAL(&g_mux);
        e = g_results[i];
        portEXIT_CRITICAL(&g_mux);

        strncpy(lbl[i], e.name, 32);
        lbl[i][32] = '\0';
        snprintf(val[i], sizeof(val[i]), "%d dBm", (int)e.rssi);
        snprintf(g_ble_detail[i], sizeof(g_ble_detail[i]),
                 "Name: %s\nMAC: %s\nRSSI: %d dBm\nConnectable: %s",
                 e.name, e.addr, (int)e.rssi, e.connectable ? "yes" : "no");

        MenuItem it = { LV_SYMBOL_BLUETOOTH, lbl[i], val[i],
                        ble_open_detail, (void *)(intptr_t)i };
        menu_list_add(c->list, &it);
        c->displayed++;
    }

    if (g_scan_done && count == c->displayed) {
        lv_label_set_text_fmt(c->status_lbl, LV_SYMBOL_OK " %d devices", count);
    } else {
        lv_label_set_text_fmt(c->status_lbl, LV_SYMBOL_BLUETOOTH " %d...", count);
    }
}

static void ble_on_up(AppView *v)   { menu_list_up(((BleCtx *)v->ctx)->list); }
static void ble_on_down(AppView *v) { menu_list_down(((BleCtx *)v->ctx)->list); }
static void ble_on_ok(AppView *v)   { menu_list_activate(((BleCtx *)v->ctx)->list); }

static void ble_on_destroy(AppView *v) {
    BleCtx *c = (BleCtx *)v->ctx;
    if (c->poll) lv_timer_delete(c->poll);
    ble_stop_scan();
    menu_list_destroy(c->list);
    free(c);
}

static AppView *ble_scanner_view_create(void) {
    AppView *v = app_view_new();
    v->root = screen_base_create("BLE SCAN", true);

    BleCtx *c = (BleCtx *)calloc(1, sizeof(BleCtx));

    c->status_lbl = lv_label_create(v->root);
    lv_obj_set_style_text_color(c->status_lbl, theme_clr(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(c->status_lbl, FONT_SMALL, 0);
    lv_obj_set_pos(c->status_lbl, 8, SCREEN_CONTENT_TOP + 2);

    c->list = menu_list_create(v->root, SCREEN_CONTENT_TOP + 18, SCREEN_CONTENT_H - 18);

    MenuItem rescan = { LV_SYMBOL_REFRESH, "Rescan", NULL, ble_rescan, c };
    menu_list_add(c->list, &rescan);

    v->ctx        = c;
    v->on_up      = ble_on_up;
    v->on_down    = ble_on_down;
    v->on_ok      = ble_on_ok;
    v->on_destroy = ble_on_destroy;

    c->poll = lv_timer_create(ble_poll, 400, c);
    return v;
}

static AppView *ble_info_view_create(void) {
    char buf[96];
    ble_ensure_init();
    snprintf(buf, sizeof(buf),
             "MAC: %s\nBLE Name: RF-MULTITOOL\n\n"
             "ESP32-S3 Bluetooth 5.0\nClass: BLE (LE only)",
             BLEDevice::getAddress().toString().c_str());
    return info_view_create("BLE INFO", buf);
}

static void open_scanner(void *u) { (void)u; nav_push(ble_scanner_view_create()); }
static void open_info(void *u)    { (void)u; nav_push(ble_info_view_create()); }

AppView *bt_view_create(void) {
    static const MenuItem items[] = {
        { LV_SYMBOL_BLUETOOTH, "BLE Scanner", NULL, open_scanner, NULL },
        { LV_SYMBOL_LIST,      "BLE Info",    NULL, open_info,    NULL },
    };
    return list_view_create("BLUETOOTH", items, 2);
}
