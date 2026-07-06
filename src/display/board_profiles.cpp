#include "board_profiles.h"
#include <sdkconfig.h>
#include <string.h>

// ------------------------------------------------------------------
// Generische SPI-Farbdisplay-Verdrahtung (ILI9488/ST7796S/GC9A01 nutzen
// eigene Pins, siehe unten) - 1:1 aus dem esp-idf-Branch uebernommen.
// ------------------------------------------------------------------
#if CONFIG_IDF_TARGET_ESP32C3
#define GENERIC_SPI_MOSI 4
#define GENERIC_SPI_MISO 5
#define GENERIC_SPI_SCLK 6
#define GENERIC_SPI_CS   7
#define GENERIC_SPI_DC   10
#define GENERIC_SPI_RST  3
#define GENERIC_SPI_BL   1
#else
#define GENERIC_SPI_MOSI 23
#define GENERIC_SPI_MISO 19
#define GENERIC_SPI_SCLK 18
#define GENERIC_SPI_CS   5
#define GENERIC_SPI_DC   17
#define GENERIC_SPI_RST  16
#define GENERIC_SPI_BL   4
#endif

#define GC9A01_SDA 3
#define GC9A01_SCL 4
#define GC9A01_CS  1
#define GC9A01_DC  10
#define GC9A01_RST 0
#define GC9A01_BL  (-1)

#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
#define BOOT_BUTTON_GPIO 9
#else
#define BOOT_BUTTON_GPIO 0
#endif

#if BOOT_BUTTON_GPIO == GC9A01_RST
#define GC9A01_NAV_BUTTON (-1)
#else
#define GC9A01_NAV_BUTTON BOOT_BUTTON_GPIO
#endif

#define JC8048_DE     40
#define JC8048_VSYNC  41
#define JC8048_HSYNC  39
#define JC8048_PCLK   42
#define JC8048_BL     2
#define JC8048_DATA { 8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14 }

#define JC8048_TOUCH_SDA 19
#define JC8048_TOUCH_SCL 20
#define JC8048_TOUCH_INT 18
#define JC8048_TOUCH_RST 38
#define JC8048_TOUCH_ADDR 0x5D

static const board_profile_t s_profiles[DISPLAY_TYPE_COUNT] = {
    [DISPLAY_CYD_ILI9341] = {
        .name = "ESP32-2432S028 - ILI9341 320x240, SPI + XPT2046-Touch",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_RECT, .has_touch = true,
        .mosi = 13, .miso = 12, .sclk = 14, .cs = 15, .dc = 2, .rst = -1, .bl = 21,
        .spi_hz = 40 * 1000 * 1000,
        .touch_cs = 33, .touch_irq = 36, .touch_mosi = 32, .touch_miso = 39, .touch_clk = 25,
        .touch_spi_hz = 2 * 1000 * 1000,
        .nav_button = -1,
        .h_res = 320, .v_res = 240, .bgr = true, .color_16bit = true,
    },
    [DISPLAY_ILI9488] = {
        .name = "ILI9488 (480x320, SPI, kein Touch)",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_RECT, .has_touch = false,
        .mosi = GENERIC_SPI_MOSI, .miso = GENERIC_SPI_MISO, .sclk = GENERIC_SPI_SCLK,
        .cs = GENERIC_SPI_CS, .dc = GENERIC_SPI_DC, .rst = GENERIC_SPI_RST, .bl = GENERIC_SPI_BL,
        .spi_hz = 40 * 1000 * 1000,
        .nav_button = -1,
        .h_res = 480, .v_res = 320, .bgr = false, .color_16bit = true,
    },
    [DISPLAY_ST7796S] = {
        .name = "ST7796S (480x320, SPI, kein Touch)",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_RECT, .has_touch = false,
        .mosi = GENERIC_SPI_MOSI, .miso = GENERIC_SPI_MISO, .sclk = GENERIC_SPI_SCLK,
        .cs = GENERIC_SPI_CS, .dc = GENERIC_SPI_DC, .rst = GENERIC_SPI_RST, .bl = GENERIC_SPI_BL,
        .spi_hz = 40 * 1000 * 1000,
        .nav_button = -1,
        .h_res = 480, .v_res = 320, .bgr = false, .color_16bit = true,
    },
    [DISPLAY_GC9A01] = {
        .name = "GC9A01 (240x240, rund, SPI, kein Touch)",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_ROUND, .has_touch = false,
        .mosi = GC9A01_SDA, .miso = -1, .sclk = GC9A01_SCL,
        .cs = GC9A01_CS, .dc = GC9A01_DC, .rst = GC9A01_RST, .bl = GC9A01_BL,
        .spi_hz = 40 * 1000 * 1000,
        .nav_button = GC9A01_NAV_BUTTON,
        .h_res = 240, .v_res = 240, .bgr = true, .color_16bit = true,
    },
    [DISPLAY_SSD1309_I2C] = {
        .name = "SSD1309 (128x64, monochrom, I2C, kein Touch)",
        .bus = LCD_BUS_I2C, .shape = LCD_SHAPE_MONO, .has_touch = false,
        .i2c_sda = 8, .i2c_scl = 9, .i2c_addr = 0x3C, .i2c_hz = 400 * 1000,
        .nav_button = -1,
        .h_res = 128, .v_res = 64, .bgr = false, .color_16bit = false,
    },
    [DISPLAY_GUITION_JC8048W550] = {
        .name = "Guition JC8048W550(C) - ST7262 800x480 RGB-Parallel + GT911-Touch",
        .bus = LCD_BUS_RGB, .shape = LCD_SHAPE_WIDE, .has_touch = true,
        .bl = JC8048_BL,
        .rgb_data = JC8048_DATA,
        .rgb_hsync = JC8048_HSYNC, .rgb_vsync = JC8048_VSYNC, .rgb_de = JC8048_DE, .rgb_pclk = JC8048_PCLK,
        .rgb_pclk_hz = 15 * 1000 * 1000,
        .rgb_hsync_pulse_width = 4, .rgb_hsync_back_porch = 8, .rgb_hsync_front_porch = 8,
        .rgb_vsync_pulse_width = 4, .rgb_vsync_back_porch = 8, .rgb_vsync_front_porch = 8,
        .i2c_sda = JC8048_TOUCH_SDA, .i2c_scl = JC8048_TOUCH_SCL, .i2c_addr = JC8048_TOUCH_ADDR,
        .i2c_hz = 400 * 1000,
        .touch_rst = JC8048_TOUCH_RST, .touch_int = JC8048_TOUCH_INT,
        .nav_button = -1,
        .h_res = 800, .v_res = 480, .bgr = false, .color_16bit = true,
    },
    [DISPLAY_NONE] = {
        .name = "Kein Display (Panel-Init ueberspringen)",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_RECT, .has_touch = false,
        .mosi = -1, .miso = -1, .sclk = -1, .cs = -1, .dc = -1, .rst = -1, .bl = -1,
        .nav_button = -1,
        .h_res = 0, .v_res = 0, .bgr = false, .color_16bit = false,
    },
};

bool board_profile_is_available(display_type_t type)
{
    if (type == DISPLAY_NONE) return true;
    if (type == DISPLAY_GUITION_JC8048W550) {
#if CONFIG_IDF_TARGET_ESP32S3
        return true;
#else
        return false;
#endif
    }
#if CONFIG_IDF_TARGET_ESP32
    return type == DISPLAY_CYD_ILI9341;
#else
    return type != DISPLAY_CYD_ILI9341;
#endif
}

display_type_t board_profile_default(void)
{
    return DISPLAY_NONE;
}

const board_profile_t *board_profile_get(display_type_t type)
{
    if (type < 0 || type >= DISPLAY_TYPE_COUNT || !board_profile_is_available(type)) {
        type = board_profile_default();
    }
    return &s_profiles[type];
}

void pin_override_set_defaults(pin_override_t *ov)
{
    ov->mosi = ov->miso = ov->sclk = ov->cs = ov->dc = ov->rst = ov->bl = PIN_UNSET;
    ov->touch_cs = ov->touch_irq = ov->touch_mosi = ov->touch_miso = ov->touch_clk = PIN_UNSET;
    ov->i2c_sda = ov->i2c_scl = PIN_UNSET;
    ov->i2c_addr = 0;
    ov->nav_button = PIN_UNSET;
    ov->touch_rst = ov->touch_int = PIN_UNSET;
}

static int apply_pin(int16_t override, int base)
{
    return (override == PIN_UNSET) ? base : (int)override;
}

board_profile_t board_profile_apply_overrides(const board_profile_t *base, const pin_override_t *ov)
{
    board_profile_t p = *base;
    if (!ov) return p;

    p.mosi = apply_pin(ov->mosi, base->mosi);
    p.miso = apply_pin(ov->miso, base->miso);
    p.sclk = apply_pin(ov->sclk, base->sclk);
    p.cs   = apply_pin(ov->cs,   base->cs);
    p.dc   = apply_pin(ov->dc,   base->dc);
    p.rst  = apply_pin(ov->rst,  base->rst);
    p.bl   = apply_pin(ov->bl,   base->bl);

    p.touch_cs   = apply_pin(ov->touch_cs,   base->touch_cs);
    p.touch_irq  = apply_pin(ov->touch_irq,  base->touch_irq);
    p.touch_mosi = apply_pin(ov->touch_mosi, base->touch_mosi);
    p.touch_miso = apply_pin(ov->touch_miso, base->touch_miso);
    p.touch_clk  = apply_pin(ov->touch_clk,  base->touch_clk);

    p.i2c_sda = apply_pin(ov->i2c_sda, base->i2c_sda);
    p.i2c_scl = apply_pin(ov->i2c_scl, base->i2c_scl);
    p.i2c_addr = (ov->i2c_addr == 0) ? base->i2c_addr : ov->i2c_addr;

    p.nav_button = apply_pin(ov->nav_button, base->nav_button);

    p.touch_rst = apply_pin(ov->touch_rst, base->touch_rst);
    p.touch_int = apply_pin(ov->touch_int, base->touch_int);
    return p;
}

static const char *s_keys[DISPLAY_TYPE_COUNT] = {
    [DISPLAY_CYD_ILI9341] = "cyd_ili9341",
    [DISPLAY_ILI9488]     = "ili9488",
    [DISPLAY_ST7796S]     = "st7796s",
    [DISPLAY_GC9A01]      = "gc9a01",
    [DISPLAY_SSD1309_I2C] = "ssd1309_i2c",
    [DISPLAY_GUITION_JC8048W550] = "guition_jc8048w550",
    [DISPLAY_NONE]        = "none",
};

const char *board_profile_key(display_type_t type)
{
    if (type < 0 || type >= DISPLAY_TYPE_COUNT) type = DISPLAY_CYD_ILI9341;
    return s_keys[type];
}

display_type_t board_profile_from_key(const char *key)
{
    if (!key) return DISPLAY_CYD_ILI9341;
    for (int i = 0; i < DISPLAY_TYPE_COUNT; i++) {
        if (strcmp(key, s_keys[i]) == 0) return (display_type_t)i;
    }
    return DISPLAY_CYD_ILI9341;
}
