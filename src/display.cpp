#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <SPI.h>
#include <lvgl.h>

static SPIClass *lcd_spi = nullptr;
static lv_display_t *g_disp = nullptr;
static uint8_t g_brightness = BRIGHTNESS_DEFAULT;

static uint16_t draw_buf1[LCD_WIDTH * 20];
static uint16_t draw_buf2[LCD_WIDTH * 20];

static void spi_write_cmd(uint8_t cmd) {
    digitalWrite(LCD_DC, LOW);
    digitalWrite(LCD_CS, LOW);
    lcd_spi->transfer(cmd);
    digitalWrite(LCD_CS, HIGH);
}

static void spi_write_data(uint8_t data) {
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, LOW);
    lcd_spi->transfer(data);
    digitalWrite(LCD_CS, HIGH);
}

static void spi_write_data16(uint16_t data) {
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, LOW);
    lcd_spi->transfer16(data);
    digitalWrite(LCD_CS, HIGH);
}

static void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    x1 += LCD_COL_OFFSET;
    x2 += LCD_COL_OFFSET;
    y1 += LCD_ROW_OFFSET;
    y2 += LCD_ROW_OFFSET;

    spi_write_cmd(0x2A);
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, LOW);
    lcd_spi->transfer(x1 >> 8);
    lcd_spi->transfer(x1 & 0xFF);
    lcd_spi->transfer(x2 >> 8);
    lcd_spi->transfer(x2 & 0xFF);
    digitalWrite(LCD_CS, HIGH);

    spi_write_cmd(0x2B);
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, LOW);
    lcd_spi->transfer(y1 >> 8);
    lcd_spi->transfer(y1 & 0xFF);
    lcd_spi->transfer(y2 >> 8);
    lcd_spi->transfer(y2 & 0xFF);
    digitalWrite(LCD_CS, HIGH);

    spi_write_cmd(0x2C);
}

static void lcd_hw_init(void) {
    pinMode(LCD_DC,  OUTPUT);
    pinMode(LCD_CS,  OUTPUT);
    pinMode(LCD_RST, OUTPUT);
    pinMode(LCD_BL,  OUTPUT);

    digitalWrite(LCD_CS,  HIGH);
    digitalWrite(LCD_RST, HIGH);
    delay(5);
    digitalWrite(LCD_RST, LOW);
    delay(20);
    digitalWrite(LCD_RST, HIGH);
    delay(150);

    spi_write_cmd(0x01);
    delay(150);

    spi_write_cmd(0x11);
    delay(120);

    spi_write_cmd(0xB2);
    spi_write_data(0x0C);
    spi_write_data(0x0C);
    spi_write_data(0x00);
    spi_write_data(0x33);
    spi_write_data(0x33);

    spi_write_cmd(0xB7);
    spi_write_data(0x35);

    spi_write_cmd(0xBB);
    spi_write_data(0x19);

    spi_write_cmd(0xC0);
    spi_write_data(0x2C);

    spi_write_cmd(0xC2);
    spi_write_data(0x01);

    spi_write_cmd(0xC3);
    spi_write_data(0x12);

    spi_write_cmd(0xC4);
    spi_write_data(0x20);

    spi_write_cmd(0xC6);
    spi_write_data(0x0F);

    spi_write_cmd(0xD0);
    spi_write_data(0xA4);
    spi_write_data(0xA1);

    spi_write_cmd(0xE0);
    spi_write_data(0xD0); spi_write_data(0x04); spi_write_data(0x0D);
    spi_write_data(0x11); spi_write_data(0x13); spi_write_data(0x2B);
    spi_write_data(0x3F); spi_write_data(0x54); spi_write_data(0x4C);
    spi_write_data(0x18); spi_write_data(0x0D); spi_write_data(0x0B);
    spi_write_data(0x1F); spi_write_data(0x23);

    spi_write_cmd(0xE1);
    spi_write_data(0xD0); spi_write_data(0x04); spi_write_data(0x0C);
    spi_write_data(0x11); spi_write_data(0x13); spi_write_data(0x2C);
    spi_write_data(0x3F); spi_write_data(0x44); spi_write_data(0x51);
    spi_write_data(0x2F); spi_write_data(0x1F); spi_write_data(0x1F);
    spi_write_data(0x20); spi_write_data(0x23);

    spi_write_cmd(0x21);

    spi_write_cmd(0x3A);
    spi_write_data(0x55);

    spi_write_cmd(0x36);
    spi_write_data(LCD_MADCTL);

    spi_write_cmd(0x29);
    delay(20);

    display_set_brightness(g_brightness);
}

void display_set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    g_brightness = pct;
    uint32_t duty = BL_MIN_DUTY +
                    ((uint32_t)(BL_MAX_DUTY - BL_MIN_DUTY) * pct) / 100;
    analogWrite(LCD_BL, (int)duty);
}

uint8_t display_get_brightness(void) {
    return g_brightness;
}

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    lcd_set_window(area->x1, area->y1, area->x2, area->y2);

    uint16_t *p = (uint16_t *)px_map;
    uint32_t count = w * h;

    lv_draw_sw_rgb565_swap(px_map, count);

    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, LOW);
    lcd_spi->writeBytes((uint8_t *)p, count * 2);
    digitalWrite(LCD_CS, HIGH);

    lv_display_flush_ready(disp);
}

void display_init(void) {
    lcd_spi = new SPIClass(HSPI);
    lcd_spi->begin(LCD_SCLK, -1, LCD_MOSI, LCD_CS);
    lcd_spi->setFrequency(LCD_SPI_FREQ_HZ);

    lcd_hw_init();

    g_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(g_disp, disp_flush);
    lv_display_set_buffers(g_disp, draw_buf1, draw_buf2, sizeof(draw_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
}

lv_display_t *display_get(void) {
    return g_disp;
}
