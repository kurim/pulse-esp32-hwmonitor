// ------------------------------------------------------------------
// Minimal-UI fuer runde Displays (GC9A01, 240x240) - kein Touch, Navigation
// per Boot-Taste (board_profile_t.nav_button, siehe display_ui.c:
// nav_button_cb()):
// - Screen "Overview": zwei konzentrische Ringe (aussen CPU, innen GPU) fuer
//   die Auslastung, Uhrzeit in der Mitte.
// - Screen "Wetter": Temperatur/Feuchte/Wind/Regen statt der Ringe.
// - Standby (unabhaengig vom gewaehlten Screen): nur Uhrzeit + kompakte
//   Wetterzeile, da dieses Panel keinen Backlight-Pin zum Abdunkeln hat.
// ------------------------------------------------------------------
#include "display_ui_internal.h"

#include <time.h>
#include <string.h>
#include <stdio.h>

static lv_obj_t *round_arc_cpu, *round_arc_gpu, *round_lbl_time;
static lv_obj_t *round_row_cpu, *round_lbl_cpu, *round_row_gpu, *round_lbl_gpu;
// Wetterzeilen (Temp, Feuchte, Wind, Regen) - werden sowohl auf dem
// Wetter-Screen als auch im Standby gezeigt (dort zusaetzlich zur groesseren
// Uhrzeit, siehe refresh_round_ui()), daher kein separates Standby-Widget-Set.
static lv_obj_t *round_row_weather[4], *round_lbl_weather[4];

// Screens des runden Minimal-UIs, per Boot-Taste umschaltbar. Standby
// ueberlagert beide Screens mit einer eigenen, reduzierten Ansicht (siehe
// refresh_round_ui()).
enum { ROUND_SCR_OVERVIEW = 0, ROUND_SCR_WEATHER = 1, ROUND_SCR_COUNT = 2 };
static int s_round_screen = ROUND_SCR_OVERVIEW;

void round_ui_cycle_screen(void)
{
    s_round_screen = (s_round_screen + 1) % ROUND_SCR_COUNT;
    refresh_round_ui();
}

// Icon+Text-Zeile fuer das runde UI: eigener horizontaler Flex-Container
// (Breite an den Inhalt angepasst), damit Icon (MDI-Font) und Text (trotz
// unterschiedlicher Fonts, siehe mdi_icons.h-Einschraenkung) als Einheit
// mittig ausgerichtet werden koennen. icon==NULL laesst den Icon-Teil weg.
// *out_text erhaelt das Text-Label zum spaeteren Aktualisieren, Rueckgabewert
// ist der Zeilen-Container zum Ein-/Ausblenden.
static lv_obj_t *make_round_line(lv_obj_t *parent, const char *icon, lv_color_t icon_color,
                                  const lv_font_t *text_font, lv_color_t text_color, lv_obj_t **out_text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (icon) make_label(row, icon, &mdi_icons_20, icon_color);
    *out_text = make_label(row, "", text_font, text_color);
    return row;
}

void build_round_ui(void)
{
    scr_main = lv_obj_create(NULL);
    style_screen(scr_main);

    int d = s_hres < s_vres ? s_hres : s_vres; // Durchmesser = kleinere Kante
    // Zwei konzentrische Ringe (aussen CPU, innen GPU) statt nebeneinander-
    // liegender Halbkreise: beide ueber denselben Winkelbereich 120-60 Grad
    // im Uhrzeigersinn (= 300 Grad Bogen, 60 Grad Luecke unten bei 6 Uhr statt
    // eines kompletten Kreises), duenner heller Indikator-Bogen auf einer
    // dickeren, transluzenten Hintergrundspur - Layout an ein per ESPHome-
    // LVGL-Designer gebautes Referenzbild angelehnt.
    const int ARC_START = 120, ARC_END = 60;
    const int ARC_BG_W = 20, ARC_FG_W = 10;
    int outer_d = d - 4;         // CPU, fast randlos aussen
    int inner_d = outer_d - 40;  // GPU, mit sichtbarem Abstand zum aeusseren Ring

    round_arc_cpu = lv_arc_create(scr_main);
    lv_obj_set_size(round_arc_cpu, outer_d, outer_d);
    lv_obj_center(round_arc_cpu);
    lv_arc_set_bg_angles(round_arc_cpu, ARC_START, ARC_END);
    // lv_arc_set_bg_angles() positioniert nur die Hintergrundspur - der
    // Indikator (Wert-abhaengiger heller Bogen) folgt einem eigenen, sonst
    // beim LVGL-Default (135/45 Grad) verbleibenden Winkelbereich und landet
    // ohne diesen Aufruf an einer voellig anderen Stelle als die sichtbare Spur.
    lv_arc_set_angles(round_arc_cpu, ARC_START, ARC_END);
    lv_arc_set_range(round_arc_cpu, 0, 100);
    // Eigentlicher Hintergrund-Rect des Arc-Widgets (nicht die Bogenlinie)
    // aus, sonst blieb ein dezentes helles Kreis-Panel hinter dem Ring stehen.
    lv_obj_set_style_bg_opa(round_arc_cpu, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(round_arc_cpu, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(round_arc_cpu, COL_CPU_BAR_A, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(round_arc_cpu, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(round_arc_cpu, ARC_BG_W, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(round_arc_cpu, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(round_arc_cpu, COL_CPU_BAR_A, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(round_arc_cpu, ARC_FG_W, LV_PART_INDICATOR);
    int cpu_pad = (ARC_BG_W - ARC_FG_W) / 2; 
    lv_obj_set_style_pad_left(round_arc_cpu, cpu_pad, LV_PART_INDICATOR);
    lv_obj_set_style_pad_right(round_arc_cpu, cpu_pad, LV_PART_INDICATOR);
    lv_obj_set_style_pad_top(round_arc_cpu, cpu_pad, LV_PART_INDICATOR);
    lv_obj_set_style_pad_bottom(round_arc_cpu, cpu_pad, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(round_arc_cpu, true, LV_PART_INDICATOR);
    lv_obj_remove_flag(round_arc_cpu, LV_OBJ_FLAG_CLICKABLE);
    // LVGL-Arcs sind eigentlich Schieberegler und zeichnen deshalb per
    // Default einen Knob (dicker Punkt an der aktuellen Werteposition) -
    // ohne Style-Entfernung sah der wie ein Fremdkoerper/Glitch auf dem
    // duennen Ring aus. Fuer reine Anzeige-Ringe weg damit.
    lv_obj_remove_style(round_arc_cpu, NULL, LV_PART_KNOB);

    round_arc_gpu = lv_arc_create(scr_main);
    lv_obj_set_size(round_arc_gpu, inner_d, inner_d);
    lv_obj_center(round_arc_gpu);
    lv_arc_set_bg_angles(round_arc_gpu, ARC_START, ARC_END);
    lv_arc_set_angles(round_arc_gpu, ARC_START, ARC_END);
    lv_arc_set_range(round_arc_gpu, 0, 100);
    lv_obj_set_style_bg_opa(round_arc_gpu, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(round_arc_gpu, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(round_arc_gpu, COL_GPU_BAR_A, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(round_arc_gpu, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(round_arc_gpu, ARC_BG_W, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(round_arc_gpu, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(round_arc_gpu, COL_GPU_BAR_A, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(round_arc_gpu, ARC_FG_W, LV_PART_INDICATOR);
    int gpu_pad = (ARC_BG_W - ARC_FG_W) / 2;
    lv_obj_set_style_pad_left(round_arc_gpu, gpu_pad, LV_PART_INDICATOR);
    lv_obj_set_style_pad_right(round_arc_gpu, gpu_pad, LV_PART_INDICATOR);
    lv_obj_set_style_pad_top(round_arc_gpu, gpu_pad, LV_PART_INDICATOR);
    lv_obj_set_style_pad_bottom(round_arc_gpu, gpu_pad, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(round_arc_gpu, true, LV_PART_INDICATOR);
    lv_obj_remove_flag(round_arc_gpu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(round_arc_gpu, NULL, LV_PART_KNOB);

    round_lbl_time = make_label(scr_main, "--:--:--", &lv_font_montserrat_24, COL_TEXT);
    lv_obj_align(round_lbl_time, LV_ALIGN_CENTER, 0, -20);

    round_row_cpu = make_round_line(scr_main, MDI_CHIP, COL_ACCENT, &lv_font_montserrat_20, COL_ACCENT, &round_lbl_cpu);
    lv_obj_align(round_row_cpu, LV_ALIGN_CENTER, 0, 10);
    round_row_gpu = make_round_line(scr_main, MDI_CHIP, COL_GPU, &lv_font_montserrat_20, COL_GPU, &round_lbl_gpu);
    lv_obj_align(round_row_gpu, LV_ALIGN_CENTER, 0, 34);

    // Wetterzeilen: unterhalb der Uhrzeit, ersetzen die Arcs/CPU/GPU-Zeilen
    // (schliessen sich gegenseitig aus, siehe refresh_round_ui()). Werden
    // sowohl auf dem Wetter-Screen als auch im Standby gezeigt - im Standby
    // steht die (dann groessere) Uhrzeit weiter oben, siehe refresh_round_ui().
    static const char *wicons[4] = { MDI_THERMOMETER, MDI_HUMIDITY, MDI_WIND, MDI_RAIN };
    const lv_color_t wcolors[4] = { COL_THERMO, COL_RAIN, COL_SUB, COL_RAIN }; // lv_color_hex() ist kein Compile-Time-Konstantenausdruck -> kein "static"
    static const int wy[4] = { 10, 36, 62, 88 };
    for (int i = 0; i < 4; i++) {
        round_row_weather[i] = make_round_line(scr_main, wicons[i], wcolors[i],
                                                &lv_font_montserrat_20, COL_TEXT, &round_lbl_weather[i]);
        lv_obj_align(round_row_weather[i], LV_ALIGN_CENTER, 0, wy[i]);
        lv_obj_add_flag(round_row_weather[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_hidden(lv_obj_t *obj, bool visible)
{
    if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void refresh_round_ui(void)
{
    char buf[32];
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year > 100) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        lv_label_set_text(round_lbl_time, buf);
    }

    // Standby: Uhrzeit groesser + weiter oben, sonst Standardgroesse/-position
    // (siehe Wunsch "idle Uhr kann weiter nach oben und/oder groesser").
    lv_obj_set_style_text_font(round_lbl_time, s_standby ? &lv_font_montserrat_28 : &lv_font_montserrat_24, 0);
    lv_obj_align(round_lbl_time, LV_ALIGN_CENTER, 0, s_standby ? -55 : -20);

    bool show_overview = !s_standby && s_round_screen == ROUND_SCR_OVERVIEW;
    // Wetterzeilen: im Standby immer (Wunsch "im Standby soll nur noch Uhr
    // und Wetter gezeigt werden" / "im Standby fehlen noch Wetterdaten"),
    // im Wachzustand nur auf dem Wetter-Screen.
    bool show_weather = s_standby || s_round_screen == ROUND_SCR_WEATHER;

    show_hidden(round_arc_cpu, show_overview);
    show_hidden(round_arc_gpu, show_overview);
    show_hidden(round_row_cpu, show_overview);
    show_hidden(round_row_gpu, show_overview);
    if (show_overview) {
        lv_arc_set_value(round_arc_cpu, (int)(hw_info.cpu_load + 0.5f));
        lv_arc_set_value(round_arc_gpu, (int)(hw_info.gpu_load + 0.5f));
        snprintf(buf, sizeof(buf), ui_str(UI_STR_ROUND_CPU_FMT), (int)(hw_info.cpu_load + 0.5f));
        lv_label_set_text(round_lbl_cpu, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_ROUND_GPU_FMT), (int)(hw_info.gpu_load + 0.5f));
        lv_label_set_text(round_lbl_gpu, buf);
    }

    for (int i = 0; i < 4; i++) show_hidden(round_row_weather[i], show_weather);
    if (show_weather) {
        if (weather_info.valid) {
            snprintf(buf, sizeof(buf), "%.0fC / %.0fC", weather_info.temp_c, weather_info.feels_like_c);
            lv_label_set_text(round_lbl_weather[0], buf);
            snprintf(buf, sizeof(buf), "%d%%", weather_info.humidity);
            lv_label_set_text(round_lbl_weather[1], buf);
            snprintf(buf, sizeof(buf), "%.0fkm/h %s", weather_info.wind_speed, weather_wind_compass(weather_info.wind_deg));
            lv_label_set_text(round_lbl_weather[2], buf);
            snprintf(buf, sizeof(buf), "%.1fmm", weather_info.rain_1h);
            lv_label_set_text(round_lbl_weather[3], buf);
        } else {
            lv_label_set_text(round_lbl_weather[0], app_config.weather_enabled ? ui_str(UI_STR_WAITING_DATA) : ui_str(UI_STR_WEATHER_OFF));
            for (int i = 1; i < 4; i++) lv_label_set_text(round_lbl_weather[i], "");
        }
    }
}
