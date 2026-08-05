#include <lvgl.h>
#include "shared_state.h"
#include "display_draw.h"
#include "display_layout.h"
#include "display_round.h"

// UNSCII-8 ist ein Bitmap-Font mit fester Zeichenbreite (Montserrat ist
// proportional) - fuer die ASCII-Art Pflicht, sonst verrutscht die
// Ausrichtung. Bereits in lv_conf.h aktiviert (LV_FONT_UNSCII_8).
static const char *kBootLogo =
    "   ___  _ __ __    ___  ___ \n"
    "  / o |/// // /  ,' _/ / _/ \n"
    " / _,'/ U // /_ _\\ `. / _/ \n"
    "/_/   \\_,'/___//___,'/___/ ";

// Reduzierte Wortmarke fuer Mono (SSD1309/SH1106, 128x64)
static const char *kBootLogoMono = "PULSE";


static lv_obj_t *s_bootLogo   = nullptr;
static lv_obj_t *s_bootStatus = nullptr;
static const char *s_activeLogo = kBootLogo;
static size_t s_animIndex = 0;
static char s_animBuffer[256];
static volatile bool s_bootLogoAnimFinished = false;

// Public-Funktion, damit loop() prüfen kann, ob die Animation durch ist
bool displayIsBootAnimFinished() {
    return s_bootLogoAnimFinished;
}

// Mono hat keine Zeichen-fuer-Zeichen-Animation (Text steht sofort komplett),
// deshalb hier ein fixes Mindest-Anzeigefenster statt der Anim-Laufzeit als
// natuerlicher Untergrenze.
static constexpr uint32_t kBootLogoMonoMinMs = 2000;

static void mono_boot_logo_timer_cb(lv_timer_t * timer) {
    s_bootLogoAnimFinished = true;
    lv_timer_del(timer);
}

static void boot_logo_timer_cb(lv_timer_t * timer) {
    if (s_activeLogo[s_animIndex] != '\0' && s_animIndex < (sizeof(s_animBuffer) - 1)) {
        s_animBuffer[s_animIndex] = s_activeLogo[s_animIndex];
        s_animIndex++;
        s_animBuffer[s_animIndex] = '\0';
        
        if (s_bootLogo && lv_obj_is_valid(s_bootLogo)) {
            lv_label_set_text(s_bootLogo, s_animBuffer);
        }
    } else {
        // Ende erreicht: Signal an die Main-Loop senden & Timer aufräumen
        s_bootLogoAnimFinished = true;
        lv_timer_del(timer);
    }
}

void displayDrawBootScreen()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    if (displayIsMono()) {
        s_bootLogoAnimFinished = false;
        lv_timer_create(mono_boot_logo_timer_cb, kBootLogoMonoMinMs, NULL);

        // 1. Logo-Label erzeugen und Text zuweisen
        s_bootLogo = lv_label_create(scr);
        lv_label_set_text(s_bootLogo, kBootLogoMono);
        lv_obj_set_style_text_color(s_bootLogo, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_bootLogo, &lv_font_unscii_8, 0); 
        lv_obj_align(s_bootLogo, LV_ALIGN_CENTER, 0, -10);

        // 2. Status-Label erzeugen
        s_bootStatus = lv_label_create(scr);
        lv_label_set_text(s_bootStatus, "Booting..."); // Oder initial leer lassen
        lv_obj_set_style_text_color(s_bootStatus, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_bootStatus, &lv_font_unscii_8, 0);
        lv_obj_set_style_text_align(s_bootStatus, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(s_bootStatus, lv_pct(100));
        lv_label_set_long_mode(s_bootStatus, LV_LABEL_LONG_WRAP);

        // Status unter dem Logo ausrichten (Y Offset +12)
        lv_obj_align(s_bootStatus, LV_ALIGN_CENTER, 0, 12);
        
        return;
    }

    // 1. Logo-Label initialisieren
    s_bootLogo = lv_label_create(scr);
    s_animIndex = 0;
    s_animBuffer[0] = '\0';
    lv_label_set_text(s_bootLogo, "");
    lv_obj_set_style_text_color(s_bootLogo, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_coord_t hor_res = lv_disp_get_hor_res(NULL);

    if (hor_res >= 800) {
        lv_obj_set_style_text_font(s_bootLogo, &lv_font_unscii_16, 0);
    } else {
        lv_obj_set_style_text_font(s_bootLogo, &lv_font_unscii_8, 0);
    }

    if (displayIsRound()) {
        lv_obj_align(s_bootLogo, LV_ALIGN_CENTER, 2, -22);
    } else {
        lv_obj_align(s_bootLogo, LV_ALIGN_CENTER, 0, -20);
    }

    // 2. Status-Label initialisieren
    s_bootStatus = lv_label_create(scr);
    lv_label_set_text(s_bootStatus, "");
    lv_obj_set_style_text_color(s_bootStatus, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_bootStatus, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_align(s_bootStatus, LV_TEXT_ALIGN_CENTER, 0);

    if (displayIsRound()) {
        lv_obj_align(s_bootStatus, LV_ALIGN_CENTER, 0, 10);
    } else {
        lv_obj_align(s_bootStatus, LV_ALIGN_BOTTOM_MID, 0, -15);
    }

    // 3. Animation starten (non-blocking)
    s_bootLogoAnimFinished = false;
    lv_timer_create(boot_logo_timer_cb, 10, NULL);
}

void displayDrawSetBootStatus(const char *text)
{
  if (!s_bootStatus) {
    return;
  }
  lv_label_set_text(s_bootStatus, text);
}

void displayDrawInit()
{
  lv_obj_t *scr = lv_scr_act();

  // Kompletter Schnitt: Boot-Logo UND Statuszeile verschwinden, volle
  // Flaeche fuer die Cards.
  lv_obj_clean(scr);
  s_bootLogo   = nullptr;
  s_bootStatus = nullptr;

  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  if (displayIsRound()) {
    displayRoundBuild(scr);
    return;
  }

  // Rechteckige Displays UND Mono (eigene mono_*-Widget-Typen, siehe
  // display_layout.cpp): kein statischer Platzhalter mehr - main.cpp ruft
  // direkt im Anschluss layout_apply() mit dem gespeicherten oder generierten
  // Grundlayout auf (siehe layout_default_json()), das wuerde einen hier
  // gebauten Platzhalter sofort wieder verwerfen.
}

void displayDrawUpdate()
{
  // Rechteckige Displays UND Mono aktualisieren sich ueber
  // layout_refresh_bindings() (separat von main.cpp aufgerufen, siehe
  // display_layout.cpp) - hier nur noch das runde Sonder-Dashboard, das
  // (physisch bedingt, siehe display_layout.h) nicht ueber den generischen
  // Layout-Editor laeuft. Guardet sich selbst (no-op, wenn
  // displayRoundBuild() nie lief).
  displayRoundUpdate();
}

void displayDrawButtonPoll()
{
  displayRoundButtonPoll();
  displayMonoButtonPoll();
}
