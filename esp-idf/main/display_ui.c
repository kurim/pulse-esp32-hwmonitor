#include "display_ui.h"
#include "shared_state.h"
#include "bsp_pins.h"
#include "touch_xpt2046.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
#include <time.h>
#include <string.h>

static const char *TAG = "ui";

// ---- Farben (RGB565-Hex wie im Arduino-Port) ----
#define COL_BG      lv_color_hex(0x000000)
#define COL_CARD    lv_color_hex(0x171C28)
#define COL_ACCENT  lv_color_hex(0x00FFFF)
#define COL_GPU     lv_color_hex(0xFFA400)
#define COL_TEXT    lv_color_hex(0xFFFFFF)
#define COL_SUB     lv_color_hex(0x8893A8)
#define COL_WARN    lv_color_hex(0xF80000)
#define COL_GREEN   lv_color_hex(0x33DD55)
#define COL_YELLOW  lv_color_hex(0xEECC33)

// ---- Bildschirmzustand ----
enum { SCR_MAIN = 0, SCR_CPU = 1, SCR_GPU = 2 };
static int s_screen = SCR_MAIN;

static lv_obj_t *scr_main, *scr_detail;

// Hauptschirm-Widgets
static lv_obj_t *lbl_time, *lbl_date, *lbl_weather, *dot_status;
static lv_obj_t *cpu_load_lbl, *cpu_info_lbl, *cpu_bar;
static lv_obj_t *gpu_load_lbl, *gpu_info_lbl, *gpu_bar;
static lv_obj_t *lbl_waiting;

// Detailschirm-Widgets
static lv_obj_t *det_title, *det_load, *det_temp, *det_power;
static lv_obj_t *chart_load, *chart_temp;
static lv_chart_series_t *ser_load, *ser_temp;

// ------------------------------------------------------------------
// Display-/Backlight-Init
// ------------------------------------------------------------------
static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num   = TFT_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = app_config.brightness,
        .hpoint     = 0,
    };
    ledc_channel_config(&c);
}

static void lcd_init(lv_display_t **out_disp)
{
    backlight_init();

    spi_bus_config_t bus = {
        .mosi_io_num     = TFT_PIN_MOSI,
        .miso_io_num     = TFT_PIN_MISO,
        .sclk_io_num     = TFT_PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = TFT_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num   = TFT_PIN_DC,
        .cs_gpio_num   = TFT_PIN_CS,
        .pclk_hz       = TFT_SPI_HZ,
        .lcd_cmd_bits  = 8,
        .lcd_param_bits = 8,
        .spi_mode      = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &io_cfg, &io));

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = TFT_PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR, // CYD i.d.R. BGR; falls Farben vertauscht: RGB
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    // Viele CYD-Panels brauchen Farbinversion. Falls das Bild invertiert wirkt,
    // hier auf false setzen.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    // Rotation -> swap/mirror + Aufloesung. UI ist auf Landscape (320x240)
    // ausgelegt (rotation 1/3); Portrait ist nicht gelayoutet.
    bool swap_xy, mirror_x, mirror_y;
    int hres, vres;
    switch (app_config.rotation) {
        case 0: swap_xy = false; mirror_x = false; mirror_y = false; hres = 240; vres = 320; break;
        case 2: swap_xy = false; mirror_x = true;  mirror_y = true;  hres = 240; vres = 320; break;
        case 3: swap_xy = true;  mirror_x = false; mirror_y = true;  hres = 320; vres = 240; break;
        case 1:
        default: swap_xy = true; mirror_x = true;  mirror_y = false; hres = 320; vres = 240; break;
    }

    lvgl_port_cfg_t pcfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&pcfg));

    lvgl_port_display_cfg_t dcfg = {
        .io_handle     = io,
        .panel_handle  = panel,
        .buffer_size   = TFT_H_RES * 40,
        .double_buffer = true,
        .hres          = hres,
        .vres          = vres,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma    = true,
            .swap_bytes  = true,
        },
    };
    *out_disp = lvgl_port_add_disp(&dcfg);
}

// ------------------------------------------------------------------
// Touch -> LVGL-Eingabegeraet
// ------------------------------------------------------------------
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;
    if (touch_xpt2046_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ------------------------------------------------------------------
// Style-Helfer
// ------------------------------------------------------------------
static void style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t border)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_border_color(c, border, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

// ------------------------------------------------------------------
// Event: Kachel angetippt / Zurueck
// ------------------------------------------------------------------
static void refresh_now(void); // fwd

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

// ------------------------------------------------------------------
// Aufbau Hauptschirm
// ------------------------------------------------------------------
static void build_tile(lv_obj_t *parent, int x, const char *title, lv_color_t accent,
                       lv_obj_t **load_lbl, lv_obj_t **bar, lv_obj_t **info_lbl, int which)
{
    lv_obj_t *card = make_card(parent, x, 40, 148, 150, accent);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, tile_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)which);

    lv_obj_t *t = make_label(card, title, &lv_font_montserrat_20, accent);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);

    *load_lbl = make_label(card, "--%", &lv_font_montserrat_48, COL_TEXT);
    lv_obj_align(*load_lbl, LV_ALIGN_CENTER, 0, -6);

    *bar = lv_bar_create(card);
    lv_obj_set_size(*bar, 120, 8);
    lv_obj_align(*bar, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_bar_set_range(*bar, 0, 100);
    lv_obj_set_style_bg_color(*bar, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*bar, accent, LV_PART_INDICATOR);

    *info_lbl = make_label(card, "-- C  -- W", &lv_font_montserrat_14, COL_SUB);
    lv_obj_align(*info_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void build_main(void)
{
    scr_main = lv_obj_create(NULL);
    style_screen(scr_main);

    // Top-Bar
    lv_obj_t *bar = lv_obj_create(scr_main);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 320, 32);
    lv_obj_set_style_bg_color(bar, COL_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_time = make_label(bar, "--:--:--", &lv_font_montserrat_20, COL_TEXT);
    lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 8, 0);

    lbl_date = make_label(bar, "", &lv_font_montserrat_14, COL_SUB);
    lv_obj_align(lbl_date, LV_ALIGN_LEFT_MID, 120, 0);

    lbl_weather = make_label(bar, "", &lv_font_montserrat_20, COL_TEXT);
    lv_obj_align(lbl_weather, LV_ALIGN_RIGHT_MID, -16, 0);

    dot_status = lv_obj_create(bar);
    lv_obj_set_size(dot_status, 10, 10);
    lv_obj_set_style_radius(dot_status, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot_status, 0, 0);
    lv_obj_set_style_bg_color(dot_status, COL_WARN, 0);
    lv_obj_align(dot_status, LV_ALIGN_RIGHT_MID, -2, 0);

    build_tile(scr_main, 6,   "CPU", COL_ACCENT, &cpu_load_lbl, &cpu_bar, &cpu_info_lbl, SCR_CPU);
    build_tile(scr_main, 166, "GPU", COL_GPU,    &gpu_load_lbl, &gpu_bar, &gpu_info_lbl, SCR_GPU);

    lbl_waiting = make_label(scr_main, "Warte auf MQTT-Daten vom PC-Client...",
                             &lv_font_montserrat_14, COL_SUB);
    lv_obj_align(lbl_waiting, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// ------------------------------------------------------------------
// Aufbau Detailschirm (CPU/GPU-Verlauf)
// ------------------------------------------------------------------
static lv_obj_t *make_chart(lv_obj_t *parent, int x, int y, int w, int h, int ymax,
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
    lv_chart_set_div_line_count(ch, 3, 0);
    *ser = lv_chart_add_series(ch, col, LV_CHART_AXIS_PRIMARY_Y);
    return ch;
}

static void build_detail(void)
{
    scr_detail = lv_obj_create(NULL);
    style_screen(scr_detail);

    lv_obj_t *back = lv_button_create(scr_detail);
    lv_obj_set_pos(back, 6, 4);
    lv_obj_set_size(back, 92, 30);
    lv_obj_set_style_bg_color(back, COL_CARD, 0);
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = make_label(back, "< Zurueck", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_center(bl);

    det_title = make_label(scr_detail, "Verlauf", &lv_font_montserrat_20, COL_ACCENT);
    lv_obj_align(det_title, LV_ALIGN_TOP_RIGHT, -8, 6);

    det_load  = make_label(scr_detail, "Auslastung: -- %", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_load, 12, 42);
    det_temp  = make_label(scr_detail, "Temperatur: -- C", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_temp, 12, 60);
    det_power = make_label(scr_detail, "Leistung:   -- W", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_power, 12, 78);

    lv_obj_t *cap1 = make_label(scr_detail, "Auslastung (%)", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(cap1, 12, 100);
    chart_load = make_chart(scr_detail, 12, 116, 296, 54, 100, COL_ACCENT, &ser_load);

    lv_obj_t *cap2 = make_label(scr_detail, "Temperatur (C)", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(cap2, 12, 176);
    chart_temp = make_chart(scr_detail, 12, 192, 296, 42, 120, COL_GPU, &ser_temp);
}

// ------------------------------------------------------------------
// Dynamische Updates
// ------------------------------------------------------------------
static void update_tile(lv_obj_t *load_lbl, lv_obj_t *bar, lv_obj_t *info_lbl,
                        float load, float temp, float power)
{
    lv_label_set_text_fmt(load_lbl, "%d%%", (int)(load + 0.5f));
    lv_bar_set_value(bar, (int)(load + 0.5f), LV_ANIM_OFF);
    lv_label_set_text_fmt(info_lbl, "%d C  %d W", (int)(temp + 0.5f), (int)(power + 0.5f));
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

static void refresh_now(void)
{
    // --- Zeit / Datum ---
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[24];
    if (ti.tm_year > 100) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        lv_label_set_text(lbl_time, buf);
        strftime(buf, sizeof(buf), "%d.%m.%Y", &ti);
        lv_label_set_text(lbl_date, buf);
    } else {
        lv_label_set_text(lbl_time, "--:--:--");
        lv_label_set_text(lbl_date, "");
    }

    // --- Wetter ---
    if (weather_info.valid) {
        lv_label_set_text_fmt(lbl_weather, "%d C", (int)(weather_info.temp_c + 0.5f));
    } else if (app_config.weather_enabled) {
        lv_label_set_text(lbl_weather, "-- ");
    } else {
        lv_label_set_text(lbl_weather, "");
    }

    // --- Status-Dot ---
    lv_color_t sc = (wifi_connected && mqtt_connected) ? COL_GREEN
                    : (wifi_connected ? COL_YELLOW : COL_WARN);
    lv_obj_set_style_bg_color(dot_status, sc, 0);

    if (s_screen == SCR_MAIN) {
        update_tile(cpu_load_lbl, cpu_bar, cpu_info_lbl,
                    hw_info.cpu_load, hw_info.cpu_temp, hw_info.cpu_power);
        update_tile(gpu_load_lbl, gpu_bar, gpu_info_lbl,
                    hw_info.gpu_load, hw_info.gpu_temp, hw_info.gpu_power);
        if (hw_info.ever_received) lv_obj_add_flag(lbl_waiting, LV_OBJ_FLAG_HIDDEN);
        else                       lv_obj_clear_flag(lbl_waiting, LV_OBJ_FLAG_HIDDEN);
    } else {
        bool is_cpu = (s_screen == SCR_CPU);
        history_t *h = is_cpu ? &cpu_history : &gpu_history;
        float load  = is_cpu ? hw_info.cpu_load  : hw_info.gpu_load;
        float temp  = is_cpu ? hw_info.cpu_temp  : hw_info.gpu_temp;
        float power = is_cpu ? hw_info.cpu_power : hw_info.gpu_power;

        lv_label_set_text(det_title, is_cpu ? "CPU Verlauf" : "GPU Verlauf");
        lv_obj_set_style_text_color(det_title, is_cpu ? COL_ACCENT : COL_GPU, 0);
        lv_label_set_text_fmt(det_load,  "Auslastung: %.1f %%", load);
        lv_label_set_text_fmt(det_temp,  "Temperatur: %.1f C", temp);
        lv_label_set_text_fmt(det_power, "Leistung:   %.1f W", power);

        update_chart(chart_load, ser_load, h->load, h->count);
        update_chart(chart_temp, ser_temp, h->temp, h->count);
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    refresh_now();
}

// ------------------------------------------------------------------
void display_ui_begin(void)
{
    lv_display_t *disp = NULL;
    lcd_init(&disp);
    touch_xpt2046_init();

    // Ab hier LVGL-Objekte nur unter Lock anlegen (esp_lvgl_port-Task laeuft).
    lvgl_port_lock(0);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    build_main();
    build_detail();
    lv_screen_load(scr_main);

    lv_timer_create(tick_cb, 1000, NULL); // 1x/Sek aktualisieren
    refresh_now();

    lvgl_port_unlock();
    ESP_LOGI(TAG, "UI initialisiert");
}
