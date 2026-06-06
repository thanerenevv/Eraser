#include "rf_hal.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>
#include <SmartRC_CC1101.h>

#define NOISE_FLOOR_DBM  (-95.0f)
#define NOISE_RANGE        1.2f
#define MAX_CARRIERS         8

typedef struct {
    float    pos;
    float    strength;
    float    width;
    float    drift_amp;
    float    drift_spd;
    float    burst_prob;
    uint32_t burst_dur_ms;
} CarrierDef;

typedef struct {
    float    drift_phase;
    bool     bursting;
    uint32_t burst_end_ms;
} CarrierState;

static const struct {
    const char *name;
    const char *chip;
    float       lo, hi;
    int         n;
    CarrierDef  def[MAX_CARRIERS];
} k_bands[RF_BAND_COUNT] = {
    {
        "433 MHz", "CC1101", 433.05f, 434.79f, 3,
        {
            { 0.500f, 40.0f, 0.00090f, 0.0008f, 0.0000060f, 0.000f,  0   },
            { 0.455f, 52.0f, 0.00110f, 0.0005f, 0.0000050f, 0.003f, 350  },
            { 0.785f, 34.0f, 0.00095f, 0.0007f, 0.0000080f, 0.014f,  75  },
        }
    },
    {
        "868 MHz", "CC1101", 868.00f, 869.70f, 2,
        {
            { 0.176f, 38.0f, 0.00080f, 0.0006f, 0.0000055f, 0.000f,  0   },
            { 0.588f, 46.0f, 0.00100f, 0.0004f, 0.0000070f, 0.002f, 300  },
        }
    },
    {
        "915 MHz", "CC1101", 902.00f, 928.00f, 3,
        {
            { 0.250f, 34.0f, 0.00120f, 0.0005f, 0.0000060f, 0.012f,  60  },
            { 0.500f, 44.0f, 0.00085f, 0.0007f, 0.0000075f, 0.000f,  0   },
            { 0.750f, 30.0f, 0.00110f, 0.0009f, 0.0000090f, 0.005f, 150  },
        }
    },
    {
        "2.4 GHz", "NRF24L01+", 2400.0f, 2483.5f, 8,
        {
            { 0.144f, 55.0f, 0.020f, 0.0001f, 0.0000010f, 0.000f,  0  },
            { 0.443f, 62.0f, 0.022f, 0.0001f, 0.0000012f, 0.000f,  0  },
            { 0.743f, 52.0f, 0.020f, 0.0001f, 0.0000008f, 0.000f,  0  },
            { 0.024f, 32.0f, 0.0018f, 0.0005f, 0.0000150f, 0.180f, 12 },
            { 0.312f, 28.0f, 0.0018f, 0.0005f, 0.0000140f, 0.160f, 12 },
            { 0.958f, 30.0f, 0.0018f, 0.0004f, 0.0000130f, 0.150f, 12 },
            { 0.479f, 24.0f, 0.0025f, 0.0008f, 0.0000250f, 0.070f, 20 },
            { 0.838f, 20.0f, 0.0022f, 0.0010f, 0.0000350f, 0.090f, 15 },
        }
    },
};

static CarrierState g_state[RF_BAND_COUNT][MAX_CARRIERS];
static uint32_t     g_last_ms  = 0;
static bool         g_inited   = false;
static bool         g_cc1101_ok = false;

void rf_hal_init(void) {
    randomSeed(esp_random());
    uint32_t now = millis();
    for (int b = 0; b < RF_BAND_COUNT; b++) {
        for (int i = 0; i < k_bands[b].n; i++) {
            g_state[b][i].drift_phase  = (float)random(0, 629) / 100.0f;
            g_state[b][i].bursting     = false;
            g_state[b][i].burst_end_ms = 0;
        }
    }
    g_last_ms = now;
    g_inited  = true;

    // Probe CC1101 on FSPI
    SmartRC_cc1101.setSpiPin(CC1101_SCK_PIN, CC1101_MISO_PIN, CC1101_MOSI_PIN, CC1101_CS_PIN);
    SmartRC_cc1101.setGDO0(CC1101_GDO0_PIN);
    SmartRC_cc1101.Init();
    g_cc1101_ok = SmartRC_cc1101.getCC1101();
    if (g_cc1101_ok) {
        SmartRC_cc1101.setMHZ(433.92f);
        SmartRC_cc1101.SetRx();
    }
}

void rf_hal_tick(void) {
    if (!g_inited) rf_hal_init();

    uint32_t now = millis();
    float    dt  = (float)(now - g_last_ms);
    g_last_ms = now;
    if (dt > 200.0f) dt = 200.0f;

    for (int b = 0; b < RF_BAND_COUNT; b++) {
        int n = k_bands[b].n;
        for (int i = 0; i < n; i++) {
            const CarrierDef *d = &k_bands[b].def[i];
            CarrierState     *s = &g_state[b][i];

            s->drift_phase += d->drift_spd * dt;
            if (s->drift_phase > 6.2832f) s->drift_phase -= 6.2832f;

            if (d->burst_prob > 0.0f) {
                if (s->bursting && now >= s->burst_end_ms)
                    s->bursting = false;
                if (!s->bursting &&
                    ((float)random(0, 100000) / 100000.0f) < d->burst_prob)
                {
                    s->bursting     = true;
                    s->burst_end_ms = now + d->burst_dur_ms;
                }
            }
        }
    }
}

bool        rf_hal_present(void)          { return g_cc1101_ok; }
const char *rf_hal_chip(RfBand b)         { return k_bands[b].chip; }
const char *rf_band_name(RfBand b)        { return k_bands[b].name; }
float       rf_band_lo_mhz(RfBand b)      { return k_bands[b].lo; }
float       rf_band_hi_mhz(RfBand b)      { return k_bands[b].hi; }
float       rf_hal_noise_floor(void)       { return NOISE_FLOOR_DBM; }

int rf_hal_rssi(RfBand band, float freq_mhz) {
    if (!g_inited) rf_hal_init();

    // Use real hardware for sub-GHz bands when CC1101 is present
    if (g_cc1101_ok && band != RF_BAND_24) {
        SmartRC_cc1101.SetRx(freq_mhz);
        delayMicroseconds(600);
        int rssi = SmartRC_cc1101.getRssi();
        if (rssi > -20)  rssi = -20;
        if (rssi < -110) rssi = -110;
        return rssi;
    }

    float lo = k_bands[band].lo, hi = k_bands[band].hi;
    float x  = (freq_mhz - lo) / (hi - lo);
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;

    float level = NOISE_FLOOR_DBM + (float)random(0, (int)(NOISE_RANGE * 100.0f + 1)) / 100.0f;

    int n = k_bands[band].n;
    for (int i = 0; i < n; i++) {
        const CarrierDef  *d = &k_bands[band].def[i];
        const CarrierState *s = &g_state[band][i];

        if (d->burst_prob > 0.0f && !s->bursting) continue;

        float pos  = d->pos + d->drift_amp * sinf(s->drift_phase);
        float dx   = x - pos;
        float bump = d->strength * expf(-(dx * dx) / d->width);
        level += bump;
    }

    int rssi = (int)level;
    if (rssi > -20)  rssi = -20;
    if (rssi < -110) rssi = -110;
    return rssi;
}
