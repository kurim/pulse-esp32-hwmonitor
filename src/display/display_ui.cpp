#include "display_ui.h"
#include "display_ui_internal.h"
#include "../shared_state.h"
#include "board_profiles.h"
#include "lgfx_profiles.h"
#include <Arduino.h>
#include <lvgl.h>

static const char *TAG = "ui";

// Panel-/UI-Zustand (siehe display_ui_internal.h)
static board_profile_t s_profile_storage;
const board_profile_t *s_profile;
int s_hres, s_vres;

lv_obj_t *scr_main;
int s_screen = SCR_MAIN;
lv_obj_t *scr_detail, *scr_settings, *scr_standby;

bool s_standby = false;

// ------------------------------------------------------------------
// Nur CYD_ILI9341 ist in Phase 1 verkabelt/getestet - die anderen 5 Profile
// (ILI9488/ST7796S/GC9A01/SSD1309/Guition) folgen in Phase 2/3 des
// Migrationsplans mit eigenen LGFX-Klassen in lgfx_profiles.h.
// ------------------------------------------------------------------
static LGFX_CYD s_lcd;
static bool s_lcd_active = false;

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    // pushImage() statt manuellem setAddrWindow()+writePixels()-Paar: Auf
    // echter CYD-Hardware zeigte JEDER writePixels()-Aufruf (unabhaengig von
    // Puffergroesse, DMA an/aus und Aufrufanzahl - ausfuehrlich mit vier
    // Streifentests eingegrenzt) grossflaechiges Farbrauschen, waehrend
    // fillScreen() und pushImage() sauber blieben. LGFX_CYD ist BGR-verdrahtet
    // (rgb_order=true in lgfx_profiles.h), pushImage() beruecksichtigt das
    // ueber den rgb565_t-Elementtyp korrekt selbst - kein zusaetzlicher
    // swap-Parameter noetig (der bei writePixels() vorhandene Parameter war
    // hier nicht die Fehlerursache).
    s_lcd.startWrite();
    s_lcd.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)px_map);
    s_lcd.endWrite();

    lv_display_flush_ready(disp);
}

// PENIRQ (touch_irq, GPIO36) direkt lesen, unabhaengig von LovyanGFX's
// SPI-Transaktion - unterscheidet "Pin sieht nie LOW" (Hardware/Verkabelung)
// von "Pin geht LOW, aber getTouch() liefert trotzdem false" (LovyanGFX-
// Konfigurationsfehler). Temporaer fuer die Touch-Fehlersuche auf dem CYD.
#define TOUCH_IRQ_PIN 36

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int32_t x, y;
    bool touched = s_lcd.getTouch(&x, &y);
    bool irq_low = digitalRead(TOUCH_IRQ_PIN) == LOW;

    // Temporaeres Debug-Log, um bei Touch-Problemen zu unterscheiden, ob
    // getTouch() ueberhaupt Ereignisse liefert (XPT2046-Verkabelung/SPI-Bus)
    // oder ob die Koordinaten falsch auf den Bildschirm gemappt werden.
    static bool s_was_touched = false;
    static bool s_was_irq_low = false;
    if (touched != s_was_touched || irq_low != s_was_irq_low) {
        log_i("touch %s x=%d y=%d (raw IRQ pin36=%s)",
              touched ? "PRESS" : "RELEASE", (int)x, (int)y, irq_low ? "LOW" : "HIGH");
        s_was_touched = touched;
        s_was_irq_low = irq_low;
    }

    // TEMP-DEBUG: bisheriges Log meldet nie etwas, wenn touched/irq_low sich
    // NIE aendern - das ist zweideutig zwischen "Pin liegt hardwareseitig
    // dauerhaft fest" und "touch_read_cb() wird von LVGL ueberhaupt nicht
    // aufgerufen". Deshalb hier zusaetzlich alle ~2s UNBEDINGT (nicht nur bei
    // Aenderung) loggen - taucht das ueberhaupt nie auf, wird der Callback
    // nicht aufgerufen (LVGL-Input-Device-Registrierung); taucht es auf,
    // zeigt roh_pin36/irq_low ob der Pin sich beim Antippen wirklich nie
    // bewegt. Nach der Fehlersuche wieder entfernen.
    static uint32_t s_debug_last_log_ms;
    if (millis() - s_debug_last_log_ms >= 2000) {
        s_debug_last_log_ms = millis();
        log_i("TEMP-DEBUG touch_read_cb() lebt: touched=%d irq_low=%d raw_pin36=%d",
              (int)touched, (int)irq_low, digitalRead(TOUCH_IRQ_PIN));
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
// Standby (Screensaver): siehe esp-idf-Original (display_ui.c) fuer die
// ausfuehrliche Begruendung - 1:1 uebernommene Logik.
// ------------------------------------------------------------------
static void enter_standby(void)
{
    if (s_standby) return;
    s_standby = true;
    if (s_profile->shape == LCD_SHAPE_RECT || s_profile->shape == LCD_SHAPE_WIDE) {
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
    } else if (s_profile->shape == LCD_SHAPE_WIDE) {
        lv_screen_load((s_screen == SCR_SETTINGS) ? scr_settings : scr_main);
    }
}

static void check_standby(void)
{
    if (app_config.standby_timeout_s == 0) {
        exit_standby();
        return;
    }
    if (s_screen == SCR_SETTINGS) {
        return;
    }
    uint32_t timeout_ms = (uint32_t)app_config.standby_timeout_s * 1000u;
    bool no_data = (now_ms() - hw_info.last_update_ms) > timeout_ms;
    if (no_data) {
        enter_standby();
    } else {
        exit_standby();
    }
}

// ------------------------------------------------------------------
// Gemeinsame Style-Helfer
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
// Dispatch: 1x/Sekunde Refresh + Standby-Check.
// ------------------------------------------------------------------
static uint32_t s_last_tick_ms;

static void tick_cb(void)
{
    check_standby();
    switch (s_profile->shape) {
        case LCD_SHAPE_MONO:  /* refresh_mono_ui();  Phase 2 */ break;
        case LCD_SHAPE_ROUND: /* refresh_round_ui(); Phase 2 */ break;
        case LCD_SHAPE_WIDE:  /* refresh_wide_ui();  Phase 3 */ break;
        default:              refresh_now();      break;
    }
}

void display_ui_begin(void)
{
    if (app_config.display_type != DISPLAY_CYD_ILI9341) {
        // Phase 1: alle anderen Profile verhalten sich wie DISPLAY_NONE -
        // Webportal bleibt trotzdem erreichbar, um den Displaytyp zu
        // korrigieren/auf CYD umzustellen, statt in einer Boot-Schleife ohne
        // jede Erreichbarkeit haengen zu bleiben.
        if (app_config.display_type != DISPLAY_NONE) {
            log_e("Displaytyp '%s' ist in dieser Migrations-Phase noch nicht "
                  "portiert (nur CYD_ILI9341) - Panel-Init uebersprungen.",
                  board_profile_key(app_config.display_type));
        }
        return;
    }

    s_profile_storage = board_profile_apply_overrides(board_profile_get(app_config.display_type),
                                                        &app_config.pin_overrides);
    s_profile = &s_profile_storage;

    s_lcd.init();
    s_lcd.setRotation(app_config.rotation);
    s_lcd.setBrightness(app_config.brightness);
    s_lcd_active = true;

    s_hres = s_lcd.width();
    s_vres = s_lcd.height();

    // TEMP-DEBUG Stufe E: das DBG-Zaehl-Label (LVGL-Timer) blieb bei "DBG:0"
    // stehen - Nutzer vermutet, LovyanGFX zeichnet den Screen nur einmalig.
    // Hier komplett ohne LVGL: 5x direkt hintereinander (mit ECHTER
    // Zeitpause, nicht wie Streifentests A-D unter einem einzigen
    // startWrite()/endWrite()-Bracket) eine kleine 60x60-Box an derselben
    // Stelle per pushImage() aus einem Fremdpuffer neu zeichnen - exakt das
    // Muster, das disp_flush_cb() im Betrieb macht (jeweils eigenes
    // startWrite()/pushImage()/endWrite() pro Aufruf, zeitlich isoliert).
    // Bewusst NICHT fillRect() (das ist der bereits erwiesenermassen
    // funktionierende interne Fuellpfad, wie fillScreen() in Stufe 1 - haette
    // hier nichts Neues gezeigt). Wechselt die Box sichtbar die Farbe, ist
    // die Uebertragung an sich in Ordnung und der Fehler liegt spezifisch
    // bei LVGL (Invalidate/Timer/Flush-Kopplung). Bleibt sie bei der ersten
    // Farbe stehen, bestaetigt das die Vermutung: LovyanGFX/pushImage()
    // zeichnet nur beim ersten (ggf. "warmen") Aufruf, nicht bei spaeteren
    // isolierten Aufrufen. Nach der Fehlersuche wieder entfernen.
    {
        static const uint16_t box_colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xFFFF};
        const uint32_t box_w = 60, box_h = 60;
        uint16_t *box_buf = (uint16_t *)malloc(box_w * box_h * sizeof(uint16_t));
        for (int i = 0; i < 5 && box_buf; i++) {
            for (uint32_t p = 0; p < box_w * box_h; p++) box_buf[p] = box_colors[i];
            s_lcd.startWrite();
            s_lcd.pushImage(100, 90, box_w, box_h, (lgfx::rgb565_t *)box_buf);
            s_lcd.endWrite();
            log_i("TEMP-DEBUG Stufe E: Box %d per pushImage() gezeichnet (Farbe 0x%04X)", i, box_colors[i]);
            delay(2000);
        }
        free(box_buf);
    }

    lv_init();

    // TEMP-DEBUG: siehe lv_conf.h (LV_USE_LOG) - macht LVGL-interne Fehler
    // (z.B. Out-of-Memory bei Label-Text-Updates nach dem initialen Aufbau)
    // sichtbar, die bisher stillschweigend verschluckt wurden.
    lv_log_register_print_cb([](lv_log_level_t level, const char *buf) {
        (void)level;
        log_i("LVGL: %s", buf);
    });

    static lv_color_t *buf1;
    static lv_color_t *buf2;
    const uint32_t buf_px = s_hres * 20; // ~ LCD_FLUSH_LINES aus dem Original
    buf1 = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));
    buf2 = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));

    lv_display_t *disp = lv_display_create(s_hres, s_vres);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_px * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    if (s_profile->has_touch) {
        // Unabhaengig von LovyanGFX's eigener Touch-SPI-Init: PENIRQ-Pin
        // zusaetzlich direkt als Eingang konfigurieren, damit touch_read_cb()
        // ihn per digitalRead() zur Fehlersuche auslesen kann.
        pinMode(TOUCH_IRQ_PIN, INPUT);

        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_display(indev, disp);

        // TEMP-DEBUG: bisheriges touch_read_cb()-Log (auch die unbedingte
        // ~2s-Variante) erschien im letzten Test ueberhaupt nicht - hier
        // pruefen ob dieser Setup-Pfad ueberhaupt durchlaeuft und der indev-
        // Zeiger gueltig ist, statt das stillschweigend anzunehmen.
        log_i("TEMP-DEBUG Touch-Indev registriert: indev=%p disp=%p", (void *)indev, (void *)disp);
    }

    ui_set_language(app_config.language);

    switch (s_profile->shape) {
        default: // LCD_SHAPE_RECT (CYD)
            build_main();
            build_detail();
            build_settings();
            build_standby();
            lv_screen_load(scr_main);
            break;
    }

    // TEMP-DEBUG: Uhr/CPU-GPU-Kacheln/Wetter aktualisieren sich laut Nutzer
    // trotz nachweislich frischer Daten (refresh_now()-Log zeigt korrekte
    // Werte) nicht sichtbar auf dem Panel. Alle bisherigen pushImage()-Tests
    // (Streifentests A-D) liefen aber nur mit VOLLER Panelbreite (x=0,
    // w=s_hres) - noch nie mit einem kleinen, beliebig positionierten
    // Bereich wie einem einzelnen Textlabel. Dieses winzige, unabhaengige
    // Label zaehlt jede Sekunde per LVGL-Timer hoch (exakt derselbe Pfad wie
    // die echten Labels: lv_label_set_text_fmt() -> Invalidate -> Flush ->
    // pushImage()), aber komplett ohne MQTT/Wetter/Zeit-Abhaengigkeit.
    // Zaehlt es auf dem Panel sichtbar hoch, ist die Rendering-Pipeline fuer
    // kleine Teilbereiche in Ordnung und der Fehler liegt spezifisch bei den
    // echten Labels. Bleibt es stehen (obwohl das Log unten "TEMP-DEBUG
    // counter -> N" zeigt), ist pushImage()/der Flush fuer kleine, nicht-
    // vollbreite Bereiche der Bug. Nach der Fehlersuche wieder entfernen.
    {
        static lv_obj_t *dbg_lbl;
        dbg_lbl = lv_label_create(scr_main);
        lv_obj_set_pos(dbg_lbl, 90, 100);
        lv_obj_set_style_bg_color(dbg_lbl, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(dbg_lbl, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(dbg_lbl, lv_color_hex(0xFF00FF), 0);
        lv_label_set_text(dbg_lbl, "DBG:0");
        lv_obj_move_foreground(dbg_lbl);

        static lv_obj_t *s_dbg_lbl_ref = dbg_lbl;
        lv_timer_create([](lv_timer_t *t) {
            (void)t;
            static int n = 0;
            n++;
            lv_label_set_text_fmt(s_dbg_lbl_ref, "DBG:%d", n);
            log_i("TEMP-DEBUG counter -> %d", n);
        }, 1000, NULL);
    }

    tick_cb();
    log_i("UI initialisiert (%s)", s_profile->name);
}

void display_ui_loop(void)
{
    lv_timer_handler();

    if (s_lcd_active && millis() - s_last_tick_ms >= 1000) {
        s_last_tick_ms = millis();
        tick_cb();
    }
}
