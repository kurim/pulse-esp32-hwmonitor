#include "display_ui.h"
#include "shared_state.h"
#include "board_profiles.h"
#include "touch_xpt2046.h"
#include "web_portal.h"
#include "weather_service.h"
#include "mdi_icons.h"

#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
// HINWEIS: Diese drei Header/Funktionsnamen folgen der ueblichen Espressif-
// Namenskonvention fuer esp_lcd-Panel-Treiber (analog zu esp_lcd_ili9341.h /
// esp_lcd_new_panel_ili9341). Noch nicht auf Hardware verifiziert - falls der
// Component-Manager eine andere Komponente/Funktion fuer eines der Panels
// aufloest, hier und in main/idf_component.yml nachziehen.
#include "esp_lcd_ili9488.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lvgl.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui";

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

// ---- Layout-Konstanten der Kachel-UI (LCD_SHAPE_RECT) ----
// Breiten skalieren mit der tatsaechlichen Panel-Aufloesung (s_hres/s_vres,
// siehe unten); Hoehen bleiben absolut wie auf dem 320x240-Referenzdisplay
// (CYD) - auf groesseren Panels (z.B. ILI9488 480x320) bleibt dadurch mehr
// Luft, statt dass Elemente verzerrt werden.
#define TOPBAR_H 62
#define CARD_Y   66
#define CARD_H   116

// Panel-/UI-Zustand, in lcd_init() bzw. den *_init()-Helfern gesetzt.
static const board_profile_t *s_profile;
static int s_hres, s_vres;

// ---- Bildschirmzustand ----
enum { SCR_MAIN = 0, SCR_CPU = 1, SCR_GPU = 2, SCR_SETTINGS = 3 };
static int s_screen = SCR_MAIN;

static lv_obj_t *scr_main, *scr_detail, *scr_settings;

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

// Widgets der Minimal-UIs (LCD_SHAPE_MONO / LCD_SHAPE_ROUND)
static lv_obj_t *mono_lbl_cpu, *mono_lbl_gpu, *mono_lbl_time;
static lv_obj_t *round_arc_cpu, *round_arc_gpu, *round_lbl_cpu, *round_lbl_gpu, *round_lbl_time;

// ------------------------------------------------------------------
// Display-/Backlight-Init
// ------------------------------------------------------------------
static void backlight_init(int bl_gpio)
{
    if (bl_gpio < 0) return; // z.B. selbstleuchtende OLEDs ohne Backlight-Pin

    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num   = bl_gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = app_config.brightness,
        .hpoint     = 0,
    };
    ledc_channel_config(&c);
}

// ------------------------------------------------------------------
// Standby: Backlight aus, wenn laenger keine MQTT-Hardwaredaten ankommen
// (weder je empfangen noch seit STANDBY_TIMEOUT_MS aktualisiert - deckt
// beide Faelle über denselben Zeitvergleich ab, da last_update_ms beim
// Boot bei 0 startet). Aufwecken per Touch (erster Touch weckt nur, loest
// keine Aktion aus) oder automatisch, sobald wieder Daten eintreffen.
// Nur fuer Panels mit Backlight-Pin relevant (siehe backlight_init()).
// ------------------------------------------------------------------
#define STANDBY_TIMEOUT_MS (2 * 60 * 1000) // 2 Minuten ohne Daten -> Standby
static bool s_standby = false;
static bool s_has_backlight = false;

static void enter_standby(void)
{
    if (s_standby || !s_has_backlight) return;
    s_standby = true;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void exit_standby(void)
{
    if (!s_standby) return;
    s_standby = false;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, app_config.brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void check_standby(void)
{
    if (!s_has_backlight) return;
    bool no_data = (now_ms() - hw_info.last_update_ms) > STANDBY_TIMEOUT_MS;
    if (no_data) {
        enter_standby();
    } else {
        exit_standby(); // Daten wieder da -> automatisch aufwecken
    }
}

// ------------------------------------------------------------------
// Farb-SPI-Panels (ILI9341/ILI9488/ST7796S/GC9A01)
// ------------------------------------------------------------------
static lv_display_t *lcd_init_color_spi(const board_profile_t *p)
{
    backlight_init(p->bl);
    s_has_backlight = (p->bl >= 0);

    spi_bus_config_t bus = {
        .mosi_io_num     = p->mosi,
        .miso_io_num     = p->miso,
        .sclk_io_num     = p->sclk,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = p->h_res * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(p->spi_host, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num   = p->dc,
        .cs_gpio_num   = p->cs,
        .pclk_hz       = p->spi_hz,
        .lcd_cmd_bits  = 8,
        .lcd_param_bits = 8,
        .spi_mode      = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)p->spi_host, &io_cfg, &io));

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = p->rst,
        .rgb_ele_order  = p->bgr ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    switch (app_config.display_type) {
        case DISPLAY_ILI9488:
            ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(io, &panel_cfg, &panel));
            break;
        case DISPLAY_ST7796S:
            ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io, &panel_cfg, &panel));
            break;
        case DISPLAY_GC9A01:
            ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io, &panel_cfg, &panel));
            break;
        case DISPLAY_CYD_ILI9341:
        default:
            ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel));
            break;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    // Auf realer CYD-Hardware (ILI9341) verifiziert: Farbinversion "true" ergab
    // ein komplett invertiertes Bild. Fuer die anderen Panels noch nicht
    // gegengeprueft - bei falschen Farben hier pro Displaytyp anpassen.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    // Rotation -> swap/mirror + Aufloesung. Nur fuer LCD_SHAPE_RECT relevant;
    // das runde GC9A01 bleibt immer in nativer Aufloesung (kein Rotationsfall
    // vorgesehen, die Round-UI ist symmetrisch aufgebaut).
    bool swap_xy = false, mirror_x = false, mirror_y = false;
    int hres = p->h_res, vres = p->v_res;
    if (p->shape == LCD_SHAPE_RECT) {
        switch (app_config.rotation) {
            case 0: swap_xy = false; mirror_x = false; mirror_y = false; hres = p->v_res; vres = p->h_res; break;
            case 2: swap_xy = false; mirror_x = true;  mirror_y = true;  hres = p->v_res; vres = p->h_res; break;
            case 3: swap_xy = true;  mirror_x = true;  mirror_y = true;  hres = p->h_res; vres = p->v_res; break;
            case 1:
            default: swap_xy = true; mirror_x = false; mirror_y = false; hres = p->h_res; vres = p->v_res; break;
        }
    }
    s_hres = hres;
    s_vres = vres;

    lvgl_port_cfg_t pcfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&pcfg));

    lvgl_port_display_cfg_t dcfg = {
        .io_handle     = io,
        .panel_handle  = panel,
        .buffer_size   = p->h_res * 40,
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
    return lvgl_port_add_disp(&dcfg);
}

// ------------------------------------------------------------------
// Monochromes I2C-OLED (SSD1309, SSD1306-kompatibles Protokoll)
// ------------------------------------------------------------------
static lv_display_t *lcd_init_mono_i2c(const board_profile_t *p)
{
    s_has_backlight = false; // OLED ist selbstleuchtend, kein Backlight-Pin

    i2c_config_t i2c_cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = p->i2c_sda,
        .scl_io_num       = p->i2c_scl,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = p->i2c_hz,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, i2c_cfg.mode, 0, 0, 0));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr          = p->i2c_addr,
        .control_phase_bytes = 1,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .dc_bit_offset     = 6,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &io_cfg, &io));

    esp_lcd_panel_ssd1306_config_t ssd_cfg = { .height = p->v_res };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .bits_per_pixel = 1,
        .vendor_config  = &ssd_cfg,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    s_hres = p->h_res;
    s_vres = p->v_res;

    lvgl_port_cfg_t pcfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&pcfg));

    lvgl_port_display_cfg_t dcfg = {
        .io_handle     = io,
        .panel_handle  = panel,
        .buffer_size   = p->h_res * p->v_res,
        .double_buffer = false,
        .hres          = p->h_res,
        .vres          = p->v_res,
        .monochrome    = true,
        .color_format  = LV_COLOR_FORMAT_I1,
        .rotation      = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags         = { .buff_dma = false, .swap_bytes = false },
    };
    return lvgl_port_add_disp(&dcfg);
}

static lv_display_t *lcd_init(void)
{
    s_profile = board_profile_get(app_config.display_type);
    ESP_LOGI(TAG, "Display: %s", s_profile->name);

    if (s_profile->bus == LCD_BUS_I2C) {
        return lcd_init_mono_i2c(s_profile);
    }
    return lcd_init_color_spi(s_profile);
}

// ------------------------------------------------------------------
// Touch -> LVGL-Eingabegeraet (nur Profile mit has_touch=true)
// ------------------------------------------------------------------
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;
    bool touched = touch_xpt2046_read(&x, &y);

    if (touched && s_standby) {
        // Erster Touch nach dem Standby weckt nur das Display auf und loest
        // keine Aktion aus (verhindert versehentliches Navigieren/Antippen
        // von Kacheln beim Aufwecken).
        exit_standby();
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (touched) {
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
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_border_color(c, border, 0);
    lv_obj_set_style_border_width(c, 2, 0);
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
// shape_rrect: einziges verbliebenes Vektor-Element (fuer das "3D"-Badge
// auf der GPU-Kachel). Alle uebrigen Icons kommen jetzt aus der Material-
// Design-Icons-Font (mdi_icons.h) statt aus handgezeichneten Formen.
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
// Navigation (nur LCD_SHAPE_RECT-UI, die anderen Shapes haben keine Touch-
// Navigation und zeigen alles auf einem einzigen Screen)
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
    lv_label_set_text(set_ap_btn_lbl, "Neustart in Setup-AP");
    lv_obj_set_style_bg_color(set_ap_btn, COL_CARD, 0);
    s_ap_confirm_timer = NULL;
}

static void ap_btn_click_cb(lv_event_t *e)
{
    (void)e;
    if (!s_ap_confirm_armed) {
        s_ap_confirm_armed = true;
        lv_label_set_text(set_ap_btn_lbl, "Wirklich? Nochmal tippen");
        lv_obj_set_style_bg_color(set_ap_btn, COL_WARN, 0);
        if (s_ap_confirm_timer) lv_timer_delete(s_ap_confirm_timer);
        s_ap_confirm_timer = lv_timer_create(ap_confirm_revert_cb, 3000, NULL);
        lv_timer_set_repeat_count(s_ap_confirm_timer, 1);
    } else {
        if (s_ap_confirm_timer) { lv_timer_delete(s_ap_confirm_timer); s_ap_confirm_timer = NULL; }
        s_ap_confirm_armed = false;
        lv_label_set_text(set_ap_btn_lbl, "Wechsle in Setup-AP...");
        lv_obj_set_style_bg_color(set_ap_btn, COL_CARD, 0);
        web_portal_force_ap();
    }
}

// ------------------------------------------------------------------
// Aufbau Hauptschirm (LCD_SHAPE_RECT)
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

static void build_main(void)
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

    lbl_humidity = make_label(bar, "--", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(lbl_humidity, 124, 39);
    lv_obj_set_width(lbl_humidity, 60);
    lv_label_set_long_mode(lbl_humidity, LV_LABEL_LONG_MODE_CLIP);

    // -- Spalte 3: Wind/Regen (jetzt uebereinander statt nebeneinander -
    // loest den vorherigen "zu wenig Abstand"-Punkt gleich mit). --
    lv_obj_t *wi = make_label(bar, MDI_WIND, &mdi_icons_20, COL_SUB);
    lv_obj_set_pos(wi, 204, 8);
    lbl_wind = make_label(bar, "--", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(lbl_wind, 227, 9);
    lv_obj_set_width(lbl_wind, 64);
    lv_label_set_long_mode(lbl_wind, LV_LABEL_LONG_MODE_CLIP);

    lv_obj_t *ri = make_label(bar, MDI_RAIN, &mdi_icons_20, COL_RAIN);
    lv_obj_set_pos(ri, 204, 37);
    lbl_rain = make_label(bar, "--", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(lbl_rain, 227, 39);
    lv_obj_set_width(lbl_rain, 36);
    lv_label_set_long_mode(lbl_rain, LV_LABEL_LONG_MODE_CLIP);

    icon_wifi = make_label(bar, MDI_WIFI, &mdi_icons_20, COL_WARN);
    lv_obj_align(icon_wifi, LV_ALIGN_TOP_RIGHT, -4, 3);

    // ---- Kacheln (Breite anhand der Panel-Aufloesung berechnet) ----
    const int margin = 6, gap = 6;
    int tile_w = (s_hres - 2 * margin - gap) / 2;
    build_tile(scr_main, margin, tile_w, "CPU", &cpu_tile, SCR_CPU, false, COL_CPU_BAR_A, COL_CPU_BAR_B);
    build_tile(scr_main, margin + tile_w + gap, tile_w, "GPU", &gpu_tile, SCR_GPU, true, COL_GPU_BAR_A, COL_GPU_BAR_B);

    // "Warte auf Daten"-Hinweis, ueberlagert die Kachel-Unterkante bis zur
    // ersten MQTT-Nachricht (danach ausgeblendet).
    lbl_waiting = make_label(scr_main, "Warte auf MQTT-Daten...", &lv_font_montserrat_14, COL_SUB);
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

static void build_detail(void)
{
    scr_detail = lv_obj_create(NULL);
    style_screen(scr_detail);

    lv_obj_t *back = lv_button_create(scr_detail);
    lv_obj_set_pos(back, 6, 4);
    lv_obj_set_size(back, 34, 28);
    lv_obj_set_style_bg_color(back, COL_CARD, 0);
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = make_label(back, MDI_ARROW_LEFT, &mdi_icons_20, COL_TEXT);
    lv_obj_center(bl);

    lv_obj_t *gear = lv_button_create(scr_detail);
    lv_obj_set_pos(gear, 44, 4);
    lv_obj_set_size(gear, 34, 28);
    lv_obj_set_style_bg_color(gear, COL_CARD, 0);
    lv_obj_add_event_cb(gear, settings_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = make_label(gear, MDI_COG, &mdi_icons_20, COL_TEXT);
    lv_obj_center(gl);

    det_title = make_label(scr_detail, "Verlauf", &lv_font_montserrat_20, COL_ACCENT);
    lv_obj_align(det_title, LV_ALIGN_TOP_RIGHT, -8, 6);

    // Kurzueberblick beider Metriken (unabhaengig davon, welcher Verlauf
    // gerade angezeigt wird), mit Trend-Pfeil.
    lbl_quick_cpu = make_label(scr_detail, "", &lv_font_montserrat_14, COL_ACCENT);
    lv_obj_align(lbl_quick_cpu, LV_ALIGN_TOP_RIGHT, -8, 30);
    lbl_quick_gpu = make_label(scr_detail, "", &lv_font_montserrat_14, COL_GPU);
    lv_obj_align(lbl_quick_gpu, LV_ALIGN_TOP_RIGHT, -8, 46);

    det_load  = make_label(scr_detail, "Auslastung (Usage): -- %", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_load, 12, 50);
    det_temp  = make_label(scr_detail, "Temperatur (Temp): -- C", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_temp, 12, 68);
    det_power = make_label(scr_detail, "Leistung (Power): -- W", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(det_power, 12, 86);

    lv_obj_t *cap1 = make_label(scr_detail, "Auslastung (%) - Verlauf", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(cap1, 12, 108);
    chart_load = make_chart(scr_detail, 12, 124, s_hres - 20, 50, 100, 5, COL_ACCENT, &ser_load);

    lv_obj_t *cap2 = make_label(scr_detail, "Temperatur (C) - Verlauf", &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(cap2, 12, 180);
    chart_temp = make_chart(scr_detail, 12, 196, s_hres - 20, 40, 120, 5, COL_GPU, &ser_temp);
}

// ------------------------------------------------------------------
// Aufbau Settings-/Status-Screen
// ------------------------------------------------------------------
static void build_settings(void)
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

    lv_obj_t *title = make_label(scr_settings, "Einstellungen", &lv_font_montserrat_20, COL_ACCENT);
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
        "WLAN/MQTT/Wetter werden weiterhin ueber das Webportal konfiguriert.",
        &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_pos(hint, 16, 158);
    lv_obj_set_width(hint, s_hres - 32);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);

    set_ap_btn = lv_button_create(scr_settings);
    lv_obj_set_pos(set_ap_btn, 16, 198);
    lv_obj_set_size(set_ap_btn, s_hres - 32, 34);
    lv_obj_set_style_bg_color(set_ap_btn, COL_CARD, 0);
    lv_obj_add_event_cb(set_ap_btn, ap_btn_click_cb, LV_EVENT_CLICKED, NULL);
    set_ap_btn_lbl = make_label(set_ap_btn, "Neustart in Setup-AP", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_center(set_ap_btn_lbl);
}

// ------------------------------------------------------------------
// Minimal-UI fuer monochrome Displays (SSD1309, 128x64) - kein Touch, kein
// Farbverlauf/Balken (1bpp), nur Text. Platzhalter-Layout, das spaeter noch
// verfeinert werden kann.
// ------------------------------------------------------------------
static void build_mono_ui(void)
{
    scr_main = lv_obj_create(NULL);
    style_screen(scr_main);

    mono_lbl_time = make_label(scr_main, "--:--:--", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(mono_lbl_time, 2, 0);

    mono_lbl_cpu = make_label(scr_main, "CPU --% --C", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(mono_lbl_cpu, 2, 24);
    mono_lbl_gpu = make_label(scr_main, "GPU --% --C", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(mono_lbl_gpu, 2, 44);
}

static void refresh_mono_ui(void)
{
    char buf[32];
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year > 100) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        lv_label_set_text(mono_lbl_time, buf);
    }
    snprintf(buf, sizeof(buf), "CPU %d%% %dC", (int)(hw_info.cpu_load + 0.5f), (int)(hw_info.cpu_temp + 0.5f));
    lv_label_set_text(mono_lbl_cpu, buf);
    snprintf(buf, sizeof(buf), "GPU %d%% %dC", (int)(hw_info.gpu_load + 0.5f), (int)(hw_info.gpu_temp + 0.5f));
    lv_label_set_text(mono_lbl_gpu, buf);
}

// ------------------------------------------------------------------
// Minimal-UI fuer runde Displays (GC9A01, 240x240) - kein Touch, zwei Arcs
// fuer CPU/GPU-Auslastung, Uhrzeit in der Mitte. Platzhalter-Layout, das
// spaeter noch durch ein ausgearbeitetes rundes Layout ersetzt werden kann.
// ------------------------------------------------------------------
static void build_round_ui(void)
{
    scr_main = lv_obj_create(NULL);
    style_screen(scr_main);

    int d = s_hres < s_vres ? s_hres : s_vres; // Durchmesser = kleinere Kante

    round_arc_cpu = lv_arc_create(scr_main);
    lv_obj_set_size(round_arc_cpu, d - 8, d - 8);
    lv_obj_center(round_arc_cpu);
    lv_arc_set_rotation(round_arc_cpu, 270);
    lv_arc_set_bg_angles(round_arc_cpu, 0, 180);
    lv_arc_set_range(round_arc_cpu, 0, 100);
    lv_obj_set_style_arc_color(round_arc_cpu, COL_CPU_BAR_A, LV_PART_INDICATOR);
    lv_obj_remove_flag(round_arc_cpu, LV_OBJ_FLAG_CLICKABLE);

    round_arc_gpu = lv_arc_create(scr_main);
    lv_obj_set_size(round_arc_gpu, d - 28, d - 28);
    lv_obj_center(round_arc_gpu);
    lv_arc_set_rotation(round_arc_gpu, 90);
    lv_arc_set_bg_angles(round_arc_gpu, 0, 180);
    lv_arc_set_range(round_arc_gpu, 0, 100);
    lv_obj_set_style_arc_color(round_arc_gpu, COL_GPU_BAR_A, LV_PART_INDICATOR);
    lv_obj_remove_flag(round_arc_gpu, LV_OBJ_FLAG_CLICKABLE);

    round_lbl_time = make_label(scr_main, "--:--:--", &lv_font_montserrat_20, COL_TEXT);
    lv_obj_align(round_lbl_time, LV_ALIGN_CENTER, 0, -20);
    round_lbl_cpu = make_label(scr_main, "CPU --%", &lv_font_montserrat_16, COL_ACCENT);
    lv_obj_align(round_lbl_cpu, LV_ALIGN_CENTER, 0, 6);
    round_lbl_gpu = make_label(scr_main, "GPU --%", &lv_font_montserrat_16, COL_GPU);
    lv_obj_align(round_lbl_gpu, LV_ALIGN_CENTER, 0, 28);
}

static void refresh_round_ui(void)
{
    char buf[16];
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year > 100) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        lv_label_set_text(round_lbl_time, buf);
    }
    lv_arc_set_value(round_arc_cpu, (int)(hw_info.cpu_load + 0.5f));
    lv_arc_set_value(round_arc_gpu, (int)(hw_info.gpu_load + 0.5f));
    snprintf(buf, sizeof(buf), "CPU %d%%", (int)(hw_info.cpu_load + 0.5f));
    lv_label_set_text(round_lbl_cpu, buf);
    snprintf(buf, sizeof(buf), "GPU %d%%", (int)(hw_info.gpu_load + 0.5f));
    lv_label_set_text(round_lbl_gpu, buf);
}

// ------------------------------------------------------------------
// Dynamische Updates (LCD_SHAPE_RECT)
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

static void refresh_now(void)
{
    char buf[64];

    // --- Zeit / Datum ---
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_year > 100) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        lv_label_set_text(lbl_time, buf);
        strftime(buf, sizeof(buf), "%d.%m.%Y", &ti);
        lv_label_set_text(lbl_date, buf);
    } else {
        lv_label_set_text(lbl_time, "--:--:--");
        lv_label_set_text(lbl_date, "");
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
    } else if (app_config.weather_enabled) {
        lv_label_set_text(lbl_weather, "--C");
        lv_label_set_text(lbl_humidity, "--%");
        lv_label_set_text(lbl_wind, "--");
        lv_label_set_text(lbl_rain, "--");
    } else {
        lv_label_set_text(lbl_weather, "");
        lv_label_set_text(lbl_humidity, "");
        lv_label_set_text(lbl_wind, "");
        lv_label_set_text(lbl_rain, "");
    }

    // --- WLAN/MQTT-Statusicon ---
    lv_color_t sc = (wifi_connected && mqtt_connected) ? COL_GREEN
                    : (wifi_connected ? COL_YELLOW : COL_WARN);
    lv_obj_set_style_text_color(icon_wifi, sc, 0);

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

        lv_label_set_text(det_title, is_cpu ? "CPU Verlauf" : "GPU Verlauf");
        lv_obj_set_style_text_color(det_title, is_cpu ? COL_ACCENT : COL_GPU, 0);

        snprintf(buf, sizeof(buf), "Auslastung (Usage): %.1f %%", load);
        lv_label_set_text(det_load, buf);
        snprintf(buf, sizeof(buf), "Temperatur (Temp): %.1f C", temp);
        lv_label_set_text(det_temp, buf);
        snprintf(buf, sizeof(buf), "Leistung (Power): %.1f W", power);
        lv_label_set_text(det_power, buf);

        // Trend-Farbe wird hier bewusst ignoriert: die Kurzuebersicht faerbt
        // die gesamte Zeile in der CPU-/GPU-Datenfarbe, nicht nur den Pfeil.
        const char *sym; lv_color_t unused_col;
        compute_trend(&cpu_history, &sym, &unused_col);
        snprintf(buf, sizeof(buf), "CPU: %.0fC %.0fW %s", hw_info.cpu_temp, hw_info.cpu_power, sym);
        lv_label_set_text(lbl_quick_cpu, buf);

        compute_trend(&gpu_history, &sym, &unused_col);
        snprintf(buf, sizeof(buf), "GPU: %.0fC %.0fW %s", hw_info.gpu_temp, hw_info.gpu_power, sym);
        lv_label_set_text(lbl_quick_gpu, buf);
        (void)unused_col;

        update_chart(chart_load, ser_load, h->load, h->count);
        update_chart(chart_temp, ser_temp, h->temp, h->count);

    } else if (s_screen == SCR_SETTINGS) {
        snprintf(buf, sizeof(buf), "Firmware: %s", FW_VERSION);
        lv_label_set_text(set_fw, buf);
        snprintf(buf, sizeof(buf), "IP-Adresse: %s%s", web_portal_ip(),
                 web_portal_ap_mode() ? " (Setup-AP)" : "");
        lv_label_set_text(set_ip, buf);
        snprintf(buf, sizeof(buf), "WLAN: %s",
                 web_portal_ap_mode() ? "Setup-AP aktiv" : (wifi_connected ? "verbunden" : "getrennt"));
        lv_label_set_text(set_wifi, buf);
        snprintf(buf, sizeof(buf), "MQTT: %s", mqtt_connected ? "verbunden" : "getrennt");
        lv_label_set_text(set_mqtt, buf);
        snprintf(buf, sizeof(buf), "Freier Speicher: %u KB", (unsigned)(esp_get_free_heap_size() / 1024));
        lv_label_set_text(set_heap, buf);
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    check_standby();
    switch (s_profile->shape) {
        case LCD_SHAPE_MONO:  refresh_mono_ui();  break;
        case LCD_SHAPE_ROUND: refresh_round_ui(); break;
        default:              refresh_now();      break;
    }
}

void display_ui_begin(void)
{
    lv_display_t *disp = lcd_init();

    if (s_profile->has_touch) {
        touch_xpt2046_init(s_profile);
    }

    // Ab hier LVGL-Objekte nur unter Lock anlegen (esp_lvgl_port-Task laeuft).
    lvgl_port_lock(0);

    if (s_profile->has_touch) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_display(indev, disp);
    }

    switch (s_profile->shape) {
        case LCD_SHAPE_MONO:
            build_mono_ui();
            lv_screen_load(scr_main);
            break;
        case LCD_SHAPE_ROUND:
            build_round_ui();
            lv_screen_load(scr_main);
            break;
        default:
            build_main();
            build_detail();
            build_settings();
            lv_screen_load(scr_main);
            break;
    }

    lv_timer_create(tick_cb, 1000, NULL); // 1x/Sek aktualisieren
    tick_cb(NULL);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "UI initialisiert (%s)", s_profile->name);
}
