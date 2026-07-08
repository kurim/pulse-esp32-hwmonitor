// LVGL9-Konfiguration fuer den Arduino/PlatformIO-Rewrite - Aequivalent zu
// den LVGL-CONFIG_LV_*-Optionen in sdkconfig.defaults (esp-idf-Branch).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_CHART   1
#define LV_USE_ARC     1
#define LV_USE_BAR     1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_UNSCII_8      1 // fuer das monochrome SSD1309-UI (Phase 2)

#define LV_FONT_DEFAULT &lv_font_montserrat_14

// TEMP-DEBUG: LV_USE_LOG war 0 - LVGL-interne Fehler (z.B. fehlgeschlagene
// Allokierungen im 48KB-Pool) waren dadurch unsichtbar. Nutzer berichtet:
// Uhr/CPU-GPU-Kacheln/Wetter/ein testweise hinzugefuegtes Debug-Label
// aktualisieren sich nach dem initialen Aufbau nie wieder, obwohl
// pushImage() selbst nachweislich (Stufe-E-Test) bei wiederholten,
// isolierten Aufrufen einwandfrei funktioniert - der Fehler liegt also
// zwischen LVGL und dem Flush, nicht in der Uebertragung. Logging an, um
// LVGL-interne Fehlermeldungen (z.B. Out-of-Memory bei nachfolgenden
// Label-Text-Allokierungen) sichtbar zu machen, plus LV_MEM_SIZE grosszuegig
// erhoeht (48KB war fuer 4 Screens + Chart-Widget + mehrere Fonts eng) als
// naheliegendster Verdaechtiger. Nach der Fehlersuche ggf. wieder senken.
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

// 128KB (Sprung von 48 -> 128KB) sprengte das DRAM-Segment auf dem
// klassischen ESP32 (kein PSRAM) um 67520 Byte - der verfuegbare Spielraum
// oberhalb von 48KB liegt also nur bei ~14KB (80KB angefordert, 67.5KB
// Ueberlauf). Auf 56KB reduziert (+8KB, mit Sicherheitsabstand zum
// errechneten Maximum von ~62KB) statt den Build komplett zu blockieren.
#define LV_MEM_SIZE (56 * 1024U)

#endif // LV_CONF_H
