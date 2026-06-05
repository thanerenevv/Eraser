#pragma once

#include <stdint.h>
#include <stdbool.h>

#define WIFI_CRED_MAX   5
#define WIFI_SSID_LEN   33
#define WIFI_PASS_LEN   64

typedef struct {
    char ssid[WIFI_SSID_LEN];
    char pass[WIFI_PASS_LEN];
} WifiCred;

void    settings_load(void);

uint8_t settings_brightness(void);
void    settings_set_brightness(uint8_t pct);

uint8_t settings_display_timeout(void);
void    settings_set_display_timeout(uint8_t minutes);

int     settings_wifi_count(void);
bool    settings_wifi_get(int idx, WifiCred *out);
void    settings_wifi_put(const char *ssid, const char *pass);
void    settings_wifi_del(int idx);
int     settings_wifi_find(const char *ssid);
