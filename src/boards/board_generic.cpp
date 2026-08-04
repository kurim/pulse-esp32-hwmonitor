#include "board_config.h"

#if defined(BOARD_GENERIC)

#include <Arduino.h>
#include "shared_state.h"
#include "displays/display_factory.h"
#include "displays/mono_oled_i2c.h"

namespace {

// display_type_t (shared_state.h, das volle Board-Profil inkl. CYD/JC8048)
// auf DisplayType (display_factory.h, nur die selbst verdrahtbaren SPI-
// Panels) abbilden. DISPLAY_NONE sowie die Werte der fest verdrahteten
// Boards sind auf generischer Hardware nicht moeglich - false zurueckgeben.
// SSD1309/SH1106 bewusst NICHT hier drin: anderer Treibertyp, eigener
// I2C-Pfad statt display_factory.h - siehe die Sonderfaelle unten in
// boardHasDisplay()/initBoardDisplay()/initLvglDisplay().
bool mapDisplayType(display_type_t in, DisplayType &out) {
  switch (in) {
    case DISPLAY_GENERIC_GC9A01:  out = DisplayType::GC9A01;  return true;
    case DISPLAY_GENERIC_ILI9488: out = DisplayType::ILI9488; return true;
    case DISPLAY_GENERIC_ST7796S: out = DisplayType::ST7796S; return true;
    default: return false;
  }
}

bool isMonoI2c(display_type_t t) {
  return t == DISPLAY_GENERIC_SSD1309 || t == DISPLAY_GENERIC_SH1106;
}

} // namespace

bool boardHasDisplay() {
  if (isMonoI2c(app_config.display_type)) return true;
  DisplayType t;
  return mapDisplayType(app_config.display_type, t);
}

bool initBoardDisplay() {
  if (isMonoI2c(app_config.display_type)) {
    bool sh1106 = app_config.display_type == DISPLAY_GENERIC_SH1106;
    auto chip = sh1106 ? disp_mono_oled_i2c::Chip::kSh1106 : disp_mono_oled_i2c::Chip::kSsd1306;
    bool ok = disp_mono_oled_i2c::initDisplay(&app_config.mono_i2c_pins, chip);
    // g_lcdWidth/g_lcdHeight/g_boardName sind eigentlich display_factory.h-
    // Globals (Arduino_GFX-Pfad) - hier trotzdem mitgesetzt, damit
    // displayWidthPx()/displayHeightPx()/BOARD_NAME (siehe display_layout.cpp,
    // web_portal.cpp) fuer SSD1309/SH1106 ohne weitere Sonderfaelle
    // funktionieren.
    g_lcdWidth  = disp_mono_oled_i2c::WIDTH;
    g_lcdHeight = disp_mono_oled_i2c::HEIGHT;
    g_boardName = sh1106 ? "SH1106" : "SSD1309";
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
  if (isMonoI2c(app_config.display_type)) {
    disp_mono_oled_i2c::initLvglDisplay();
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
