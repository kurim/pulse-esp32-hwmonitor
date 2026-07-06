#pragma once
#include <stdbool.h>
#include <stdint.h>

// ------------------------------------------------------------------
// Unterstuetzte Display-Typen. Auswahl erfolgt zur Laufzeit per Webportal
// (app_config.display_type, in Preferences/NVS gespeichert) - EIN Firmware-
// Image enthaelt alle Treiber, kein Neuflashen bei Displaywechsel noetig.
//
// Portiert 1:1 aus main/display/board_profiles.h (esp-idf-Branch) - siehe
// dort fuer die ausfuehrliche Herleitung der Pin-Zuordnungen.
// ------------------------------------------------------------------
typedef enum {
    DISPLAY_CYD_ILI9341 = 0, // ESP32-2432S028 "CYD": ILI9341 320x240 + XPT2046-Touch (Werksverdrahtung)
    DISPLAY_ILI9488,         // generisch verdrahtet: ILI9488 480x320 SPI, kein Touch
    DISPLAY_ST7796S,         // generisch verdrahtet: ST7796S 480x320 SPI, kein Touch
    DISPLAY_GC9A01,          // generisch verdrahtet: GC9A01 240x240 rund, SPI, kein Touch
    DISPLAY_SSD1309_I2C,     // generisch verdrahtet: SSD1309 128x64 monochrom, I2C, kein Touch
    DISPLAY_GUITION_JC8048W550, // Guition JC8048W550(C): ST7262 800x480 RGB-Parallel + GT911-Touch
                                // (Werksverdrahtung, nur auf ESP32-S3 verfuegbar)
    // Neu ans Ende angehaengt (nicht nach vorne einsortiert!) - der Wert wird
    // 1:1 als uint8_t in Preferences abgelegt, ein Einsortieren wuerde bei
    // bereits geflashten Geraeten die gespeicherte Zahl auf einen anderen
    // Displaytyp verschieben.
    DISPLAY_NONE,            // kein Panel - ueberspringt jede Hardware-Init
    DISPLAY_TYPE_COUNT
} display_type_t;

typedef enum {
    LCD_BUS_SPI,
    LCD_BUS_I2C,
    LCD_BUS_RGB, // 16-bit RGB565-Parallelbus, nur ESP32-S3
} lcd_bus_kind_t;

typedef enum {
    LCD_SHAPE_RECT,   // rechteckige Kachel-UI
    LCD_SHAPE_ROUND,  // rundes Minimal-UI
    LCD_SHAPE_MONO,   // monochromes Minimal-UI
    LCD_SHAPE_WIDE,   // Dashboard-UI fuer grosse Panels
} lcd_ui_shape_t;

// Beschreibt ein Panel vollstaendig: Bus, Pins, Aufloesung, Farbmodus und
// welche UI-Variante dafuer gebaut werden soll.
typedef struct {
    const char     *name;          // fuer Logs/Webportal
    lcd_bus_kind_t  bus;
    lcd_ui_shape_t  shape;
    bool            has_touch;

    // ---- SPI-Displays ----
    int mosi, miso, sclk, cs, dc, rst, bl;
    int spi_hz;

    // ---- I2C-Displays ----
    // Bei LCD_BUS_RGB werden dieselben drei Felder fuer den I2C-Bus des
    // GT911-Touch-Controllers genutzt.
    int i2c_sda, i2c_scl;
    int i2c_addr;
    int i2c_hz;

    // ---- Touch (SPI: nur CYD_ILI9341/XPT2046) ----
    int touch_cs, touch_irq, touch_mosi, touch_miso, touch_clk;
    int touch_spi_hz;

    // ---- Touch (I2C: nur LCD_BUS_RGB/GT911) ----
    int touch_rst, touch_int;

    // ---- RGB-Parallelbus (nur LCD_BUS_RGB) ----
    int rgb_data[16];    // D0..D15
    int rgb_hsync, rgb_vsync, rgb_de, rgb_pclk;
    uint32_t rgb_pclk_hz;
    uint32_t rgb_hsync_pulse_width, rgb_hsync_back_porch, rgb_hsync_front_porch;
    uint32_t rgb_vsync_pulse_width, rgb_vsync_back_porch, rgb_vsync_front_porch;

    // ---- Navigationstaste fuer Profile ohne Touch (z.B. GC9A01: BOOT-Taste)
    int nav_button;

    // ---- Aufloesung / Farbe ----
    int  h_res, v_res;
    bool bgr;               // true = BGR-Panel
    bool color_16bit;       // true = RGB565, false = monochrom (1bpp)
} board_profile_t;

// Pin-Overrides: siehe board_profiles.h im esp-idf-Branch fuer die
// ausfuehrliche Begruendung. PIN_UNSET markiert "kein Override".
#define PIN_UNSET ((int16_t)-32768)

typedef struct {
    int16_t mosi, miso, sclk, cs, dc, rst, bl;
    int16_t touch_cs, touch_irq, touch_mosi, touch_miso, touch_clk;
    int16_t i2c_sda, i2c_scl;
    uint8_t i2c_addr; // 0 = kein Override
    int16_t nav_button;
    int16_t touch_rst, touch_int;
} pin_override_t;

void pin_override_set_defaults(pin_override_t *ov);
board_profile_t board_profile_apply_overrides(const board_profile_t *base, const pin_override_t *ov);
const board_profile_t *board_profile_get(display_type_t type);
const char *board_profile_key(display_type_t type);
display_type_t board_profile_from_key(const char *key);

// Ist dieses Profil auf dem aktuellen Compile-Target ueberhaupt sinnvoll
// waehlbar? Nutzt CONFIG_IDF_TARGET_* Makros, die arduino-esp32 (als
// IDF-Komponente) ebenfalls definiert.
bool board_profile_is_available(display_type_t type);

display_type_t board_profile_default(void);
