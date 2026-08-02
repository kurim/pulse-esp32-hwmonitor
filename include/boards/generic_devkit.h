#pragma once

// -----------------------------------------------------------------------------
// Generisches ESP32/ESP32-S3/ESP32-C3-Devkit: kein fest verdrahtetes Display.
// Welches Panel (falls ueberhaupt eins angeschlossen ist) genutzt wird, steht
// erst zur Laufzeit in app_config.display_type (siehe shared_state.h,
// Webportal-Tab "Anzeige"). DISPLAY_NONE ist der Default: headless booten,
// Webportal bleibt erreichbar (siehe CLAUDE.md).
//
// Anders als die fest verdrahteten Boards (cyd_2432s028r.h, ...) sind die
// Funktionen hier NICHT inline im Header definiert, sondern nur deklariert -
// die Implementierung (src/boards/board_generic.cpp) braucht app_config,
// das an dieser Stelle des Include-Baums (board_config.h wird von
// shared_state.h VOR der app_config_t-Deklaration eingebunden) noch nicht
// sichtbar ist.
// -----------------------------------------------------------------------------
#define BOARD_NAME "Generic ESP32/S3/C3"

#include <stdint.h>

// true, wenn app_config.display_type ein tatsaechliches Panel benennt (nicht
// DISPLAY_NONE) - main.cpp ruft initBoardDisplay() bei bewusst headless
// konfigurierten Geraeten gar nicht erst auf.
bool boardHasDisplay();

bool initBoardDisplay();
void initLvglDisplay();
void boardDisplayBacklight(bool on);
void boardDisplaySetBrightness(uint8_t level);
