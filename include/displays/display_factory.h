#pragma once

// Bindet alle SPI-TFT-Displays aus include/displays/ zu einer zur Laufzeit
// waehlbaren Einheit zusammen. Ein Firmware-Image enthaelt alle Treiber,
// die Auswahl passiert ueber DisplayType (z.B. aus NVS gelesen) statt
// ueber #ifdef/Compile-Zeit-Auswahl.
//
// SSD1309 ist bewusst NICHT hier drin: monochromes OLED ohne Arduino_GFX-
// Unterstuetzung, eigener Adafruit_SSD1306-Treiber, eigene LVGL-Anbindung -
// eine andere UI-Achse (siehe CLAUDE.md "lcd_ui_shape_t"), kein Ersatz-
// Panel fuer dieselbe Arduino_GFX*-Schnittstelle.

#include "gc9a01.h"
#include "ili9488.h"
#include "st7796s.h"

enum class DisplayType : uint8_t {
  GC9A01 = 0,
  ILI9488,
  ST7796S,
};

inline Arduino_GFX  *gfx         = nullptr;
inline const char   *g_boardName = "";
inline int16_t       g_lcdWidth  = 0;
inline int16_t       g_lcdHeight = 0;

inline bool initBoardDisplay(DisplayType type, const pin_override_t *override = nullptr) {
  bool ok = false;
  switch (type) {
    case DisplayType::GC9A01:
      ok = disp_gc9a01::initDisplay(override);
      gfx = disp_gc9a01::gfx;
      g_boardName = disp_gc9a01::NAME;
      g_lcdWidth  = disp_gc9a01::WIDTH;
      g_lcdHeight = disp_gc9a01::HEIGHT;
      break;
    case DisplayType::ILI9488:
      ok = disp_ili9488::initDisplay(override);
      gfx = disp_ili9488::gfx;
      g_boardName = disp_ili9488::NAME;
      g_lcdWidth  = disp_ili9488::WIDTH;
      g_lcdHeight = disp_ili9488::HEIGHT;
      break;
    case DisplayType::ST7796S:
      ok = disp_st7796s::initDisplay(override);
      gfx = disp_st7796s::gfx;
      g_boardName = disp_st7796s::NAME;
      g_lcdWidth  = disp_st7796s::WIDTH;
      g_lcdHeight = disp_st7796s::HEIGHT;
      break;
  }
  return ok;
}

// initLvglDisplay() heisst hier bewusst anders als bei den fest verdrahteten
// Boards (siehe boards/cyd_2432s028r.h etc.) - board_generic.cpp bindet
// beide Header ein und braucht die eigene void-Variante fuer den main.cpp-
// Vertrag, ohne mit dieser hier (liefert das lv_display_t* zurueck) zu
// kollidieren.
inline lv_display_t *initLvglDisplaySelected() {
  return initLvglDisplaySPI(gfx, g_lcdWidth, g_lcdHeight);
}
