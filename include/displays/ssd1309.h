#pragma once

// SSD1309 (128x64 monochromes OLED). Frueher wurde hier Adafruit_SSD1306
// genutzt, weil Arduino_GFX "keinen SSD1309-Treiber" habe - falsch: die Lib
// hat einen Arduino_SSD1306-Treiber (SSD1309 teilt sich das Kommando-Set mit
// SSD1306, siehe https://github.com/Keralots/SmallOLED-PCMonitor) und dazu
// Arduino_Canvas_Mono, einen Software-Framebuffer-Wrapper fuer monochrome
// Panels. Damit entfaellt die komplette Adafruit-Abhaengigkeit (SSD1306+GFX+
// BusIO) - "GFX Library for Arduino" ist ohnehin schon fuer alle anderen
// Displays Pflicht-lib_dep, dieses Board kostet dadurch keine zusaetzliche
// Bibliothek mehr.
//
// Aufbau (siehe examples/Canvas/CanvasMonoOLED im GFX-Repo, dort fuer
// SSD1306):
//   Arduino_Wire (I2C-Databus) -> Arduino_SSD1306 (Arduino_G, kein
//   Arduino_GFX - kennt nur drawBitmap() mit fest verdrahteter Vollbild-
//   Adressierung, keine Teilausschnitte) -> Arduino_Canvas_Mono (echtes
//   Arduino_GFX, haelt einen eigenen 1bpp-Software-Framebuffer, den
//   draw16bitRGBBitmap() ueber die geerbte writePixel()/writePixelPreclipped()-
//   Kette automatisch aus RGB565 thresholdet - dieselbe Schwellwertlogik, die
//   hier vorher von Hand in einer Flush-Callback nachgebaut wurde). flush()
//   schiebt den kompletten Framebuffer erst auf explizite Anforderung per
//   I2C raus (SSD1306::drawBitmap() adressiert ohnehin immer Vollbild) -
//   deshalb braucht dieses Display anders als die SPI-Panels eine eigene,
//   kleine Flush-Callback statt der gemeinsamen aus display_common.h.
//
// Trotzdem im selben display_factory.h-Modell wie GC9A01/ILI9488/ST7796S
// nutzbar (ein Firmware-Image, Laufzeit-Auswahl) - deshalb wie diese in
// einen eigenen Namespace verpackt (kein #define fuer Name/Groesse/Pins,
// siehe gc9a01.h-Kommentar zum selben Thema) statt eigenstaendiger globaler
// Funktionen. Trotzdem NICHT Teil von display_factory.h selbst: andere
// Pin-Form (I2C SDA/SCL statt SPI), Arduino_Canvas_Mono statt eines
// SPI-Panels als `gfx` - board_generic.cpp behandelt es deshalb als eigenen
// Sonderfall neben DisplayType, siehe dort. Eigene UI-Achse (siehe CLAUDE.md
// "lcd_ui_shape_t", Mono ist eine andere UI, nicht dieselbe UI in klein) -
// das Dashboard selbst liegt in display_mono.h/.cpp, nicht hier (dieser
// Header bleibt reiner Treiber, analog zu gc9a01.h etc.).

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "pin_override.h"

namespace disp_ssd1309 {

inline constexpr const char *NAME   = "SSD1309";
inline constexpr bool HAS_TOUCH     = false;
inline constexpr bool HAS_SD        = false;
inline constexpr int16_t WIDTH      = 128;
inline constexpr int16_t HEIGHT     = 64;

// Default-Pins wie im Referenzprojekt (I2C ist der uebliche Anschluss fuer
// SSD1306/SSD1309-Breakouts; SPI-Variante existiert, hier nicht abgebildet).
// Strapping-Pins 2/8/9 auf dem C3 bewusst NICHT als Default gewaehlt fuer
// neue generische Boards waere sauberer, aber SDA=8/SCL=9 ist die bereits im
// README dokumentierte, auf echter Hardware verwendete Verkabelung - hier
// beibehalten statt grundlos zu aendern.
inline constexpr int8_t  DEFAULT_SDA  = 8;
inline constexpr int8_t  DEFAULT_SCL  = 9;
inline constexpr int8_t  DEFAULT_RST  = -1;
inline constexpr uint8_t DEFAULT_ADDR = 0x3C;

// I2C-Kommando-/Datenpraefixe nach SSD1306-Datenblatt-Konvention (Co=0,
// D/C#=0 fuer Kommandos, Co=0/D/C#=1 fuer Daten) - siehe Arduino_Wire-Nutzung
// im CanvasMonoOLED-Beispiel der GFX-Lib, dort dieselben Werte fuer SSD1306.
inline constexpr uint8_t I2C_COMMAND_PREFIX = 0x00;
inline constexpr uint8_t I2C_DATA_PREFIX    = 0x40;

inline Arduino_DataBus  *bus   = nullptr;
inline Arduino_G        *panel = nullptr; // roher SSD1306/1309-Treiber (kein Arduino_GFX, siehe oben)
inline Arduino_GFX      *gfx   = nullptr; // Arduino_Canvas_Mono - das, worauf LVGL tatsaechlich zeichnet

inline bool initDisplay(const pin_override_i2c_t *override = nullptr) {
  pin_override_i2c_t pins;
  pins.sda      = DEFAULT_SDA;
  pins.scl      = DEFAULT_SCL;
  pins.rst      = DEFAULT_RST;
  pins.i2c_addr = DEFAULT_ADDR;
  if (override) {
    if (override->sda != PIN_UNSET) pins.sda = override->sda;
    if (override->scl != PIN_UNSET) pins.scl = override->scl;
    if (override->rst != PIN_UNSET) pins.rst = override->rst;
    if (override->i2c_addr != 0)    pins.i2c_addr = override->i2c_addr;
  }

  // Wire.begin() hier explizit mit den (ggf. ueberschriebenen) Pins statt
  // Arduino_Wire das ueberlassen - Arduino_Wire::begin() ruft intern nur
  // Wire.begin() OHNE Pin-Argumente auf; auf bereits initialisiertem Bus ist
  // das ein no-op (siehe TwoWire::begin() im ESP32-Core: "Bus already
  // started" -> sofortiges true), die hier gesetzten Pins bleiben also
  // wirksam.
  Wire.begin(pins.sda, pins.scl);
  bus = new Arduino_Wire(pins.i2c_addr, I2C_COMMAND_PREFIX, I2C_DATA_PREFIX, &Wire);
  panel = new Arduino_SSD1306(bus, pins.rst, WIDTH, HEIGHT);
  // verticalByte=true: Framebuffer-Layout mit 8 vertikalen Pixeln pro Byte -
  // exakt das SSD1306/1309-eigene Page-Adressierungsformat, das
  // Arduino_SSD1306::drawBitmap() erwartet (siehe GFX-Lib-Beispiel
  // CanvasMonoOLED, dort dieselbe Kombination).
  gfx = new Arduino_Canvas_Mono(WIDTH, HEIGHT, panel, 0, 0, true);
  return gfx->begin();
}

// -----------------------------------------------------------------------------
// LVGL-Anbindung: eigene (kleine) Flush-Callback statt der gemeinsamen aus
// display_common.h, da Arduino_Canvas_Mono einen Software-Framebuffer ist -
// draw16bitRGBBitmap() aktualisiert nur den RAM-Puffer (per geerbtem
// Arduino_GFX::draw16bitRGBBitmap() -> writePixel() -> Arduino_Canvas_Mono::
// writePixelPreclipped(), die dort intern denselben Schwellwert-Trick nutzt:
// "irgendein Farbkanal hell genug" -> weisses Bit, sonst schwarz), das
// tatsaechliche Rausschreiben per I2C passiert erst in flush().
// -----------------------------------------------------------------------------
inline void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);

  if (lv_display_flush_is_last(disp)) {
    gfx->flush();
  }
  lv_display_flush_ready(disp);
}

inline void initLvglDisplay() {
  // alignas(LV_DRAW_BUF_ALIGN) noetig - siehe cyd_2432s028r.h fuer die
  // Begruendung (lv_display_set_buffers() prueft die Ausrichtung per Assert,
  // dessen Handler bei Fehlschlag ein stilles while(1); ist).
  static lv_color_t drawBuf[WIDTH * 16] alignas(LV_DRAW_BUF_ALIGN);
  lv_display_t *disp = lv_display_create(WIDTH, HEIGHT);
  lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
}

} // namespace disp_ssd1309
