#include "display_round.h"
#include <Arduino.h>
#include <time.h>
#include "shared_state.h"
#include "display_layout.h"
#include "mdi_icons.h"
#include "weather_service.h"

namespace {

// Rundes Dashboard (GC9A01): zwei konzentrische Arcs statt Cards - Cards mit
// rechten Winkeln wuerden an den Ecken vom runden Glas abgeschnitten (siehe
// CLAUDE.md "lcd_ui_shape_t"). Handles werden gemerkt, damit
// displayRoundUpdate() nur noch Werte setzt statt bei jedem Tick neu zu
// bauen.
lv_obj_t *s_arcCpu = nullptr;
lv_obj_t *s_arcGpu = nullptr;
lv_obj_t *s_cpuRow = nullptr;
lv_obj_t *s_gpuRow = nullptr;
lv_obj_t *s_timeRow = nullptr;
lv_obj_t *s_timeMainLbl = nullptr;
lv_obj_t *s_timeSecLbl = nullptr;

// Nur im Standby sichtbar, ersetzt visuell die (dort ausgeblendeten)
// CPU-/GPU-Ringe: Vollkreis-Ring am aeusseren Rand, fuellt sich als
// Sekundenzeiger im Lauf jeder Minute (0-60s -> 0-360 Grad). Bewusst NICHT
// an Wetterdaten gekoppelt (anders als frueher, als er die Windgeschwindigkeit
// zeigte) - Uhrzeit ist immer verfuegbar, deshalb eigene Sichtbarkeits-
// bedingung in displayRoundUpdate() (nur s_standby, nicht zusaetzlich
// weather_info.valid).
lv_obj_t *s_arcSeconds = nullptr;

// Standby: grosse zentrierte Uhr, CPU/GPU-Ringe UND die grosse Wetterzeile
// ausgeblendet - stattdessen zeigt das 2x2-Raster (siehe unten) dieselbe
// Temperatur samt Icon in kompakter Form. Zwei Ausloeser: kein
// hw_info-Update mehr seit
// app_config.standby_timeout_s (s_standbyAuto merkt sich, dass DIESER Grund
// aktiv ist, damit ein Signal-Comeback automatisch wieder aufweckt) oder
// manuell per Taste (s_manualToggleRequested, gesetzt von
// displayRoundButtonPoll() - dann bleibt s_standbyAuto false, ein
// zurueckkehrendes Signal wacht den manuell gesetzten Zustand NICHT auf).
bool s_standby = false;
bool s_standbyAuto = false;
volatile bool s_manualToggleRequested = false;

void set_visible(lv_obj_t *obj, bool visible) {
  if (visible) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

// Icon und Wert je Zeile bewusst ZWEI getrennte Label-Objekte statt ein
// einzelnes Label mit Icon-Codepoint + Text ueber Font-Fallback gemischt:
// der Mix-Ansatz zwingt beide Glyphen in dieselbe Baseline/line_height, die
// vom PRIMAEREN Font (dem Icon-Font, ursprnglich fuer eine kompakte
// 16-22px-Zeile kalibriert) vorgegeben wird - beim groesseren
// lv_font_montserrat_24-Fallback-Text fuehrte das reihenweise zu
// verschobenen/abgeschnittenen Icons, egal wie ofs_y in der Icon-Font
// nachjustiert wurde (siehe mdi_24.c-Historie). Getrennte Objekte nutzen
// dagegen die unveraenderten, in sich stimmigen Metriken jeder Font-Datei.
lv_obj_t *s_cpuIconLbl = nullptr;
lv_obj_t *s_cpuValLbl = nullptr;
lv_obj_t *s_gpuIconLbl = nullptr;
lv_obj_t *s_gpuValLbl = nullptr;
lv_obj_t *s_weatherRow = nullptr;
lv_obj_t *s_weatherIconLbl = nullptr;
lv_obj_t *s_weatherValLbl = nullptr;

// Nur im Standby sichtbares 2x2-Raster mit den Wetterwerten, die in der
// staendig sichtbaren Wetterzeile (Icon+Temperatur, oben) keinen Platz
// haben - belegt exakt den Platz, den CPU-/GPU-Zeile im Normalbetrieb
// einnehmen (siehe displayRoundUpdate()).
lv_obj_t *s_feelsRow = nullptr;
lv_obj_t *s_feelsIconLbl = nullptr;
lv_obj_t *s_feelsValLbl = nullptr;
lv_obj_t *s_humidityRow = nullptr;
lv_obj_t *s_humidityValLbl = nullptr;

// Untere Raster-Reihe: Regen-Zelle (Regen-Icon, Menge, Einheit "mm"
// darunter) und Wind-Zelle (rotierender Richtungspfeil + Himmelsrichtungs-
// Kuerzel als Kopfzeile, Geschwindigkeit, Einheit "km/h" darunter) -
// nachgebaut nach Referenzfoto des Users.
lv_obj_t *s_rainRow = nullptr;
lv_obj_t *s_rainValLbl = nullptr;
lv_obj_t *s_windDirRow = nullptr;
lv_obj_t *s_windDirIconLbl = nullptr;
lv_obj_t *s_windDirCompassLbl = nullptr;
lv_obj_t *s_windDirValLbl = nullptr;

// Transparenter Flex-Row-Container, zentriert Icon+Wert als Paar - Vorlage:
// die "status"-Widgets in display_layout.cpp.
lv_obj_t *create_icon_value_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

// Kompakte Grid-Zelle: Icon (mdi_24, User-Vorgabe "Icons muessen groesser
// werden" - vorher mdi_16) UEBER dem Wert-Text (Column statt Row -
// User-Vorgabe "Icon soll ueber dem Wert"). outIconLbl optional - nur Zellen
// mit dynamischer Icon-Farbe (Gefuehlt-Temperatur) brauchen ihr Icon-Label
// spaeter erneut (siehe displayRoundUpdate()). iconColor faerbt Icon UND
// (wenn colorValue) auch den Wert-Text in derselben Farbe - User-Vorgabe
// "Wert in Icon-Farbe bei Temperatur + Feuchtigkeit".
// Transparente Flex-Column, zentriert ihre Kinder uebereinander - Grundform
// aller Standby-Grid-Zellen.
lv_obj_t *create_grid_col(lv_obj_t *parent) {
  lv_obj_t *col = lv_obj_create(parent);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, 0, 0);
  lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_layout(col, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return col;
}

lv_obj_t *create_grid_cell(lv_obj_t *parent, const char *icon, lv_obj_t **outValLbl,
                            lv_obj_t **outIconLbl = nullptr,
                            uint32_t iconColor = 0x94A3B8, bool colorValue = false,
                            const char *unit = nullptr) {
  lv_obj_t *col = create_grid_col(parent);

  lv_obj_t *iconLbl = lv_label_create(col);
  lv_label_set_text(iconLbl, icon);
  lv_obj_set_style_text_color(iconLbl, lv_color_hex(iconColor), 0);
  lv_obj_set_style_text_font(iconLbl, &mdi_24, 0);
  if (outIconLbl) *outIconLbl = iconLbl;

  *outValLbl = lv_label_create(col);
  lv_label_set_text(*outValLbl, "");
  lv_obj_set_style_text_color(*outValLbl, lv_color_hex(colorValue ? iconColor : 0xE2E8F0), 0);
  lv_obj_set_style_text_font(*outValLbl, &lv_font_montserrat_20, 0);

  // Optionale, statische Einheitenzeile unter dem Wert (Referenzfoto: grosse
  // weisse Zahl, kleine graue Einheit darunter statt Einheit im Wert-String).
  if (unit) {
    lv_obj_t *unitLbl = lv_label_create(col);
    lv_label_set_text(unitLbl, unit);
    lv_obj_set_style_text_color(unitLbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(unitLbl, &lv_font_montserrat_14, 0);
  }

  return col;
}

// Animiert EINEN Rand (Start- oder Endwinkel) des Indikators auf target_deg -
// LVGL 9.5 kennt kein lv_arc_set_value_anim(), der sanfte Uebergang laeuft
// deshalb ueber eine manuelle lv_anim_t direkt auf lv_arc_set_start_angle()/
// lv_arc_set_end_angle() als Exec-Callback (lv_value_precise_t ist bei
// LV_USE_FLOAT=0, dem Default hier, int32_t - passt direkt auf
// lv_anim_exec_xcb_t ohne Praezisionsverlust).
void animate_arc_edge(lv_obj_t *arc, bool moveStart, int32_t target_deg) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, arc);
  if (moveStart) {
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_start_angle);
    lv_anim_set_values(&a, (int32_t)lv_arc_get_angle_start(arc), target_deg);
  } else {
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_end_angle);
    lv_anim_set_values(&a, (int32_t)lv_arc_get_angle_end(arc), target_deg);
  }
  lv_anim_set_duration(&a, 300);
  lv_anim_start(&a);
}

// Animiert den Wert (CPU/GPU-Ringe, normaler value-basierter NORMAL-Modus
// mit fixem Start) statt ihn hart per lv_arc_set_value() zu springen -
// gleiches Prinzip wie animate_arc_edge() oben, nur auf lv_arc_set_value()
// statt einem Winkel als Exec-Callback.
void animate_arc_value(lv_obj_t *arc, int32_t target) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, arc);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
  lv_anim_set_values(&a, lv_arc_get_value(arc), target);
  lv_anim_set_duration(&a, 300);
  lv_anim_start(&a);
}

// Sekunden-Ring: baut sich in geraden Minuten am Endwinkel auf (Start fix bei
// 0 = 12 Uhr, Ende waechst 0->360 im Uhrzeigersinn). In ungeraden Minuten NICHT
// die Spitze rueckwaerts einziehen (saehe aus wie ein Ruecklauf) - stattdessen
// bleibt das Ende fix bei 360 (derselbe Punkt, an dem die Fuellphase endete)
// und der STARTWINKEL waechst vorwaerts (0->360) nach, frisst den beleuchteten
// Bogen also von hinten her auf, in derselben Drehrichtung wie er entstand.
// Direkte Winkelsteuerung statt lv_arc_set_value()/lv_arc_set_range(), da
// LVGLs eingebaute Value-Modi (NORMAL/REVERSE/SYMMETRICAL) kein "Ende fix,
// Start wandert vorwaerts" kennen. Eckige statt runde Kappe bei Breite 0
// (Start==Ende), da eine runde Kappe dort wie ein isolierter Punkt statt wie
// ein leerer Ring aussieht - tritt nur im Sekunde-0-Moment jeder geraden
// Minute auf (Uebergang vom vollen Ring zum leeren Start der Fuellphase).
void update_seconds_arc(uint8_t current_second, uint8_t current_minute) {
  int32_t sweep_deg = (int32_t)current_second * 360 / 60;
  bool filling = (current_minute % 2 == 0);

  int32_t start_deg = filling ? 0 : sweep_deg;
  int32_t end_deg = filling ? sweep_deg : 360;
  lv_obj_set_style_arc_rounded(s_arcSeconds, start_deg != end_deg, LV_PART_INDICATOR);

  if (filling) {
    lv_arc_set_start_angle(s_arcSeconds, 0); // fix, keine Animation noetig
    animate_arc_edge(s_arcSeconds, false, sweep_deg);
  } else {
    lv_arc_set_end_angle(s_arcSeconds, 360); // fix, keine Animation noetig
    animate_arc_edge(s_arcSeconds, true, sweep_deg);
  }
}

} // namespace

void displayRoundBuild(lv_obj_t *scr)
{
  s_arcCpu = s_arcGpu = s_arcSeconds = s_cpuRow = s_gpuRow = nullptr;
  s_timeRow = s_timeMainLbl = s_timeSecLbl = nullptr;
  s_cpuIconLbl = s_cpuValLbl = s_gpuIconLbl = s_gpuValLbl = nullptr;
  s_weatherRow = nullptr;
  s_weatherIconLbl = s_weatherValLbl = nullptr;
  s_feelsRow = s_humidityRow = s_rainRow = s_windDirRow = nullptr;
  s_feelsIconLbl = s_windDirIconLbl = s_windDirCompassLbl = nullptr;
  s_feelsValLbl = s_humidityValLbl = s_rainValLbl = s_windDirValLbl = nullptr;
  s_standby = s_standbyAuto = false;
  s_manualToggleRequested = false;

  lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  // Groessen relativ zur tatsaechlichen Display-Aufloesung statt fest
  // verdrahteter Pixelwerte - passt sich automatisch an, falls dieses Layout
  // je auf einem anderen runden Display als dem 240x240-GC9A01 laeuft.
  int d = displayWidthPx() < displayHeightPx() ? displayWidthPx() : displayHeightPx();
  constexpr int ARC_START = 120, ARC_END = 60; // 60 Grad Luecke unten (zentriert bei 90 Grad = 6-Uhr-Position)
  constexpr int ARC_BG_W = 20, ARC_FG_W = 10;  // Hintergrund-Spur breiter als aktiver Indikator
  int outer_d = d - 4;         // CPU, fast randlos aussen
  int inner_d = outer_d - 40;  // GPU, mit sichtbarem Abstand zum aeusseren Ring

  // Aeusserer Ring: CPU.
  s_arcCpu = lv_arc_create(scr);
  lv_obj_set_size(s_arcCpu, outer_d, outer_d);
  lv_obj_center(s_arcCpu);
  lv_arc_set_bg_angles(s_arcCpu, ARC_START, ARC_END);
  lv_arc_set_angles(s_arcCpu, ARC_START, ARC_END);
  lv_arc_set_range(s_arcCpu, 0, 100);

  lv_obj_set_style_bg_opa(s_arcCpu, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_arcCpu, 0, LV_PART_MAIN);

  // Hintergrund-Spur: gedaempfter Ton der eigenen Ringfarbe (20% Deckkraft)
  // statt eines neutralen Grautons.
  lv_obj_set_style_arc_color(s_arcCpu, lv_color_hex(app_config.cpu_arc_color), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s_arcCpu, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arcCpu, ARC_BG_W, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_arcCpu, true, LV_PART_MAIN);

  // Aktiver Indikator: volle Deckkraft, schmaler als die Spur.
  // LVGL 9.5 kennt kein Arc-Gradient (arc_grad_* existiert nur fuer
  // Backgrounds) - Volltonfarbe statt des urspruenglich geplanten
  // Cyan->Violett-Verlaufs.
  lv_obj_set_style_arc_color(s_arcCpu, lv_color_hex(app_config.cpu_arc_color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(s_arcCpu, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(s_arcCpu, ARC_FG_W, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arcCpu, true, LV_PART_INDICATOR);

  // Padding zentriert den schmaleren Indikator innerhalb der breiteren
  // Spur, statt dass beide an derselben Kante ausgerichtet sind.
  int cpu_pad = (ARC_BG_W - ARC_FG_W) / 2;
  lv_obj_set_style_pad_left(s_arcCpu, cpu_pad, LV_PART_INDICATOR);
  lv_obj_set_style_pad_right(s_arcCpu, cpu_pad, LV_PART_INDICATOR);
  lv_obj_set_style_pad_top(s_arcCpu, cpu_pad, LV_PART_INDICATOR);
  lv_obj_set_style_pad_bottom(s_arcCpu, cpu_pad, LV_PART_INDICATOR);

  lv_obj_remove_flag(s_arcCpu, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_arcCpu, NULL, LV_PART_KNOB);

  // Innerer Ring: GPU - derselbe Aufbau, kleinerer Durchmesser.
  s_arcGpu = lv_arc_create(scr);
  lv_obj_set_size(s_arcGpu, inner_d, inner_d);
  lv_obj_center(s_arcGpu);
  lv_arc_set_bg_angles(s_arcGpu, ARC_START, ARC_END);
  lv_arc_set_angles(s_arcGpu, ARC_START, ARC_END);
  lv_arc_set_range(s_arcGpu, 0, 100);

  lv_obj_set_style_bg_opa(s_arcGpu, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_arcGpu, 0, LV_PART_MAIN);

  lv_obj_set_style_arc_color(s_arcGpu, lv_color_hex(app_config.gpu_arc_color), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s_arcGpu, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arcGpu, ARC_BG_W, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_arcGpu, true, LV_PART_MAIN);

  lv_obj_set_style_arc_color(s_arcGpu, lv_color_hex(app_config.gpu_arc_color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(s_arcGpu, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(s_arcGpu, ARC_FG_W, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arcGpu, true, LV_PART_INDICATOR);

  int gpu_pad = (ARC_BG_W - ARC_FG_W) / 2;
  lv_obj_set_style_pad_left(s_arcGpu, gpu_pad, LV_PART_INDICATOR);
  lv_obj_set_style_pad_right(s_arcGpu, gpu_pad, LV_PART_INDICATOR);
  lv_obj_set_style_pad_top(s_arcGpu, gpu_pad, LV_PART_INDICATOR);
  lv_obj_set_style_pad_bottom(s_arcGpu, gpu_pad, LV_PART_INDICATOR);

  lv_obj_remove_flag(s_arcGpu, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_arcGpu, NULL, LV_PART_KNOB);

  // Sekunden-Ring (nur Standby): Vollkreis statt Gauge-mit-Luecke, bg/fg
  // gleich breit - deshalb keine Padding-Zentrierung wie bei CPU/GPU noetig,
  // beide Teile liegen schon deckungsgleich. Fixer Silberton statt
  // wetterabhaengiger Farbe (frueher hier: Windgeschwindigkeit) - der Ring
  // hat keinen Bezug mehr zu den Wetterdaten.
  constexpr int SEC_ARC_W = 8;
  constexpr uint32_t SEC_ARC_COLOR = 0xCBD5E1;
  s_arcSeconds = lv_arc_create(scr);
  lv_obj_set_size(s_arcSeconds, outer_d, outer_d);
  lv_obj_center(s_arcSeconds);
  lv_arc_set_bg_angles(s_arcSeconds, 0, 360);
  // Indikator-Winkel werden in update_seconds_arc() direkt gesetzt (kein
  // lv_arc_set_value()/lv_arc_set_range() - siehe Kommentar dort), deshalb
  // hier nur der zonenlose Startzustand (unsichtbar, Breite 0).
  lv_arc_set_angles(s_arcSeconds, 0, 0);
  // LVGL-Arcs zaehlen ihre Winkel ab der 3-Uhr-Position - fuer einen
  // Sekundenzeiger muss Sekunde 0 aber oben (12 Uhr) liegen.
  lv_arc_set_rotation(s_arcSeconds, 270);

  lv_obj_set_style_bg_opa(s_arcSeconds, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_arcSeconds, 0, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_arcSeconds, lv_color_hex(SEC_ARC_COLOR), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s_arcSeconds, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arcSeconds, SEC_ARC_W, LV_PART_MAIN);

  lv_obj_set_style_arc_color(s_arcSeconds, lv_color_hex(SEC_ARC_COLOR), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(s_arcSeconds, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(s_arcSeconds, SEC_ARC_W, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arcSeconds, true, LV_PART_INDICATOR);

  lv_obj_remove_flag(s_arcSeconds, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_arcSeconds, NULL, LV_PART_KNOB);

  // Zentrale Texte. lv_label_create() setzt sonst LVGLs eingebauten
  // Platzhaltertext ("Text") - der bliebe sichtbar, bis displayRoundUpdate()
  // das erste Mal laeuft (bei der Wetterzeile ggf. dauerhaft, wenn Wetter
  // gar nicht konfiguriert ist) - deshalb hier explizit leer setzen statt
  // den LVGL-Default durchscheinen zu lassen.
  // Sekunden bewusst als EIGENES, kleineres Label statt eines Fonts fuer den
  // ganzen "HH:MM:SS"-String - direkt an das grosse HH:MM angehaengt sieht
  // das wie ein Subscript aus. Cross-Achse auf END statt CENTER (anders als
  // create_icon_value_row()) haengt die kleine Zeit an die Grundlinie der
  // grossen, statt sie mittig dazwischen zu haengen.
  s_timeRow = lv_obj_create(scr);
  lv_obj_remove_flag(s_timeRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_timeRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_timeRow, 0, 0);
  lv_obj_set_style_pad_all(s_timeRow, 0, 0);
  lv_obj_set_style_pad_column(s_timeRow, 2, 0);
  lv_obj_set_size(s_timeRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_layout(s_timeRow, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_timeRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_timeRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_align(s_timeRow, LV_ALIGN_CENTER, 0, -35);

  s_timeMainLbl = lv_label_create(s_timeRow);
  lv_label_set_text(s_timeMainLbl, "");
  lv_obj_set_style_text_color(s_timeMainLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(s_timeMainLbl, &lv_font_montserrat_24, 0);

  s_timeSecLbl = lv_label_create(s_timeRow);
  lv_label_set_text(s_timeSecLbl, "");
  lv_obj_set_style_text_color(s_timeSecLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(s_timeSecLbl, &lv_font_montserrat_14, 0);
  // Manueller Pixel-Nudge nach oben - Flex-Cross-Align (END) allein setzt
  // die Sekunden etwas zu tief an, translate_y lässt sich innerhalb der Row
  // unabhängig vom Layout weiter feinjustieren.
  lv_obj_set_style_translate_y(s_timeSecLbl, -4, 0);

  // CPU-Zeile: mdi_24 (icon-only, unveraenderte Original-Metrik) + separates
  // Montserrat-24-Wert-Label nebeneinander in einer Flex-Row.
  lv_obj_t *cpuRow = create_icon_value_row(scr);
  lv_obj_align(cpuRow, LV_ALIGN_CENTER, 0, -5);
  s_cpuRow = cpuRow;

  s_cpuIconLbl = lv_label_create(cpuRow);
  lv_label_set_text(s_cpuIconLbl, MDI_CHIP);
  lv_obj_set_style_text_color(s_cpuIconLbl, lv_color_hex(app_config.cpu_arc_color), 0);
  lv_obj_set_style_text_font(s_cpuIconLbl, &mdi_24, 0);

  // Feste Breite + zentrierter Text: "100%" (3-stelliger worst case) soll
  // nicht breiter werden als "1%" und dabei den Ring streifen bzw. das Icon
  // seitlich verschieben.
  s_cpuValLbl = lv_label_create(cpuRow);
  lv_label_set_text(s_cpuValLbl, "");
  lv_obj_set_width(s_cpuValLbl, 66);
  lv_obj_set_style_text_align(s_cpuValLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_cpuValLbl, lv_color_hex(app_config.cpu_arc_color), 0);
  lv_obj_set_style_text_font(s_cpuValLbl, &lv_font_montserrat_24, 0);

  // GPU-Zeile, gleicher Aufbau.
  lv_obj_t *gpuRow = create_icon_value_row(scr);
  lv_obj_align(gpuRow, LV_ALIGN_CENTER, 0, 22);
  s_gpuRow = gpuRow;

  s_gpuIconLbl = lv_label_create(gpuRow);
  lv_label_set_text(s_gpuIconLbl, MDI_GPU);
  lv_obj_set_style_text_color(s_gpuIconLbl, lv_color_hex(app_config.gpu_arc_color), 0);
  lv_obj_set_style_text_font(s_gpuIconLbl, &mdi_24, 0);

  s_gpuValLbl = lv_label_create(gpuRow);
  lv_label_set_text(s_gpuValLbl, "");
  lv_obj_set_width(s_gpuValLbl, 66);
  lv_obj_set_style_text_align(s_gpuValLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_gpuValLbl, lv_color_hex(app_config.gpu_arc_color), 0);
  lv_obj_set_style_text_font(s_gpuValLbl, &lv_font_montserrat_24, 0);

  // Wetterzeile darunter: Icon-Farbe wechselt je nach Bedingung (siehe
  // weather_icon_color()) - direkt als Textfarbe des Icon-Labels statt
  // ueber einen Recolor-Span, da Icon jetzt ein eigenes Objekt ist. Bleibt
  // leer, solange weather_info nie gueltig wird (siehe displayRoundUpdate())
  // - bewusst kein Platzhaltertext.
  s_weatherRow = create_icon_value_row(scr);
  lv_obj_align(s_weatherRow, LV_ALIGN_CENTER, 0, 49);

  s_weatherIconLbl = lv_label_create(s_weatherRow);
  lv_label_set_text(s_weatherIconLbl, "");
  lv_obj_set_style_text_font(s_weatherIconLbl, &mdi_24, 0);

  s_weatherValLbl = lv_label_create(s_weatherRow);
  lv_label_set_text(s_weatherValLbl, "");
  lv_obj_set_style_text_color(s_weatherValLbl, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_text_font(s_weatherValLbl, &lv_font_montserrat_24, 0);

  // Standby-Raster: oben Temperatur (derselbe Wert wie in der Primaerzeile,
  // weather_info.temp_c - nicht feels_like_c) + Luftfeuchtigkeit
  // nebeneinander, darunter mittig eine kombinierte Zelle aus rotierendem
  // Windrichtungs-Pfeil mit Regenmenge/Windgeschwindigkeit als zweizeiliger
  // Wert darunter (ersetzt die vormals getrennten Regen-/Wind-Zellen).
  // Temperatur-Zelle zeigt dasselbe dynamische Wetter-Icon wie die
  // Wetterzeile oben (statt eines statischen Thermometers) - Icon-Label
  // dafuer gemerkt, Startfarbe hier irrelevant (wird in displayRoundUpdate()
  // bei jedem Tick ueberschrieben). Luftfeuchtigkeit hat keine
  // wetterlagen-abhaengige Farbe, deshalb fest auf Wasser-Blau statt
  // dynamisch nachgefuehrt.
  constexpr uint32_t kHumidityColor = 0x38BDF8;
  s_feelsRow = create_grid_cell(scr, MDI_THERMOMETER, &s_feelsValLbl, &s_feelsIconLbl, 0xFDB813, true);
  lv_obj_align(s_feelsRow, LV_ALIGN_CENTER, -48, -18);

  s_humidityRow = create_grid_cell(scr, MDI_WATER_PERCENT, &s_humidityValLbl, nullptr, kHumidityColor, true);
  lv_obj_align(s_humidityRow, LV_ALIGN_CENTER, 48, -18);

  // Regen-Zelle links unten: Regen-Icon in Regen-Blau, Menge als grosse
  // Zahl, "mm" klein darunter (Referenzfoto).
  s_rainRow = create_grid_cell(scr, MDI_WEATHER_RAINY, &s_rainValLbl, nullptr,
                               0x38BDF8, false, "mm");
  lv_obj_align(s_rainRow, LV_ALIGN_CENTER, -48, 46);

  // Wind-Zelle rechts unten: Kopfzeile aus rotierendem Richtungspfeil +
  // Himmelsrichtungs-Kuerzel (statt eines statischen "Windy"-Icons),
  // darunter Geschwindigkeit als grosse Zahl und "km/h" klein. Der Pfeil ist
  // LV_SYMBOL_UP (in jeder LVGL-Montserrat-Schrift eingebettet, kein eigenes
  // MDI-Icon dafuer vorhanden) und wird in displayRoundUpdate() per
  // transform_rotation auf wind_deg gedreht (0 Grad = Nord = ungedreht nach
  // oben). Pivot auf die Objektmitte, sonst rotiert LVGL um die Objekt-Ecke
  // statt um den sichtbaren Pfeil-Mittelpunkt.
  s_windDirRow = create_grid_col(scr);
  lv_obj_align(s_windDirRow, LV_ALIGN_CENTER, 48, 46);

  lv_obj_t *windHead = lv_obj_create(s_windDirRow);
  lv_obj_remove_flag(windHead, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(windHead, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(windHead, 0, 0);
  lv_obj_set_style_pad_all(windHead, 0, 0);
  lv_obj_set_style_pad_column(windHead, 2, 0);
  lv_obj_set_size(windHead, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_layout(windHead, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(windHead, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(windHead, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  s_windDirIconLbl = lv_label_create(windHead);
  lv_label_set_text(s_windDirIconLbl, LV_SYMBOL_UP);
  lv_obj_set_style_text_color(s_windDirIconLbl, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_text_font(s_windDirIconLbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_transform_pivot_x(s_windDirIconLbl, lv_pct(50), 0);
  lv_obj_set_style_transform_pivot_y(s_windDirIconLbl, lv_pct(50), 0);

  s_windDirCompassLbl = lv_label_create(windHead);
  lv_label_set_text(s_windDirCompassLbl, "");
  lv_obj_set_style_text_color(s_windDirCompassLbl, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_text_font(s_windDirCompassLbl, &lv_font_montserrat_20, 0);

  s_windDirValLbl = lv_label_create(s_windDirRow);
  lv_label_set_text(s_windDirValLbl, "");
  lv_obj_set_style_text_color(s_windDirValLbl, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_text_font(s_windDirValLbl, &lv_font_montserrat_20, 0);

  lv_obj_t *windUnitLbl = lv_label_create(s_windDirRow);
  lv_label_set_text(windUnitLbl, "km/h");
  lv_obj_set_style_text_color(windUnitLbl, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(windUnitLbl, &lv_font_montserrat_14, 0);
}

void displayRoundUpdate(void)
{
  if (!s_arcCpu) return;

  hw_data_lock();
  float cpuLoad = hw_info.cpu_load;
  float gpuLoad = hw_info.gpu_load;
  bool everReceived = hw_info.ever_received;
  int64_t lastUpdateMs = hw_info.last_update_ms;
  hw_data_unlock();

  // Automatischer Standby bei Signalverlust - noSignal ueberschreibt einen
  // manuell per Taste beendeten Standby wieder, solange weiterhin nichts
  // ankommt (siehe Kommentar bei s_standby oben).
  bool noSignal = everReceived &&
                  (now_ms() - lastUpdateMs) > (int64_t)app_config.standby_timeout_s * 1000;
  if (noSignal) {
    s_standby = true;
    s_standbyAuto = true;
  } else if (s_standbyAuto) {
    s_standby = false;
    s_standbyAuto = false;
  }
  if (s_manualToggleRequested) {
    s_manualToggleRequested = false;
    s_standby = !s_standby;
    s_standbyAuto = false;
  }

  set_visible(s_weatherRow, !s_standby);
  set_visible(s_arcCpu, !s_standby);
  set_visible(s_arcGpu, !s_standby);
  set_visible(s_cpuRow, !s_standby);
  set_visible(s_gpuRow, !s_standby);
  // Grid nur zeigen, wenn ueberhaupt gueltige Wetterdaten vorliegen - sonst
  // leere Zellen statt der no-placeholder-Konvention dieser Datei (siehe
  // Kommentar bei der Wetterzeile oben). Der Sekunden-Ring haengt NICHT an
  // dieser Bedingung - Uhrzeit ist immer da, auch ohne Wetterdaten.
  bool showWeatherGrid = s_standby && weather_info.valid;
  set_visible(s_feelsRow, showWeatherGrid);
  set_visible(s_humidityRow, showWeatherGrid);
  set_visible(s_rainRow, showWeatherGrid);
  set_visible(s_windDirRow, showWeatherGrid);
  set_visible(s_arcSeconds, s_standby);
  lv_obj_set_style_text_font(s_timeMainLbl, s_standby ? &lv_font_montserrat_32 : &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_font(s_timeSecLbl, s_standby ? &lv_font_montserrat_16 : &lv_font_montserrat_14, 0);
  // Im Standby braucht die groessere Uhr zusaetzlichen Abstand nach oben,
  // weil das Raster darunter durch die Icon-ueber-Wert-Anordnung (siehe
  // create_grid_cell()) deutlich hoeher baut als die vorherige
  // Icon-neben-Wert-Zeile.
  lv_obj_align(s_timeRow, LV_ALIGN_CENTER, 0, s_standby ? -68 : -35);

  int cpuPct = (int)(cpuLoad + 0.5f);
  int gpuPct = (int)(gpuLoad + 0.5f);
  animate_arc_value(s_arcCpu, cpuPct);
  animate_arc_value(s_arcGpu, gpuPct);

  // %d statt %.0f: dieser Toolchain-Build hat keinen Float-Support in
  // snprintf ("%.0f" produziert nur ein literales "f" statt der Zahl,
  // Rundung passiert deshalb vorher manuell ueber cpuPct/gpuPct oben).
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", cpuPct);
  lv_label_set_text(s_cpuValLbl, buf);
  snprintf(buf, sizeof(buf), "%d%%", gpuPct);
  lv_label_set_text(s_gpuValLbl, buf);

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char timeBuf[8];
  strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
  lv_label_set_text(s_timeMainLbl, timeBuf);
  strftime(timeBuf, sizeof(timeBuf), ":%S", &t);
  lv_label_set_text(s_timeSecLbl, timeBuf);
  if (s_standby) {
    update_seconds_arc((uint8_t)t.tm_sec, (uint8_t)t.tm_min);
  }

  if (weather_info.valid) {
    int tempRounded = (int)(weather_info.temp_c + (weather_info.temp_c >= 0 ? 0.5f : -0.5f));
    lv_obj_set_style_text_color(s_weatherIconLbl, lv_color_hex(weather_icon_color(weather_info.icon)), 0);
    lv_label_set_text(s_weatherIconLbl, weather_icon_mdi(weather_info.icon));
    snprintf(buf, sizeof(buf), "%d\xc2\xb0" "C", tempRounded);
    lv_label_set_text(s_weatherValLbl, buf);

    // Nur relevant, wenn das Grid gerade sichtbar ist (Standby) - kostet im
    // Normalbetrieb ein paar ueberfluessige lv_label_set_text()-Aufrufe auf
    // ausgeblendete Labels, unkritisch bei 500ms-Takt.
    uint32_t feelsColor = weather_icon_color(weather_info.icon);
    lv_obj_set_style_text_color(s_feelsIconLbl, lv_color_hex(feelsColor), 0);
    lv_obj_set_style_text_color(s_feelsValLbl, lv_color_hex(feelsColor), 0);
    lv_label_set_text(s_feelsIconLbl, weather_icon_mdi(weather_info.icon));
    snprintf(buf, sizeof(buf), "%d\xc2\xb0" "C", tempRounded);
    lv_label_set_text(s_feelsValLbl, buf);

    snprintf(buf, sizeof(buf), "%d%%", weather_info.humidity);
    lv_label_set_text(s_humidityValLbl, buf);

    // Regen mit einer Nachkommastelle ueber Zehntel-Ganzzahl-Rechnung, wie
    // ueberall sonst in dieser Datei (siehe Kommentar bei cpuPct/gpuPct).
    int rainTenths = (int)(weather_info.rain_1h * 10 + 0.5f);
    snprintf(buf, sizeof(buf), "%d.%d", rainTenths / 10, rainTenths % 10);
    lv_label_set_text(s_rainValLbl, buf);

    // Pfeil zeigt per Rotation in die Richtung, aus der der Wind kommt
    // (0 Grad = Nord = Pfeil unrotiert nach oben) - siehe Kommentar bei der
    // Zellen-Erstellung in displayRoundBuild().
    lv_obj_set_style_transform_rotation(s_windDirIconLbl, weather_info.wind_deg * 10, 0);
    lv_label_set_text(s_windDirCompassLbl, weather_wind_compass(weather_info.wind_deg));
    int windRounded = (int)(weather_info.wind_speed + 0.5f);
    snprintf(buf, sizeof(buf), "%d", windRounded);
    lv_label_set_text(s_windDirValLbl, buf);
  }
}

// BOOT-Taste als Standby-Umschalter, auf jedem generischen Devkit sinnvoll
// (kein CYD/JC8048W550 - die haben Touch und keine freie BOOT-Taste dafuer).
// Pin ist chip-abhaengig: C3 hat seine BOOT-Taste auf GPIO9 (siehe CLAUDE.md
// "Nav-Button fuer Profile ohne Touch"), auf ESP32/S3-Devkits (u.a. dem real
// getesteten ESP32-S3-Zero) liegt sie auf GPIO0 - dort sind BOOT und RESET
// zwei getrennte physische Taster, GPIO0 ist NICHT mit RST kurzgeschlossen
// (fruehere Annahme "auf ESP32/S3 kein physischer Taster dahinter" war
// falsch, User-Feedback anhand echter Hardware).
#if defined(BOARD_GENERIC)

void displayRoundButtonPoll(void)
{
#if CONFIG_IDF_TARGET_ESP32C3
  constexpr uint8_t kButtonPin = 9;
#else
  constexpr uint8_t kButtonPin = 0;
#endif
  constexpr uint32_t kDebounceMs = 40;

  static bool initialized = false;
  static bool lastLevel = true;        // INPUT_PULLUP: HIGH = losgelassen
  static uint32_t lastChangeMs = 0;

  if (!initialized) {
    pinMode(kButtonPin, INPUT_PULLUP);
    initialized = true;
  }

  bool level = digitalRead(kButtonPin) != 0;
  uint32_t now = millis();
  if (level != lastLevel && (now - lastChangeMs) >= kDebounceMs) {
    lastChangeMs = now;
    if (lastLevel && !level) { // fallende Flanke: losgelassen -> gedrueckt
      s_manualToggleRequested = true;
    }
    lastLevel = level;
  }
}

#else

void displayRoundButtonPoll(void) {}

#endif
