#include "board_config.h"

#if defined(BOARD_GENERIC)

#include <Arduino.h>
#include "shared_state.h"
#include "displays/display_factory.h"
#include "displays/ssd1309.h"

namespace {

// display_type_t (shared_state.h, das volle Board-Profil inkl. CYD/JC8048)
// auf DisplayType (display_factory.h, nur die selbst verdrahtbaren SPI-
// Panels) abbilden. DISPLAY_NONE sowie die Werte der fest verdrahteten
// Boards sind auf generischer Hardware nicht moeglich - false zurueckgeben.
// SSD1309 bewusst NICHT hier drin: anderer Treibertyp (Adafruit_GFX statt
// Arduino_GFX), eigener I2C-Pfad statt display_factory.h - siehe die
// SSD1309-Zweige unten in boardHasDisplay()/initBoardDisplay()/
// initLvglDisplay().
bool mapDisplayType(display_type_t in, DisplayType &out) {
  switch (in) {
    case DISPLAY_GENERIC_GC9A01:  out = DisplayType::GC9A01;  return true;
    case DISPLAY_GENERIC_ILI9488: out = DisplayType::ILI9488; return true;
    case DISPLAY_GENERIC_ST7796S: out = DisplayType::ST7796S; return true;
    default: return false;
  }
}

} // namespace

bool boardHasDisplay() {
  if (app_config.display_type == DISPLAY_GENERIC_SSD1309) return true;
  DisplayType t;
  return mapDisplayType(app_config.display_type, t);
}

bool initBoardDisplay() {
  if (app_config.display_type == DISPLAY_GENERIC_SSD1309) {
    bool ok = disp_ssd1309::initDisplay(&app_config.ssd1309_pins);
    // g_lcdWidth/g_lcdHeight/g_boardName sind eigentlich display_factory.h-
    // Globals (Arduino_GFX-Pfad) - hier trotzdem mitgesetzt, damit
    // displayWidthPx()/displayHeightPx()/BOARD_NAME (siehe display_layout.cpp,
    // web_portal.cpp) fuer SSD1309 ohne weitere Sonderfaelle funktionieren.
    g_lcdWidth  = disp_ssd1309::WIDTH;
    g_lcdHeight = disp_ssd1309::HEIGHT;
    g_boardName = disp_ssd1309::NAME;
    return ok;
  }

  DisplayType t;
  if (!mapDisplayType(app_config.display_type, t)) {
    return false; // DISPLAY_NONE: kein Fehler, es ist nur kein Panel konfiguriert
  }
  // Pin-Overrides kommen direkt aus app_config (vom config_store_load() in
  // main.cpp bereits aus NVS geladen, per Webportal editierbar) - kein
  // eigener zweiter NVS-Zugriff hier noetig.
  return ::initBoardDisplay(t, &app_config.pin_overrides);
}

void initLvglDisplay() {
  if (app_config.display_type == DISPLAY_GENERIC_SSD1309) {
    disp_ssd1309::initLvglDisplay();
    return;
  }
  initLvglDisplaySelected();
}

void boardDisplayBacklight(bool /*on*/) {
  // Kein eigener Zugriff auf den BL-Pin noetig - initBoardDisplay() (siehe
  // include/displays/gc9a01.h, ili9488.h, st7796s.h) schaltet das Backlight
  // bereits beim initDisplay() HIGH, sofern ein gueltiger Pin konfiguriert
  // ist. Ein separates "aus" gibt es fuer generische Panels bewusst nicht.
}

void boardDisplaySetBrightness(uint8_t /*level*/) {
  // Kein PWM-Dimmen fuer generische Panels: der BL-Pin variiert je
  // gewaehltem Displaytyp und wird aktuell nur an/aus geschaltet (siehe
  // initBoardDisplay() oben). BOARD_HAS_BACKLIGHT_PWM bleibt fuer
  // BOARD_GENERIC deshalb undefiniert - das Webportal blendet den Regler
  // entsprechend aus.
}

#endif // BOARD_GENERIC
