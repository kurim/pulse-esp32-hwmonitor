#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------
// Unterstuetzte Display-Typen. Auswahl erfolgt zur Laufzeit per Webportal
// (app_config.display_type, in NVS gespeichert) - EIN Firmware-Image
// enthaelt alle Treiber, kein Neuflashen bei Displaywechsel noetig.
//
// Pin-Zuordnungen unten sind Standard-Verdrahtungsvorschlaege fuer einen
// generischen ESP32/S3/C3-Dev-Board-Aufbau (ausser CYD_ILI9341, das die
// feste Werksverdrahtung des ESP32-2432S028-Boards beschreibt). Bei
// abweichender eigener Verdrahtung muessen die Defines unten angepasst
// werden - eine Pin-Konfiguration ueber das Webportal ist (noch) nicht
// vorgesehen, die Pins werden dort aber angezeigt (siehe web_portal.c).
// ------------------------------------------------------------------
typedef enum {
    DISPLAY_CYD_ILI9341 = 0, // ESP32-2432S028 "CYD": ILI9341 320x240 + XPT2046-Touch (Werksverdrahtung)
    DISPLAY_ILI9488,         // generisch verdrahtet: ILI9488 480x320 SPI, kein Touch
    DISPLAY_ST7796S,         // generisch verdrahtet: ST7796S 480x320 SPI, kein Touch
    DISPLAY_GC9A01,          // generisch verdrahtet: GC9A01 240x240 rund, SPI, kein Touch
    DISPLAY_SSD1309_I2C,     // generisch verdrahtet: SSD1309 128x64 monochrom, I2C, kein Touch
    // Neu ans Ende angehaengt (nicht nach vorne einsortiert!) - der Wert wird
    // 1:1 als uint8_t in NVS abgelegt (config_store.c), ein Einsortieren
    // wuerde bei bereits geflashten Geraeten die gespeicherte Zahl auf einen
    // anderen Displaytyp verschieben.
    DISPLAY_NONE,            // kein Panel - ueberspringt jede Hardware-Init in display_ui.c
    DISPLAY_TYPE_COUNT
} display_type_t;

typedef enum {
    LCD_BUS_SPI,
    LCD_BUS_I2C,
} lcd_bus_kind_t;

typedef enum {
    LCD_SHAPE_RECT,   // rechteckige Kachel-UI (build_main/build_detail/build_settings)
    LCD_SHAPE_ROUND,  // rundes Minimal-UI (build_round_ui)
    LCD_SHAPE_MONO,   // monochromes Minimal-UI (build_mono_ui)
} lcd_ui_shape_t;

// Beschreibt ein Panel vollstaendig: Bus, Pins, Aufloesung, Farbmodus und
// welche UI-Variante dafuer gebaut werden soll. board_profiles.c haelt eine
// statische Tabelle davon, eine je display_type_t.
typedef struct {
    const char     *name;          // fuer Logs/Webportal, z.B. "ILI9488 (480x320, SPI)"
    lcd_bus_kind_t  bus;
    lcd_ui_shape_t  shape;
    bool            has_touch;

    // ---- SPI-Displays ----
    int mosi, miso, sclk, cs, dc, rst, bl;
    int spi_hz;
    spi_host_device_t spi_host;

    // ---- I2C-Displays ----
    int i2c_sda, i2c_scl;
    int i2c_addr;
    int i2c_hz;

    // ---- Touch (nur CYD_ILI9341) ----
    int touch_cs, touch_irq, touch_mosi, touch_miso, touch_clk;
    spi_host_device_t touch_spi_host;
    int touch_spi_hz;

    // ---- Navigationstaste fuer Profile ohne Touch (z.B. GC9A01: BOOT-Taste
    // des Devboards) - -1, wenn keine vorgesehen. Zieht beim Druecken gegen
    // GND (interner Pullup), siehe display_ui.c: nav_button_init()/-_cb().
    int nav_button;

    // ---- Aufloesung / Farbe ----
    int  h_res, v_res;      // native Aufloesung (rotation=1/3 vertauscht bei RECT-Shape)
    bool bgr;               // true = BGR-Panel (RGB_ELEMENT_ORDER_BGR)
    bool color_16bit;       // true = RGB565, false = monochrom (1bpp)
} board_profile_t;

// ------------------------------------------------------------------
// Pin-Overrides: die obigen Pins sind Vorschlaege/Defaults, im Webportal
// aber pro Feld ueberschreibbar (Karte "Pin-Belegung"), damit man bei
// abweichender eigener Verdrahtung nicht den Quellcode anpassen und neu
// bauen muss. PIN_UNSET markiert "kein Override, Default aus board_profile_t
// verwenden" - GPIO-Nummern koennen legitim -1 sein (z.B. "kein Reset-Pin"),
// daher ein eigener Sentinel-Wert ausserhalb des gueltigen GPIO-Bereichs.
// Es gibt genau EIN Override-Set (nicht pro Displaytyp), da ein Geraet in
// der Praxis dauerhaft mit einem physischen Display betrieben wird; beim
// Wechsel des Displaytyps im Webportal wird es automatisch zurueckgesetzt
// (siehe web_portal.c: h_config_post), da alte Pins fuer ein anderes Panel
// i.d.R. nicht mehr passen.
// ------------------------------------------------------------------
#define PIN_UNSET ((int16_t)-32768)

typedef struct {
    int16_t mosi, miso, sclk, cs, dc, rst, bl;
    int16_t touch_cs, touch_irq, touch_mosi, touch_miso, touch_clk;
    int16_t i2c_sda, i2c_scl;
    uint8_t i2c_addr; // 0 = kein Override (0x00 ist keine gueltige I2C-Geraeteadresse)
    int16_t nav_button;
} pin_override_t;

// Setzt alle Felder auf "kein Override" (Defaults aus board_profiles.c gelten).
void pin_override_set_defaults(pin_override_t *ov);

// Wendet ov auf base an (PIN_UNSET/0-Felder bleiben beim Default aus base) und
// gibt das Ergebnis als eigenstaendige Kopie zurueck (kein Pointer mehr auf
// die statische Tabelle in board_profiles.c).
board_profile_t board_profile_apply_overrides(const board_profile_t *base, const pin_override_t *ov);

// Liefert das Profil fuer den gegebenen Displaytyp (immer gueltig, faellt bei
// out-of-range auf DISPLAY_CYD_ILI9341 zurueck). Liefert die kompilierten
// Defaults OHNE Pin-Overrides - siehe board_profile_apply_overrides().
const board_profile_t *board_profile_get(display_type_t type);

// Kurzname fuer Webportal-Dropdown ("cyd_ili9341", "ili9488", ...).
const char *board_profile_key(display_type_t type);

// Parst einen Kurznamen zurueck in den Enum-Wert (DISPLAY_CYD_ILI9341 als Fallback).
display_type_t board_profile_from_key(const char *key);

// Ist dieses Profil auf dem aktuellen Compile-Target (idf.py set-target)
// ueberhaupt sinnvoll waehlbar? DISPLAY_CYD_ILI9341 beschreibt die feste
// Werksverdrahtung des ESP32-2432S028-Boards (klassischer ESP32) und ergibt
// auf anderen Chips (S3/C3/...) keinen Sinn; die generischen Profile
// ergeben dagegen auf jedem Chip Sinn. Wird genutzt, um die Webportal-
// Displayauswahl build-spezifisch auszublenden (siehe web_portal.c).
bool board_profile_is_available(display_type_t type);

// Sinnvoller Default fuer config_set_defaults(): DISPLAY_NONE, damit ein
// frisch geflashtes Geraet (bzw. ein anderer Chip/Board als angenommen)
// nicht ungefragt SPI/I2C-Pins eines evtl. falschen Panels anspricht - das
// hat sich als problematisch erwiesen, wenn (noch) gar kein Display bzw.
// ein Board mit abweichender Verdrahtung angeschlossen ist. Displaytyp wird
// im Webportal ausgewaehlt, danach greift er nach dem naechsten Neustart.
display_type_t board_profile_default(void);

#ifdef __cplusplus
}
#endif
