#pragma once

// Monochrome 128x64 I2C-OLEDs: SSD1306/SSD1309 (128 native GDDRAM-Spalten)
// und SH1106/SSD1315 (132 Spalten, z.B. das als "SSD1306-kompatibel"
// verkaufte GME12864-77 - mit dem falschen der beiden Treiber gewaehlt
// zeigt das Panel zwar etwas an, aber horizontal verschoben). Ein Header
// statt zwei getrennter Dateien: die beiden Chips unterscheiden sich fuer
// uns nur in der Arduino_GFX-Klasse (Arduino_SSD1306 vs. Arduino_SH1106,
// via Chip-Parameter unten gewaehlt) - alles andere (Pins, LVGL-Anbindung,
// Bus-Workarounds) ist identisch, eine Aufspaltung haette nur Code und
// (durch je einen eigenen LVGL-Draw-Buffer) RAM dupliziert.
//
// Nutzt Arduino_GFX' eigene SSD1306/SH1106-Treiber statt einer
// zusaetzlichen Adafruit-Abhaengigkeit - "GFX Library for Arduino" ist
// ohnehin schon fuer alle anderen Displays Pflicht-lib_dep. Beide Treiber
// sind selbst kein Arduino_GFX (nur Arduino_G, kennen nur drawBitmap() mit
// fest verdrahteter Vollbild-Adressierung) - deshalb hier zusaetzlich in
// Arduino_Canvas_Mono gewrappt, das den eigentlichen 1bpp-Software-
// Framebuffer haelt, den LVGL bemalt.
//
// Eigene UI-Achse (siehe CLAUDE.md "lcd_ui_shape_t", Mono ist eine andere UI,
// nicht dieselbe UI in klein) - das Dashboard laeuft ueber den generischen
// Layout-Editor (eigene mono_*-Widget-Typen, siehe display_layout.cpp),
// dieser Header bleibt reiner Treiber. NICHT Teil von display_factory.h:
// andere Pin-Form (I2C statt SPI), Arduino_Canvas_Mono statt eines
// SPI-Panels als `gfx` - board_generic.cpp behandelt es als eigenen
// Sonderfall neben DisplayType.

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "pin_override.h"

namespace disp_mono_oled_i2c {

inline constexpr bool HAS_TOUCH = false;
inline constexpr bool HAS_SD    = false;
inline constexpr int16_t WIDTH  = 128;
inline constexpr int16_t HEIGHT = 64;

// SDA=8/SCL=9: bereits im README dokumentierte, auf echter Hardware
// verwendete Verkabelung. Identisch fuer beide Chips (derselbe I2C-Bus,
// nur unterschiedlicher Controller dahinter).
inline constexpr int8_t  DEFAULT_SDA  = 8;
inline constexpr int8_t  DEFAULT_SCL  = 9;
inline constexpr int8_t  DEFAULT_RST  = -1;
inline constexpr uint8_t DEFAULT_ADDR = 0x3C;

// I2C-Kommando-/Datenpraefixe nach SSD1306-Datenblatt-Konvention (Co=0,
// D/C#=0 fuer Kommandos, Co=0/D/C#=1 fuer Daten) - SH1106 teilt sich dieselbe
// Konvention.
inline constexpr uint8_t I2C_COMMAND_PREFIX = 0x00;
inline constexpr uint8_t I2C_DATA_PREFIX    = 0x40;

enum class Chip { kSsd1306, kSh1106 };

// Arduino_Wire::begin() ist fuer unseren Fall aus zwei Gruenden ungeeignet,
// beide auf echter Hardware reproduziert:
// 1. Es nimmt keine SDA/SCL-Parameter entgegen (nur Wire.begin() ohne
//    Pins), custom Pins brauchen also vorher einen eigenen Wire.begin(sda,
//    scl)-Aufruf. Der zweite, redundante Wire.begin() aus
//    Arduino_Wire::begin() ist auf dem neuen i2c-ng-Treiber
//    (arduino-esp32 >=3.x) kein harmloses No-Op mehr - er hat den
//    I2C-Bus-Handle nachweislich ungueltig gemacht
//    ("i2c_master_transmit failed: ESP_ERR_INVALID_STATE" bei jedem
//    folgenden Transfer).
// 2. Es macht ausserdem einen eigenen I2C-Adress-Probe und liefert bei
//    einem nicht sofort ack'enden Geraet false - auf derselben Hardware
//    bereits als Fehlalarm aufgetreten (Panel war nachweislich da und
//    ansprechbar).
// Diese Klasse ueberspringt beides und setzt _speed (protected, per
// Vererbung erreichbar) stattdessen sauber auf 100kHz statt undefiniert zu
// bleiben (100kHz statt 400kHz: der ~1KB-Vollbild-Push bei jedem Flush ist
// auf Breadboard-/Jumper-Verkabelung empfindlicher als ein einzelnes
// Kommando).
class MonoI2CBus : public Arduino_Wire {
public:
  using Arduino_Wire::Arduino_Wire;

  bool begin(int32_t speed = GFX_NOT_DEFINED, int8_t dataMode = GFX_NOT_DEFINED) override {
    (void)dataMode;
    _speed = (speed == GFX_NOT_DEFINED) ? 100000 : speed;
    return true;
  }
};

inline Arduino_DataBus *bus   = nullptr;
inline Arduino_G       *panel = nullptr; // roher SSD1306/SH1106-Treiber (kein Arduino_GFX, siehe oben)
inline Arduino_GFX     *gfx   = nullptr; // Arduino_Canvas_Mono - das, worauf LVGL tatsaechlich zeichnet

inline bool initDisplay(const pin_override_i2c_t *override, Chip chip) {
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

  Wire.begin(pins.sda, pins.scl);
  bus = new MonoI2CBus(pins.i2c_addr, I2C_COMMAND_PREFIX, I2C_DATA_PREFIX, &Wire);
  panel = (chip == Chip::kSh1106)
              ? static_cast<Arduino_G *>(new Arduino_SH1106(bus, pins.rst, WIDTH, HEIGHT))
              : static_cast<Arduino_G *>(new Arduino_SSD1306(bus, pins.rst, WIDTH, HEIGHT));
  // verticalByte=true: 8 vertikale Pixel pro Byte, das native
  // Page-Adressierungsformat, das beide Treiber's drawBitmap() erwarten.
  gfx = new Arduino_Canvas_Mono(WIDTH, HEIGHT, panel, 0, 0, true);

  bool ok = gfx->begin();
  if (ok) {
    // Arduino_Canvas_Mono's Framebuffer ist malloc()'t (nicht genullt), und
    // die GDDRAM des Controllers selbst startet nach Power-on ebenfalls mit
    // Zufallsinhalt - einmal explizit loeschen und pushen, bevor LVGL
    // anfaengt, einzelne Kacheln zu bemalen.
    gfx->fillScreen(0);
    gfx->flush();
  }
  return ok;
}

// Eigene (kleine) Flush-Callback statt der gemeinsamen aus
// display_common.h: Arduino_Canvas_Mono ist ein Software-Framebuffer,
// draw16bitRGBBitmap() aktualisiert nur den RAM-Puffer, das tatsaechliche
// Rausschreiben per I2C passiert erst in flush().
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

} // namespace disp_mono_oled_i2c
