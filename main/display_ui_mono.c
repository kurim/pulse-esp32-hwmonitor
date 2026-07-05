// ------------------------------------------------------------------
// Minimal-UI fuer monochrome Displays (SSD1309, 128x64) - kein Touch.
// Oben Uhrzeit/Datum + Wetter-Icon mit Aussentemperatur, darunter zwei
// schmale Rahmen-Karten (CPU/GPU) mit je drei Icon+Wert-Zeilen (Auslastung/
// Leistung/Temperatur). Layout in einem ESPHome-LVGL-Designer entworfen und
// hier 1:1 als lv_obj/lv_label-Aufrufe nachgebaut.
// ------------------------------------------------------------------
#include "display_ui_internal.h"

#include <time.h>
#include <string.h>
#include <stdio.h>

static lv_obj_t *mono_lbl_time, *mono_lbl_date;
static lv_obj_t *mono_icon_weather, *mono_lbl_weather;
static lv_obj_t *mono_lbl_cpu_load, *mono_lbl_cpu_power, *mono_lbl_cpu_temp;
static lv_obj_t *mono_lbl_gpu_load, *mono_lbl_gpu_power, *mono_lbl_gpu_temp;

static lv_obj_t *mono_card(lv_obj_t *parent, int x, int y, int w, int h, const char *title)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(c, COL_TEXT, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 2, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = make_label(c, title, &lv_font_montserrat_8, COL_TEXT);
    lv_obj_set_width(hdr, w - 2);
    lv_obj_set_style_text_align(hdr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(hdr, 0, 0);
    return c;
}

// Icon + rechtsbuendiger Wert in einer Zeile innerhalb einer mono_card().
static void mono_metric_row(lv_obj_t *card, const char *icon, int y, lv_obj_t **out_val)
{
    lv_obj_t *ic = make_label(card, icon, &mdi_icons_10, COL_TEXT);
    lv_obj_set_pos(ic, 6, y);

    lv_obj_t *val = make_label(card, "", &lv_font_montserrat_8, COL_TEXT);
    lv_obj_set_pos(val, 20, y);
    lv_obj_set_width(val, 32);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    *out_val = val;
}

void build_mono_ui(void)
{
    scr_main = lv_obj_create(NULL);
    style_screen(scr_main);

    mono_lbl_time = make_label(scr_main, "--:--:--", &lv_font_montserrat_8, COL_TEXT);
    lv_obj_set_pos(mono_lbl_time, 2, 0);

    mono_lbl_date = make_label(scr_main, "--.--.----", &lv_font_montserrat_8, COL_TEXT);
    lv_obj_set_width(mono_lbl_date, 40);
    lv_obj_set_style_text_align(mono_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(mono_lbl_date, LV_ALIGN_TOP_RIGHT, -2, 0);

    // Aussentemperatur (Wetter-Service) - nur sichtbar, wenn Wetter aktiv und
    // schon mindestens einmal erfolgreich abgerufen (siehe refresh_mono_ui()).
    mono_icon_weather = make_label(scr_main, MDI10_SUN, &mdi_icons_10, COL_TEXT);
    lv_obj_set_pos(mono_icon_weather, 47, 0);
    lv_obj_add_flag(mono_icon_weather, LV_OBJ_FLAG_HIDDEN);
    mono_lbl_weather = make_label(scr_main, "", &lv_font_montserrat_8, COL_TEXT);
    lv_obj_set_pos(mono_lbl_weather, 57, 0);
    lv_obj_add_flag(mono_lbl_weather, LV_OBJ_FLAG_HIDDEN);

    const int card_y = 17, card_w = 60, card_h = 45;
    lv_obj_t *cpu_card = mono_card(scr_main, 2, card_y, card_w, card_h, "CPU");
    mono_metric_row(cpu_card, MDI10_CHIP,        9, &mono_lbl_cpu_load);
    mono_metric_row(cpu_card, MDI10_FLASH,       19, &mono_lbl_cpu_power);
    mono_metric_row(cpu_card, MDI10_THERMOMETER, 29, &mono_lbl_cpu_temp);

    lv_obj_t *gpu_card = mono_card(scr_main, 66, card_y, card_w, card_h, "GPU");
    mono_metric_row(gpu_card, MDI10_CHIP,        9, &mono_lbl_gpu_load);
    mono_metric_row(gpu_card, MDI10_FLASH,       19, &mono_lbl_gpu_power);
    mono_metric_row(gpu_card, MDI10_THERMOMETER, 29, &mono_lbl_gpu_temp);
}

void refresh_mono_ui(void)
{
    char buf[32];
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year > 100) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        lv_label_set_text(mono_lbl_time, buf);
        strftime(buf, sizeof(buf), "%d.%m.%Y", &ti);
        lv_label_set_text(mono_lbl_date, buf);
    }

    if (weather_info.valid) {
        lv_obj_remove_flag(mono_icon_weather, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mono_lbl_weather, LV_OBJ_FLAG_HIDDEN);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_TEMP_FMT), (int)(weather_info.temp_c + 0.5f));
        lv_label_set_text(mono_lbl_weather, buf);
    } else {
        lv_obj_add_flag(mono_icon_weather, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mono_lbl_weather, LV_OBJ_FLAG_HIDDEN);
    }

    snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_PCT_FMT), (int)(hw_info.cpu_load + 0.5f));
    lv_label_set_text(mono_lbl_cpu_load, buf);
    snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_POWER_FMT), (int)(hw_info.cpu_power + 0.5f));
    lv_label_set_text(mono_lbl_cpu_power, buf);
    snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_TEMP_FMT), (int)(hw_info.cpu_temp + 0.5f));
    lv_label_set_text(mono_lbl_cpu_temp, buf);

    snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_PCT_FMT), (int)(hw_info.gpu_load + 0.5f));
    lv_label_set_text(mono_lbl_gpu_load, buf);
    snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_POWER_FMT), (int)(hw_info.gpu_power + 0.5f));
    lv_label_set_text(mono_lbl_gpu_power, buf);
    snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_TEMP_FMT), (int)(hw_info.gpu_temp + 0.5f));
    lv_label_set_text(mono_lbl_gpu_temp, buf);
}
