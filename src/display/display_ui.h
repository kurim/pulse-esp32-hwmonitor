#pragma once

// Initialisiert das in app_config.display_type gewaehlte Panel (LovyanGFX),
// LVGL9 und ggf. Touch, baut die dazu passende Oberflaeche auf.
//
// Migrations-Phase 1: nur DISPLAY_CYD_ILI9341 ist tatsaechlich verkabelt -
// jeder andere Wert verhaelt sich wie DISPLAY_NONE (Panel-Init wird
// uebersprungen, Webportal bleibt trotzdem erreichbar), bis die Profile in
// Phase 2/3 folgen. Siehe Migrationsplan.
void display_ui_begin(void);

// Muss regelmaessig aus loop() aufgerufen werden (LVGL-Tick/Timer-Handler -
// esp_lvgl_port lief im esp-idf-Original in einem eigenen Task, hier laeuft
// alles im Arduino loop()).
void display_ui_loop(void);
