#include "display_ui.h"
#include "shared_state.h"
#include "board_profiles.h"
#include "touch_xpt2046.h"
#include "web_portal.h"
#include "weather_service.h"
#include "mdi_icons.h"
#include "ui_strings.h"

#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h" // Core esp_lcd: SSD1306/SSD1309 (esp_lcd_new_panel_ssd1306)
#include "esp_lcd_ili9341.h"
#include "esp_lcd_ili9488.h"      // Community component atanisoft/esp_lcd_ili9488
#include "esp_lcd_st7796.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lvgl.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui";

// Wie ESP_ERROR_CHECK, aber statt abort() nur eine Fehlermeldung + Rueckgabe
// NULL aus der umschliessenden Funktion. Eine falsche Pin-/Displaywahl (z.B.
// nach dem Wechsel auf einen anderen Zielchip, bei dem ein generisches Profil
// eine dort nicht existierende GPIO-Nummer nutzt) darf das Geraet nicht in
// eine Boot-Schleife schicken, in der das Webportal nie erreichbar wird, um
// die Auswahl zu korrigieren - siehe display_ui_begin().
#define LCD_CHECK(x) do { \
        esp_err_t __err_rc = (x); \
        if (__err_rc != ESP_OK) { \
            ESP_LOGE(TAG, "Display-Init fehlgeschlagen (%s): %s", #x, esp_err_to_name(__err_rc)); \
            return NULL; \
        } \
    } while (0)

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
// s_profile zeigt auf s_profile_storage (kompilierter Default + im Webportal
// gesetzte Pin-Overrides gemergt, siehe board_profile_apply_overrides()),
// nicht mehr direkt auf die statische Tabelle in board_profiles.c.
static board_profile_t s_profile_storage;
static const board_profile_t *s_profile;
static int s_hres, s_vres;

// ---- Bildschirmzustand ----
enum { SCR_MAIN = 0, SCR_CPU = 1, SCR_GPU = 2, SCR_SETTINGS = 3 };
static int s_screen = SCR_MAIN;

static lv_obj_t *scr_main, *scr_detail, *scr_settings, *scr_standby;

// Standby-Screen-Widgets (LCD_SHAPE_RECT) - grosse Uhrzeit + Datum + kompakte
// Wetterzeile, analog zum Standby des runden Minimal-UIs (siehe
// refresh_round_ui()), statt wie frueher nur das Backlight abzuschalten.
static lv_obj_t *standby_lbl_time, *standby_lbl_date, *standby_lbl_weather;

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
static lv_obj_t *round_arc_cpu, *round_arc_gpu, *round_lbl_time;
static lv_obj_t *round_row_cpu, *round_lbl_cpu, *round_row_gpu, *round_lbl_gpu;
// Wetterzeilen (Temp, Feuchte, Wind, Regen) - werden sowohl auf dem
// Wetter-Screen als auch im Standby gezeigt (dort zusaetzlich zur groesseren
// Uhrzeit, siehe refresh_round_ui()), daher kein separates Standby-Widget-Set.
static lv_obj_t *round_row_weather[4], *round_lbl_weather[4];

// Screens des runden Minimal-UIs, per Boot-Taste umschaltbar (nav_button in
// board_profiles.h). Standby ueberlagert beide Screens mit einer eigenen,
// reduzierten Ansicht (siehe refresh_round_ui()).
enum { ROUND_SCR_OVERVIEW = 0, ROUND_SCR_WEATHER = 1, ROUND_SCR_COUNT = 2 };
static int s_round_screen = ROUND_SCR_OVERVIEW;

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
// Standby: wenn laenger keine MQTT-Hardwaredaten ankommen (weder je
// empfangen noch seit app_config.standby_timeout_s aktualisiert - deckt
// beide Faelle über denselben Zeitvergleich ab, da last_update_ms beim Boot
// bei 0 startet). Timeout ist im Webportal einstellbar, 0 = deaktiviert.
// Aufwecken per Touch/Navigationstaste (erster Druck weckt nur, loest keine
// Aktion aus) oder automatisch, sobald wieder Daten eintreffen.
//
// Einheitlich fuer alle Panels: der Bildschirminhalt wird auf eine reduzierte
// Anzeige (nur Uhrzeit+Wetter) umgeschaltet statt Details zu zeigen - bei
// LCD_SHAPE_RECT (CYD/ILI9488/ST7796S) durch Umschalten auf einen eigenen
// Standby-Screen (siehe scr_standby), bei LCD_SHAPE_ROUND analog ueber
// refresh_round_ui(). Das Backlight bleibt dabei unangetastet (frueher wurde
// es bei Panels mit Backlight-Pin abgeschaltet - das liess den Touch beim
// naechsten Aufwecken kurzzeitig "blind" wirken und wich vom Verhalten der
// anderen Panels ab).
// ------------------------------------------------------------------
static bool s_standby = false;

static void enter_standby(void)
{
    if (s_standby) return;
    s_standby = true;
    if (s_profile->shape == LCD_SHAPE_RECT) {
        lv_screen_load(scr_standby);
    }
}

static void exit_standby(void)
{
    if (!s_standby) return;
    s_standby = false;
    if (s_profile->shape == LCD_SHAPE_RECT) {
        lv_obj_t *target = scr_main;
        if (s_screen == SCR_CPU || s_screen == SCR_GPU) target = scr_detail;
        else if (s_screen == SCR_SETTINGS)              target = scr_settings;
        lv_screen_load(target);
    }
}

static void check_standby(void)
{
    if (app_config.standby_timeout_s == 0) {
        exit_standby(); // Standby deaktiviert - falls gerade aktiv, sofort aufwecken
        return;
    }
    uint32_t timeout_ms = (uint32_t)app_config.standby_timeout_s * 1000u;
    bool no_data = (now_ms() - hw_info.last_update_ms) > timeout_ms;
    if (no_data) {
        enter_standby();
    } else {
        exit_standby(); // Daten wieder da -> automatisch aufwecken
    }
}

// ------------------------------------------------------------------
// Farb-SPI-Panels (ILI9341/ILI9488/ST7796S/GC9A01)
// ------------------------------------------------------------------
// Hoehe einer LVGL-Flush-Kachel in Zeilen (Breite x LCD_FLUSH_LINES Pixel).
// Kleiner = weniger internes SRAM fuer den Flush-Puffer, aber mehr einzelne
// SPI-Transaktionen pro Vollbild. Kein unterstuetztes Board hat PSRAM, daher
// bewusst klein gehalten statt auf Durchsatz optimiert.
#define LCD_FLUSH_LINES 20

static lv_display_t *lcd_init_color_spi(const board_profile_t *p)
{
    backlight_init(p->bl);

    spi_bus_config_t bus = {
        .mosi_io_num     = p->mosi,
        .miso_io_num     = p->miso,
        .sclk_io_num     = p->sclk,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = p->h_res * LCD_FLUSH_LINES * 2 * sizeof(uint16_t),
    };
    LCD_CHECK(spi_bus_initialize(p->spi_host, &bus, SPI_DMA_CH_AUTO));

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
    LCD_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)p->spi_host, &io_cfg, &io));

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = p->rst,
        .rgb_ele_order  = p->bgr ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    switch (app_config.display_type) {
        case DISPLAY_ILI9488:
            // atanisoft/esp_lcd_ili9488 braucht zusaetzlich die Groesse des
            // internen RGB565->RGB666-Konvertierungspuffers (in Pixeln) -
            // an die groesste zu erwartende draw_bitmap()-Flaeche angelehnt,
            // hier eine LVGL-Flush-Kachel (Breite x LCD_FLUSH_LINES Zeilen,
            // s. dcfg unten).
            LCD_CHECK(esp_lcd_new_panel_ili9488(io, &panel_cfg, p->h_res * LCD_FLUSH_LINES, &panel));
            break;
        case DISPLAY_ST7796S:
            LCD_CHECK(esp_lcd_new_panel_st7796(io, &panel_cfg, &panel));
            break;
        case DISPLAY_GC9A01:
            LCD_CHECK(esp_lcd_new_panel_gc9a01(io, &panel_cfg, &panel));
            break;
        case DISPLAY_CYD_ILI9341:
        default:
            LCD_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel));
            break;
    }

    LCD_CHECK(esp_lcd_panel_reset(panel));
    LCD_CHECK(esp_lcd_panel_init(panel));
    // Auf realer CYD-Hardware (ILI9341) verifiziert: Farbinversion "true" ergab
    // ein komplett invertiertes Bild, also dort "aus" (Default) richtig. Bei
    // GC9A01 war ohne Inversion der Hintergrund hell statt dunkel - deshalb
    // im Webportal umschaltbar statt fest verdrahtet (app_config.color_invert).
    LCD_CHECK(esp_lcd_panel_invert_color(panel, app_config.color_invert));
    LCD_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    // Rotation -> swap/mirror + Aufloesung. Fuer LCD_SHAPE_RECT (Landscape-
    // Kachel-UI) waehlt rotation die vier Landscape/Portrait-Faelle.
    //
    // LCD_SHAPE_ROUND hat keine Landscape/Portrait-Unterscheidung (Panel ist
    // quadratisch/rund) - rotation waehlt hier stattdessen direkt zwischen den
    // vier moeglichen Spiegel-Kombinationen (mirror_x/mirror_y einzeln bzw.
    // zusammen), da nicht vorhersagbar ist, welche Kombination bei einer
    // gegebenen Verbauung/Panel-Charge die richtige ist (mirror_x+mirror_y
    // zusammen ist KEIN garantiertes "richtig herum", sondern nur 180°
    // gegenueber mirror_x/mirror_y einzeln - je nach Scan-Richtung des
    // jeweiligen Panels kann das Ergebnis seitenverkehrt statt kopfueber
    // sein). Im Webportal einfach "Rotation" durchprobieren (0-3), bis
    // Text weder seitenverkehrt noch kopfueber erscheint - kein Neuflashen
    // noetig, die Auswahl greift nach dem naechsten Neustart.
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
    } else if (p->shape == LCD_SHAPE_ROUND) {
        switch (app_config.rotation) {
            case 1:  mirror_x = true;  mirror_y = false; break;
            case 2:  mirror_x = false; mirror_y = true;  break;
            case 3:  mirror_x = true;  mirror_y = true;  break;
            case 0:
            default: mirror_x = false; mirror_y = false; break;
        }
    }
    s_hres = hres;
    s_vres = vres;

    lvgl_port_cfg_t pcfg = ESP_LVGL_PORT_INIT_CONFIG();
    LCD_CHECK(lvgl_port_init(&pcfg));

    lvgl_port_display_cfg_t dcfg = {
        .io_handle     = io,
        .panel_handle  = panel,
        // Einzelpuffer statt Doppelpuffer: keines der unterstuetzten Boards
        // hat PSRAM, ein zweiter Flush-Puffer wuerde internes SRAM verdoppeln.
        // LVGL wartet beim Flush dadurch synchron auf das SPI-DMA-Ende statt
        // vorzurendern - fuer eine Dashboard-UI mit wenigen Updates/Sekunde
        // kein spuerbarer Nachteil, spart aber deutlich Speicher.
        .buffer_size   = p->h_res * LCD_FLUSH_LINES,
        .double_buffer = false,
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
    // ESP-IDF >=5.2/6.x: esp_lcd_new_panel_io_i2c erwartet einen
    // i2c_master_bus_handle_t aus dem neuen i2c_master-Treiber, nicht mehr
    // den legacy i2c_port_t (driver/i2c.h). Legacy-API brach auf IDF 6.0.2
    // mit "esp_lcd_i2c_bus_handle_t undeclared" / falscher Parameteranzahl.
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = p->i2c_sda,
        .scl_io_num = p->i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    LCD_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr          = p->i2c_addr,
        .scl_speed_hz      = p->i2c_hz,
        .control_phase_bytes = 1,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .dc_bit_offset     = 6,
    };
    LCD_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io));

    esp_lcd_panel_ssd1306_config_t ssd_cfg = { .height = p->v_res };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .bits_per_pixel = 1,
        .vendor_config  = &ssd_cfg,
    };
    esp_lcd_panel_handle_t panel = NULL;
    LCD_CHECK(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel));
    LCD_CHECK(esp_lcd_panel_reset(panel));
    LCD_CHECK(esp_lcd_panel_init(panel));
    LCD_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    s_hres = p->h_res;
    s_vres = p->v_res;

    lvgl_port_cfg_t pcfg = ESP_LVGL_PORT_INIT_CONFIG();
    LCD_CHECK(lvgl_port_init(&pcfg));

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
    if (app_config.display_type == DISPLAY_NONE) {
        ESP_LOGI(TAG, "Kein Display ausgewaehlt - Panel-Init uebersprungen.");
        return NULL;
    }

    s_profile_storage = board_profile_apply_overrides(board_profile_get(app_config.display_type),
                                                        &app_config.pin_overrides);
    s_profile = &s_profile_storage;
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

static void refresh_round_ui(void); // fwd (Definition erst weiter unten, wird aber schon in nav_button_cb() gebraucht)

// ------------------------------------------------------------------
// Navigationstaste (nur Profile ohne Touch mit nav_button>=0, aktuell nur
// GC9A01: BOOT-Taste des Devboards). Taste zieht beim Druecken gegen GND
// (interner Pullup) - erster Druck nach dem Standby weckt nur auf (analog
// zu touch_read_cb), im Wachzustand schaltet sie zwischen den Screens des
// runden Minimal-UIs um. Wird per eigenem 150ms-Timer gepollt (nicht ueber
// den 1Hz-tick_cb, damit sich Tastendruecke nicht traege anfuehlen).
// ------------------------------------------------------------------
static bool s_nav_btn_prev_high = true;

static void nav_button_init(const board_profile_t *p)
{
    if (p->nav_button < 0) return;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << p->nav_button,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
}

static void nav_button_cb(lv_timer_t *t)
{
    (void)t;
    bool level_high = gpio_get_level(s_profile->nav_button) != 0;
    if (!level_high && s_nav_btn_prev_high) {
        // Fallende Flanke = Tastendruck.
        if (s_standby) {
            exit_standby();
        } else if (s_profile->shape == LCD_SHAPE_ROUND) {
            s_round_screen = (s_round_screen + 1) % ROUND_SCR_COUNT;
            refresh_round_ui();
        }
    }
    s_nav_btn_prev_high = level_high;
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
// Navigation (LCD_SHAPE_RECT: Touch; LCD_SHAPE_ROUND: Boot-Taste zwischen
// Overview/Wetter, siehe nav_button_cb() weiter oben. LCD_SHAPE_MONO hat
// weiterhin keine Navigation und zeigt alles auf einem einzigen Screen.)
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
    // ersten MQTT-Nachricht (danach ausgeblendet).
    lbl_waiting = make_label(scr_main, ui_str(UI_STR_WAITING_MQTT), &lv_font_montserrat_14, COL_SUB);
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
// Standby-Screen (LCD_SHAPE_RECT) - grosse Uhrzeit/Datum + kompakte
// Wetterzeile, analog zur reduzierten Standby-Ansicht des runden Minimal-UIs.
// ------------------------------------------------------------------
static void build_standby(void)
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

    mono_lbl_cpu = make_label(scr_main, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(mono_lbl_cpu, 2, 24);
    mono_lbl_gpu = make_label(scr_main, "", &lv_font_montserrat_14, COL_TEXT);
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
    snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_CPU_FMT), (int)(hw_info.cpu_load + 0.5f), (int)(hw_info.cpu_temp + 0.5f));
    lv_label_set_text(mono_lbl_cpu, buf);
    snprintf(buf, sizeof(buf), ui_str(UI_STR_MONO_GPU_FMT), (int)(hw_info.gpu_load + 0.5f), (int)(hw_info.gpu_temp + 0.5f));
    lv_label_set_text(mono_lbl_gpu, buf);
}

// ------------------------------------------------------------------
// Minimal-UI fuer runde Displays (GC9A01, 240x240) - kein Touch, Navigation
// per Boot-Taste (board_profile_t.nav_button, siehe nav_button_cb()):
// - Screen "Overview": zwei Arcs fuer CPU/GPU-Auslastung, Uhrzeit in der Mitte.
// - Screen "Wetter": Temperatur/Feuchte/Wind/Regen statt der Arcs.
// - Standby (unabhaengig vom gewaehlten Screen): nur Uhrzeit + kompakte
//   Wetterzeile, da dieses Panel keinen Backlight-Pin zum Abdunkeln hat.
// Platzhalter-Layout, das spaeter noch durch ein ausgearbeitetes rundes
// Design ersetzt werden kann.
// ------------------------------------------------------------------
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

static void build_round_ui(void)
{
    scr_main = lv_obj_create(NULL);
    style_screen(scr_main);

    int d = s_hres < s_vres ? s_hres : s_vres; // Durchmesser = kleinere Kante
    // Zwei konzentrische Ringe statt links/rechts geteilter Haelften: CPU
    // ganz aussen, GPU nach innen versetzt auf derselben "Bahn". Beide
    // nutzen denselben 320°-Bogen mit einer gemeinsamen 40°-Luecke unten
    // (Suedpunkt = 90° im lokalen Arc-Frame), dort wo die CPU/GPU-Textzeilen
    // sitzen - so bleibt der Bereich unter dem Text frei statt vom Ring
    // durchquert zu werden.
    const int arc_gap_deg = 40;             // Breite der Luecke unten
    const int arc_span_deg = 360 - arc_gap_deg; // 320°
    const int arc_rotation = 90 + arc_gap_deg / 2; // Start der Luecke bei Suedpunkt zentrieren
    const int arc_width = 10;
    const int arc_ring_gap = 4;              // radialer Abstand zwischen CPU- und GPU-Ring

    int arc_d = d - 16;

    round_arc_cpu = lv_arc_create(scr_main);
    lv_obj_set_size(round_arc_cpu, arc_d, arc_d);
    lv_obj_center(round_arc_cpu);
    lv_arc_set_rotation(round_arc_cpu, arc_rotation);
    lv_arc_set_bg_angles(round_arc_cpu, 0, arc_span_deg);
    lv_arc_set_range(round_arc_cpu, 0, 100);
    lv_obj_set_style_arc_width(round_arc_cpu, arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(round_arc_cpu, arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(round_arc_cpu, COL_CPU_BAR_A, LV_PART_INDICATOR);
    lv_obj_remove_flag(round_arc_cpu, LV_OBJ_FLAG_CLICKABLE);
    // LVGL-Arcs sind eigentlich Schieberegler und zeichnen deshalb per
    // Default einen Knob (dicker Punkt an der aktuellen Werteposition) -
    // ohne Style-Entfernung sah der wie ein Fremdkoerper/Glitch auf dem
    // duennen Ring aus. Fuer reine Anzeige-Ringe weg damit.
    lv_obj_remove_style(round_arc_cpu, NULL, LV_PART_KNOB);

    int arc_d_gpu = arc_d - 2 * (arc_width + arc_ring_gap);
    round_arc_gpu = lv_arc_create(scr_main);
    lv_obj_set_size(round_arc_gpu, arc_d_gpu, arc_d_gpu);
    lv_obj_center(round_arc_gpu);
    lv_arc_set_rotation(round_arc_gpu, arc_rotation);
    lv_arc_set_bg_angles(round_arc_gpu, 0, arc_span_deg);
    lv_arc_set_range(round_arc_gpu, 0, 100);
    lv_obj_set_style_arc_width(round_arc_gpu, arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(round_arc_gpu, arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(round_arc_gpu, COL_GPU_BAR_A, LV_PART_INDICATOR);
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

static void refresh_round_ui(void)
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

    // --- WLAN/MQTT-Statusicon ---
    lv_color_t sc = (wifi_connected && mqtt_connected) ? COL_GREEN
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
        snprintf(buf, sizeof(buf), ui_str(UI_STR_MQTT_FMT), mqtt_connected ? ui_str(UI_STR_CONNECTED) : ui_str(UI_STR_DISCONNECTED));
        lv_label_set_text(set_mqtt, buf);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_FREE_HEAP_FMT), (unsigned)(esp_get_free_heap_size() / 1024));
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
    if (!disp) {
        // DISPLAY_NONE (lcd_init() hat bereits geloggt) oder Panel-Init
        // fehlgeschlagen (z.B. Profil mit auf diesem Chip nicht existierender
        // GPIO-Nummer, siehe LCD_CHECK oben). In beiden Faellen bewusst NICHT
        // abbrechen: das Geraet soll trotzdem WLAN/Webportal starten, damit
        // sich der Displaytyp dort (aus)waehlen/korrigieren laesst, statt in
        // einer Boot-Schleife ohne jede Erreichbarkeit haengen zu bleiben.
        if (app_config.display_type != DISPLAY_NONE) {
            ESP_LOGE(TAG, "Kein Display initialisiert - Webportal bleibt trotzdem erreichbar, "
                          "Displaytyp dort korrigieren und neu starten.");
        }
        return;
    }

    if (s_profile->has_touch) {
        touch_xpt2046_init(s_profile);
    }
    nav_button_init(s_profile);

    // Sprache vor dem Bauen der Screens setzen - Screens werden nur einmal
    // gebaut, eine spaetere Aenderung von app_config.language greift erst
    // nach einem Neustart (siehe ui_strings.h).
    ui_set_language(app_config.language);

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
            build_standby();
            lv_screen_load(scr_main);
            break;
    }

    lv_timer_create(tick_cb, 1000, NULL); // 1x/Sek aktualisieren
    if (s_profile->nav_button >= 0) {
        lv_timer_create(nav_button_cb, 150, NULL); // schnelleres Polling fuer reaktionsfreudige Taste
    }
    tick_cb(NULL);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "UI initialisiert (%s)", s_profile->name);
}
