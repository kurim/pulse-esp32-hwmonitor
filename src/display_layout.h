#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Editierbares Display-Layout ("Dashboard Editor" im WebUI) - siehe
// display_layout.cpp fuer die Widget-Registry und das Thread-Safety-Konzept.
// Nicht auf runden Displays nutzbar (siehe displaySupportsLayoutEditor()) -
// frei positionierte rechteckige Widgets wuerden an den Ecken vom runden
// Glas abgeschnitten (aktuell betrifft das nur GC9A01, 240x240 Framebuffer
// aber physisch rund - "quadratisch" war die falsche Auflosungs-basierte
// Abgrenzung, die eigentliche Achse ist rund vs. rechteckig, siehe
// CLAUDE.md "lcd_ui_shape_t"). Alle rechteckigen Displays (auch nicht-
// quadratische wie JC8048W550 800x480 oder ILI9488/ST7796S 320x480) sind
// dagegen unproblematisch.

// Baut das Layout aus einem JSON-Array von Widget-Objekten
// ({"id","type","x","y","w","h","props"}) neu auf. Loescht dabei den
// kompletten aktiven Screen (lv_obj_clean) - NUR aus dem LVGL-Task
// (main.cpp setup()/loop()) aufrufen, niemals direkt aus einem Web-Handler.
void layout_apply(JsonArrayConst widgets);

// Erstellt die interne Befehls-Queue - einmalig aus setup() aufrufen, bevor
// layout_queue_push_apply() von anderen Tasks genutzt werden kann.
void layout_queue_begin(void);

// Reiht ein komplettes Layout-JSON zur Anwendung ein - sicher aus jedem Task
// aufrufbar (insbesondere aus web_portal.cpp-Handlern, die im AsyncTCP-Task
// laufen). Nicht blockierend; liefert false, wenn die Queue voll ist.
bool layout_queue_push_apply(const String &json);

// Verarbeitet anstehende Befehle - NUR aus dem LVGL-Task (main.cpp loop())
// aufrufen, analog zu lv_timer_handler().
void layout_queue_process(void);

// Aktualisiert alle Widgets mit "bind"-Eigenschaft aus hw_info/weather_info -
// NUR aus dem LVGL-Task aufrufen, gedrosselt (nicht bei jeder loop()-Iteration
// noetig), siehe main.cpp.
void layout_refresh_bindings(void);

// true, wenn das aktuell aktive Display rund ist (aktuell nur GC9A01). Nur
// aussagekraeftig, wenn ueberhaupt ein Display vorhanden ist (siehe
// s_hasDisplay in main.cpp) - display_draw.cpp ruft das ausschliesslich aus
// Codepfaden auf, die das bereits sichergestellt haben.
bool displayIsRound(void);

// true, wenn das aktuell aktive Display monochrom ist (aktuell nur
// SSD1309) - eigene lcd_ui_shape_t-Achse neben rund/rechteckig, siehe
// CLAUDE.md. Analog zu displayIsRound() nur unter BOARD_GENERIC ueberhaupt
// wahr.
bool displayIsMono(void);

// true, wenn das aktuell aktive Display rechteckig ist UND ueberhaupt
// vorhanden (DISPLAY_NONE -> false). Rundes (GC9A01) oder monochromes
// (SSD1309) Display oder kein Display -> false.
bool displaySupportsLayoutEditor(void);
int16_t displayWidthPx(void);
int16_t displayHeightPx(void);

// Generiert das Grundlayout (Datum/Uhrzeit-, Wetter-, CPU-, GPU-Karte +
// Footer) fuer eckige Farbdisplays als Layout-JSON (dasselbe Format wie
// layout_apply()/layout_store_save() erwarten). Wird verwendet, wenn noch
// kein Layout im NVS gespeichert ist - siehe main.cpp (Boot-Fallback) und
// web_portal.cpp::handleGetLayout() (Editor zeigt denselben Default statt
// eines leeren Canvas).
String layout_default_json(int16_t w, int16_t h);

// Nur auf der CYD relevant (XPT2046-Touch) - true, solange Touch noch nie
// erfolgreich kalibriert wurde (immer false auf allen anderen Boards). Nach
// wifi_provision_is_settled()/vor displayDrawInit() in main.cpp abfragen,
// um bei Bedarf beginFirstBootTouchCalibration() statt des normalen
// Dashboards zu zeigen.
bool touchCalibrationNeeded(void);

// Zeigt den Kalibrier-Bildschirm direkt auf dem aktiven Screen (fuer den
// Erstboot-Fall, noch kein Einstellungen-Overlay vorhanden) - no-op auf
// Boards ohne XPT2046-Touch. app_config.touch_calibrated wird beim
// Abschliessen true (siehe display_layout.cpp), main.cpp kann das pollen,
// um zum normalen Boot-Ablauf zurueckzukehren.
void beginFirstBootTouchCalibration(void);
