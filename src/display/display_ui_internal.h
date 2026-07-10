#pragma once
// ------------------------------------------------------------------
// Interne Schnittstelle zwischen display_ui.cpp (Panel-/LVGL-Init, Touch,
// Standby-Dispatch) und den layoutspezifischen Dateien display_ui_rect.cpp /
// _round.cpp / _mono.cpp / _wide.cpp (je eine pro UI-Form, siehe
// board_profiles.h: lcd_ui_shape_t). Portiert 1:1 aus dem esp-idf-Branch.
// ------------------------------------------------------------------
#include "lvgl.h"
#include "board_profiles.h"
#include "../shared_state.h"
#include "ui_strings.h"
#include "../net/weather_service.h"
#include "mdi_icons.h"
#include "mdi_icons_10.h"

// ---- Farben ----
#define COL_BG       lv_color_hex(0x000000)
#define COL_CARD     lv_color_hex(0x171C28)
#define COL_ACCENT   lv_color_hex(0x00FFFF)   // CPU-Datenfarbe (Chart/Text)
#define COL_GPU      lv_color_hex(0xFFA400)   // GPU-Datenfarbe (Chart/Text)
#define COL_TEXT     lv_color_hex(0xFFFFFF)
#define COL_SUB      lv_color_hex(0x8893A8)
#define COL_WARN     lv_color_hex(0xF80000)
#define COL_GREEN    lv_color_hex(0x33DD55)
#define COL_YELLOW   lv_color_hex(0xEECC33)

#define COL_CARD_BORDER lv_color_hex(0x2FB6E0)
#define COL_CPU_BAR_A   lv_color_hex(0x2FB6E0)
#define COL_CPU_BAR_B   lv_color_hex(0x0D5A8A)
#define COL_GPU_BAR_A   lv_color_hex(0xE9E14C)
#define COL_GPU_BAR_B   lv_color_hex(0x6FCB4C)
#define COL_BADGE_BG    lv_color_hex(0x2FB6E0)
#define COL_BADGE_TEXT  lv_color_hex(0x04222A)
#define COL_THERMO      lv_color_hex(0xE0555A)
#define COL_RAIN        lv_color_hex(0x4FA6E0)

// ---- Panel-/UI-Zustand, gesetzt von display_ui.cpp ----
extern const board_profile_t *s_profile;
extern int s_hres, s_vres;
extern bool s_standby;

extern lv_obj_t *scr_main;

enum { SCR_MAIN = 0, SCR_CPU = 1, SCR_GPU = 2, SCR_SETTINGS = 3 };
extern int s_screen;
extern lv_obj_t *scr_detail, *scr_settings, *scr_standby, *scr_boot;

// ---- Gemeinsame Style-Helfer (Definition in display_ui.cpp) ----
void style_screen(lv_obj_t *scr);
lv_obj_t *make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t col);

// ---- Kachel-UI (LCD_SHAPE_RECT: CYD/ILI9488/ST7796S) - display_ui_rect.cpp ----
void build_main(void);
void build_detail(void);
void build_settings(void);
void build_standby(void);
void build_boot(void);
void refresh_now(void);

// ---- Rundes Minimal-UI (GC9A01) - display_ui_round.cpp (Phase 2) ----
void build_round_ui(void);
void refresh_round_ui(void);
void round_ui_cycle_screen(void);

// ---- Monochromes Minimal-UI (SSD1309) - display_ui_mono.cpp (Phase 2) ----
void build_mono_ui(void);
void refresh_mono_ui(void);

// ---- Dashboard-UI fuer grosse Panels (Guition JC8048W550) - display_ui_wide.cpp (Phase 3) ----
void build_wide_ui(void);
void refresh_wide_ui(void);
