#pragma once

#define LCD_WIDTH   320
#define LCD_HEIGHT  170

#define LCD_MOSI    17
#define LCD_SCLK    18
#define LCD_CS      7
#define LCD_DC      15
#define LCD_RST     16
#define LCD_BL      6

#define LCD_SPI_FREQ_HZ  (40 * 1000 * 1000)

#define LCD_COL_OFFSET   0
#define LCD_ROW_OFFSET   35

#define LCD_MADCTL       0x60

#define BTN_UP           41
#define BTN_DOWN         40
#define BTN_BACK         39
#define BTN_OK           0
#define BTN_EXTRA        38

#define IR_TX_PIN        4
#define IR_RX_PIN        5

#define BTN_DEBOUNCE_MS    20
#define BTN_HOLD_MS        600
#define BTN_REPEAT_DELAY_MS 350
#define BTN_REPEAT_RATE_MS  90

#define LVGL_TICK_PERIOD_MS  1
#define LVGL_TASK_STACK_KB   16
#define LVGL_TASK_PRIORITY   5
#define LVGL_TASK_CORE       1

#define STATUS_BAR_H    22
#define MENU_ITEM_H     28
#define MENU_ITEMS_MAX  16

#define NAV_STACK_MAX   8

#define BL_MIN_DUTY     14
#define BL_MAX_DUTY     255
#define BRIGHTNESS_DEFAULT   80
#define DISPLAY_TIMEOUT_DEFAULT 3

#define BATT_ADC_PIN    (-1)
#define BATT_DIV_RATIO  2.0f

#define RF_SPECTRUM_BINS 30

#define GPIO_EXPO_COUNT  7
#define GPIO_PIN_0       1
#define GPIO_PIN_1       2
#define GPIO_PIN_2       3
#define GPIO_PIN_3       45
#define GPIO_PIN_4       46
#define GPIO_PIN_5       47
#define GPIO_PIN_6       48

#define ETH_SCK_PIN    GPIO_PIN_3   // GPIO 45
#define ETH_MISO_PIN   GPIO_PIN_4   // GPIO 46
#define ETH_MOSI_PIN   GPIO_PIN_5   // GPIO 47
#define ETH_CS_PIN     GPIO_PIN_6   // GPIO 48

// NRF24L01+ wiring
#define NRF24_CE_PIN    9
#define NRF24_CSN_PIN  10
#define NRF24_SCK_PIN  12
#define NRF24_MISO_PIN 13
#define NRF24_MOSI_PIN 11

// CC1101 wiring — shares the same FSPI bus (SCK/MOSI/MISO) as NRF24,
// so both can sit on the same 4-wire bus with separate CS pins.
#define CC1101_SCK_PIN   12   // shared with NRF24_SCK_PIN
#define CC1101_MISO_PIN  13   // shared with NRF24_MISO_PIN
#define CC1101_MOSI_PIN  11   // shared with NRF24_MOSI_PIN
#define CC1101_CS_PIN    14
#define CC1101_GDO0_PIN  21
