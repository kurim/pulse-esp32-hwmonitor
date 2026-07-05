#include "display_ui.h"
#include "display_ui_internal.h"
#include "shared_state.h"
#include "board_profiles.h"
#include "touch_xpt2046.h"

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
#include "esp_log.h"
#include "lvgl.h"

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

// Panel-/UI-Zustand, in lcd_init() bzw. den *_init()-Helfern gesetzt.
// s_profile zeigt auf s_profile_storage (kompilierter Default + im Webportal
// gesetzte Pin-Overrides gemergt, siehe board_profile_apply_overrides()),
// nicht mehr direkt auf die statische Tabelle in board_profiles.c.
static board_profile_t s_profile_storage;
const board_profile_t *s_profile;
int s_hres, s_vres;

// Von den Layout-Dateien (display_ui_rect/_round/_mono.c) befuellt, siehe
// display_ui_internal.h.
lv_obj_t *scr_main;
int s_screen = SCR_MAIN;
lv_obj_t *scr_detail, *scr_settings, *scr_standby;

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
bool s_standby = false;

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

// ------------------------------------------------------------------
// Navigationstaste (nur Profile ohne Touch mit nav_button>=0, aktuell nur
// GC9A01: BOOT-Taste des Devboards). Taste zieht beim Druecken gegen GND
// (interner Pullup) - erster Druck nach dem Standby weckt nur auf (analog
// zu touch_read_cb), im Wachzustand schaltet sie zwischen den Screens des
// runden Minimal-UIs um (siehe display_ui_round.c: round_ui_cycle_screen()).
// Wird per eigenem 150ms-Timer gepollt (nicht ueber den 1Hz-tick_cb, damit
// sich Tastendruecke nicht traege anfuehlen).
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
            round_ui_cycle_screen();
        }
    }
    s_nav_btn_prev_high = level_high;
}

// ------------------------------------------------------------------
// Gemeinsame Style-Helfer (auch von display_ui_rect/_round/_mono.c genutzt,
// siehe display_ui_internal.h)
// ------------------------------------------------------------------
void style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

// ------------------------------------------------------------------
// Dispatch: 1x/Sekunde Refresh + Standby-Check, unabhaengig vom Panel-Typ.
// ------------------------------------------------------------------
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