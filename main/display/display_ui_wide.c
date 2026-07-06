// ------------------------------------------------------------------
// Dashboard-UI fuer grosse Panels (aktuell nur Guition JC8048W550, 800x480,
// LCD_SHAPE_WIDE). Eigenstaendig wie display_ui_rect/_round/_mono.c (kein
// gemeinsamer Code, siehe display_ui_internal.h) - einziger inhaltlicher
// Unterschied zum Kachel-UI in display_ui_rect.c: CPU/GPU-Verlaufsgrafen
// sitzen direkt auf der Hauptseite (genug Platz auf 800x480), es gibt daher
// keine eigene Detail-Unterseite/kein SCR_CPU/SCR_GPU. Einstellungen bleiben
// eine eigene Unterseite (Web-Portal-Hinweis, AP-Reset-Knopf etc. brauchen
// nur selten Platz auf der Hauptseite).
// ------------------------------------------------------------------
#include "display_ui_internal.h"
#include "web_portal.h"

#include "esp_system.h" // esp_get_free_heap_size (Settings-Screen)
#include <time.h>
#include <string.h>
#include <stdio.h>

#define TOPBAR_H 96
#define CARD_Y   104
#define CARD_H   368

typedef struct {
    lv_obj_t *load_lbl;
    lv_obj_t *bar;
    lv_obj_t *temp_lbl;
    lv_obj_t *power_lbl;
    lv_obj_t *trend_lbl;
    lv_obj_t *chart;
    lv_chart_series_t *ser;
} wide_tile_ctx_t;
static wide_tile_ctx_t w_cpu_tile, w_gpu_tile;

// Hauptschirm-Widgets
static lv_obj_t *w_lbl_time, *w_lbl_date;
static lv_obj_t *w_lbl_weather, *w_lbl_humidity, *w_lbl_wind, *w_lbl_rain;
static lv_obj_t *w_icon_status;
static lv_obj_t *w_lbl_waiting;

// Settings-Screen-Widgets
static lv_obj_t *w_set_fw, *w_set_ip, *w_set_wifi, *w_set_mqtt, *w_set_heap;
static lv_obj_t *w_set_ap_btn, *w_set_ap_btn_lbl;
static bool s_ap_confirm_armed = false;
static lv_timer_t *s_ap_confirm_timer = NULL;

// Standby-Screen-Widgets
static lv_obj_t *w_standby_lbl_time, *w_standby_lbl_date, *w_standby_lbl_weather;

// ------------------------------------------------------------------
// Software-"Light Mode" (app_config.color_invert wird fuer dieses Panel
// NICHT als Hardware-Pixelinversion genutzt, siehe lcd_init_rgb() in
// display_ui.c - eine blanke Hardware-Invertierung dreht alle Farben
// gleichermassen um und macht z.B. aus der gelben Sonne ein Blau. Stattdessen
// nur Hintergrund/Karten/Text/Sekundaertext hier per eigenem Mini-Theme
// getauscht; Icon-/Akzentfarben (COL_YELLOW/COL_RAIN/COL_ACCENT/COL_GPU/...)
// bleiben unverändert dieselben wie im Dark Mode. Wird einmalig beim
// Bildschirmaufbau berechnet (Umschalten der Checkbox greift wie bei
// Displaytyp/Rotation/Sprache erst nach einem Neustart).
// ------------------------------------------------------------------
static lv_color_t W_BG, W_CARD, W_TEXT, W_SUB;

static void init_wide_theme(void)
{
    if (app_config.color_invert) {
        W_BG   = lv_color_hex(0xEDEFF3);
        W_CARD = lv_color_hex(0xFFFFFF);
        W_TEXT = lv_color_hex(0x12151B);
        W_SUB  = lv_color_hex(0x4B5566);
    } else {
        W_BG   = COL_BG;
        W_CARD = COL_CARD;
        W_TEXT = COL_TEXT;
        W_SUB  = COL_SUB;
    }
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t border)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, W_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_color(c, border, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *shape_dot(lv_obj_t *parent, int w, int h, int radius, lv_color_t col)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_set_style_bg_color(r, col, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
    return r;
}

static void compute_trend(const history_t *h, const char **sym, lv_color_t *col)
{
    if (h->count < 4) { *sym = "-"; *col = W_SUB; return; }
    int back = (h->count > 10) ? 10 : (h->count - 1);
    float newest = h->load[HIST_LEN - 1];
    float older  = h->load[HIST_LEN - 1 - back];
    float diff = newest - older;
    if (diff > 3.0f)       { *sym = LV_SYMBOL_UP;   *col = COL_GREEN; }
    else if (diff < -3.0f) { *sym = LV_SYMBOL_DOWN; *col = COL_WARN;  }
    else                   { *sym = "-";             *col = W_SUB;  }
}

// ------------------------------------------------------------------
// Navigation
// ------------------------------------------------------------------
static void settings_click_cb(lv_event_t *e)
{
    (void)e;
    s_screen = SCR_SETTINGS;
    lv_screen_load(scr_settings);
    refresh_wide_ui();
}

static void settings_back_cb(lv_event_t *e)
{
    (void)e;
    s_screen = SCR_MAIN;
    lv_screen_load(scr_main);
    refresh_wide_ui();
}

static void ap_confirm_revert_cb(lv_timer_t *t)
{
    (void)t;
    s_ap_confirm_armed = false;
    lv_label_set_text(w_set_ap_btn_lbl, ui_str(UI_STR_AP_BTN_IDLE));
    lv_obj_set_style_bg_color(w_set_ap_btn, W_CARD, 0);
    s_ap_confirm_timer = NULL;
}

static void ap_btn_click_cb(lv_event_t *e)
{
    (void)e;
    if (!s_ap_confirm_armed) {
        s_ap_confirm_armed = true;
        lv_label_set_text(w_set_ap_btn_lbl, ui_str(UI_STR_AP_BTN_CONFIRM));
        lv_obj_set_style_bg_color(w_set_ap_btn, COL_WARN, 0);
        if (s_ap_confirm_timer) lv_timer_delete(s_ap_confirm_timer);
        s_ap_confirm_timer = lv_timer_create(ap_confirm_revert_cb, 3000, NULL);
        lv_timer_set_repeat_count(s_ap_confirm_timer, 1);
    } else {
        if (s_ap_confirm_timer) { lv_timer_delete(s_ap_confirm_timer); s_ap_confirm_timer = NULL; }
        s_ap_confirm_armed = false;
        lv_label_set_text(w_set_ap_btn_lbl, ui_str(UI_STR_AP_BTN_SWITCHING));
        lv_obj_set_style_bg_color(w_set_ap_btn, W_CARD, 0);
        web_portal_force_ap();
    }
}

// ------------------------------------------------------------------
// Kachel mit eingebettetem Verlaufsgraf (ersetzt die separate Detailseite
// des Kachel-UIs - auf 800x480 passt beides auf einen Blick).
// ------------------------------------------------------------------
static void build_tile(lv_obj_t *parent, int x, int w, const char *title, wide_tile_ctx_t *tile,
                       bool is_gpu, lv_color_t bar_a, lv_color_t bar_b)
{
    lv_obj_t *card = make_card(parent, x, CARD_Y, w, CARD_H, COL_CARD_BORDER);

    lv_obj_t *chip = make_label(card, MDI_CHIP, &mdi_icons_20, is_gpu ? COL_GPU : COL_ACCENT);
    lv_obj_set_pos(chip, 12, 8);
    lv_obj_t *t = make_label(card, title, &lv_font_montserrat_20, W_TEXT);
    lv_obj_set_pos(t, 38, 6);

    if (is_gpu) {
        lv_obj_t *badge = shape_dot(card, 32, 20, 8, COL_BADGE_BG);
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -12, 8);
        lv_obj_t *bl = make_label(badge, "3D", &lv_font_montserrat_14, COL_BADGE_TEXT);
        lv_obj_center(bl);
    }

    tile->load_lbl = make_label(card, "--", &lv_font_montserrat_48, W_TEXT);
    lv_obj_set_pos(tile->load_lbl, 16, 34);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, w - 32, 20);
    lv_obj_set_pos(row, 16, 96);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_label(row, MDI_THERMOMETER, &mdi_icons_20, COL_THERMO);
    tile->temp_lbl  = make_label(row, "--C", &lv_font_montserrat_16, W_SUB);
    tile->power_lbl = make_label(row, LV_SYMBOL_CHARGE " --W", &lv_font_montserrat_16, W_SUB);
    tile->trend_lbl = make_label(row, "-", &lv_font_montserrat_16, W_SUB);

    tile->bar = lv_bar_create(card);
    lv_obj_set_size(tile->bar, w - 32, 10);
    lv_obj_set_pos(tile->bar, 16, 128);
    lv_bar_set_range(tile->bar, 0, 100);
    lv_obj_set_style_radius(tile->bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile->bar, W_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(tile->bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tile->bar, bar_a, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(tile->bar, bar_b, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(tile->bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(tile->bar, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_t *cap = make_label(card, ui_str(UI_STR_USAGE_CHART_CAPTION), &lv_font_montserrat_14, W_SUB);
    lv_obj_set_pos(cap, 16, 150);

    tile->chart = lv_chart_create(card);
    lv_obj_set_pos(tile->chart, 12, 170);
    lv_obj_set_size(tile->chart, w - 24, CARD_H - 170 - 12);
    lv_obj_set_style_bg_color(tile->chart, W_BG, 0);
    lv_obj_set_style_bg_opa(tile->chart, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile->chart, 0, 0);
    lv_obj_set_style_border_color(tile->chart, W_SUB, 0);
    lv_obj_set_style_border_width(tile->chart, 1, 0);
    // Div-Linien-Farbe explizit (nicht nur border_color) - LVGL zieht die
    // Rasterlinien sonst mit einer eigenen Theme-Default-Farbe, die auf
    // realer Hardware im Light-Mode kaum sichtbar war.
    lv_obj_set_style_line_color(tile->chart, W_SUB, LV_PART_MAIN);
    // Nur linker/rechter Rand - oben/unten (0%/100%) keine eigene Linie, die
    // stiess sonst (kombiniert mit der Default-Eckenrundung) am Rand vorbei
    // ueber den Rahmen hinaus.
    lv_obj_set_style_border_side(tile->chart, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_pad_all(tile->chart, 2, 0);
    lv_obj_set_style_size(tile->chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(tile->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(tile->chart, HIST_LEN);
    lv_chart_set_range(tile->chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    // 3 Rasterlinien (25/50/75%) - explizite line_color oben sorgt dafuer,
    // dass sie in beiden Themes sichtbar sind (vorher nicht gesetzt, dadurch
    // kaum sichtbar/scheinbar falsch platziert im Light-Mode).
    lv_chart_set_div_line_count(tile->chart, 3, 0);
    tile->ser = lv_chart_add_series(tile->chart, is_gpu ? COL_GPU : COL_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
}

// ------------------------------------------------------------------
// Hauptschirm
// ------------------------------------------------------------------
static void build_wide_main(void)
{
    scr_main = lv_obj_create(NULL);
    style_screen(scr_main);
    lv_obj_set_style_bg_color(scr_main, W_BG, 0);

    lv_obj_t *bar = lv_obj_create(scr_main);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, s_hres, TOPBAR_H);
    lv_obj_set_style_bg_color(bar, W_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    // Blaue Linie unten, wie bei den beiden Kacheln (COL_CARD_BORDER-Rahmen).
    lv_obj_set_style_border_color(bar, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    w_lbl_time = make_label(bar, "--:--:--", &lv_font_montserrat_28, W_TEXT);
    lv_obj_set_pos(w_lbl_time, 12, 8);
    w_lbl_date = make_label(bar, "", &lv_font_montserrat_16, W_SUB);
    lv_obj_set_pos(w_lbl_date, 12, 56);

    // Alle vier Wetterwerte (Temperatur/Feuchte/Wind/Regen) einheitlich in
    // Uhrzeit-Schriftgroesse (28px Text + 28px MDI-Icons) statt der
    // vorherigen uneinheitlichen Groessen - macht die Navbar insgesamt hoeher
    // (siehe TOPBAR_H oben).
    lv_obj_t *sun = make_label(bar, MDI_SUN, &mdi_icons_28, COL_YELLOW);
    lv_obj_set_pos(sun, 210, 10);
    w_lbl_weather = make_label(bar, "--C", &lv_font_montserrat_28, W_TEXT);
    lv_obj_set_pos(w_lbl_weather, 244, 8);
    lv_obj_set_width(w_lbl_weather, 110);
    lv_label_set_long_mode(w_lbl_weather, LV_LABEL_LONG_MODE_CLIP);

    lv_obj_t *hu = make_label(bar, MDI_HUMIDITY, &mdi_icons_28, COL_RAIN);
    lv_obj_set_pos(hu, 210, 58);
    w_lbl_humidity = make_label(bar, "--", &lv_font_montserrat_28, W_SUB);
    lv_obj_set_pos(w_lbl_humidity, 244, 56);
    lv_obj_set_width(w_lbl_humidity, 110);
    lv_label_set_long_mode(w_lbl_humidity, LV_LABEL_LONG_MODE_CLIP);

    lv_obj_t *wi = make_label(bar, MDI_WIND, &mdi_icons_28, W_SUB);
    lv_obj_set_pos(wi, 400, 10);
    w_lbl_wind = make_label(bar, "--", &lv_font_montserrat_28, W_SUB);
    lv_obj_set_pos(w_lbl_wind, 434, 8);
    lv_obj_set_width(w_lbl_wind, 260);
    lv_label_set_long_mode(w_lbl_wind, LV_LABEL_LONG_MODE_CLIP);

    lv_obj_t *ri = make_label(bar, MDI_RAIN, &mdi_icons_28, COL_RAIN);
    lv_obj_set_pos(ri, 400, 58);
    w_lbl_rain = make_label(bar, "--", &lv_font_montserrat_28, W_SUB);
    lv_obj_set_pos(w_lbl_rain, 434, 56);
    lv_obj_set_width(w_lbl_rain, 200);
    lv_label_set_long_mode(w_lbl_rain, LV_LABEL_LONG_MODE_CLIP);

    w_icon_status = shape_dot(bar, 16, 16, 8, COL_WARN);
    lv_obj_align(w_icon_status, LV_ALIGN_RIGHT_MID, -76, 0);

    lv_obj_t *settings_btn = lv_obj_create(bar);
    lv_obj_set_size(settings_btn, 64, TOPBAR_H);
    lv_obj_align(settings_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_btn, 0, 0);
    lv_obj_clear_flag(settings_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(settings_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings_btn, settings_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gear = make_label(settings_btn, MDI_COG, &mdi_icons_20, W_SUB);
    lv_obj_center(gear);

    const int margin = 8, gap = 8;
    int tile_w = (s_hres - 2 * margin - gap) / 2;
    build_tile(scr_main, margin, tile_w, "CPU", &w_cpu_tile, false, COL_CPU_BAR_A, COL_CPU_BAR_B);
    build_tile(scr_main, margin + tile_w + gap, tile_w, "GPU", &w_gpu_tile, true, COL_GPU_BAR_A, COL_GPU_BAR_B);

    w_lbl_waiting = make_label(scr_main,
        (app_config.hw_source == HW_SOURCE_USB) ? ui_str(UI_STR_WAITING_DATA) : ui_str(UI_STR_WAITING_MQTT),
        &lv_font_montserrat_14, W_SUB);
    lv_obj_set_width(w_lbl_waiting, s_hres - 16);
    lv_obj_set_style_text_align(w_lbl_waiting, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(w_lbl_waiting, W_BG, 0);
    lv_obj_set_style_bg_opa(w_lbl_waiting, LV_OPA_80, 0);
    lv_obj_set_style_pad_ver(w_lbl_waiting, 3, 0);
    lv_obj_align(w_lbl_waiting, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// ------------------------------------------------------------------
// Settings-Screen (identisch im Inhalt zum Kachel-UI, eigene Instanz)
// ------------------------------------------------------------------
static void build_wide_settings(void)
{
    scr_settings = lv_obj_create(NULL);
    style_screen(scr_settings);
    lv_obj_set_style_bg_color(scr_settings, W_BG, 0);

    lv_obj_t *card = make_card(scr_settings, s_hres / 2 - 220, 40, 440, 400, COL_CARD_BORDER);

    lv_obj_t *back = lv_button_create(card);
    lv_obj_set_pos(back, 12, 12);
    lv_obj_set_size(back, 44, 32);
    lv_obj_set_style_bg_color(back, W_BG, 0);
    lv_obj_add_event_cb(back, settings_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = make_label(back, MDI_ARROW_LEFT, &mdi_icons_20, W_TEXT);
    lv_obj_center(bl);

    lv_obj_t *title = make_label(card, ui_str(UI_STR_SETTINGS_TITLE), &lv_font_montserrat_20, COL_ACCENT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    w_set_fw   = make_label(card, "", &lv_font_montserrat_14, W_TEXT);
    lv_obj_set_pos(w_set_fw, 20, 62);
    w_set_ip   = make_label(card, "", &lv_font_montserrat_14, W_TEXT);
    lv_obj_set_pos(w_set_ip, 20, 84);
    w_set_wifi = make_label(card, "", &lv_font_montserrat_14, W_TEXT);
    lv_obj_set_pos(w_set_wifi, 20, 106);
    w_set_mqtt = make_label(card, "", &lv_font_montserrat_14, W_TEXT);
    lv_obj_set_pos(w_set_mqtt, 20, 128);
    w_set_heap = make_label(card, "", &lv_font_montserrat_14, W_SUB);
    lv_obj_set_pos(w_set_heap, 20, 150);

    lv_obj_t *hint = make_label(card, ui_str(UI_STR_SETTINGS_HINT), &lv_font_montserrat_14, W_SUB);
    lv_obj_set_pos(hint, 20, 178);
    lv_obj_set_width(hint, 400);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);

    w_set_ap_btn = lv_button_create(card);
    lv_obj_set_pos(w_set_ap_btn, 20, 260);
    lv_obj_set_size(w_set_ap_btn, 400, 40);
    lv_obj_set_style_bg_color(w_set_ap_btn, W_CARD, 0);
    lv_obj_add_event_cb(w_set_ap_btn, ap_btn_click_cb, LV_EVENT_CLICKED, NULL);
    w_set_ap_btn_lbl = make_label(w_set_ap_btn, ui_str(UI_STR_AP_BTN_IDLE), &lv_font_montserrat_14, W_TEXT);
    lv_obj_center(w_set_ap_btn_lbl);
}

// ------------------------------------------------------------------
// Standby-Screen - analog display_ui_rect.c: grosse Uhrzeit/Datum + kompakte
// Wetterzeile.
// ------------------------------------------------------------------
static void build_wide_standby(void)
{
    scr_standby = lv_obj_create(NULL);
    style_screen(scr_standby);
    lv_obj_set_style_bg_color(scr_standby, W_BG, 0);

    w_standby_lbl_time = make_label(scr_standby, "--:--:--", &lv_font_montserrat_48, W_TEXT);
    lv_obj_align(w_standby_lbl_time, LV_ALIGN_CENTER, 0, -40);

    w_standby_lbl_date = make_label(scr_standby, "", &lv_font_montserrat_20, W_SUB);
    lv_obj_align(w_standby_lbl_date, LV_ALIGN_CENTER, 0, 30);

    w_standby_lbl_weather = make_label(scr_standby, "", &lv_font_montserrat_20, W_SUB);
    lv_obj_set_width(w_standby_lbl_weather, s_hres - 40);
    lv_obj_set_style_text_align(w_standby_lbl_weather, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(w_standby_lbl_weather, LV_ALIGN_CENTER, 0, 66);
}

void build_wide_ui(void)
{
    init_wide_theme();
    build_wide_main();
    build_wide_settings();
    build_wide_standby();
}

// ------------------------------------------------------------------
// Dynamische Updates
// ------------------------------------------------------------------
static void update_tile(wide_tile_ctx_t *tile, const history_t *hist, float load, float temp, float power)
{
    lv_label_set_text_fmt(tile->load_lbl, "%d%%", (int)(load + 0.5f));
    lv_bar_set_value(tile->bar, (int)(load + 0.5f), LV_ANIM_OFF);

    char buf[16];
    snprintf(buf, sizeof(buf), "%dC", (int)(temp + 0.5f));
    lv_label_set_text(tile->temp_lbl, buf);
    snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %dW", (int)(power + 0.5f));
    lv_label_set_text(tile->power_lbl, buf);

    const char *sym; lv_color_t col;
    compute_trend(hist, &sym, &col);
    lv_label_set_text(tile->trend_lbl, sym);
    lv_obj_set_style_text_color(tile->trend_lbl, col, 0);

    int start = HIST_LEN - hist->count;
    for (int i = 0; i < HIST_LEN; i++) {
        int32_t v = (i < start) ? LV_CHART_POINT_NONE : (int32_t)(hist->load[i] + 0.5f);
        lv_chart_set_value_by_id(tile->chart, tile->ser, i, v);
    }
    lv_chart_refresh(tile->chart);
}

void refresh_wide_ui(void)
{
    char buf[64];

    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year > 100) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        lv_label_set_text(w_lbl_time, buf);
        lv_label_set_text(w_standby_lbl_time, buf);
        strftime(buf, sizeof(buf), "%d.%m.%Y", &ti);
        lv_label_set_text(w_lbl_date, buf);
        lv_label_set_text(w_standby_lbl_date, buf);
    } else {
        lv_label_set_text(w_lbl_time, "--:--:--");
        lv_label_set_text(w_lbl_date, "");
        lv_label_set_text(w_standby_lbl_time, "--:--:--");
        lv_label_set_text(w_standby_lbl_date, "");
    }

    if (weather_info.valid) {
        snprintf(buf, sizeof(buf), "%.0fC", weather_info.temp_c);
        lv_label_set_text(w_lbl_weather, buf);
        snprintf(buf, sizeof(buf), "%d%%", weather_info.humidity);
        lv_label_set_text(w_lbl_humidity, buf);
        snprintf(buf, sizeof(buf), "%.0fkm/h %s",
                 weather_info.wind_speed, weather_wind_compass(weather_info.wind_deg));
        lv_label_set_text(w_lbl_wind, buf);
        snprintf(buf, sizeof(buf), "%.1fmm", weather_info.rain_1h);
        lv_label_set_text(w_lbl_rain, buf);
        snprintf(buf, sizeof(buf), "%.0fC  %d%%  %.0fkm/h %s  %.1fmm",
                 weather_info.temp_c, weather_info.humidity, weather_info.wind_speed,
                 weather_wind_compass(weather_info.wind_deg), weather_info.rain_1h);
        lv_label_set_text(w_standby_lbl_weather, buf);
    } else if (app_config.weather_enabled) {
        lv_label_set_text(w_lbl_weather, "--C");
        lv_label_set_text(w_lbl_humidity, "--%");
        lv_label_set_text(w_lbl_wind, "--");
        lv_label_set_text(w_lbl_rain, "--");
        lv_label_set_text(w_standby_lbl_weather, "--");
    } else {
        lv_label_set_text(w_lbl_weather, "");
        lv_label_set_text(w_lbl_humidity, "");
        lv_label_set_text(w_lbl_wind, "");
        lv_label_set_text(w_lbl_rain, "");
        lv_label_set_text(w_standby_lbl_weather, "");
    }

    bool data_source_ok = (app_config.hw_source == HW_SOURCE_USB) ? serial_connected : mqtt_connected;
    lv_color_t sc = (wifi_connected && data_source_ok) ? COL_GREEN
                    : (wifi_connected ? COL_YELLOW : COL_WARN);
    lv_obj_set_style_bg_color(w_icon_status, sc, 0);

    if (s_screen == SCR_SETTINGS) {
        snprintf(buf, sizeof(buf), ui_str(UI_STR_FW_FMT), FW_VERSION);
        lv_label_set_text(w_set_fw, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_IP_FMT), web_portal_ip(),
                 web_portal_ap_mode() ? ui_str(UI_STR_IP_SETUP_AP_SUFFIX) : "");
        lv_label_set_text(w_set_ip, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_WIFI_FMT),
                 web_portal_ap_mode() ? ui_str(UI_STR_WIFI_SETUP_AP) : (wifi_connected ? ui_str(UI_STR_CONNECTED) : ui_str(UI_STR_DISCONNECTED)));
        lv_label_set_text(w_set_wifi, buf);
        if (app_config.hw_source == HW_SOURCE_USB) {
            snprintf(buf, sizeof(buf), ui_str(UI_STR_USB_FMT),
                     serial_connected ? ui_str(UI_STR_CONNECTED) : ui_str(UI_STR_DISCONNECTED));
        } else {
            snprintf(buf, sizeof(buf), ui_str(UI_STR_MQTT_FMT),
                     mqtt_connected ? ui_str(UI_STR_CONNECTED) : ui_str(UI_STR_DISCONNECTED));
        }
        lv_label_set_text(w_set_mqtt, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_FREE_HEAP_FMT), (unsigned)(esp_get_free_heap_size() / 1024));
        lv_label_set_text(w_set_heap, buf);
    } else {
        update_tile(&w_cpu_tile, &cpu_history, hw_info.cpu_load, hw_info.cpu_temp, hw_info.cpu_power);
        update_tile(&w_gpu_tile, &gpu_history, hw_info.gpu_load, hw_info.gpu_temp, hw_info.gpu_power);
        if (hw_info.ever_received) lv_obj_add_flag(w_lbl_waiting, LV_OBJ_FLAG_HIDDEN);
        else                       lv_obj_clear_flag(w_lbl_waiting, LV_OBJ_FLAG_HIDDEN);
    }
}
