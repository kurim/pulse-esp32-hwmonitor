#pragma once
#include <lvgl.h>

// Rundes Dashboard (GC9A01): zwei konzentrische Arcs (CPU/GPU) + Uhrzeit +
// Wetter. Ausgelagert aus display_draw.cpp fuer bessere Uebersicht - wird
// ausschliesslich von dort aus aufgerufen (displayDrawInit()/
// displayDrawUpdate()), nicht direkt aus main.cpp.

// Baut das runde Dashboard auf scr neu auf (Arcs, Labels). scr muss vorher
// bereits geleert sein (lv_obj_clean), siehe displayDrawInit().
void displayRoundBuild(lv_obj_t *scr);

// Aktualisiert CPU/GPU/Uhrzeit/Wetter aus hw_info/weather_info - no-op,
// wenn displayRoundBuild() noch nicht lief. Wertet dabei auch Standby aus
// (Signal-Timeout und/oder Button, siehe displayRoundButtonPoll()).
void displayRoundUpdate(void);

// Liest die Standby-Taste (BOOT-Taste, jedes generische Devkit - Pin ist
// chip-abhaengig, siehe display_round.cpp) - unabhaengig vom 500ms-Update-
// Takt aus displayRoundUpdate() aufrufen, damit ein kurzer Tastendruck
// nicht zwischen zwei Polls verloren geht. No-op auf CYD/JC8048W550.
// Merkt sich einen Tastendruck bis zum naechsten displayRoundUpdate()-Aufruf.
void displayRoundButtonPoll(void);
