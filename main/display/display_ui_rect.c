// ------------------------------------------------------------------
// Kachel-UI fuer rechteckige Farbdisplays (LCD_SHAPE_RECT: CYD-ILI9341,
// ILI9488, ST7796S - alle drei teilen sich dieses Layout, nur Aufloesung/
// Rotation unterscheiden sich, siehe board_profiles.c). Vier Screens:
// Hauptschirm (Kacheln), Detail (Verlaufschart CPU/GPU), Einstellungen,
// Standby.
// ------------------------------------------------------------------
#include "display_ui_internal.h"
#include "web_portal.h"

#include "esp_system.h" // esp_get_free_heap_size (Settings-Screen)
#include <time.h>
#include <string.h>
#include <stdio.h>

// ---- Layout-Konstanten ----
// Breiten skalieren mit der tatsaechlichen Panel-Aufloesung (s_hres/s_vres);
// Hoehen bleiben absolut wie auf dem 320x240-Referenzdisplay (CYD) - auf
// groesseren Panels (z.B. ILI9488 480x320) bleibt dadurch mehr Luft, statt
// dass Elemente verzerrt werden.
#define TOPBAR_H 62
#define CARD_Y   66
#define CARD_H   116

// Kachel-Widgets, gebuendelt pro Kachel (CPU/GPU)
typedef struct {
    lv_obj_t *load_lbl;
    lv_obj_t *bar;
    lv_obj_t *temp_lbl;
    lv_obj_t *power_lbl;
    lv_obj_t *trend_lbl;
} tile_ctx_t;
static tile_ctx_t cpu_tile, gpu_tile;

// Hauptschirm-Widgets (Top-Bar, 3-Spalten-Raster: Zeit/Datum |
// Temperatur/Feuchte | Wind/Regen)
static lv_obj_t *lbl_time, *lbl_date;
static lv_obj_t *lbl_weather, *lbl_humidity, *lbl_wind, *lbl_rain;
static lv_obj_t *icon_wifi;
static lv_obj_t *lbl_waiting;

// Detailschirm-Widgets
static lv_obj_t *det_title, *det_load, *det_temp, *det_power;
static lv_obj_t *lbl_quick_cpu, *lbl_quick_gpu;
static lv_obj_t *chart_load, *chart_temp;
static lv_chart_series_t *ser_load, *ser_temp;

// Settings-Screen-Widgets
static lv_obj_t *set_fw, *set_ip, *set_wifi, *set_mqtt, *set_heap;
static lv_obj_t *set_ap_btn, *set_ap_btn_lbl;
static bool s_ap_confirm_armed = false;
static lv_timer_t *s_ap_confirm_timer = NULL;

// Standby-Screen-Widgets - grosse Uhrzeit + Datum + kompakte Wetterzeile,
// analog zum Standby des runden Minimal-UIs.
static lv_obj_t *standby_lbl_time, *standby_lbl_date, *standby_lbl_weather;

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t border)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_border_color(c, border, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

// ------------------------------------------------------------------
// shape_rrect: einziges verbliebenes Vektor-Element (fuer das "3D"-Badge
// auf der GPU-Kachel und den WLAN/MQTT-Statuspunkt). Alle uebrigen Icons
// kommen aus der Material-Design-Icons-Font (mdi_icons.h).
// ------------------------------------------------------------------
static lv_obj_t *shape_rrect(lv_obj_t *parent, int w, int h, int radius, lv_color_t col)
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

// ------------------------------------------------------------------
// Trend-Pfeil (steigend/fallend/stabil) aus dem Auslastungs-Verlauf
// ------------------------------------------------------------------
static void compute_trend(const history_t *h, const char **sym, lv_color_t *col)
{
    if (h->count < 4) { *sym = "-"; *col = COL_SUB; return; }
    int back = (h->count > 10) ? 10 : (h->count - 1);
    float newest = h->load[HIST_LEN - 1];
    float older  = h->load[HIST_LEN - 1 - back];
    float diff = newest - older;
    if (diff > 3.0f)       { *sym = LV_SYMBOL_UP;   *col = COL_GREEN; }
    else if (diff < -3.0f) { *sym = LV_SYMBOL_DOWN; *col = COL_WARN;  }
    else                   { *sym = "-";             *col = COL_SUB;  }
}

// ------------------------------------------------------------------
// Navigation (Touch auf Kacheln/Zurueck/Zahnrad)
// ------------------------------------------------------------------
static void tile_click_cb(lv_event_t *e)
{
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    s_screen = which;
    lv_screen_load(scr_detail);
    refresh_now();
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    s_screen = SCR_MAIN;
    lv_screen_load(scr_main);
    refresh_now();
}

static void settings_click_cb(lv_event_t *e)
{
    (void)e;
    s_screen = SCR_SETTINGS;
    lv_screen_load(scr_settings);
    refresh_now();
}

static void settings_back_cb(lv_event_t *e)
{
    (void)e;
    s_screen = SCR_MAIN;
    lv_screen_load(scr_main);
    refresh_now();
}

// "Neustart in Setup-AP": zwei Taps noetig (Bestaetigung), da das die
// aktuelle WLAN-Verbindung trennt. Erster Tap "bewaffnet" den Knopf fuer 3s,
// zweiter Tap innerhalb des Fensters loest tatsaechlich aus.
static void ap_confirm_revert_cb(lv_timer_t *t)
{
    (void)t;
    s_ap_confirm_armed = false;
    lv_label_set_text(set_ap_btn_lbl, ui_str(UI_STR_AP_BTN_IDLE));
    lv_obj_set_style_bg_color(set_ap_btn, COL_CARD, 0);
    s_ap_confirm_timer = NULL;
}

static void ap_btn_click_cb(lv_event_t *e)
{
    (void)e;
    if (!s_ap_confirm_armed) {
        s_ap_confirm_armed = true;
        lv_label_set_text(set_ap_btn_lbl, ui_str(UI_STR_AP_BTN_CONFIRM));
        lv_obj_set_style_bg_color(set_ap_btn, COL_WARN, 0);
        if (s_ap_confirm_timer) lv_timer_delete(s_ap_confirm_timer);
        s_ap_confirm_timer = lv_timer_create(ap_confirm_revert_cb, 3000, NULL);
        lv_timer_set_repeat_count(s_ap_confirm_timer, 1);
    } else {
        if (s_ap_confirm_timer) { lv_timer_delete(s_ap_confirm_timer); s_ap_confirm_timer = NULL; }
        s_ap_confirm_armed = false;
        lv_label_set_text(set_ap_btn_lbl, ui_str(UI_STR_AP_BTN_SWITCHING));
        lv_obj_set_style_bg_color(set_ap_btn, COL_CARD, 0);
        web_portal_force_ap();
    }
}

// ------------------------------------------------------------------
// Aufbau Hauptschirm
// ------------------------------------------------------------------
static void build_tile(lv_obj_t *parent, int x, int w, const char *title, tile_ctx_t *tile, int which,
                       bool is_gpu, lv_color_t bar_a, lv_color_t bar_b)
{
    lv_obj_t *card = make_card(parent, x, CARD_Y, w, CARD_H, COL_CARD_BORDER);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, tile_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)which);

    lv_obj_t *chip = make_label(card, MDI_CHIP, &mdi_icons_20, is_gpu ? COL_GPU : COL_ACCENT);
    lv_obj_set_pos(chip, 8, 2);

    lv_obj_t *t = make_label(card, title, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_set_pos(t, 32, 5);

    if (is_gpu) {
        lv_obj_t *badge = shape_rrect(card, 28, 16, 8, COL_BADGE_BG);
        lv_obj_set_pos(badge, w - 34, 6);
        lv_obj_t *bl = make_label(badge, "3D", &lv_font_montserrat_14, COL_BADGE_TEXT);
        lv_obj_center(bl);
    }

    tile->load_lbl = make_label(card, "--", &lv_font_montserrat_48, COL_TEXT);
    lv_obj_align(tile->load_lbl, LV_ALIGN_CENTER, 0, -14);

    tile->bar = lv_bar_create(card);
    lv_obj_set_size(tile->bar, w - 28, 8);
    lv_obj_align(tile->bar, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_bar_set_range(tile->bar, 0, 100);
    lv_obj_set_style_radius(tile->bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile->bar, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(tile->bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tile->bar, bar_a, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(tile->bar, bar_b, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(tile->bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(tile->bar, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, w - 16, 18);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 5, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_label(row, MDI_THERMOMETER, &mdi_icons_20, COL_THERMO);
    tile->temp_lbl  = make_label(row, "--C", &lv_font_montserrat_14, COL_SUB);
    tile->power_lbl = make_label(row, LV_SYMBOL_CHARGE " --W", &lv_font_montserrat_14, COL_SUB);
    tile->trend_lbl = make_label(row, "-", &lv_font_montserrat_14, COL_SUB);
}

void build_main(void)
{
    scr_main = lv_obj_create(NULL);
    style_screen(scr_main);

    // ---- Top-Bar ----
    lv_obj_t *bar = lv_obj_create(scr_main);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, s_hres, TOPBAR_H);
    lv_obj_set_style_bg_color(bar, COL_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // Top-Bar als 3-Spalten-Raster (Wunsch des Nutzers):
    //   Uhrzeit    | Temperatur       | Wind
    //   Datum      | Luftfeuchtigkeit | Regen
    // Spalten: Zeit/Datum x=8, Temp/Feuchte x=124, Wind/Regen x=204.
    // MDI-Icons sind 20px breit (aus der generierten Font ausgelesen).

    // -- Spalte 1: Zeit/Datum --
    lbl_time = make_label(bar, "--:--:--", &lv_font_montserrat_24, COL_TEXT);
    lv_obj_set_pos(lbl_time, 8, 4);
    lbl_date = make_label(bar, "", &lv_font_montserrat_16, COL_SUB);
    lv_obj_set_pos(lbl_date, 8, 38);

    // -- Spalte 2: Temperatur/Luftfeuchtigkeit --
    lv_obj_t *sun = make_label(bar, MDI_SUN, &mdi_icons_20, COL_YELLOW);
    lv_obj_set_pos(sun, 124, 8);
    lbl_weather = make_label(bar, "--C", &lv_font_montserrat_20, COL_TEXT);
    lv_obj_set_pos(lbl_weather, 147, 6);
    lv_obj_set_width(lbl_weather, 50);
    lv_label_set_long_mode(lbl_weather, LV_LABEL_LONG_MODE_CLIP);

    lv_obj_t *hu = make_label(bar, MDI_HUMIDITY, &mdi_icons_20, COL_RAIN);
    lv_obj_set_pos(hu, 124, 37);
    lbl_humidity = make_label(bar, "--", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(lbl_humidity, 147, 39);
    lv_obj_set_width(lbl_humidity, 37);
    lv_label_set_long_mode(lbl_humidity, LV_LABEL_LONG_MODE_CLIP);

    // -- Spalte 3: Wind/Regen (jetzt uebereinander statt nebeneinander -
    // loest den vorherigen "zu wenig Abstand"-Punkt gleich mit). --
    lv_obj_t *wi = make_label(bar, MDI_WIND, &mdi_icons_20, COL_SUB);
    lv_obj_set_pos(wi, 204, 8);
    lbl_wind = make_label(bar, "--", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(lbl_wind, 227, 9);
    // Bis knapp an den rechten Rand (Statuspunkt sitzt jetzt unten rechts,
    // siehe icon_wifi weiter unten) - vorher bei fixen 64px wurde z.B.
    // "34km/h NNW" am "W" abgeschnitten.
    lv_obj_set_width(lbl_wind, s_hres - 227 - 6);
    lv_label_set_long_mode(lbl_wind, LV_LABEL_LONG_MODE_CLIP);

    lv_obj_t *ri = make_label(bar, MDI_RAIN, &mdi_icons_20, COL_RAIN);
    lv_obj_set_pos(ri, 204, 37);
    lbl_rain = make_label(bar, "--", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(lbl_rain, 227, 39);
    lv_obj_set_width(lbl_rain, 36);
    lv_label_set_long_mode(lbl_rain, LV_LABEL_LONG_MODE_CLIP);

    // Statuspunkt statt WLAN-Icon, unten rechts (nicht oben rechts) platziert:
    // die Windzeile braucht bei laengeren Werten ("34km/h NNW") die volle
    // Zeilenbreite bis zum rechten Rand, die Regenzeile darunter ist kurz
    // genug ("0.0"), um daneben Platz zu lassen.
    icon_wifi = shape_rrect(bar, 14, 14, 7, COL_WARN);
    lv_obj_align(icon_wifi, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

    // ---- Kacheln (Breite anhand der Panel-Aufloesung berechnet) ----
    const int margin = 6, gap = 6;
    int tile_w = (s_hres - 2 * margin - gap) / 2;
    build_tile(scr_main, margin, tile_w, "CPU", &cpu_tile, SCR_CPU, false, COL_CPU_BAR_A, COL_CPU_BAR_B);
    build_tile(scr_main, margin + tile_w + gap, tile_w, "GPU", &gpu_tile, SCR_GPU, true, COL_GPU_BAR_A, COL_GPU_BAR_B);

    // "Warte auf Daten"-Hinweis, ueberlagert die Kachel-Unterkante bis zur
    // ersten Nachricht der konfigurierten Quelle (danach ausgeblendet). Text
    // wird einmalig beim Boot passend zu app_config.hw_source gewaehlt - ein
    // Quellenwechsel greift ohnehin erst nach einem Neustart (gleiches Muster
    // wie Sprache/Displaytyp/Rotation).
    lbl_waiting = make_label(scr_main,
        (app_config.hw_source == HW_SOURCE_USB) ? ui_str(UI_STR_WAITING_DATA) : ui_str(UI_STR_WAITING_MQTT),
        &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_width(lbl_waiting, s_hres - 12);
    lv_obj_set_style_text_align(lbl_waiting, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(lbl_waiting, COL_BG, 0);
    lv_obj_set_style_bg_opa(lbl_waiting, LV_OPA_80, 0);
    lv_obj_set_style_pad_ver(lbl_waiting, 2, 0);
    lv_obj_set_pos(lbl_waiting, 6, CARD_Y + CARD_H - 18);

    // ---- Settings-Knopf (unten) ----
    lv_obj_t *settings_btn = lv_obj_create(scr_main);
    lv_obj_set_pos(settings_btn, 0, CARD_Y + CARD_H);
    lv_obj_set_size(settings_btn, s_hres, s_vres - (CARD_Y + CARD_H));
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_btn, 0, 0);
    lv_obj_clear_flag(settings_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(settings_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings_btn, settings_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *gear = make_label(settings_btn, MDI_COG, &mdi_icons_20, COL_SUB);
    lv_obj_align(gear, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t *set_lbl = make_label(settings_btn, "SETTINGS", &lv_font_montserrat_14, COL_SUB);
    lv_obj_align(set_lbl, LV_ALIGN_TOP_MID, 0, 26);
}

// ------------------------------------------------------------------
// Aufbau Detailschirm (CPU/GPU-Verlauf)
// ------------------------------------------------------------------
static lv_obj_t *make_chart(lv_obj_t *parent, int x, int y, int w, int h, int ymax, int major_cnt,
                            lv_color_t col, lv_chart_series_t **ser)
{
    lv_obj_t *ch = lv_chart_create(parent);
    lv_obj_set_pos(ch, x, y);
    lv_obj_set_size(ch, w, h);
    lv_obj_set_style_bg_color(ch, COL_BG, 0);
    lv_obj_set_style_bg_opa(ch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ch, COL_SUB, 0);
    lv_obj_set_style_border_width(ch, 1, 0);
    lv_obj_set_style_pad_all(ch, 2, 0);
    lv_obj_set_style_size(ch, 0, 0, LV_PART_INDICATOR); // keine Punkt-Marker
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch, HIST_LEN);
    lv_chart_set_range(ch, LV_CHART_AXIS_PRIMARY_Y, 0, ymax);
    // Kartenrand dient bereits als oberste/unterste Gitterlinie (0/ymax);
    // nur die "inneren" major_cnt-2 Werte brauchen eine eigene Linie.
    // Hinweis: lv_chart_set_axis_tick()/LV_PART_TICKS (numerische
    // Achsenbeschriftung) gibt es in LVGL 9 nicht mehr (in 9.0 entfernt,
    // Ersatz waere ein separates lv_scale-Widget) - daher nur Gitterlinien
    // ohne Zahlen, wie schon in der zuvor auf Hardware bestaetigten Version.
    lv_chart_set_div_line_count(ch, major_cnt - 2, 0);
    *ser = lv_chart_add_series(ch, col, LV_CHART_AXIS_PRIMARY_Y);
    return ch;
}

void build_detail(void)
{
    scr_detail = lv_obj_create(NULL);
    style_screen(scr_detail);

    lv_obj_t *back = lv_button_create(scr_detail);
    lv_obj_set_pos(back, 6, 4);
    lv_obj_set_size(back, 44, 28);
    lv_obj_set_style_bg_color(back, COL_CARD, 0);
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = make_label(back, MDI_ARROW_LEFT, &mdi_icons_20, COL_TEXT);
    lv_obj_center(bl);

    det_title = make_label(scr_detail, ui_str(UI_STR_HISTORY_TITLE), &lv_font_montserrat_20, COL_ACCENT);
    lv_obj_align(det_title, LV_ALIGN_TOP_RIGHT, -8, 6);

    // Kurzueberblick beider Metriken (unabhaengig davon, welcher Verlauf
    // gerade angezeigt wird), mit Trend-Pfeil.
    lbl_quick_cpu = make_label(scr_detail, "", &lv_font_montserrat_14, COL_ACCENT);
    lv_obj_align(lbl_quick_cpu, LV_ALIGN_TOP_RIGHT, -8, 30);
    lbl_quick_gpu = make_label(scr_detail, "", &lv_font_montserrat_14, COL_GPU);
    lv_obj_align(lbl_quick_gpu, LV_ALIGN_TOP_RIGHT, -8, 46);

    det_load  = make_label(scr_detail, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_load, 12, 50);
    det_temp  = make_label(scr_detail, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_temp, 12, 68);
    det_power = make_label(scr_detail, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_power, 12, 86);

    lv_obj_t *cap1 = make_label(scr_detail, ui_str(UI_STR_USAGE_CHART_CAPTION), &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(cap1, 12, 108);
    chart_load = make_chart(scr_detail, 12, 124, s_hres - 20, 50, 100, 5, COL_ACCENT, &ser_load);

    lv_obj_t *cap2 = make_label(scr_detail, ui_str(UI_STR_TEMP_CHART_CAPTION), &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(cap2, 12, 180);
    chart_temp = make_chart(scr_detail, 12, 196, s_hres - 20, 40, 120, 5, COL_GPU, &ser_temp);
}

// ------------------------------------------------------------------
// Aufbau Settings-/Status-Screen
// ------------------------------------------------------------------
void build_settings(void)
{
    scr_settings = lv_obj_create(NULL);
    style_screen(scr_settings);

    lv_obj_t *back = lv_button_create(scr_settings);
    lv_obj_set_pos(back, 6, 6);
    lv_obj_set_size(back, 40, 30);
    lv_obj_set_style_bg_color(back, COL_CARD, 0);
    lv_obj_add_event_cb(back, settings_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = make_label(back, MDI_ARROW_LEFT, &mdi_icons_20, COL_TEXT);
    lv_obj_center(bl);

    lv_obj_t *title = make_label(scr_settings, ui_str(UI_STR_SETTINGS_TITLE), &lv_font_montserrat_20, COL_ACCENT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    set_fw   = make_label(scr_settings, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(set_fw, 16, 54);
    set_ip   = make_label(scr_settings, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(set_ip, 16, 74);
    set_wifi = make_label(scr_settings, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(set_wifi, 16, 94);
    set_mqtt = make_label(scr_settings, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(set_mqtt, 16, 114);
    set_heap = make_label(scr_settings, "", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(set_heap, 16, 134);

    lv_obj_t *hint = make_label(scr_settings,
        ui_str(UI_STR_SETTINGS_HINT),
        &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(hint, 16, 158);
    lv_obj_set_width(hint, s_hres - 32);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);

    set_ap_btn = lv_button_create(scr_settings);
    lv_obj_set_pos(set_ap_btn, 16, 198);
    lv_obj_set_size(set_ap_btn, s_hres - 32, 34);
    lv_obj_set_style_bg_color(set_ap_btn, COL_CARD, 0);
    lv_obj_add_event_cb(set_ap_btn, ap_btn_click_cb, LV_EVENT_CLICKED, NULL);
    set_ap_btn_lbl = make_label(set_ap_btn, ui_str(UI_STR_AP_BTN_IDLE), &lv_font_montserrat_14, COL_TEXT);
    lv_obj_center(set_ap_btn_lbl);
}

// ------------------------------------------------------------------
// Standby-Screen - grosse Uhrzeit/Datum + kompakte Wetterzeile, analog zur
// reduzierten Standby-Ansicht des runden Minimal-UIs.
// ------------------------------------------------------------------
void build_standby(void)
{
    scr_standby = lv_obj_create(NULL);
    style_screen(scr_standby);

    standby_lbl_time = make_label(scr_standby, "--:--:--", &lv_font_montserrat_48, COL_TEXT);
    lv_obj_align(standby_lbl_time, LV_ALIGN_CENTER, 0, -30);

    standby_lbl_date = make_label(scr_standby, "", &lv_font_montserrat_16, COL_SUB);
    lv_obj_align(standby_lbl_date, LV_ALIGN_CENTER, 0, 24);

    standby_lbl_weather = make_label(scr_standby, "", &lv_font_montserrat_16, COL_SUB);
    lv_obj_set_width(standby_lbl_weather, s_hres - 20);
    lv_obj_set_style_text_align(standby_lbl_weather, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(standby_lbl_weather, LV_ALIGN_CENTER, 0, 54);
}

// ------------------------------------------------------------------
// Dynamische Updates
// ------------------------------------------------------------------
static void update_tile(tile_ctx_t *tile, const history_t *hist, float load, float temp, float power)
{
    lv_label_set_text_fmt(tile->load_lbl, "%d", (int)(load + 0.5f));
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
}

static void update_chart(lv_obj_t *chart, lv_chart_series_t *ser, const float *data, uint8_t count)
{
    int start = HIST_LEN - count;
    for (int i = 0; i < HIST_LEN; i++) {
        int32_t v = (i < start) ? LV_CHART_POINT_NONE : (int32_t)(data[i] + 0.5f);
        lv_chart_set_value_by_id(chart, ser, i, v);
    }
    lv_chart_refresh(chart);
}

void refresh_now(void)
{
    char buf[64];

    // --- Zeit / Datum ---
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year > 100) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        lv_label_set_text(lbl_time, buf);
        lv_label_set_text(standby_lbl_time, buf);
        strftime(buf, sizeof(buf), "%d.%m.%Y", &ti);
        lv_label_set_text(lbl_date, buf);
        lv_label_set_text(standby_lbl_date, buf);
    } else {
        lv_label_set_text(lbl_time, "--:--:--");
        lv_label_set_text(lbl_date, "");
        lv_label_set_text(standby_lbl_time, "--:--:--");
        lv_label_set_text(standby_lbl_date, "");
    }

    // --- Wetter-Block (3-Spalten-Raster: Temp / Feuchte / Wind / Regen) ---
    if (weather_info.valid) {
        snprintf(buf, sizeof(buf), "%.0fC", weather_info.temp_c);
        lv_label_set_text(lbl_weather, buf);
        snprintf(buf, sizeof(buf), "%d%%", weather_info.humidity);
        lv_label_set_text(lbl_humidity, buf);
        snprintf(buf, sizeof(buf), "%.0fkm/h %s",
                 weather_info.wind_speed, weather_wind_compass(weather_info.wind_deg));
        lv_label_set_text(lbl_wind, buf);
        // Ohne "mm"-Einheit: das Regen-Icon liefert den Kontext, und die
        // 48px breite Box (Sicherheitsabstand zum WLAN-Icon) reichte fuer
        // "X.Xmm" nicht zuverlässig (wurde auf Hardware abgeschnitten).
        snprintf(buf, sizeof(buf), "%.1f", weather_info.rain_1h);
        lv_label_set_text(lbl_rain, buf);
        snprintf(buf, sizeof(buf), "%.0fC  %d%%  %.0fkm/h %s  %.1fmm",
                 weather_info.temp_c, weather_info.humidity, weather_info.wind_speed,
                 weather_wind_compass(weather_info.wind_deg), weather_info.rain_1h);
        lv_label_set_text(standby_lbl_weather, buf);
    } else if (app_config.weather_enabled) {
        lv_label_set_text(lbl_weather, "--C");
        lv_label_set_text(lbl_humidity, "--%");
        lv_label_set_text(lbl_wind, "--");
        lv_label_set_text(lbl_rain, "--");
        lv_label_set_text(standby_lbl_weather, "--");
    } else {
        lv_label_set_text(lbl_weather, "");
        lv_label_set_text(lbl_humidity, "");
        lv_label_set_text(lbl_wind, "");
        lv_label_set_text(lbl_rain, "");
        lv_label_set_text(standby_lbl_weather, "");
    }

    // --- WLAN/Hardwaredaten-Statusicon (Quelle: MQTT oder USB/Seriell) ---
    bool data_source_ok = (app_config.hw_source == HW_SOURCE_USB) ? serial_connected : mqtt_connected;
    lv_color_t sc = (wifi_connected && data_source_ok) ? COL_GREEN
                    : (wifi_connected ? COL_YELLOW : COL_WARN);
    lv_obj_set_style_bg_color(icon_wifi, sc, 0);

    if (s_screen == SCR_MAIN) {
        update_tile(&cpu_tile, &cpu_history, hw_info.cpu_load, hw_info.cpu_temp, hw_info.cpu_power);
        update_tile(&gpu_tile, &gpu_history, hw_info.gpu_load, hw_info.gpu_temp, hw_info.gpu_power);
        if (hw_info.ever_received) lv_obj_add_flag(lbl_waiting, LV_OBJ_FLAG_HIDDEN);
        else                       lv_obj_clear_flag(lbl_waiting, LV_OBJ_FLAG_HIDDEN);

    } else if (s_screen == SCR_CPU || s_screen == SCR_GPU) {
        bool is_cpu = (s_screen == SCR_CPU);
        history_t *h = is_cpu ? &cpu_history : &gpu_history;
        float load  = is_cpu ? hw_info.cpu_load  : hw_info.gpu_load;
        float temp  = is_cpu ? hw_info.cpu_temp  : hw_info.gpu_temp;
        float power = is_cpu ? hw_info.cpu_power : hw_info.gpu_power;

        lv_label_set_text(det_title, is_cpu ? ui_str(UI_STR_CPU_HISTORY) : ui_str(UI_STR_GPU_HISTORY));
        lv_obj_set_style_text_color(det_title, is_cpu ? COL_ACCENT : COL_GPU, 0);

        snprintf(buf, sizeof(buf), ui_str(UI_STR_USAGE_FMT), load);
        lv_label_set_text(det_load, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_TEMP_FMT), temp);
        lv_label_set_text(det_temp, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_POWER_FMT), power);
        lv_label_set_text(det_power, buf);

        // Trend-Farbe wird hier bewusst ignoriert: die Kurzuebersicht faerbt
        // die gesamte Zeile in der CPU-/GPU-Datenfarbe, nicht nur den Pfeil.
        const char *sym; lv_color_t unused_col;
        compute_trend(&cpu_history, &sym, &unused_col);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_QUICK_CPU_FMT), hw_info.cpu_temp, hw_info.cpu_power, sym);
        lv_label_set_text(lbl_quick_cpu, buf);

        compute_trend(&gpu_history, &sym, &unused_col);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_QUICK_GPU_FMT), hw_info.gpu_temp, hw_info.gpu_power, sym);
        lv_label_set_text(lbl_quick_gpu, buf);
        (void)unused_col;

        if (is_cpu) {
            lv_obj_clear_flag(lbl_quick_cpu, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_quick_gpu, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lbl_quick_cpu, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_quick_gpu, LV_OBJ_FLAG_HIDDEN);
        }

        update_chart(chart_load, ser_load, h->load, h->count);
        update_chart(chart_temp, ser_temp, h->temp, h->count);

    } else if (s_screen == SCR_SETTINGS) {
        snprintf(buf, sizeof(buf), ui_str(UI_STR_FW_FMT), FW_VERSION);
        lv_label_set_text(set_fw, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_IP_FMT), web_portal_ip(),
                 web_portal_ap_mode() ? ui_str(UI_STR_IP_SETUP_AP_SUFFIX) : "");
        lv_label_set_text(set_ip, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_WIFI_FMT),
                 web_portal_ap_mode() ? ui_str(UI_STR_WIFI_SETUP_AP) : (wifi_connected ? ui_str(UI_STR_CONNECTED) : ui_str(UI_STR_DISCONNECTED)));
        lv_label_set_text(set_wifi, buf);
        if (app_config.hw_source == HW_SOURCE_USB) {
            snprintf(buf, sizeof(buf), ui_str(UI_STR_USB_FMT),
                     serial_connected ? ui_str(UI_STR_CONNECTED) : ui_str(UI_STR_DISCONNECTED));
        } else {
            snprintf(buf, sizeof(buf), ui_str(UI_STR_MQTT_FMT),
                     mqtt_connected ? ui_str(UI_STR_CONNECTED) : ui_str(UI_STR_DISCONNECTED));
        }
        lv_label_set_text(set_mqtt, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_FREE_HEAP_FMT), (unsigned)(esp_get_free_heap_size() / 1024));
        lv_label_set_text(set_heap, buf);
    }
}
