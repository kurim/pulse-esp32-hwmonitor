#pragma once
// ------------------------------------------------------------------
// Interne Schnittstelle zwischen display_ui.c (Panel-/LVGL-Init, Touch,
// Navigationstaste, Standby-Dispatch) und den layoutspezifischen Dateien
// display_ui_rect.c / display_ui_round.c / display_ui_mono.c (je eine pro
// UI-Form, siehe board_profiles.h: lcd_ui_shape_t). Kein Public-Header -
// wird nur von den display_ui*.c-Dateien eingebunden, main/display_ui.h
// bleibt die einzige nach aussen sichtbare API (display_ui_begin()).
//
// Aufteilung ist nach UI-FORM, nicht nach Panel-Modell: ILI9341/ILI9488/
// ST7796S (alle LCD_SHAPE_RECT) teilen sich dieselbe Kachel-UI in
// display_ui_rect.c - eine Datei pro Panel-Modell wuerde diesen Code
// verdreifachen, obwohl nur GC9A01 (rund) und SSD1309 (mono) tatsaechlich
// eigene Layouts brauchen.
// ------------------------------------------------------------------
#include "lvgl.h"
#include "board_profiles.h"
#include "shared_state.h"
#include "ui_strings.h"
#include "weather_service.h"
#include "mdi_icons.h"
#include "mdi_icons_10.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Farben (von allen drei Layouts + dem gemeinsamen Kern genutzt) ----
#define COL_BG       lv_color_hex(0x000000)
#define COL_CARD     lv_color_hex(0x171C28)
#define COL_ACCENT   lv_color_hex(0x00FFFF)   // CPU-Datenfarbe (Chart/Text)
#define COL_GPU      lv_color_hex(0xFFA400)   // GPU-Datenfarbe (Chart/Text)
#define COL_TEXT     lv_color_hex(0xFFFFFF)
#define COL_SUB      lv_color_hex(0x8893A8)
#define COL_WARN     lv_color_hex(0xF80000)
#define COL_GREEN    lv_color_hex(0x33DD55)
#define COL_YELLOW   lv_color_hex(0xEECC33)

// Karten-Rahmen (beide Kacheln teilen sich denselben "Frame"-Farbton;
// Unterscheidung erfolgt ueber Balken-Gradient + "3D"-Badge auf GPU).
#define COL_CARD_BORDER lv_color_hex(0x2FB6E0)
#define COL_CPU_BAR_A   lv_color_hex(0x2FB6E0)
#define COL_CPU_BAR_B   lv_color_hex(0x0D5A8A)
#define COL_GPU_BAR_A   lv_color_hex(0xE9E14C)
#define COL_GPU_BAR_B   lv_color_hex(0x6FCB4C)
#define COL_BADGE_BG    lv_color_hex(0x2FB6E0)
#define COL_BADGE_TEXT  lv_color_hex(0x04222A)
#define COL_THERMO      lv_color_hex(0xE0555A)
#define COL_RAIN        lv_color_hex(0x4FA6E0)

// ---- Panel-/UI-Zustand, gesetzt von display_ui.c (lcd_init()) ----
extern const board_profile_t *s_profile;
extern int s_hres, s_vres;
extern bool s_standby;

// Aktuell geladener Screen (immer genau einer der drei Layouts aktiv, siehe
// display_ui_begin()) - gemeinsames Objekt, da alle drei build_*_ui()-
// Funktionen ihren Hauptschirm hier ablegen und display_ui.c ihn laedt.
extern lv_obj_t *scr_main;

// Nur von display_ui_rect.c genutzt, aber von display_ui.c's
// enter_standby()/exit_standby() direkt gebraucht (Umschalten auf den
// Standby-Screen bzw. zurueck zum zuletzt aktiven Rect-Screen).
enum { SCR_MAIN = 0, SCR_CPU = 1, SCR_GPU = 2, SCR_SETTINGS = 3 };
extern int s_screen;
extern lv_obj_t *scr_detail, *scr_settings, *scr_standby;

// ---- Gemeinsame Style-Helfer (Definition in display_ui.c) ----
void style_screen(lv_obj_t *scr);
lv_obj_t *make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t col);

// ---- Kachel-UI (LCD_SHAPE_RECT: CYD/ILI9488/ST7796S) - display_ui_rect.c ----
void build_main(void);
void build_detail(void);
void build_settings(void);
void build_standby(void);
void refresh_now(void);

// ---- Rundes Minimal-UI (GC9A01) - display_ui_round.c ----
void build_round_ui(void);
void refresh_round_ui(void);
// Wechselt den Screen des runden UIs (Overview/Wetter) - Aufruf aus
// display_ui.c's nav_button_cb(), haelt s_round_screen/ROUND_SCR_COUNT
// als Implementierungsdetail von display_ui_round.c.
void round_ui_cycle_screen(void);

// ---- Monochromes Minimal-UI (SSD1309) - display_ui_mono.c ----
void build_mono_ui(void);
void refresh_mono_ui(void);

// ---- Dashboard-UI fuer grosse Panels (Guition JC8048W550) - display_ui_wide.c ----
// Eigenstaendig wie die drei UIs oben (kein gemeinsamer Code mit
// display_ui_rect.c) - Verlaufsgrafen sitzen direkt auf der Hauptseite statt
// auf einer eigenen Detailseite, siehe build_wide_ui().
void build_wide_ui(void);
void refresh_wide_ui(void);

#ifdef __cplusplus
}
#endif
