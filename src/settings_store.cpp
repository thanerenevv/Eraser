#include "settings_store.h"
#include "config.h"
#include "display.h"
#include <Preferences.h>
#include <string.h>

static Preferences prefs;
static uint8_t     g_brightness        = BRIGHTNESS_DEFAULT;
static uint8_t     g_display_timeout   = DISPLAY_TIMEOUT_DEFAULT;
static uint8_t     g_wifi_count        = 0;
static WifiCred    g_wifi_creds[WIFI_CRED_MAX];

#define NS      "multitool"
#define K_BRI   "bright"
#define K_DTO   "dto"
#define K_WN    "wn"

static void wifi_key_ssid(int idx, char *buf) { snprintf(buf, 8, "ws%d", idx); }
static void wifi_key_pass(int idx, char *buf) { snprintf(buf, 8, "wp%d", idx); }

static void load_wifi_creds(void) {
    g_wifi_count = prefs.getUChar(K_WN, 0);
    if (g_wifi_count > WIFI_CRED_MAX) g_wifi_count = 0;
    char ks[8], kp[8];
    for (int i = 0; i < g_wifi_count; i++) {
        wifi_key_ssid(i, ks);
        wifi_key_pass(i, kp);
        prefs.getString(ks, g_wifi_creds[i].ssid, WIFI_SSID_LEN);
        prefs.getString(kp, g_wifi_creds[i].pass, WIFI_PASS_LEN);
    }
}

static void flush_wifi_creds(void) {
    prefs.putUChar(K_WN, g_wifi_count);
    char ks[8], kp[8];
    for (int i = 0; i < g_wifi_count; i++) {
        wifi_key_ssid(i, ks);
        wifi_key_pass(i, kp);
        prefs.putString(ks, g_wifi_creds[i].ssid);
        prefs.putString(kp, g_wifi_creds[i].pass);
    }
}

void settings_load(void) {
    prefs.begin(NS, false);
    g_brightness      = prefs.getUChar(K_BRI, BRIGHTNESS_DEFAULT);
    g_display_timeout = prefs.getUChar(K_DTO, DISPLAY_TIMEOUT_DEFAULT);
    if (g_brightness > 100)       g_brightness      = BRIGHTNESS_DEFAULT;
    if (g_display_timeout > 60)   g_display_timeout = DISPLAY_TIMEOUT_DEFAULT;
    display_set_brightness(g_brightness);
    load_wifi_creds();
}

uint8_t settings_brightness(void) { return g_brightness; }

void settings_set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    g_brightness = pct;
    display_set_brightness(pct);
    prefs.putUChar(K_BRI, pct);
}

uint8_t settings_display_timeout(void) { return g_display_timeout; }

void settings_set_display_timeout(uint8_t minutes) {
    if (minutes > 60) minutes = 60;
    g_display_timeout = minutes;
    prefs.putUChar(K_DTO, minutes);
}

int settings_wifi_count(void) { return g_wifi_count; }

bool settings_wifi_get(int idx, WifiCred *out) {
    if (idx < 0 || idx >= g_wifi_count) return false;
    *out = g_wifi_creds[idx];
    return true;
}

int settings_wifi_find(const char *ssid) {
    for (int i = 0; i < g_wifi_count; i++) {
        if (strncmp(g_wifi_creds[i].ssid, ssid, WIFI_SSID_LEN - 1) == 0)
            return i;
    }
    return -1;
}

void settings_wifi_put(const char *ssid, const char *pass) {
    int idx = settings_wifi_find(ssid);
    if (idx < 0) {
        if (g_wifi_count >= WIFI_CRED_MAX) idx = 0;
        else idx = g_wifi_count++;
    }
    strncpy(g_wifi_creds[idx].ssid, ssid, WIFI_SSID_LEN - 1);
    g_wifi_creds[idx].ssid[WIFI_SSID_LEN - 1] = '\0';
    strncpy(g_wifi_creds[idx].pass, pass, WIFI_PASS_LEN - 1);
    g_wifi_creds[idx].pass[WIFI_PASS_LEN - 1] = '\0';
    flush_wifi_creds();
}

void settings_wifi_del(int idx) {
    if (idx < 0 || idx >= g_wifi_count) return;
    for (int i = idx; i < g_wifi_count - 1; i++)
        g_wifi_creds[i] = g_wifi_creds[i + 1];
    g_wifi_count--;
    flush_wifi_creds();
    char ks[8], kp[8];
    wifi_key_ssid(g_wifi_count, ks);
    wifi_key_pass(g_wifi_count, kp);
    prefs.remove(ks);
    prefs.remove(kp);
}
