#include "display_mono.h"

// SSD1309 ist nur auf generischen Devkits waehlbar (app_config.display_type,
// siehe board_generic.cpp) - auf CYD/JC8048W550 (fest verdrahtete Boards,
// eigene initBoardDisplay()-Implementierung) kann dieser Codepfad nie
// erreicht werden. display_draw.cpp ruft displayMonoBuild()/
// displayMonoUpdate() aber unbedingt fuer JEDES Board auf (wie
// displayRoundBuild()/-Update()) - deshalb hier derselbe Kniff wie bei
// displayRoundButtonPoll() in display_round.cpp: echte Implementierung nur
// unter BOARD_GENERIC (zieht sonst unnoetig Adafruit_SSD1306/Wire in jedes
// Firmware-Image), sonst leere Stubs, damit main.cpp/display_draw.cpp nicht
// pro Board unterscheiden muessen.
#if defined(BOARD_GENERIC)

#include <Arduino.h>
#include <time.h>
#include "shared_state.h"
#include "displays/ssd1309.h"

// -----------------------------------------------------------------------------
// Dashboard fuer das SSD1309 (128x64, monochrom) - inhaltlich dasselbe
// Grundgeruest wie das runde Dashboard (display_round.cpp: Uhrzeit, CPU-/
// GPU-Zeile mit Last/Temp/Watt, Standby -> grosse zentrierte Uhr), aber als
// reiner Text/Balken-Nachbau statt Icons/Arcs/Farbe:
//
// - KEINE mdi_*-Icons und KEIN lv_font_montserrat_* fuer Text auf diesem
//   Display. Der esp-idf-Schwesterzweig dieses Projekts hat auf echter
//   SSD1309-Hardware bereits genau das durchprobiert (siehe README.md
//   "SSD1309 text/icon legibility"): alle vorhandenen Icon-/Text-Fonts sind
//   mit --bpp 4 (anti-aliasing) generiert, disp_ssd1309::flushCb()
//   schwellwertet aber pro Pixel hart auf Schwarz/Weiss (Luminanz >=128).
//   Kantenpixel mit nur teilweiser Deckung fallen dabei unter den
//   Schwellwert und verschwinden - bei den hier moeglichen kleinen
//   Schriftgroessen praktisch der ganze Glyph. lv_font_unscii_8/
//   lv_font_unscii_16 sind reine 1bpp-Bitmapfonts ohne Antialiasing (in
//   lv_conf.h bereits fuer genau diesen Zweck aktiviert) - einzige hier
//   sichere Wahl. lv_bar-Fuellungen sind unproblematisch (reine
//   Volltonflaechen, keine Kantengraustufen).
// - unscii deckt nur den ASCII-Bereich 0x20-0x7E ab (dieselbe Einschraenkung
//   wie bei den eingebauten Montserrat-Fonts, siehe display_layout.cpp-
//   Kommentar zu "Schliessen" statt "Schließen") - deshalb hier "C"/"W" ohne
//   Gradzeichen statt "°C".
// - Zeilenbreite bewusst konservativ (Datum ohne Jahr, Uhrzeit ohne Sekunden)
//   statt die vollen 128px auszureizen - exakte Metriken von lv_font_unscii_8
//   sind auf dieser (laut CLAUDE.md nicht selbst testbaren) Hardware nicht
//   nachgemessen, lieber Luft lassen als riskieren, dass die Kopfzeile
//   umbricht oder den Live-Punkt ueberdeckt.
// -----------------------------------------------------------------------------

namespace {

using disp_ssd1309::WIDTH;
using disp_ssd1309::HEIGHT;

lv_obj_t *s_timeLbl = nullptr;
lv_obj_t *s_dot = nullptr;
lv_obj_t *s_divider1 = nullptr;
lv_obj_t *s_divider2 = nullptr;
lv_obj_t *s_cpuLbl = nullptr;
lv_obj_t *s_cpuBar = nullptr;
lv_obj_t *s_cpuStatLbl = nullptr;
lv_obj_t *s_gpuLbl = nullptr;
lv_obj_t *s_gpuBar = nullptr;
lv_obj_t *s_gpuStatLbl = nullptr;
lv_obj_t *s_standbyLbl = nullptr;
bool s_standby = false;

void set_visible(lv_obj_t *obj, bool visible) {
  if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// Scharfkantiges Rechteck ohne Rundung/Rahmen - Vorlage fuer Trennlinie und
// Live-Punkt (Kreise/abgerundete Ecken werden von LVGL antialiasiert
// gezeichnet, dieselbe Dithering-Falle wie bei Fonts, siehe Kommentar oben).
lv_obj_t *create_solid_rect(lv_obj_t *parent, int16_t w, int16_t h, int16_t x, int16_t y) {
  lv_obj_t *r = lv_obj_create(parent);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(r, w, h);
  lv_obj_set_pos(r, x, y);
  lv_obj_set_style_radius(r, 0, 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_bg_color(r, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  return r;
}

} // namespace

void displayMonoBuild(lv_obj_t *scr) {
  s_timeLbl = s_dot = s_divider1 = s_divider2 = nullptr;
  s_cpuLbl = s_cpuBar = s_cpuStatLbl = nullptr;
  s_gpuLbl = s_gpuBar = s_gpuStatLbl = nullptr;
  s_standbyLbl = nullptr;
  s_standby = false;

  // Dunkler Hintergrund fest verdrahtet statt dem LVGL-Theme-Default zu
  // vertrauen - ein heller Hintergrund waere auf einem staendig
  // eingeschalteten selbstleuchtenden OLED nicht nur falsch, sondern ein
  // Burn-in-Risiko (siehe README.md "Display colors", dort fuer SSD1309 auf
  // echter Hardware bereits einmal genau so aufgefallen).
  lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  // Kopfzeile: Datum+Uhrzeit links, Live-Punkt rechts (dasselbe 5s-Kriterium
  // wie beim runden/eckigen Dashboard, siehe CLAUDE.md) - hier als
  // Sichtbarkeit statt Farbwechsel, da im 1bpp-Schwellwertbild kein
  // gedaempfter Zwischenton moeglich ist.
  s_timeLbl = lv_label_create(scr);
  lv_label_set_text(s_timeLbl, "");
  lv_obj_set_style_text_color(s_timeLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_timeLbl, &lv_font_unscii_8, 0);
  lv_obj_set_pos(s_timeLbl, 0, 0);

  s_dot = create_solid_rect(scr, 4, 4, WIDTH - 4, 2);

  s_divider1 = create_solid_rect(scr, WIDTH, 1, 0, 9);

  // CPU-Zeile: Last im Label selbst ("CPU 62%" statt getrenntem Wert-Label -
  // fuer ein einzeiliges Icon-loses Label gibt es keinen Grund fuer ein
  // zweites Objekt), darunter Balken, darunter Temp+Watt.
  s_cpuLbl = lv_label_create(scr);
  lv_label_set_text(s_cpuLbl, "CPU");
  lv_obj_set_style_text_color(s_cpuLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_cpuLbl, &lv_font_unscii_8, 0);
  lv_obj_set_pos(s_cpuLbl, 0, 11);

  s_cpuBar = lv_bar_create(scr);
  lv_obj_set_size(s_cpuBar, WIDTH, 5);
  lv_obj_set_pos(s_cpuBar, 0, 20);
  lv_obj_set_style_radius(s_cpuBar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_cpuBar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_cpuBar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_cpuBar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_cpuBar, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_cpuBar, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_cpuBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(s_cpuBar, 0, 100);
  lv_bar_set_value(s_cpuBar, 0, LV_ANIM_OFF);

  s_cpuStatLbl = lv_label_create(scr);
  lv_label_set_text(s_cpuStatLbl, "");
  lv_obj_set_style_text_color(s_cpuStatLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_cpuStatLbl, &lv_font_unscii_8, 0);
  lv_obj_set_pos(s_cpuStatLbl, 0, 27);

  s_divider2 = create_solid_rect(scr, WIDTH, 1, 0, 36);

  // GPU-Zeile, gleicher Aufbau, 27px tiefer als die CPU-Zeile (Divider ->
  // Label -> Balken -> Stat, siehe oben).
  s_gpuLbl = lv_label_create(scr);
  lv_label_set_text(s_gpuLbl, "GPU");
  lv_obj_set_style_text_color(s_gpuLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_gpuLbl, &lv_font_unscii_8, 0);
  lv_obj_set_pos(s_gpuLbl, 0, 38);

  s_gpuBar = lv_bar_create(scr);
  lv_obj_set_size(s_gpuBar, WIDTH, 5);
  lv_obj_set_pos(s_gpuBar, 0, 47);
  lv_obj_set_style_radius(s_gpuBar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_gpuBar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_gpuBar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_gpuBar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_gpuBar, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_gpuBar, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_gpuBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(s_gpuBar, 0, 100);
  lv_bar_set_value(s_gpuBar, 0, LV_ANIM_OFF);

  s_gpuStatLbl = lv_label_create(scr);
  lv_label_set_text(s_gpuStatLbl, "");
  lv_obj_set_style_text_color(s_gpuStatLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_gpuStatLbl, &lv_font_unscii_8, 0);
  lv_obj_set_pos(s_gpuStatLbl, 0, 54);

  // Standby: grosse zentrierte Uhr, sonst nichts - Nachbau des runden
  // Dashboards (display_round.cpp), dort aus demselben Grund eingefuehrt
  // (Signalverlust -> Inhalt reduzieren statt nur die Helligkeit senken, da
  // dieses Board keinen Backlight-Pin hat). Startzustand versteckt, siehe
  // displayMonoUpdate().
  s_standbyLbl = lv_label_create(scr);
  lv_label_set_text(s_standbyLbl, "");
  lv_obj_set_style_text_color(s_standbyLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_standbyLbl, &lv_font_unscii_16, 0);
  lv_obj_center(s_standbyLbl);
  lv_obj_add_flag(s_standbyLbl, LV_OBJ_FLAG_HIDDEN);
}

void displayMonoUpdate(void) {
  if (!s_timeLbl) return;

  hw_data_lock();
  float cpuLoad = hw_info.cpu_load, cpuTemp = hw_info.cpu_temp, cpuPower = hw_info.cpu_power;
  float gpuLoad = hw_info.gpu_load, gpuTemp = hw_info.gpu_temp, gpuPower = hw_info.gpu_power;
  bool everReceived = hw_info.ever_received;
  int64_t lastUpdateMs = hw_info.last_update_ms;
  hw_data_unlock();

  // 5s-Kriterium fuer den Live-Punkt (siehe CLAUDE.md), standby_timeout_s
  // fuer den Uebergang in den reduzierten Standby-Inhalt (0 = deaktiviert,
  // dasselbe Feld wie beim runden/eckigen Dashboard, app_config-weit
  // gemeinsam genutzt statt eines eigenen mono-spezifischen Timeouts).
  bool live = everReceived && (now_ms() - lastUpdateMs) < 5000;
  set_visible(s_dot, live);

  bool noSignal = app_config.standby_timeout_s > 0 && everReceived &&
                  (now_ms() - lastUpdateMs) > (int64_t)app_config.standby_timeout_s * 1000;
  s_standby = noSignal;

  bool showDash = !s_standby;
  set_visible(s_divider1, showDash);
  set_visible(s_divider2, showDash);
  set_visible(s_cpuLbl, showDash);
  set_visible(s_cpuBar, showDash);
  set_visible(s_cpuStatLbl, showDash);
  set_visible(s_gpuLbl, showDash);
  set_visible(s_gpuBar, showDash);
  set_visible(s_gpuStatLbl, showDash);
  set_visible(s_standbyLbl, s_standby);

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char timeBuf[16];
  if (s_standby) {
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
    lv_label_set_text(s_standbyLbl, timeBuf);
  }
  // Kopfzeile bleibt auch im Standby sichtbar (dieselbe Konvention wie beim
  // runden Dashboard: Uhrzeit ist immer da, unabhaengig vom Standby-Grund).
  strftime(timeBuf, sizeof(timeBuf), "%d.%m %H:%M", &t);
  lv_label_set_text(s_timeLbl, timeBuf);

  if (!showDash) return; // Rest ist ausgeblendet, keine Werte noetig

  // %d statt %.0f: dieser Toolchain-Build hat keinen Float-Support in
  // snprintf (siehe display_round.cpp fuer denselben Kommentar/Grund) -
  // Rundung deshalb manuell vorher auf int.
  char buf[24];
  int cpuPct = (int)(cpuLoad + 0.5f);
  int cpuTempR = (int)(cpuTemp + 0.5f);
  int cpuPowerR = (int)(cpuPower + 0.5f);
  snprintf(buf, sizeof(buf), "CPU %d%%", cpuPct);
  lv_label_set_text(s_cpuLbl, buf);
  lv_bar_set_value(s_cpuBar, cpuPct, LV_ANIM_OFF);
  snprintf(buf, sizeof(buf), "%dC %dW", cpuTempR, cpuPowerR);
  lv_label_set_text(s_cpuStatLbl, buf);

  int gpuPct = (int)(gpuLoad + 0.5f);
  int gpuTempR = (int)(gpuTemp + 0.5f);
  int gpuPowerR = (int)(gpuPower + 0.5f);
  snprintf(buf, sizeof(buf), "GPU %d%%", gpuPct);
  lv_label_set_text(s_gpuLbl, buf);
  lv_bar_set_value(s_gpuBar, gpuPct, LV_ANIM_OFF);
  snprintf(buf, sizeof(buf), "%dC %dW", gpuTempR, gpuPowerR);
  lv_label_set_text(s_gpuStatLbl, buf);
}

#else

void displayMonoBuild(lv_obj_t *) {}
void displayMonoUpdate(void) {}

#endif // BOARD_GENERIC
