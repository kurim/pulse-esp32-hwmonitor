#pragma once
#include <lvgl.h>

// Mono-Dashboard (SSD1309, 128x64 monochrom): Text/Balken statt Icons/Arcs
// (siehe display_mono.cpp fuer die Begruendung). Ausgelagert aus
// display_draw.cpp fuer bessere Uebersicht - wird ausschliesslich von dort
// aus aufgerufen (displayDrawInit()/displayDrawUpdate()), analog zu
// display_round.h.

// Baut das Mono-Dashboard auf scr neu auf. scr muss vorher bereits geleert
// sein (lv_obj_clean), siehe displayDrawInit().
void displayMonoBuild(lv_obj_t *scr);

// Aktualisiert Uhrzeit/CPU/GPU aus hw_info - no-op, wenn displayMonoBuild()
// noch nicht lief. Wertet dabei auch Standby aus (Signal-Timeout, siehe
// app_config.standby_timeout_s).
void displayMonoUpdate(void);
