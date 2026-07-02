#include "board_profiles.h"
#include "sdkconfig.h"
#include <string.h>

// ------------------------------------------------------------------
// Generische SPI-Farbdisplay-Verdrahtung (ILI9488/ST7796S/GC9A01): dieselben
// Pins fuer alle drei, da es sich um denselben "Display an generischem
// ESP32/S3/C3-Devboard verdrahten"-Anwendungsfall handelt. Bei eigener
// Verdrahtung hier anpassen.
// ------------------------------------------------------------------
#define GENERIC_SPI_MOSI 23
#define GENERIC_SPI_MISO 19
#define GENERIC_SPI_SCLK 18
#define GENERIC_SPI_CS   5
#define GENERIC_SPI_DC   17
#define GENERIC_SPI_RST  16
#define GENERIC_SPI_BL   4

static const board_profile_t s_profiles[DISPLAY_TYPE_COUNT] = {
    [DISPLAY_CYD_ILI9341] = {
        .name = "ESP32-2432S028 - ILI9341 320x240, SPI + XPT2046-Touch",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_RECT, .has_touch = true,
        .mosi = 13, .miso = 12, .sclk = 14, .cs = 15, .dc = 2, .rst = -1, .bl = 21,
        .spi_hz = 40 * 1000 * 1000, .spi_host = SPI2_HOST,
        .touch_cs = 33, .touch_irq = 36, .touch_mosi = 32, .touch_miso = 39, .touch_clk = 25,
        .touch_spi_host = SPI3_HOST, .touch_spi_hz = 2 * 1000 * 1000,
        .h_res = 320, .v_res = 240, .bgr = true, .color_16bit = true,
    },
    [DISPLAY_ILI9488] = {
        .name = "ILI9488 (480x320, SPI, kein Touch)",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_RECT, .has_touch = false,
        .mosi = GENERIC_SPI_MOSI, .miso = GENERIC_SPI_MISO, .sclk = GENERIC_SPI_SCLK,
        .cs = GENERIC_SPI_CS, .dc = GENERIC_SPI_DC, .rst = GENERIC_SPI_RST, .bl = GENERIC_SPI_BL,
        .spi_hz = 40 * 1000 * 1000, .spi_host = SPI2_HOST,
        .h_res = 480, .v_res = 320, .bgr = false, .color_16bit = true,
    },
    [DISPLAY_ST7796S] = {
        .name = "ST7796S (480x320, SPI, kein Touch)",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_RECT, .has_touch = false,
        .mosi = GENERIC_SPI_MOSI, .miso = GENERIC_SPI_MISO, .sclk = GENERIC_SPI_SCLK,
        .cs = GENERIC_SPI_CS, .dc = GENERIC_SPI_DC, .rst = GENERIC_SPI_RST, .bl = GENERIC_SPI_BL,
        .spi_hz = 40 * 1000 * 1000, .spi_host = SPI2_HOST,
        .h_res = 480, .v_res = 320, .bgr = false, .color_16bit = true,
    },
    [DISPLAY_GC9A01] = {
        .name = "GC9A01 (240x240, rund, SPI, kein Touch)",
        .bus = LCD_BUS_SPI, .shape = LCD_SHAPE_ROUND, .has_touch = false,
        .mosi = GENERIC_SPI_MOSI, .miso = GENERIC_SPI_MISO, .sclk = GENERIC_SPI_SCLK,
        .cs = GENERIC_SPI_CS, .dc = GENERIC_SPI_DC, .rst = GENERIC_SPI_RST, .bl = GENERIC_SPI_BL,
        .spi_hz = 40 * 1000 * 1000, .spi_host = SPI2_HOST,
        .h_res = 240, .v_res = 240, .bgr = true, .color_16bit = true,
    },
    [DISPLAY_SSD1309_I2C] = {
        .name = "SSD1309 (128x64, monochrom, I2C, kein Touch)",
        .bus = LCD_BUS_I2C, .shape = LCD_SHAPE_MONO, .has_touch = false,
        .i2c_sda = 8, .i2c_scl = 9, .i2c_addr = 0x3C, .i2c_hz = 400 * 1000,
        .h_res = 128, .v_res = 64, .bgr = false, .color_16bit = false,
    },
};

bool board_profile_is_available(display_type_t type)
{
#if CONFIG_IDF_TARGET_ESP32
    // Klassischer ESP32: nur das CYD-Profil (feste Werksverdrahtung des
    // ESP32-2432S028) - die generischen Profile sind hier nicht gemeint,
    // Auswahl im Webportal daher unnoetig.
    return type == DISPLAY_CYD_ILI9341;
#else
    // Jeder andere Chip (S3/C3/...): CYD-Werksverdrahtung ergibt keinen
    // Sinn (falsche Pins/Board), nur die generischen Profile anbieten.
    return type != DISPLAY_CYD_ILI9341;
#endif
}

display_type_t board_profile_default(void)
{
    for (int i = 0; i < DISPLAY_TYPE_COUNT; i++) {
        if (board_profile_is_available((display_type_t)i)) return (display_type_t)i;
    }
    return DISPLAY_CYD_ILI9341;
}

const board_profile_t *board_profile_get(display_type_t type)
{
    if (type < 0 || type >= DISPLAY_TYPE_COUNT || !board_profile_is_available(type)) {
        type = board_profile_default();
    }
    return &s_profiles[type];
}

static const char *s_keys[DISPLAY_TYPE_COUNT] = {
    [DISPLAY_CYD_ILI9341] = "cyd_ili9341",
    [DISPLAY_ILI9488]     = "ili9488",
    [DISPLAY_ST7796S]     = "st7796s",
    [DISPLAY_GC9A01]      = "gc9a01",
    [DISPLAY_SSD1309_I2C] = "ssd1309_i2c",
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
