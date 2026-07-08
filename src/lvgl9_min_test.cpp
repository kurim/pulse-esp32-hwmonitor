// Isolierter LVGL9-Minimaltest fuer das "Dashboard aktualisiert sich nach
// dem ersten Rendern nie wieder"-Problem (siehe display_ui.cpp/
// display_ui_rect.cpp fuer die volle App-Diagnose). Nur aktiv im
// PlatformIO-Environment "esp32_lvgl9_min_test" (-DLVGL9_MIN_TEST, siehe
// platformio.ini) - ersetzt dort komplett setup()/loop() aus main.cpp
// (dort per #ifndef ausgeblendet).
//
// Zweck: EXAKT dieselbe LVGL9+LovyanGFX(LGFX_CYD)-Verdrahtung wie
// display_ui.cpp (lv_display_create/lv_display_set_buffers/disp_flush_cb
// via pushImage()), aber mit nur einem einzigen Screen und einem einzigen
// Label, das per lv_timer_create() jede Sekunde hochzaehlt - komplett ohne
// WLAN/MQTT/Wetter/mehrere Screens/Charts. Das offizielle LovyanGFX-Demo
// (LVGL8, lovyan03/LovyanGFX Autodetect-Beispiel) laeuft auf derselben
// Hardware nachweislich einwandfrei durchgehend; kommt dieser Zaehler hier
// sichtbar hoch, ist die LVGL9-Verdrahtung an sich in Ordnung und der
// Fehler steckt spezifisch irgendwo in der komplexen App-UI
// (display_ui_rect.cpp). Bleibt er stehen, ist es ein grundsaetzliches
// LVGL9-Verdrahtungsproblem, unabhaengig von der restlichen App.
#ifdef LVGL9_MIN_TEST

#include <Arduino.h>
#include <lvgl.h>
#define LGFX_USE_V1
#include "display/lgfx_profiles.h"

static LGFX_CYD s_lcd;
static lv_obj_t *s_lbl;

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    s_lcd.startWrite();
    s_lcd.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)px_map);
    s_lcd.endWrite();
    lv_display_flush_ready(disp);
}

static void counter_timer_cb(lv_timer_t *t)
{
    (void)t;
    static int n = 0;
    n++;
    lv_label_set_text_fmt(s_lbl, "MIN-TEST:%d", n);
    log_i("LVGL9_MIN_TEST: counter -> %d", n);
}

void setup(void)
{
    Serial.begin(115200);
    delay(300);
    Serial.println("=== LVGL9-Minimaltest (temporaer) ===");

    s_lcd.init();
    s_lcd.setRotation(1);
    s_lcd.setBrightness(200);

    int hres = s_lcd.width();
    int vres = s_lcd.height();

    lv_init();

    static lv_color_t *buf1;
    static lv_color_t *buf2;
    const uint32_t buf_px = hres * 20;
    buf1 = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));
    buf2 = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));

    lv_display_t *disp = lv_display_create(hres, vres);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_px * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    s_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_lbl);
    lv_label_set_text(s_lbl, "MIN-TEST:0");

    lv_timer_create(counter_timer_cb, 1000, NULL);

    Serial.println("LVGL9-Minimaltest laeuft.");
}

void loop(void)
{
    lv_timer_handler();
}

#endif // LVGL9_MIN_TEST
