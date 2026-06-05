#pragma once

#include <stdint.h>
#include <stdbool.h>

// Hardware abstraction for the sub-GHz / 2.4GHz front-ends. Today it ships with
// a built-in simulator so the UI is fully functional with no radio attached;
// dropping in a real CC1101 / NRF24L01+ driver means implementing these calls.

typedef enum {
    RF_BAND_433 = 0,   // 433 MHz ISM (CC1101)
    RF_BAND_868,       // 868 MHz ISM (CC1101)
    RF_BAND_915,       // 915 MHz ISM (CC1101)
    RF_BAND_24,        // 2.4 GHz    (NRF24L01+)
    RF_BAND_COUNT
} RfBand;

void        rf_hal_init(void);
bool        rf_hal_present(void);          // true once a real radio is detected
const char *rf_hal_chip(RfBand band);      // chip backing a band ("CC1101"...)

const char *rf_band_name(RfBand band);
float       rf_band_lo_mhz(RfBand band);   // sweep start
float       rf_band_hi_mhz(RfBand band);   // sweep end
float       rf_hal_noise_floor(void);      // ambient noise floor (dBm)

// Advance the (simulated) RF model by wall-clock time. Call this once per UI
// frame *before* sampling the spectrum so that the many per-bin rf_hal_rssi()
// reads within a frame all observe one coherent snapshot.
void rf_hal_tick(void);

// Received signal strength (dBm) at an absolute frequency, in [-110, -20].
int rf_hal_rssi(RfBand band, float freq_mhz);
