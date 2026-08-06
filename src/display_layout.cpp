#include "display_layout.h"
#include "shared_state.h"
#include "config_store.h"
#include "mdi_icons.h"
#include "weather_service.h"
#include "wifi_provision.h"
#include "lv_template.h"
#include <lvgl.h>
#include <time.h>
#include <map>
#include <vector>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#if defined(BOARD_GENERIC)
// g_lcdWidth/g_lcdHeight: bei BOARD_GENERIC steht die Aufloesung erst zur
// Laufzeit fest (app_config.display_type) - siehe display_draw.cpp fuer
// dasselbe Muster.
#include "displays/display_factory.h"
#endif

namespace {

std::map<String, lv_obj_t *> s_widgets;   // id -> Root-Handle, fuer spaeteres Nachschlagen

// --- Karten-Widgets (eckige Farbdisplays: CYD/JC8048W550) ---
//
// Nachbau des vom User handgebauten Grundlayout-Fotos (Datum/Uhrzeit-,
// Wetter-, CPU-, GPU-Karte + Footer) als feste, in sich geschlossene
// Widget-Typen - jede Karte zieht als EIN Objekt im Editor, nicht als Haufen
// einzeln zu positionierender Teile. Nutzt die bereits an anderer Stelle
// etablierten Farben/Icons/Funktionen (app_config.cpu_arc_color/
// gpu_arc_color, mdi_icons.h, weather_service.h) statt neue zu erfinden -
// "das bisherige Farblayout" (create_card()/style_card aus lv_template.h)
// bleibt die gemeinsame Basis alle vier Karten.
//
// Jede Karte hat eine eigene Refresh-Funktion statt eines generischen
// 1:1-Bind-Mechanismus: die Karten haben mehrere unterschiedlich formatierte
// Sub-Werte (Icon-Farbe, "%d%%", "%d\xc2\xb0" "C" etc.), siehe
// refresh_datetime_widgets()/refresh_hw_card_widgets()/
// refresh_weather_card_widgets() unten. Die frei konfigurierbaren
// label/slider/button/status-Primitive samt generischem Bind-Pfad-System
// (props["bind"] -> hw_info/weather_info) sind entfernt worden - blieben im
// Editor ungenutzt, die festen Karten decken alles ab, was tatsaechlich
// gebraucht wird.
struct DateTimeWidget {
  lv_obj_t *dateLbl;
  lv_obj_t *timeMainLbl; // "HH:MM"
  lv_obj_t *timeSecLbl;  // ":SS", kleiner - gleiche Aufteilung wie beim runden Display (display_round.cpp)
  lv_obj_t *dot;
};
struct HwCardWidget {
  bool isGpu;
  lv_obj_t *iconLbl;
  lv_obj_t *valLbl;
  lv_obj_t *bar;
  lv_obj_t *tempLbl;
  lv_obj_t *powerLbl;
};
struct WeatherCardWidget {
  lv_obj_t *mainIconLbl;
  lv_obj_t *mainValLbl;
  lv_obj_t *windValLbl;
  lv_obj_t *humidityValLbl;
  lv_obj_t *rainValLbl;
};

// Verlaufsgraph-Karte (User-Vorgabe: "Chart fuer CPU+GPU mit Bindungs
// option auf Load, Temp, Power und Temp+Power"). Nutzt die bereits
// vorhandenen cpu_history/gpu_history-Ringpuffer (shared_state.h,
// 60 Samples/1 Hz, gefuellt in hw_data.cpp) - die Karte selbst erzeugt
// keine neuen Daten, sie ist nur ein weiterer Konsument der schon
// laufenden Verlaufsaufzeichnung.
enum class ChartMetric { kLoad, kTemp, kPower, kTempPower };
struct ChartCardWidget {
  bool isGpu;
  ChartMetric metric;
  lv_obj_t *chart;
  lv_chart_series_t *series1; // Load/Temp/Power, oder Temp bei kTempPower
  lv_chart_series_t *series2; // nur bei kTempPower belegt (Power, sekundaere Y-Achse)
};

std::vector<DateTimeWidget>    s_datetimeWidgets;
std::vector<HwCardWidget>      s_hwCardWidgets;
std::vector<WeatherCardWidget> s_weatherCardWidgets;
std::vector<ChartCardWidget>   s_chartCardWidgets;

// JC8048W550 (800x480) hat gegenueber der CYD (320x240) ueber 5x so viele
// Pixel - dieselben Icon-Groessen wirken darauf winzig (User-Feedback
// "Icons groesser, zumindest beim JC8048W550"). Ueber die Pixelflaeche statt
// nur die Breite entschieden, damit ILI9488/ST7796S (320x480, schmal aber
// hoch) nicht faelschlich als "gross" gelten - deren Kartenbreite ist
// genauso knapp wie bei der CYD.
bool is_big_display() {
  return (int32_t)displayWidthPx() * (int32_t)displayHeightPx() >= 300000;
}

// Kleine Icon+Wert-Zeile, Vorlage fuer die Unterzeilen aller Karten (CPU-
// Temp/Watt, Wetter-Wind/Feuchte/Regen) - transparent, keine eigene Kontur,
// nur die Karte drumherum hat den Rahmen.
lv_obj_t *create_icon_value_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

lv_obj_t *create_datetime_card(lv_obj_t *parent, JsonObjectConst) {
  card_container_t c = create_card(parent, 150, 90);

  // Kopfzeile: Datum links, Verbindungs-Punkt rechts - eigene Flex-Row mit
  // voller Kartenbreite (lv_pct loest gegen den Card-Innenbereich auf, siehe
  // pad_all in style_card), damit SPACE_BETWEEN die beiden wirklich an die
  // Raender drueckt statt sie eng nebeneinander zu zentrieren.
  lv_obj_t *headRow = lv_obj_create(c.card);
  lv_obj_remove_flag(headRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(headRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(headRow, 0, 0);
  lv_obj_set_style_pad_all(headRow, 0, 0);
  lv_obj_set_width(headRow, lv_pct(100));
  lv_obj_set_height(headRow, LV_SIZE_CONTENT);
  lv_obj_set_layout(headRow, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(headRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *dateLbl = lv_label_create(headRow);
  lv_label_set_text(dateLbl, "");
  lv_obj_set_style_text_color(dateLbl, lv_color_hex(0xE0E8F2), 0);
  lv_obj_set_style_text_font(dateLbl, &lv_font_montserrat_16, 0);

  // Verbindungs-Punkt: gruen, solange innerhalb der letzten 5s echte
  // hw_info-Daten ankamen (dasselbe 5s-Kriterium wie in CLAUDE.md fuer den
  // ESP-seitigen Timeout beschrieben), sonst gedaempftes Grau statt rot -
  // "keine Daten" ist der Normalzustand direkt nach dem Boot, kein Fehler.
  lv_obj_t *dot = lv_obj_create(headRow);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dot, 10, 10);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0x64748B), 0);

  // Sekunden als eigenes, kleineres Label statt Teil von "HH:MM:SS" - gleiche
  // Aufteilung wie beim runden Display (display_round.cpp), dort auf
  // User-Wunsch eingefuehrt, jetzt hier ebenso ("Sekunden sollen wie beim
  // round screen auch kleiner dargestellt werden"). Cross-Achse auf END
  // haengt die kleinen Sekunden an die Grundlinie der grossen HH:MM statt
  // sie mittig dazwischen zu haengen.
  lv_obj_t *timeRow = lv_obj_create(c.card);
  lv_obj_remove_flag(timeRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(timeRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(timeRow, 0, 0);
  lv_obj_set_style_pad_all(timeRow, 0, 0);
  lv_obj_set_style_pad_column(timeRow, 2, 0);
  lv_obj_set_size(timeRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_layout(timeRow, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(timeRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(timeRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  lv_obj_t *timeMainLbl = lv_label_create(timeRow);
  lv_label_set_text(timeMainLbl, "");
  lv_obj_set_style_text_color(timeMainLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(timeMainLbl, &lv_font_montserrat_32, 0);

  lv_obj_t *timeSecLbl = lv_label_create(timeRow);
  lv_label_set_text(timeSecLbl, "");
  lv_obj_set_style_text_color(timeSecLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(timeSecLbl, &lv_font_montserrat_16, 0);
  // Manueller Pixel-Nudge nach oben - Flex-Cross-Align (END) allein setzt die
  // Sekunden zu tief an (gleicher Fix wie beim runden Display, siehe
  // display_round.cpp).
  lv_obj_set_style_translate_y(timeSecLbl, -6, 0);

  s_datetimeWidgets.push_back({dateLbl, timeMainLbl, timeSecLbl, dot});
  return c.card;
}

// Gemeinsamer Bau fuer CPU-/GPU-Karte: Icon+Titel-Zeile, grosser
// Prozent-Wert, Balken, Temp/Watt-Unterzeile - Akzentfarbe kommt aus
// app_config.cpu_arc_color/gpu_arc_color (dieselben Farben, die auch das
// runde Display fuer seine Ringe nutzt, siehe display_round.cpp - im
// WebUI unter "Anzeige" fuer beide Display-Formen gemeinsam einstellbar).
lv_obj_t *build_hw_card(lv_obj_t *parent, bool isGpu) {
  uint32_t accent = isGpu ? app_config.gpu_arc_color : app_config.cpu_arc_color;
  const lv_font_t *smallIconFont = is_big_display() ? &mdi_24 : &mdi_16;
  card_container_t c = create_card(parent, 140, 140);

  lv_obj_t *titleRow = create_icon_value_row(c.card);
  lv_obj_t *icon = lv_label_create(titleRow);
  lv_label_set_text(icon, isGpu ? MDI_GPU : MDI_CHIP);
  lv_obj_set_style_text_color(icon, lv_color_hex(accent), 0);
  lv_obj_set_style_text_font(icon, &mdi_24, 0);
  lv_obj_t *title = lv_label_create(titleRow);
  lv_label_set_text(title, isGpu ? "GPU" : "CPU");
  lv_obj_set_style_text_color(title, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

  lv_obj_t *valLbl = lv_label_create(c.card);
  lv_label_set_text(valLbl, "");
  lv_obj_set_style_text_color(valLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_32, 0);
  // Manueller Pixel-Nudge nach oben (User-Vorgabe) - SPACE_BETWEEN verteilt
  // sonst gleich viel Abstand vor UND nach dem Wert, translate_y zieht ihn
  // naeher an die Titelzeile heran.
  lv_obj_set_style_translate_y(valLbl, -4, 0);

  // Prozent statt Pixelbreite: die Karte ist im Editor frei in der Groesse
  // veraenderbar, ein fester Pixelwert wuerde bei kleineren Karten ueber den
  // Rand hinausragen bzw. bei groesseren unnoetig schmal wirken.
  lv_obj_t *bar = lv_bar_create(c.card);
  lv_obj_set_size(bar, lv_pct(85), 6);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x334155), LV_PART_MAIN);
  // Verlauf gruen->rot (User-Vorgabe "gruen 0 rot 100") statt Volltonfarbe -
  // bg_color ist der "0"-Bezugspunkt, bg_grad_color der "100"-Bezugspunkt
  // (analog zu lv_style_set_bg_grad_color/lv_style_set_bg_grad_dir). Bewusst
  // nicht mehr die CPU/GPU-Akzentfarbe - die bleibt Icon/Titel/Wert vorbehalten.
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x32CD32), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0xDC0900), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  // Weiter nach oben als der Wert (User-Vorgabe "load bar auch weiter nach
  // oben verschieben") - groesserer Nudge als bei valLbl, rueckt den Balken
  // dichter an den Wert heran.
  lv_obj_set_style_translate_y(bar, -6, 0);

  lv_obj_t *bottomRow = create_icon_value_row(c.card);
  lv_obj_t *tempIcon = lv_label_create(bottomRow);
  lv_label_set_text(tempIcon, MDI_THERMOMETER);
  lv_obj_set_style_text_color(tempIcon, lv_color_hex(0xDC0900), 0); // Rot statt Pink (User-Vorgabe)
  lv_obj_set_style_text_font(tempIcon, smallIconFont, 0);
  lv_obj_t *tempLbl = lv_label_create(bottomRow);
  lv_label_set_text(tempLbl, "");
  lv_obj_set_style_text_color(tempLbl, lv_color_hex(0xE0E8F2), 0);
  lv_obj_set_style_text_font(tempLbl, &lv_font_montserrat_14, 0);

  lv_obj_t *powerIcon = lv_label_create(bottomRow);
  lv_label_set_text(powerIcon, MDI_FLASH);
  lv_obj_set_style_text_color(powerIcon, lv_color_hex(0xFFED29), 0); // Gelb statt Orange (User-Vorgabe)
  lv_obj_set_style_text_font(powerIcon, smallIconFont, 0);
  lv_obj_t *powerLbl = lv_label_create(bottomRow);
  lv_label_set_text(powerLbl, "");
  lv_obj_set_style_text_color(powerLbl, lv_color_hex(0xE0E8F2), 0);
  lv_obj_set_style_text_font(powerLbl, &lv_font_montserrat_14, 0);

  s_hwCardWidgets.push_back({isGpu, icon, valLbl, bar, tempLbl, powerLbl});
  return c.card;
}
lv_obj_t *create_cpu_card(lv_obj_t *parent, JsonObjectConst) { return build_hw_card(parent, false); }
lv_obj_t *create_gpu_card(lv_obj_t *parent, JsonObjectConst) { return build_hw_card(parent, true); }

// Flache CPU-/GPU-Karte: eigener zweiter Kartenstil neben build_hw_card()
// oben, nicht dessen Ersatz - beide bleiben im Editor nebeneinander
// waehlbar. User-Vorgabe anhand eines Referenzfotos: weisser Rahmen auf
// schwarzem Grund, grosse Ueberschrift ("CPU"/"GPU"), darunter Last/Watt/
// Temperatur als schlichte weisse Icon+Wert-Zeilen - bewusst OHNE
// Akzentfarbe (app_config.cpu_arc_color/gpu_arc_color) und OHNE Balken,
// anders als build_hw_card(). Eigene, lokale Style-Ueberschreibung von
// style_card (lv_template.h) direkt auf der Karteninstanz - der gemeinsame
// Stil (dunkler Slate-Hintergrund, gedaempfter Cyan-Rand) bleibt fuer alle
// anderen Kartentypen unangetastet.
struct HwCardFlatWidget {
  bool isGpu;
  lv_obj_t *loadLbl;
  lv_obj_t *powerLbl;
  lv_obj_t *tempLbl;
};
std::vector<HwCardFlatWidget> s_hwCardFlatWidgets;

lv_obj_t *build_hw_card_flat(lv_obj_t *parent, bool isGpu) {
  const lv_font_t *iconFont = is_big_display() ? &mdi_24 : &mdi_16;
  card_container_t c = create_card(parent, 160, 200);
  lv_obj_set_style_bg_color(c.card, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(c.card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(c.card, lv_color_white(), 0);
  lv_obj_set_style_border_opa(c.card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c.card, 3, 0);
  lv_obj_set_style_radius(c.card, 16, 0);

  lv_obj_t *title = lv_label_create(c.card);
  lv_label_set_text(title, isGpu ? "GPU" : "CPU");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);

  // Icon+Wert-Zeile in Referenzfoto-Reihenfolge (Last, Watt, Temperatur) -
  // anders als build_hw_card()'s Unterzeile (dort Temp+Watt zusammen NACH
  // Last/Balken).
  auto addRow = [&](const char *icon) {
    lv_obj_t *row = create_icon_value_row(c.card);
    lv_obj_t *iconLbl = lv_label_create(row);
    lv_label_set_text(iconLbl, icon);
    lv_obj_set_style_text_color(iconLbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(iconLbl, iconFont, 0);
    lv_obj_t *valLbl = lv_label_create(row);
    lv_label_set_text(valLbl, "");
    lv_obj_set_style_text_color(valLbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_32, 0);
    return valLbl;
  };

  lv_obj_t *loadLbl  = addRow(isGpu ? MDI_GPU : MDI_CHIP);
  lv_obj_t *powerLbl = addRow(MDI_FLASH);
  lv_obj_t *tempLbl  = addRow(MDI_THERMOMETER);

  s_hwCardFlatWidgets.push_back({isGpu, loadLbl, powerLbl, tempLbl});
  return c.card;
}
lv_obj_t *create_cpu_card_flat(lv_obj_t *parent, JsonObjectConst) { return build_hw_card_flat(parent, false); }
lv_obj_t *create_gpu_card_flat(lv_obj_t *parent, JsonObjectConst) { return build_hw_card_flat(parent, true); }

// Wetterkarte: 3 Zeilen statt 2 (User-Vorgabe "Wetter kann 3-zeilig, dann
// sollte alles reinpassen") - Hauptzeile (Icon/Farbe dynamisch aus
// weather_icon_mdi()/weather_icon_color(), siehe refresh_weather_card_widgets())
// bewusst ALLEIN in ihrer eigenen Zeile statt mit anderen Werten um die
// Breite zu konkurrieren - genau das fuehrte auf der CYD (Karte nur ~150px
// breit) zuvor zu ueberlappendem Text. Wind+Feuchte darunter zu zweit,
// Regen als dritte Zeile allein. Windrichtung (weather_wind_compass())
// bewusst weggelassen - dieselben Funktionen wie beim runden Display
// (display_round.cpp), aber fuer die schmale Karte auf das Noetigste gekuerzt.
lv_obj_t *create_weather_card(lv_obj_t *parent, JsonObjectConst) {
  const lv_font_t *smallIconFont = is_big_display() ? &mdi_20 : &mdi_16;
  const lv_font_t *smallValFont  = is_big_display() ? &lv_font_montserrat_16 : &lv_font_montserrat_12;
  card_container_t c = create_card(parent, 150, 90);

  lv_obj_t *mainRow = create_icon_value_row(c.card);
  lv_obj_t *mainIconLbl = lv_label_create(mainRow);
  lv_label_set_text(mainIconLbl, MDI_WEATHER_SUNNY);
  lv_obj_set_style_text_font(mainIconLbl, &mdi_24, 0);
  lv_obj_t *mainValLbl = lv_label_create(mainRow);
  lv_label_set_text(mainValLbl, "");
  lv_obj_set_style_text_color(mainValLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(mainValLbl, &lv_font_montserrat_24, 0);

  auto createStatsRow = [&]() {
    lv_obj_t *row = lv_obj_create(c.card);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
  };

  auto addStat = [&](lv_obj_t *row, const char *icon, lv_obj_t **outVal) {
    lv_obj_t *pair = create_icon_value_row(row);
    lv_obj_t *iconLbl = lv_label_create(pair);
    lv_label_set_text(iconLbl, icon);
    lv_obj_set_style_text_color(iconLbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(iconLbl, smallIconFont, 0);
    *outVal = lv_label_create(pair);
    lv_label_set_text(*outVal, "");
    lv_obj_set_style_text_color(*outVal, lv_color_hex(0xE0E8F2), 0);
    lv_obj_set_style_text_font(*outVal, smallValFont, 0);
  };

  lv_obj_t *windValLbl, *humidityValLbl, *rainValLbl;
  lv_obj_t *windHumidityRow = createStatsRow();
  addStat(windHumidityRow, MDI_WINDY, &windValLbl);
  addStat(windHumidityRow, MDI_WATER_PERCENT, &humidityValLbl);

  lv_obj_t *rainRow = createStatsRow();
  addStat(rainRow, MDI_WEATHER_RAINY, &rainValLbl);

  s_weatherCardWidgets.push_back({mainIconLbl, mainValLbl, windValLbl, humidityValLbl, rainValLbl});
  return c.card;
}

ChartMetric parseChartMetric(const char *s) {
  if (strcmp(s, "temp") == 0)       return ChartMetric::kTemp;
  if (strcmp(s, "power") == 0)      return ChartMetric::kPower;
  if (strcmp(s, "temp_power") == 0) return ChartMetric::kTempPower;
  return ChartMetric::kLoad;
}

// Verlaufsgraph fuer CPU ODER GPU (props["source"]), Messwert waehlbar
// (props["metric"]: load/temp/power/temp_power). Bei "temp_power" laufen
// zwei Serien auf getrennten Y-Achsen (Primaer=Temp 0-105 C, Sekundaer=
// Power 0-350 W) - beide auf eine einzige Achse zu zwingen waere bei so
// unterschiedlichen Wertebereichen irrefuehrend (eine Serie wirkt sonst
// praktisch flach). Farben wie in build_hw_card()'s Unterzeile (Temp rot,
// Power gelb), Load in der jeweiligen CPU/GPU-Akzentfarbe.
lv_obj_t *create_chart_card(lv_obj_t *parent, JsonObjectConst props) {
  bool isGpu = strcmp(props["source"] | "cpu", "gpu") == 0;
  ChartMetric metric = parseChartMetric(props["metric"] | "load");
  uint32_t accent = isGpu ? app_config.gpu_arc_color : app_config.cpu_arc_color;

  card_container_t c = create_card(parent, 150, 120);

  lv_obj_t *titleRow = create_icon_value_row(c.card);
  lv_obj_t *icon = lv_label_create(titleRow);
  lv_label_set_text(icon, isGpu ? MDI_GPU : MDI_CHIP);
  lv_obj_set_style_text_color(icon, lv_color_hex(accent), 0);
  lv_obj_set_style_text_font(icon, &mdi_24, 0);
  lv_obj_t *title = lv_label_create(titleRow);
  const char *metricLabel = metric == ChartMetric::kLoad ? "Load"
                           : metric == ChartMetric::kTemp ? "Temp"
                           : metric == ChartMetric::kPower ? "Power"
                                                            : "Temp/Power";
  char titleBuf[24];
  snprintf(titleBuf, sizeof(titleBuf), "%s %s", isGpu ? "GPU" : "CPU", metricLabel);
  lv_label_set_text(title, titleBuf);
  lv_obj_set_style_text_color(title, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

  lv_obj_t *chart = lv_chart_create(c.card);
  lv_obj_set_size(chart, lv_pct(100), lv_pct(72));
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, HIST_LEN);
  lv_chart_set_div_line_count(chart, 3, 0);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_all(chart, 2, 0);
  // Keine Punktmarker, nur die Linie - bei 60 Punkten auf einer ~150px
  // schmalen Karte waeren Marker nur ein dichter Farbklumpen.
  lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

  lv_chart_series_t *series1 = nullptr;
  lv_chart_series_t *series2 = nullptr;
  switch (metric) {
    case ChartMetric::kLoad:
      lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
      series1 = lv_chart_add_series(chart, lv_color_hex(accent), LV_CHART_AXIS_PRIMARY_Y);
      break;
    case ChartMetric::kTemp:
      lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 105);
      series1 = lv_chart_add_series(chart, lv_color_hex(0xDC0900), LV_CHART_AXIS_PRIMARY_Y);
      break;
    case ChartMetric::kPower:
      lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 350);
      series1 = lv_chart_add_series(chart, lv_color_hex(0xFFED29), LV_CHART_AXIS_PRIMARY_Y);
      break;
    case ChartMetric::kTempPower:
      lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 105);
      lv_chart_set_axis_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 350);
      series1 = lv_chart_add_series(chart, lv_color_hex(0xDC0900), LV_CHART_AXIS_PRIMARY_Y);
      series2 = lv_chart_add_series(chart, lv_color_hex(0xFFED29), LV_CHART_AXIS_SECONDARY_Y);
      break;
  }

  s_chartCardWidgets.push_back({isGpu, metric, chart, series1, series2});
  return c.card;
}

// --- Mono-Widgets (SSD1309/SH1106, 128x64) ---
//
// Eigene, bewusst schlichte Widget-Typen statt der Karten oben - Mono hat
// laut dem fruehreren display_mono.cpp-Kommentar (1bpp-Schwellwert-
// Renderer, siehe disp_mono_oled_i2c::flushCb()) grundsaetzlich KEINE
// anti-aliasing-faehigen Formen (keine Rundungen/Farbverlaeufe), nur harte
// Rechtecke/Linien und die unscii-Bitmapfonts. create_card() (Rahmen mit
// Radius) ist hier deshalb bewusst NICHT die Basis, anders als bei den
// Karten-Widgets oben.
struct MonoClockWidget   { lv_obj_t *lbl; };
struct MonoDateWidget    { lv_obj_t *lbl; };
struct MonoWeekdayWidget { lv_obj_t *lbl; };
struct MonoWeatherWidget { lv_obj_t *lbl; };
struct MonoStatWidget {
  bool isGpu;
  lv_obj_t *lbl;
  lv_obj_t *bar;
  lv_obj_t *statLbl;
};
struct MonoHwCardWidget {
  bool isGpu;
  lv_obj_t *loadLbl;
  lv_obj_t *powerLbl;
  lv_obj_t *tempLbl;
};
struct MonoLabelValueWidget {
  lv_obj_t *valLbl;
  String key;
  String unit;
};

std::vector<MonoClockWidget>      s_monoClockWidgets;
std::vector<MonoDateWidget>       s_monoDateWidgets;
std::vector<MonoWeekdayWidget>    s_monoWeekdayWidgets;
std::vector<MonoWeatherWidget>    s_monoWeatherWidgets;
std::vector<MonoStatWidget>       s_monoStatWidgets;
std::vector<MonoHwCardWidget>     s_monoHwCardWidgets;
std::vector<MonoLabelValueWidget> s_monoLabelValueWidgets;

// props["big"]: false = kleine Kopfzeilen-Uhr (unscii_8, wie im alten
// hartcodierten Dashboard), true = grosse zentrierte Standby-Uhr
// (unscii_16, wie der bisherige display_mono.cpp-Standby-Bildschirm). Die
// Layout-Engine kennt die zugewiesene Groesse eines Widgets erst NACH dem
// create()-Aufruf (layout_apply() setzt x/y/w/h danach, siehe unten) -
// deshalb ein explizites Prop statt eine Grossenheuristik zur Bauzeit.
// Text bewusst IMMER zentriert (nicht nur bei "big") - layout_apply() setzt
// nach dem create()-Aufruf immer eine explizite Breite (lv_obj_set_size),
// zentrierter Text sieht darin sowohl in einer schmalen Kopfzeilen-Haelfte
// als auch ueber die volle 128px-Breite im Standby gut aus, kein Sonderfall
// noetig.
lv_obj_t *create_mono_clock(lv_obj_t *parent, JsonObjectConst props) {
  bool big = props["big"] | false;
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "");
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl, big ? &lv_font_unscii_16 : &lv_font_unscii_8, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  s_monoClockWidgets.push_back({lbl});
  return lbl;
}

lv_obj_t *create_mono_date(lv_obj_t *parent, JsonObjectConst) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "");
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  s_monoDateWidgets.push_back({lbl});
  return lbl;
}

lv_obj_t *create_mono_weekday(lv_obj_t *parent, JsonObjectConst) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "");
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  s_monoWeekdayWidgets.push_back({lbl});
  return lbl;
}

// Nur die Temperatur (User-Vorgabe "Wetter (Temperatur)") - kein Grad-
// Zeichen, dieselbe Einschraenkung wie beim CPU/GPU-Balken weiter unten:
// unscii deckt nur ASCII 0x20-0x7E ab.
lv_obj_t *create_mono_weather_temp(lv_obj_t *parent, JsonObjectConst) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "--");
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  s_monoWeatherWidgets.push_back({lbl});
  return lbl;
}

// 12x12-Pixel-Wetter-Icons (aus MDI-Glyphen abgeleitet, User-Vorgabe) fuer
// mono_standby_weather weiter unten - gleiche Grundtechnik wie die 7x7-Icons
// weiter unten (kMonoIconChip/Flash/Therm), nur 2 Byte statt 1 Byte pro
// Zeile (12 relevante Spalten passen nicht mehr in ein Byte). Nur 4 Icons
// statt der 9 in weather_icon_mdi() (mdi_icons.h) - Bewoelkt/Nebel/Schnee
// bekommen ueber mono_weather_icon12_for_owm() unten das naechstliegende
// der 4 zugeteilt, dieselbe "kein sechstes Icon erzeugen"-Haltung wie dort.
static const uint8_t kIconSunny12[24] = {
  0b00000110, 0b00000000,
  0b01000110, 0b00100000,
  0b00100000, 0b01000000,
  0b00001111, 0b00000000,
  0b00011111, 0b10000000,
  0b11011111, 0b10110000,
  0b11011111, 0b10110000,
  0b00011111, 0b10000000,
  0b00001111, 0b00000000,
  0b00100000, 0b01000000,
  0b01000110, 0b00100000,
  0b00000110, 0b00000000,
};
static const uint8_t kIconPartlyCloudy12[24] = {
  0b00001100, 0b00000000,
  0b01001100, 0b10000000,
  0b00100000, 0b01000000,
  0b00001111, 0b00000000,
  0b00011111, 0b11000000,
  0b00111111, 0b11100000,
  0b01111111, 0b11110000,
  0b11111111, 0b11111000,
  0b11111111, 0b11111000,
  0b01111111, 0b11110000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
};
static const uint8_t kIconRainy12[24] = {
  0b00000111, 0b00000000,
  0b00011111, 0b11000000,
  0b00111111, 0b11100000,
  0b01111111, 0b11110000,
  0b11111111, 0b11111000,
  0b00000000, 0b00000000,
  0b01001001, 0b00100000,
  0b00100100, 0b10000000,
  0b00000000, 0b00000000,
  0b01001001, 0b00100000,
  0b00100100, 0b10000000,
  0b00000000, 0b00000000,
};
static const uint8_t kIconLightning12[24] = {
  0b00000111, 0b00000000,
  0b00011111, 0b11000000,
  0b00111111, 0b11100000,
  0b01111111, 0b11110000,
  0b11111111, 0b11111000,
  0b00000110, 0b00000000,
  0b00001100, 0b00000000,
  0b00011111, 0b00000000,
  0b00000110, 0b00000000,
  0b00001100, 0b00000000,
  0b00001000, 0b00000000,
  0b00010000, 0b00000000,
};

// Zeichnet ein 12x12-Wetter-Icon per LV_EVENT_DRAW_MAIN direkt in den LVGL-
// Draw-Layer, statt (wie die statischen 7x7-Icons weiter unten,
// kMonoIconChip/Flash/Therm) ein eigenes lv_obj_t pro gesetztem Pixel zu
// erzeugen. Grund: bis zu 6 Icons x 144 Pixel = bis zu 864 zusaetzliche
// Objekte sprengen den festen, nur 48 KB grossen LVGL-Speicherpool
// (LV_MEM_SIZE, lv_conf.h) - beobachtet als Absturz auf echter Hardware
// (ESP32-C3: StoreProhibited kurz nach dem ersten Forecast-Fetch;
// ESP32-S3-4MB: komplettes Haengen, kein Reboot). Das war selbst NACH einem
// Fix reproduzierbar, der das wiederholte Neuzeichnen bei unveraendertem
// Icon vermied - das reine STEHEN dieser vielen Objekte (nicht nur ihr
// wiederholtes Neu-Erzeugen) sprengt den Pool bereits beim ersten Zeichnen.
// Der Icon-Pointer haengt direkt am Holder-Objekt (lv_obj_set_user_data(),
// siehe refresh_mono_standby_weather_widgets() weiter unten) - kein
// einziges zusaetzliches lv_obj_t noetig, nur die paar Bytes CPU-Zeit fuer
// die Draw-Calls selbst.
//
// scale=1 zeichnet die 12x12-Rohdaten 1:1 (Tages-Icons im Forecast-Block,
// 12x12-Holder), scale=2 skaliert jedes Quell-Pixel auf einen 2x2-Block
// hoch (das grosse aktuelle Icon oben, 24x24-Holder) - es gibt keine
// groesseren Icon-Bitmaps, ein nicht-ganzzahliger Faktor wuerde ungleich
// grosse Pixel ergeben, daher nur 1x/2x als feste Varianten.
static void mono_weather_icon_draw(lv_event_t *e, int scale) {
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);
  const uint8_t *icon = (const uint8_t *)lv_obj_get_user_data(obj);
  if (!icon) return;

  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t objCoords;
  lv_obj_get_coords(obj, &objCoords);

  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc); // Default bereits weiss/voll deckend/radius 0 - genau das gewuenschte harte Rechteck.

  for (int8_t r = 0; r < 12; r++) {
    for (int8_t c = 0; c < 12; c++) {
      uint8_t byteVal = icon[r * 2 + c / 8];
      if (!(byteVal & (1 << (7 - (c % 8))))) continue;
      lv_area_t px = {
        objCoords.x1 + c * scale, objCoords.y1 + r * scale,
        objCoords.x1 + c * scale + scale - 1, objCoords.y1 + r * scale + scale - 1
      };
      lv_draw_rect(layer, &dsc, &px);
    }
  }
}
static void mono_weather_icon_draw_event_cb(lv_event_t *e) { mono_weather_icon_draw(e, 1); }
static void mono_weather_icon_draw_event_cb_2x(lv_event_t *e) { mono_weather_icon_draw(e, 2); }

// Ordnet einen OWM-Icon-Code (weather_info.icon/forecast_day_t.icon, siehe
// shared_state.h) einem der 4 kIcon*12-Bitmaps zu - dieselbe 2-stellige-ID-
// Logik wie weather_icon_mdi() (weather_service.cpp), nur auf 4 statt 9
// Ziele verdichtet: 3/4 (bewoelkt) und 50 (Nebel) fallen auf "teils
// bewoelkt", 13 (Schnee) auf "Regen" (naeher als Sonne/Gewitter).
const uint8_t *mono_weather_icon12_for_owm(const char *owmCode) {
  if (!owmCode || !owmCode[0] || !owmCode[1]) return kIconSunny12;
  int id = (owmCode[0] - '0') * 10 + (owmCode[1] - '0');
  if (id == 1) return kIconSunny12;
  if (id == 9 || id == 10 || id == 13) return kIconRainy12;
  if (id == 11) return kIconLightning12;
  return kIconPartlyCloudy12; // 2,3,4,50
}

// Verdichteter Tages-Forecast (forecast_info, siehe shared_state.h/
// weather_service.cpp fetch_forecast()) als echte Tabelle (User-Vorgabe):
// Kopfzeile mit Wochentagen, darunter "T" (Tageshoechst-)/"N" (Nacht-
// Tiefst-)Zeile, Tage als Spalten. props["days"] (1..FORECAST_DAYS, Default
// 3) waehlt die Spaltenzahl. Kein LVGL-Grid-Layout (lv_obj_set_style_grid_
// *_dsc_array haelt einen ROHEN Zeiger auf das Deskriptor-Array fuer die
// gesamte Objekt-Lebensdauer - in einem std::vector<MonoForecastWidget>
// gespeichert waere der bei einer Vector-Reallokation (weiterer push_back)
// ploetzlich dangling). Stattdessen 3 verschachtelte Flex-Zeilen: eine
// schmale, feste Label-Spalte ("", "T", "N") + N Tages-Zellen mit
// flex_grow(1) - da jede der 3 Zeilen dieselbe Struktur (1 feste + N
// wachsende Zellen) und dieselbe Breite (lv_pct(100) der Box) hat, richten
// sich die Spalten ueber alle 3 Zeilen hinweg exakt aus, ganz ohne
// Grid-API. Kein Icon (Icon-Fonts sind auf diesem Renderer unsicher, siehe
// Kommentar bei den Pixel-Icons weiter unten) - bei 3 Spalten in 128px
// Breite waere ohnehin kein Platz dafuer.
struct MonoForecastWidget {
  lv_obj_t *dayLbls[FORECAST_DAYS];
  lv_obj_t *hiLbls[FORECAST_DAYS];
  lv_obj_t *loLbls[FORECAST_DAYS];
  int colCount;
};
std::vector<MonoForecastWidget> s_monoForecastWidgets;

lv_obj_t *create_mono_forecast(lv_obj_t *parent, JsonObjectConst props) {
  int cols = props["days"] | 3;
  if (cols < 1) cols = 1;
  if (cols > FORECAST_DAYS) cols = FORECAST_DAYS;

  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  // Explizit gesetzt statt dem Theme-Default ueberlassen (User-Feedback:
  // "8px sind zuviel") - lv_obj_create() erbt sonst den vom aktiven Theme
  // vorgegebenen Row-Gap fuer Flex-Container, der zusaetzlich zur 8px
  // hohen unscii_8-Zeile draufkommt.
  lv_obj_set_style_pad_row(box, 4, 0);
  lv_obj_set_layout(box, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

  auto makeRow = [&]() {
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    // Explizit klein statt dem Theme-Default ueberlassen (derselbe Grund
    // wie beim pad_row oben) - bei mehr Spalten (User-Vorgabe: bis zu 5)
    // summiert sich ein groesserer Default-Spaltenabstand schnell zu
    // spuerbar weniger Platz pro Zelle auf, bis der Text (z.B. "27C")
    // umbricht statt einzeilig zu bleiben.
    lv_obj_set_style_pad_column(row, 2, 0);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    return row;
  };
  auto makeLabelCell = [&](lv_obj_t *row, const char *text) {
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
    lv_obj_set_width(lbl, 9); // 1 Zeichen (8px unscii_8) + 1px Luft
    return lbl;
  };
  auto makeDayCell = [&](lv_obj_t *row) {
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "--");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    // CLIP statt des LVGL-Standards WRAP: reicht die Zellenbreite bei sehr
    // vielen Spalten trotzdem nicht (z.B. 5 Tage auf 128px), soll lieber
    // ein Zeichen abgeschnitten werden als dass die Zeile umbricht und
    // dabei die Zeilenhoehe dieser einen Reihe gegenueber den anderen
    // beiden verschiebt (genau das war das gemeldete Symptom: "C" landet
    // eine Zeile drunter).
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(lbl, 1);
    return lbl;
  };

  MonoForecastWidget w{};
  w.colCount = cols;

  lv_obj_t *headerRow = makeRow();
  makeLabelCell(headerRow, "");
  for (int i = 0; i < cols; i++) w.dayLbls[i] = makeDayCell(headerRow);

  lv_obj_t *hiRow = makeRow();
  makeLabelCell(hiRow, "T");
  for (int i = 0; i < cols; i++) w.hiLbls[i] = makeDayCell(hiRow);

  lv_obj_t *loRow = makeRow();
  makeLabelCell(loRow, "N");
  for (int i = 0; i < cols; i++) w.loLbls[i] = makeDayCell(loRow);

  s_monoForecastWidgets.push_back(w);
  return box;
}

// Vollbild-Standby-Vorlage (128x64, User-Vorgabe): aktuelle Temperatur+Icon
// gross oben (24x24, 2x hochskaliert - siehe mono_weather_icon_draw oben),
// darunter eine dreizeilige Tagesvorhersage (Hoechst-Temp / 12x12-Icon /
// Tiefst-Temp je Tag). Bewusst ein EIGENER Widget-Typ statt eines Umbaus von
// mono_forecast oben (User-Entscheidung) - mono_forecast bleibt die
// kompakte Inline-Tabelle ohne Icons fuer beliebige Platzierung neben
// anderen Widgets, dies hier ist ein eigenstaendiges Vollbild-Widget nur
// fuers Standby-Set. Keine Wochentags-Kopfzeile, keine festen Label-Spalten
// ("T"/"N") und keine Trennlinie zwischen aktueller Temperatur und
// Vorhersage (User-Vorgabe: der dadurch freie Platz geht an ein groesseres
// aktuelles Icon oben, die Vorhersage-Gruppe ruckt via LV_FLEX_ALIGN_
// SPACE_BETWEEN auf dem Root-Flex so weit wie moeglich nach unten). Die
// Tages-Icons bleiben bei 12x12 (nicht 2x wie das aktuelle Icon) - erst das
// schafft in der Hoehe (128x64, siehe Kommentar bei create_mono_standby_
// weather) ueberhaupt Platz fuer die dritte Zeile (Tiefstwerte). Keine
// Wortbeschreibung je Tag (User-Entscheidung: das Icon traegt die
// Information allein - "Teils bewoelkt"/"Gewitter" passen bei 4-5 Spalten
// auf 128px ohnehin nicht in unscii_8, 8px/Zeichen). Beide Temp-Zeilen
// bleiben bewusst unscii_8 statt unscii_16 - bei 16px/Zeichen wuerde "-12C"
// (4 Zeichen) in einer 128px/4-Spalten-Zeile abgeschnitten und eine falsche
// Temperatur vortaeuschen.
// Icon-Holder sind leere 12x12-Boxen (aktuelles Icon: 24x24), die refresh_
// mono_standby_weather_widgets() bei jedem Refresh nur per lv_obj_set_
// user_data()+lv_obj_invalidate() neu bestueckt - anders als die statischen
// 7x7-Icons oben
// (Chip/Flash/Therm, immer dasselbe Icon) haengt das Wetter-Icon von Live-
// Daten ab. curIconDrawn/dayIconDrawn merken das zuletzt gezeichnete Icon
// (Pointer auf eines der statischen kIcon*12-Arrays, nullptr = noch keins/
// "--") - layout_refresh_bindings() ruft die Refresh-Funktion alle 500ms
// auf, DAUERHAFT (main.cpp), nicht nur bei tatsaechlich neuen Wetterdaten.
// Ohne diesen Cache wuerden bei jedem Aufruf alle Icon-Holder per lv_obj_
// clean() geleert und mit bis zu 144 einzelnen 1x1-Objekten neu bestueckt
// (12x12, bis zu 6 Icons gleichzeitig) - dieser Objekt-Erzeugungs-/
// Loeschzyklus 2x/Sekunde fragmentierte auf echter Hardware (ESP32-C3,
// 400 KB SRAM, kein PSRAM) den Heap innerhalb kurzer Zeit bis lv_obj_
// create() Muell/NULL zurueckgab, beobachtet als "StoreProhibited"-Crash
// kurz nachdem forecast_info gueltig wurde (siehe Absturz-Log). Mit dem
// Cache wird ein Icon nur bei tatsaechlichem Kategoriewechsel neu gezeichnet.
struct MonoStandbyWeatherWidget {
  lv_obj_t *curTempLbl;
  lv_obj_t *curIconHolder;
  const uint8_t *curIconDrawn;
  lv_obj_t *dayTempLbls[FORECAST_DAYS];
  lv_obj_t *dayLoLbls[FORECAST_DAYS];
  lv_obj_t *dayIconHolders[FORECAST_DAYS];
  const uint8_t *dayIconDrawn[FORECAST_DAYS];
  int colCount;
};
std::vector<MonoStandbyWeatherWidget> s_monoStandbyWeatherWidgets;

lv_obj_t *create_mono_standby_weather(lv_obj_t *parent, JsonObjectConst props) {
  int cols = props["days"] | 4;
  if (cols < 1) cols = 1;
  if (cols > FORECAST_DAYS) cols = FORECAST_DAYS;

  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_layout(box, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  // Nur noch 2 direkte Kinder (curRow, forecastGroup weiter unten) seit
  // Trennlinie und "T"-Spalte weg sind - SPACE_BETWEEN schiebt curRow an den
  // oberen und forecastGroup an den unteren Rand der Box, den kompletten
  // freigewordenen Platz dazwischen (User-Vorgabe: Vorhersage "so weit wie
  // moeglich nach unten").
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  MonoStandbyWeatherWidget w{};
  w.colCount = cols;

  // Kopfzeile: grosse aktuelle Temperatur + Icon als Gruppe zentriert (Flex-
  // Row statt fixem Pixel-Offset fuers Icon - die Temp-Textbreite variiert
  // ("--", "5C", "-12C"), ein fester Offset wuerde bei kurzen/negativen
  // Werten nicht neben dem Text "kleben"). Hoehe LV_SIZE_CONTENT statt fix
  // 18, damit die Zeile mit dem jetzt 24x24 statt 12x12 grossen Icon
  // mitwaechst.
  lv_obj_t *curRow = lv_obj_create(box);
  lv_obj_remove_flag(curRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(curRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(curRow, 0, 0);
  lv_obj_set_style_pad_all(curRow, 0, 0);
  lv_obj_set_style_pad_column(curRow, 4, 0);
  lv_obj_set_width(curRow, lv_pct(100));
  lv_obj_set_height(curRow, LV_SIZE_CONTENT);
  lv_obj_set_layout(curRow, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(curRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(curRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  w.curTempLbl = lv_label_create(curRow);
  lv_label_set_text(w.curTempLbl, "--");
  lv_obj_set_style_text_color(w.curTempLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(w.curTempLbl, &lv_font_unscii_16, 0);

  w.curIconHolder = lv_obj_create(curRow);
  lv_obj_remove_flag(w.curIconHolder, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(w.curIconHolder, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(w.curIconHolder, 0, 0);
  lv_obj_set_style_pad_all(w.curIconHolder, 0, 0);
  lv_obj_set_size(w.curIconHolder, 24, 24);
  lv_obj_add_event_cb(w.curIconHolder, mono_weather_icon_draw_event_cb_2x, LV_EVENT_DRAW_MAIN, nullptr);

  // Vorhersage-Gruppe (Temp-Zeile + Icon-Zeile je Tag) als eigener
  // Flex-Container, damit die Box oben SPACE_BETWEEN nur zwischen curRow und
  // dieser Gruppe verteilt statt die beiden Zeilen darin auseinanderzuziehen.
  lv_obj_t *forecastGroup = lv_obj_create(box);
  lv_obj_remove_flag(forecastGroup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(forecastGroup, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(forecastGroup, 0, 0);
  lv_obj_set_style_pad_all(forecastGroup, 0, 0);
  lv_obj_set_style_pad_row(forecastGroup, 3, 0);
  lv_obj_set_width(forecastGroup, lv_pct(100));
  lv_obj_set_height(forecastGroup, LV_SIZE_CONTENT);
  lv_obj_set_layout(forecastGroup, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(forecastGroup, LV_FLEX_FLOW_COLUMN);

  // Drei Zeilen (Hoechst-Temp / Icon / Tiefst-Temp je Tag). Keine feste
  // Label-Spalte mehr (User-Vorgabe: "T"/"N" weg) - die Tages-Zellen wachsen
  // jetzt per flex_grow(1) ueber die volle Breite, alle drei Zeilen bleiben
  // trotzdem exakt ausgerichtet, weil sie dieselbe Spaltenzahl/-breite
  // verwenden. Icons bewusst bei 12x12 (nicht 2x wie oben) belassen -
  // schafft erst den Hoehenspielraum fuer die dritte Zeile, siehe Kommentar
  // vor create_mono_standby_weather.
  auto makeRow = [&](lv_obj_t *parentRow) {
    lv_obj_t *row = lv_obj_create(parentRow);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 2, 0);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    return row;
  };
  auto makeTempCell = [&](lv_obj_t *row) {
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "--");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(lbl, 1);
    return lbl;
  };
  auto makeIconCell = [&](lv_obj_t *row) {
    lv_obj_t *cell = lv_obj_create(row);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_set_height(cell, 12);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *holder = lv_obj_create(cell);
    lv_obj_remove_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(holder, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(holder, 0, 0);
    lv_obj_set_style_pad_all(holder, 0, 0);
    lv_obj_set_size(holder, 12, 12);
    lv_obj_add_event_cb(holder, mono_weather_icon_draw_event_cb, LV_EVENT_DRAW_MAIN, nullptr);
    return holder;
  };

  lv_obj_t *tempRow = makeRow(forecastGroup);
  for (int i = 0; i < cols; i++) w.dayTempLbls[i] = makeTempCell(tempRow);

  lv_obj_t *iconRow = makeRow(forecastGroup);
  for (int i = 0; i < cols; i++) w.dayIconHolders[i] = makeIconCell(iconRow);

  lv_obj_t *loRow = makeRow(forecastGroup);
  for (int i = 0; i < cols; i++) w.dayLoLbls[i] = makeTempCell(loRow);

  s_monoStandbyWeatherWidgets.push_back(w);
  return box;
}

// CPU-/GPU-Statuszeile: Label ("CPU 62%") + Balken + Temp/Watt-Zeile,
// deckungsgleich mit dem alten hartcodierten display_mono.cpp-Dashboard,
// nur als generische Widget-Fabrik statt fest verdrahteter Positionen.
lv_obj_t *build_mono_stat(lv_obj_t *parent, bool isGpu) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);

  lv_obj_t *lbl = lv_label_create(box);
  lv_label_set_text(lbl, isGpu ? "GPU" : "CPU");
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
  lv_obj_set_pos(lbl, 0, 0);

  lv_obj_t *bar = lv_bar_create(box);
  lv_obj_set_pos(bar, 0, 9);
  lv_obj_set_size(bar, lv_pct(100), 5);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);

  lv_obj_t *statLbl = lv_label_create(box);
  lv_label_set_text(statLbl, "");
  lv_obj_set_style_text_color(statLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(statLbl, &lv_font_unscii_8, 0);
  lv_obj_set_pos(statLbl, 0, 16);

  s_monoStatWidgets.push_back({isGpu, lbl, bar, statLbl});
  return box;
}
lv_obj_t *create_mono_cpu_stat(lv_obj_t *parent, JsonObjectConst) { return build_mono_stat(parent, false); }
lv_obj_t *create_mono_gpu_stat(lv_obj_t *parent, JsonObjectConst) { return build_mono_stat(parent, true); }

// Handgezeichnete 7x7-Pixel-Icons fuer die Mono-Karte unten - kein Icon-Font
// (MDI-Fonts sind wie lv_font_montserrat_* mit --bpp 4/Antialiasing
// generiert, siehe Kommentar oben in diesem Abschnitt), sondern dieselbe
// Technik wie Pac-Man's Ziffern-Pellets (User-Referenz clock_pacman.cpp:
// dort werden Ziffern aus einer 5x7-Bitmaske als reine Vollkreis-Pellets
// gezeichnet, hart pixelweise, keine Kurven/Anti-Aliasing) bzw. wie das
// bereits bestehende 4-Quadrate-"Icon" im Footer (create_footer()
// weiter unten) - ein Bit pro Pixel, jedes gesetzte Bit wird als eigenes
// 1px-Rechteck (radius 0, keine Rundung) gezeichnet. Ein Byte pro Zeile,
// die 7 relevanten Spalten liegen in Bit 6 (links) bis Bit 0 (rechts).
static const uint8_t kMonoIconChip[7] = {
  0b0101010,
  0b1111111,
  0b1000001,
  0b1000001,
  0b1000001,
  0b1111111,
  0b0101010,
};
static const uint8_t kMonoIconFlash[7] = {
  0b0000110, // . . . XX .  (Start oben rechts)
  0b0001100, // . . XX . .
  0b0011000, // . XX . . .
  0b0111100, // . XXXX . .  (Scharfer Knick)
  0b0000110, // . . . XX .
  0b0001100, // . . XX . .
  0b0011000, // . XX . . .  (Ende unten links)
};
static const uint8_t kMonoIconTherm[7] = {
  0b0011000,
  0b0010000,
  0b0011000,
  0b0010000,
  0b0111000,
  0b1111100,
  0b0111000,
};

void draw_mono_pixel_icon(lv_obj_t *parent, const uint8_t rows[7], int16_t x, int16_t y) {
  for (int8_t r = 0; r < 7; r++) {
    for (int8_t c = 0; c < 7; c++) {
      if (!(rows[r] & (1 << (6 - c)))) continue;
      lv_obj_t *px = lv_obj_create(parent);
      lv_obj_remove_flag(px, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_radius(px, 0, 0);
      lv_obj_set_style_border_width(px, 0, 0);
      lv_obj_set_style_bg_color(px, lv_color_white(), 0);
      lv_obj_set_style_bg_opa(px, LV_OPA_COVER, 0);
      lv_obj_set_size(px, 1, 1);
      lv_obj_set_pos(px, x + c, y + r);
    }
  }
}

// Mono-Variante der "flachen" Karte (cpu_card_flat/gpu_card_flat oben,
// User-Referenzfoto: weisser Rahmen, grosse Ueberschrift, Last/Watt/Temp
// als eigene Zeilen), jetzt MIT Icons (siehe draw_mono_pixel_icon() oben -
// User-Vorgabe anhand von clock_pacman.cpp, dass handgezeichnete
// Pixel-Icons anders als Icon-Fonts auf dem 1bpp-Renderer funktionieren)
// und OHNE abgerundete Ecken (lv_obj_set_style_radius 0 - eine Rundung wird
// von LVGL antialiasiert gezeichnet, exakt das Problem, das mono ueberall
// sonst vermeidet). Last-Zeile nutzt fuer CPU UND GPU dasselbe Chip-Icon -
// die Ueberschrift unterscheidet ohnehin schon zwischen beiden, ein
// separates GPU-Icon bei 7x7px waere kaum unterscheidbar.
// props["border"] (Default true): Rahmen ein-/ausschaltbar - User-Vorgabe,
// manche Layouts wollen die Karte nur als unsichtbaren Platzhalter fuer
// Last/Watt/Temp nutzen, ohne die Linien.
// props["big"] (Default false): unscii_16 statt unscii_8 fuer alle 4 Zeilen
// (Titel + 3 Werte) - User-Vorgabe fuer eine 64x64 grosse Karte ("Font
// maximieren"). unscii_16 ist mit 16px Vorschubbreite pro Zeichen genauso
// breit wie hoch (siehe Kommentar bei kBootLogoMono in display_draw.cpp) -
// bei 64px Breite ist neben einem Icon (7px) UND vierstelligem Text
// ("100%") in dieser Groesse kein Platz mehr, deshalb im big-Modus KEINE
// Icons, nur die (dafuer maximal grossen) Zahlen.
lv_obj_t *build_mono_hw_card(lv_obj_t *parent, bool isGpu, JsonObjectConst props) {
  bool border = props["border"] | true;
  bool big = props["big"] | false;
  const lv_font_t *font = big ? &lv_font_unscii_16 : &lv_font_unscii_8;
  int16_t rowH = big ? 16 : 10;

  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_border_width(box, border ? 1 : 0, 0);
  lv_obj_set_style_border_color(box, lv_color_white(), 0);
  lv_obj_set_style_pad_all(box, big ? 0 : 2, 0);

  lv_obj_t *title = lv_label_create(box);
  lv_label_set_text(title, isGpu ? "GPU" : "CPU");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, font, 0);
  lv_obj_set_pos(title, big ? 0 : 2, big ? 0 : 1);

  lv_obj_t *loadLbl = lv_label_create(box);
  lv_label_set_text(loadLbl, "");
  lv_obj_set_style_text_color(loadLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(loadLbl, font, 0);

  lv_obj_t *powerLbl = lv_label_create(box);
  lv_label_set_text(powerLbl, "");
  lv_obj_set_style_text_color(powerLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(powerLbl, font, 0);

  lv_obj_t *tempLbl = lv_label_create(box);
  lv_label_set_text(tempLbl, "");
  lv_obj_set_style_text_color(tempLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(tempLbl, font, 0);

  if (big) {
    // Kein Icon (siehe Kommentar oben) - Zahl beginnt bei x=0, volle Breite.
    lv_obj_set_pos(loadLbl, 0, rowH);
    lv_obj_set_pos(powerLbl, 0, rowH * 2);
    lv_obj_set_pos(tempLbl, 0, rowH * 3);
  } else {
    // Icon links (7px + 2px Abstand), Text ab x=11.
    draw_mono_pixel_icon(box, kMonoIconChip, 2, 12);
    lv_obj_set_pos(loadLbl, 11, 12);
    draw_mono_pixel_icon(box, kMonoIconFlash, 2, 22);
    lv_obj_set_pos(powerLbl, 11, 22);
    draw_mono_pixel_icon(box, kMonoIconTherm, 2, 32);
    lv_obj_set_pos(tempLbl, 11, 32);
  }

  s_monoHwCardWidgets.push_back({isGpu, loadLbl, powerLbl, tempLbl});
  return box;
}
lv_obj_t *create_mono_cpu_card(lv_obj_t *parent, JsonObjectConst props) { return build_mono_hw_card(parent, false, props); }
lv_obj_t *create_mono_gpu_card(lv_obj_t *parent, JsonObjectConst props) { return build_mono_hw_card(parent, true, props); }

// Freistehende 1px-Linie (o.ae. per w/h frei skalierbar) - reiner weisser
// Volltonblock ohne Rand/Rundung, exakt dasselbe Muster wie die frueher in
// display_mono.cpp fest verdrahteten Trennlinien (dort "s_divider1/2"),
// jetzt aber frei platzierbar/dimensionierbar ueber den Layout-Editor.
// w/h bestimmen Ausrichtung: h klein + w gross -> horizontale Linie, w klein
// + h gross -> vertikale Linie - kein eigenes Props-Feld noetig, das
// generische x/y/w/h aus layout_apply() reicht.
lv_obj_t *create_mono_line(lv_obj_t *parent, JsonObjectConst) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_bg_color(o, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

// Freies "Label - Wert": props["key"] bindet gegen einen der 6 festen
// hw_info-Namen ODER (Fallback) gegen dyn_values (siehe shared_state.h/
// hw_data.cpp) - die PC-App liefert mehr Sensorwerte als nur die 6
// cpu_/gpu_-Grundfelder, dieses Widget macht sie ohne Firmware-Aenderung
// nutzbar. props["label"] ist der Anzeigetext, props["unit"] ein freies
// Suffix (z.B. "%", "C", "W", "MB").
lv_obj_t *create_mono_label_value(lv_obj_t *parent, JsonObjectConst props) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *labelLbl = lv_label_create(row);
  lv_obj_set_style_text_color(labelLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(labelLbl, &lv_font_unscii_8, 0);
  lv_label_set_text(labelLbl, props["label"] | "");

  lv_obj_t *valLbl = lv_label_create(row);
  lv_obj_set_style_text_color(valLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(valLbl, &lv_font_unscii_8, 0);
  lv_label_set_text(valLbl, "--");

  const char *key  = props["key"]  | "";
  const char *unit = props["unit"] | "";
  s_monoLabelValueWidgets.push_back({valLbl, String(key), String(unit)});
  return row;
}

// --- Dashboard/Einstellungen als echte Tabs (Footer-Icons) ---
//
// Settings war zuerst ein Vollbild-Overlay ueber dem Dashboard - User-Vorgabe
// war stattdessen ein echter Tab-Wechsel: Footer bleibt IMMER sichtbar/
// bedienbar, das jeweils aktive Icon bekommt einen Aktivitaets-Rahmen (schon
// vorher in addPill() vorbereitet, jetzt tatsaechlich dynamisch statt fest
// verdrahtet), und es gibt keinen "Schliessen"-Knopf mehr - das Dashboard-
// Icon uebernimmt diese Rolle. Statt eines Vollbild-Backdrops wird das
// Dashboard-Grid (alle Widgets AUSSER dem Footer, siehe
// s_dashboardContentWidgets/layout_apply()) beim Tab-Wechsel einfach
// aus-/eingeblendet.
enum class ActiveTab { kDashboard, kSettings };
ActiveTab s_activeTab = ActiveTab::kDashboard;
lv_obj_t *s_settingsContent = nullptr;
lv_obj_t *s_footerObj = nullptr;
lv_obj_t *s_dashboardPill = nullptr;
lv_obj_t *s_settingsPill = nullptr;
std::vector<lv_obj_t *> s_dashboardContentWidgets;

// Nur bei displayIsMono() befuellt (siehe layout_apply()) - Widgets aus dem
// zweiten, im Editor separat editierbaren Standby-Set. Werden beim Bau
// erst einmal versteckt (LV_OBJ_FLAG_HIDDEN) und von
// refresh_mono_standby_visibility() gegenlaeufig zu
// s_dashboardContentWidgets ein-/ausgeblendet - dasselbe "beides bauen, nur
// Sichtbarkeit umschalten"-Muster wie beim Dashboard/Settings-Tab oben,
// nur mit Dashboard/Standby statt Dashboard/Settings.
std::vector<lv_obj_t *> s_standbyWidgets;

// Dasselbe Zustands-Dreigespann wie s_standby/s_standbyAuto/
// s_manualToggleRequested in display_round.cpp (dort ausfuehrlich
// begruendet: Auto-Standby darf ein manuelles Umschalten nicht
// ueberschreiben, ein manuelles Umschalten muss aber den naechsten
// Auto-Zyklus wieder uebernehmen koennen) - hier vor layout_apply()
// deklariert (nicht erst bei refresh_mono_standby_visibility() weiter
// unten), da layout_apply() sie bei jedem Rebuild zuruecksetzen muss.
bool s_monoStandby = false;
bool s_monoStandbyAuto = false;
volatile bool s_monoManualToggleRequested = false;

void set_pill_active(lv_obj_t *pill, bool active) {
  lv_obj_set_style_border_width(pill, active ? 2 : 1, 0);
  lv_obj_set_style_border_color(pill, lv_color_hex(active ? 0x38BDF8 : 0x334155), 0);
  lv_obj_set_style_border_opa(pill, active ? LV_OPA_COVER : LV_OPA_60, 0);
}

void hide_settings_content(void) {
  if (s_settingsContent) {
    lv_obj_delete(s_settingsContent);
    s_settingsContent = nullptr;
  }
}

void set_dashboard_content_visible(bool visible) {
  for (lv_obj_t *w : s_dashboardContentWidgets) {
    if (visible) lv_obj_remove_flag(w, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
  }
}

void build_settings_content(void); // Vorwaertsdeklaration, siehe unten (braucht app_config/WiFi etc.)

void show_dashboard_tab(lv_event_t *) {
  if (s_activeTab == ActiveTab::kDashboard) return;
  s_activeTab = ActiveTab::kDashboard;
  hide_settings_content();
  set_dashboard_content_visible(true);
  if (s_dashboardPill) set_pill_active(s_dashboardPill, true);
  if (s_settingsPill) set_pill_active(s_settingsPill, false);
}

void show_settings_tab(lv_event_t *) {
  if (s_activeTab == ActiveTab::kSettings) return;
  s_activeTab = ActiveTab::kSettings;
  set_dashboard_content_visible(false);
  build_settings_content();
  if (s_dashboardPill) set_pill_active(s_dashboardPill, false);
  if (s_settingsPill) set_pill_active(s_settingsPill, true);
}

void reboot_to_ap_clicked(lv_event_t *) {
  wifi_provision_forget();
  delay(200);
  ESP.restart();
}

#if defined(BOARD_CYD_2432S028R)
// --- Touch-Kalibrierung (nur CYD/XPT2046) ---
//
// Ecken-Antippen "nach Auge" war zweimal in Folge zu ungenau (resistive
// Touchscreens reagieren nahe am physischen Rand oft unzuverlaessig) - dieser
// Kalibrier-Bildschirm nutzt stattdessen zwei klar definierte, deutlich in
// den Bildschirm eingerueckte Zielpunkte (25%/25% und 75%/75%) statt der
// Ecken. Volle 0..Breite/Hoehe-Grenzwerte werden per linearer Extrapolation
// daraus berechnet: die 25%-75%-Spanne ist die Haelfte der vollen Spanne,
// je zur Haelfte ueber jeden der beiden Punkte hinaus extrapoliert.
struct TouchCalPoint { float xPct, yPct; };
constexpr TouchCalPoint kTouchCalPoints[2] = {{0.25f, 0.25f}, {0.75f, 0.75f}};

lv_obj_t *s_calOverlay = nullptr;
lv_obj_t *s_calCrosshair = nullptr;
lv_obj_t *s_calLabel = nullptr;
int s_calStep = 0;
int16_t s_calRawX[2];
int16_t s_calRawY[2];

void show_touch_cal_point(int step) {
  int16_t targetX = (int16_t)(kTouchCalPoints[step].xPct * displayWidthPx());
  int16_t targetY = (int16_t)(kTouchCalPoints[step].yPct * displayHeightPx());
  lv_obj_set_pos(s_calCrosshair, targetX - 14, targetY - 14);
  char buf[32];
  snprintf(buf, sizeof(buf), "Punkt %d von 2 antippen", step + 1);
  lv_label_set_text(s_calLabel, buf);
}

// Reagiert auf JEDEN Tastendruck irgendwo auf dem Vollbild-Overlay statt nur
// exakt auf den Kreis - LVGLs Treffertest fuer den Kreis wuerde selbst schon
// eine (noch unkalibrierte!) Touch-Position voraussetzen, genau das soll
// hier ja erst ermittelt werden. touchCtrl.touched()/getPoint() werden
// bewusst ein zweites Mal aufgerufen (nicht ueber data->point aus dem
// Indev) - liefert dieselbe physische Beruehrung, SPI-Reads sind
// Mikrosekunden schnell gegenueber der Dauer eines menschlichen Antippens.
void touch_cal_pressed(lv_event_t *) {
  if (!touchCtrl.touched()) return; // Sicherheitscheck, sollte bei PRESSED nie eintreten
  TS_Point p = touchCtrl.getPoint();
  s_calRawX[s_calStep] = p.x;
  s_calRawY[s_calStep] = p.y;
  s_calStep++;

  if (s_calStep < 2) {
    show_touch_cal_point(s_calStep);
    return;
  }

  int16_t xLo = (s_calRawX[0] < s_calRawX[1]) ? s_calRawX[0] : s_calRawX[1];
  int16_t xHi = (s_calRawX[0] < s_calRawX[1]) ? s_calRawX[1] : s_calRawX[0];
  int16_t xSpan = xHi - xLo;
  int16_t yLo = (s_calRawY[0] < s_calRawY[1]) ? s_calRawY[0] : s_calRawY[1];
  int16_t yHi = (s_calRawY[0] < s_calRawY[1]) ? s_calRawY[1] : s_calRawY[0];
  int16_t ySpan = yHi - yLo;

  app_config.touch_x_min = xLo - xSpan / 2;
  app_config.touch_x_max = xHi + xSpan / 2;
  app_config.touch_y_min = yLo - ySpan / 2;
  app_config.touch_y_max = yHi + ySpan / 2;
  app_config.touch_calibrated = true;
  config_store_save(&app_config);
  applyTouchCalibration(app_config.touch_x_min, app_config.touch_x_max,
                         app_config.touch_y_min, app_config.touch_y_max);

  if (s_calOverlay) {
    lv_obj_delete(s_calOverlay);
    s_calOverlay = nullptr;
  }
}

void start_touch_calibration(lv_event_t *) {
  if (s_calOverlay) return; // schon offen
  // Kalibrier-Overlay ist ein eigenstaendiges, kurzlebiges Vollbild-Overlay
  // (echt modal, kein Tab) - deckt den Settings-Tab-Inhalt dahinter einfach
  // ab, es gibt daher nichts mehr zu schliessen wie frueher beim
  // Settings-Popup.

  s_calStep = 0;
  s_calOverlay = lv_obj_create(lv_scr_act());
  lv_obj_remove_flag(s_calOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_calOverlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(s_calOverlay, 0, 0);
  lv_obj_set_style_bg_color(s_calOverlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_calOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_calOverlay, 0, 0);
  lv_obj_set_style_radius(s_calOverlay, 0, 0);
  lv_obj_add_event_cb(s_calOverlay, touch_cal_pressed, LV_EVENT_PRESSED, nullptr);

  s_calLabel = lv_label_create(s_calOverlay);
  lv_obj_set_style_text_color(s_calLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(s_calLabel, &lv_font_montserrat_16, 0);
  lv_obj_align(s_calLabel, LV_ALIGN_TOP_MID, 0, 16);

  // Kreis statt Punkt, unclickable - ein Tastendruck DARAUF soll genauso
  // zaehlen wie daneben, siehe touch_cal_pressed() oben, das Overlay
  // dahinter faengt in jedem Fall alles ab.
  s_calCrosshair = lv_obj_create(s_calOverlay);
  lv_obj_remove_flag(s_calCrosshair, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_calCrosshair, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_calCrosshair, 28, 28);
  lv_obj_set_style_radius(s_calCrosshair, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_calCrosshair, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(s_calCrosshair, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_border_width(s_calCrosshair, 3, 0);

  show_touch_cal_point(0);
}
#endif // BOARD_CYD_2432S028R

void build_settings_content(void) {
  if (s_settingsContent) return; // schon aufgebaut

  // Kein Vollbild-Backdrop mehr noetig: das Dashboard-Grid ist beim Aufruf
  // bereits ausgeblendet (siehe show_settings_tab()), der Bildschirm
  // dahinter ist ohnehin schwarz. Hoehe wird bewusst per lv_obj_get_y() vom
  // bereits existierenden Footer abgeleitet statt geschaetzt - der Footer
  // ist zu diesem Zeitpunkt laengst gebaut (layout_apply() erzeugt ihn wie
  // jedes andere Widget aus der JSON-Liste), seine y-Position ist also der
  // exakte verfuegbare Platz, nicht nur eine Annahme.
  int16_t topMargin = 8;
  int16_t footerY = s_footerObj ? lv_obj_get_y(s_footerObj) : displayHeightPx();
  int16_t availH = footerY - topMargin - 6;
  if (availH < 40) availH = 40; // Notbremse fuer sehr kleine/ungewoehnliche Layouts

  card_container_t c = create_card(lv_scr_act(), displayWidthPx() - 20, availH);
  s_settingsContent = c.card;
  lv_obj_align(s_settingsContent, LV_ALIGN_TOP_MID, 0, topMargin);
  // Anders als die uebrigen Karten im Projekt MUSS dieser Inhalt scrollbar
  // sein: er ist auf der CYD hoeher als der verfuegbare Platz ueber dem
  // Footer (Titel+3 Zeilen+bis zu 2 Buttons > verfuegbare ~180px) -
  // create_card() deaktiviert Scrollen standardmaessig, hier bewusst wieder
  // aktiviert.
  lv_obj_add_flag(s_settingsContent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_settingsContent, LV_DIR_VER);
  lv_obj_set_style_pad_all(c.card, 10, 0);
  lv_obj_set_style_pad_row(c.card, 6, 0);

  lv_obj_t *title = lv_label_create(c.card);
  lv_label_set_text(title, "Einstellungen");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

  auto addRow = [&](const char *label, const String &value) {
    lv_obj_t *row = lv_obj_create(c.card);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, value.c_str());
    lv_obj_set_style_text_color(v, lv_color_hex(0xE0E8F2), 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
  };

  addRow("IP-Adresse", WiFi.localIP().toString());

  // hw_source bestimmt, welcher der beiden Verbindungswege ueberhaupt aktiv
  // ist (siehe CLAUDE.md/main.cpp) - nur den relevanten anzeigen statt
  // beide, der andere ist per Design immer getrennt.
  bool connected = (app_config.hw_source == HW_SOURCE_MQTT) ? mqtt_connected : serial_connected;
  addRow(app_config.hw_source == HW_SOURCE_MQTT ? "MQTT" : "USB",
         connected ? "Verbunden" : "Getrennt");

  char heapBuf[16];
  snprintf(heapBuf, sizeof(heapBuf), "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  addRow("Freier Speicher", heapBuf);

  // Font/Textfarbe/Hoehe explizit gesetzt statt sich auf das LVGL-Theme zu
  // verlassen (wie im Rest der Datei ueberall sonst schon Konvention) - ohne
  // das sah der Button-Text kaputt/undefiniert aus. "Schliessen" statt
  // "Schließen": keiner der eingebauten lv_font_montserrat_*-Fonts enthaelt
  // ein Glyph fuer "ß" (nur ASCII 0x20-0x7E + Gradzeichen + Icon-Symbole,
  // siehe deren cmap) - wuerde sonst stillschweigend wegfallen statt
  // dargestellt zu werden.
#if defined(BOARD_CYD_2432S028R)
  lv_obj_t *calBtn = lv_button_create(c.card);
  lv_obj_set_width(calBtn, lv_pct(100));
  lv_obj_set_height(calBtn, 32);
  lv_obj_set_style_bg_color(calBtn, lv_color_hex(0x334155), 0);
  lv_obj_t *calBtnLbl = lv_label_create(calBtn);
  lv_label_set_text(calBtnLbl, "Touch kalibrieren");
  lv_obj_set_style_text_color(calBtnLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(calBtnLbl, &lv_font_montserrat_14, 0);
  lv_obj_center(calBtnLbl);
  lv_obj_add_event_cb(calBtn, start_touch_calibration, LV_EVENT_CLICKED, nullptr);
#endif

  lv_obj_t *apBtn = lv_button_create(c.card);
  lv_obj_set_width(apBtn, lv_pct(100));
  lv_obj_set_height(apBtn, 32);
  lv_obj_set_style_bg_color(apBtn, lv_color_hex(0xDC2626), 0);
  lv_obj_t *apBtnLbl = lv_label_create(apBtn);
  lv_label_set_text(apBtnLbl, "In AP-Modus neustarten");
  lv_obj_set_style_text_color(apBtnLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(apBtnLbl, &lv_font_montserrat_14, 0);
  // Breite/Ausrichtung explizit statt LV_SIZE_CONTENT zu vertrauen - der
  // Text ist mit die laengste Zeichenkette im gesamten UI, ohne feste
  // Breite/Umbruchsteuerung wirkte er auf der CYD unsauber (User-Feedback).
  lv_obj_set_width(apBtnLbl, lv_pct(100));
  lv_label_set_long_mode(apBtnLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(apBtnLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(apBtnLbl);
  lv_obj_add_event_cb(apBtn, reboot_to_ap_clicked, LV_EVENT_CLICKED, nullptr);
  // Kein "Schliessen"-Button mehr - Zurueck zum Dashboard laeuft jetzt ueber
  // den Footer (Dashboard-Tab links), das ist im Tab-Modell die natuerliche
  // Navigation statt eines redundanten zweiten Wegs.
}

// Footer-Navigationsleiste: 2 Icons als echte Tabs (User-Vorgabe) - Dashboard
// (Grid, links) und Einstellungen (Zahnrad, rechts). Das vormals mittlere,
// rein dekorative Chart-Icon (LV_SYMBOL_LIST, nie klickbar) ist komplett
// entfernt. Beide Pills tragen jetzt einen Klick-Handler und einen
// Aktivitaets-Rahmen, der dynamisch dem aktuellen Tab folgt (siehe
// set_pill_active()/show_dashboard_tab()/show_settings_tab() oben) statt wie
// zuvor fest auf dem (jetzt geloeschten) Chart-Icon zu kleben.
lv_obj_t *create_footer(lv_obj_t *parent, JsonObjectConst) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Footer-Hoehe skaliert bereits proportional mit dem Board (siehe
  // layout_default_json()), die Icons DARIN blieben aber bisher fest auf
  // CYD-Groesse verdrahtet - auf dem JC8048W550 (80px hoher Footer statt 40)
  // wirkten sie dadurch winzig/verloren (User-Feedback). is_big_display()-
  // Skalierung wie schon bei den Karten-Icons.
  const int16_t pillSize   = is_big_display() ? 54 : 36;
  const int16_t pillRadius = is_big_display() ? 14 : 10;
  const int16_t gridBoxSize = is_big_display() ? 24 : 16;
  const int16_t gridSqSize  = is_big_display() ? 9 : 6;
  const int16_t gridGap     = is_big_display() ? 5 : 3;
  // mdi_24 statt mdi_28: mdi_28.c verweist intern auf lv_font_montserrat_28
  // als Fallback-Font, der in lv_conf.h nicht aktiviert ist - auf der CYD
  // faellt das nicht auf (is_big_display() ist dort ueber LCD_WIDTH/HEIGHT-
  // Makros compile-time-konstant false, der Compiler wirft die &mdi_28-Seite
  // komplett weg), auf BOARD_GENERIC (esp32/esp32s3/esp32c3, Displaygroesse
  // erst zur Laufzeit aus NVS bekannt) bleiben beide Zweige erreichbar und
  // der Linker bricht mit "undefined reference to lv_font_montserrat_28" ab.
  // mdi_24 ist bereits an anderer Stelle (Karten-Icons) erfolgreich verlinkt.
  const lv_font_t *footerIconFont = is_big_display() ? &mdi_24 : &mdi_20;

  auto addPill = [&](bool active) -> lv_obj_t * {
    lv_obj_t *pill = lv_obj_create(row);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pill, pillSize, pillSize);
    lv_obj_set_style_radius(pill, pillRadius, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
    set_pill_active(pill, active);
    lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return pill;
  };

  // Dashboard (Grid) LINKS, Einstellungen (Zahnrad) RECHTS - explizite
  // User-Vorgabe. Aktiver Tab beim (Neu-)Aufbau ist immer Dashboard
  // (s_activeTab wird in layout_apply() bei jedem Rebuild zurueckgesetzt).
  s_dashboardPill = addPill(s_activeTab == ActiveTab::kDashboard);
  lv_obj_t *gridBox = lv_obj_create(s_dashboardPill);
  lv_obj_remove_flag(gridBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(gridBox, gridBoxSize, gridBoxSize);
  lv_obj_set_style_bg_opa(gridBox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(gridBox, 0, 0);
  lv_obj_set_style_pad_all(gridBox, 0, 0);
  lv_obj_set_style_pad_gap(gridBox, gridGap, 0);
  lv_obj_set_layout(gridBox, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(gridBox, LV_FLEX_FLOW_ROW_WRAP);
  for (int i = 0; i < 4; i++) {
    lv_obj_t *sq = lv_obj_create(gridBox);
    lv_obj_remove_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sq, gridSqSize, gridSqSize);
    lv_obj_set_style_radius(sq, 1, 0);
    lv_obj_set_style_bg_color(sq, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_border_width(sq, 0, 0);
  }
  lv_obj_add_event_cb(s_dashboardPill, show_dashboard_tab, LV_EVENT_CLICKED, nullptr);

  s_settingsPill = addPill(s_activeTab == ActiveTab::kSettings);
  lv_obj_t *settingsIcon = lv_label_create(s_settingsPill);
  lv_label_set_text(settingsIcon, MDI_COG);
  lv_obj_set_style_text_color(settingsIcon, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(settingsIcon, footerIconFont, 0);
  lv_obj_add_event_cb(s_settingsPill, show_settings_tab, LV_EVENT_CLICKED, nullptr);

  s_footerObj = row;
  return row;
}

struct WidgetTypeEntry {
  const char *type;
  lv_obj_t *(*create)(lv_obj_t *, JsonObjectConst);
};

const WidgetTypeEntry kWidgetTypes[] = {
    {"datetime_card", create_datetime_card},
    {"cpu_card", create_cpu_card},
    {"gpu_card", create_gpu_card},
    {"cpu_card_flat", create_cpu_card_flat},
    {"gpu_card_flat", create_gpu_card_flat},
    {"weather_card", create_weather_card},
    {"chart_card", create_chart_card},
    {"footer", create_footer},
    {"mono_clock", create_mono_clock},
    {"mono_date", create_mono_date},
    {"mono_weekday", create_mono_weekday},
    {"mono_weather_temp", create_mono_weather_temp},
    {"mono_forecast", create_mono_forecast},
    {"mono_standby_weather", create_mono_standby_weather},
    {"mono_cpu_stat", create_mono_cpu_stat},
    {"mono_gpu_stat", create_mono_gpu_stat},
    {"mono_cpu_card", create_mono_cpu_card},
    {"mono_gpu_card", create_mono_gpu_card},
    {"mono_line", create_mono_line},
    {"mono_label_value", create_mono_label_value},
};

enum class LayoutCmdType { kApplyFull };

struct LayoutCmd {
  LayoutCmdType type;
  String *payload; // heap-alloziert, vom Consumer (layout_queue_process) freigegeben
};

QueueHandle_t s_layoutQueue = nullptr;

} // namespace

void layout_apply(JsonArrayConst widgets, JsonArrayConst standbyWidgets) {
  lv_obj_t *scr = lv_scr_act();
  // lv_obj_clean() macht alle bisherigen s_widgets-Handles sofort ungueltig -
  // Map/Bindings deshalb VOR dem Neuaufbau leeren, nicht danach.
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  s_widgets.clear();
  s_datetimeWidgets.clear();
  s_hwCardWidgets.clear();
  s_hwCardFlatWidgets.clear();
  s_weatherCardWidgets.clear();
  s_chartCardWidgets.clear();
  s_monoClockWidgets.clear();
  s_monoDateWidgets.clear();
  s_monoWeekdayWidgets.clear();
  s_monoWeatherWidgets.clear();
  s_monoForecastWidgets.clear();
  s_monoStandbyWeatherWidgets.clear();
  s_monoStatWidgets.clear();
  s_monoHwCardWidgets.clear();
  s_monoLabelValueWidgets.clear();
  // Der Settings-Tab-Inhalt sowie Footer/Pill-Handles sind Kinder von scr -
  // lv_obj_clean() hat sie (falls vorhanden) gerade mit geloescht, die
  // Zeiger waeren sonst dangling. Tab-Zustand faellt bei jedem Rebuild
  // bewusst auf "Dashboard" zurueck (kein Grund, ueber einen Layout-Wechsel
  // hinweg im Settings-Tab zu bleiben).
  s_settingsContent = nullptr;
  s_footerObj = nullptr;
  s_dashboardPill = nullptr;
  s_settingsPill = nullptr;
  s_dashboardContentWidgets.clear();
  s_standbyWidgets.clear();
  // Standby-Zustand faellt bei jedem Rebuild zurueck auf "Dashboard sichtbar"
  // (analog zu s_standby=s_standbyAuto=false in displayRoundBuild()) - ohne
  // das bliebe ein vor dem Reset aktiver Standby (Timeout oder manuell per
  // BOOT-Taste) bestehen und der neu gebaute Dashboard-Inhalt waere sofort
  // wieder versteckt, obwohl "Auf Grundlayout zuruecksetzen" gerade erst
  // ausgeloest wurde.
  s_monoStandby = false;
  s_monoStandbyAuto = false;
  s_monoManualToggleRequested = false;
  s_activeTab = ActiveTab::kDashboard;

  for (JsonObjectConst w : widgets) {
    const char *type = w["type"] | "";
    lv_obj_t *o = nullptr;
    for (auto &e : kWidgetTypes) {
      if (strcmp(e.type, type) == 0) {
        o = e.create(scr, w["props"]);
        break;
      }
    }
    if (!o) {
      log_w("display_layout: unbekannter Widget-Typ '%s'", type);
      continue;
    }
    lv_obj_set_pos(o, w["x"] | 0, w["y"] | 0);
    lv_obj_set_size(o, w["w"] | 60, w["h"] | 30);

    // Alles ausser dem Footer gehoert zum Dashboard-Tab-Inhalt und wird beim
    // Tab-Wechsel ein-/ausgeblendet (siehe set_dashboard_content_visible());
    // der Footer selbst bleibt IMMER sichtbar, das ist der ganze Sinn der
    // Tab-Umstellung.
    if (strcmp(type, "footer") != 0) s_dashboardContentWidgets.push_back(o);

    const char *id = w["id"] | "";
    if (id[0]) s_widgets[id] = o;
  }

  // Zweites Widget-Set, nur bei Mono ueberhaupt befuellt (siehe
  // layout_default_json() - auf allen anderen Aufloesungen ist
  // standbyWidgets ein leeres Array, diese Schleife also ein No-Op).
  // Anfangs immer versteckt - refresh_mono_standby_visibility() entscheidet
  // pro Refresh-Zyklus, welches der beiden Sets sichtbar ist.
  for (JsonObjectConst w : standbyWidgets) {
    const char *type = w["type"] | "";
    lv_obj_t *o = nullptr;
    for (auto &e : kWidgetTypes) {
      if (strcmp(e.type, type) == 0) {
        o = e.create(scr, w["props"]);
        break;
      }
    }
    if (!o) {
      log_w("display_layout: unbekannter Standby-Widget-Typ '%s'", type);
      continue;
    }
    lv_obj_set_pos(o, w["x"] | 0, w["y"] | 0);
    lv_obj_set_size(o, w["w"] | 60, w["h"] | 30);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    s_standbyWidgets.push_back(o);

    const char *id = w["id"] | "";
    if (id[0]) s_widgets[id] = o;
  }
}

void layout_queue_begin(void) {
  s_layoutQueue = xQueueCreate(4, sizeof(LayoutCmd));
}

bool layout_queue_push_apply(const String &json) {
  if (!s_layoutQueue) return false;
  LayoutCmd cmd{LayoutCmdType::kApplyFull, new String(json)};
  if (xQueueSend(s_layoutQueue, &cmd, 0) != pdTRUE) {
    delete cmd.payload; // Queue voll - verwerfen statt Speicher zu lecken
    return false;
  }
  return true;
}

void layout_queue_process(void) {
  if (!s_layoutQueue) return;
  LayoutCmd cmd;
  while (xQueueReceive(s_layoutQueue, &cmd, 0) == pdTRUE) {
    JsonDocument doc;
    if (deserializeJson(doc, *cmd.payload) == DeserializationError::Ok) {
      layout_apply(doc["widgets"].as<JsonArrayConst>(), doc["standby_widgets"].as<JsonArrayConst>());
    }
    delete cmd.payload;
  }
}

static void refresh_datetime_widgets(void) {
  if (s_datetimeWidgets.empty()) return;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char dateBuf[16], mainBuf[8], secBuf[8];
  strftime(dateBuf, sizeof(dateBuf), "%d.%m.%Y", &t);
  strftime(mainBuf, sizeof(mainBuf), "%H:%M", &t);
  strftime(secBuf, sizeof(secBuf), ":%S", &t);

  hw_data_lock();
  // 5s statt app_config.standby_timeout_s: das ist der in CLAUDE.md
  // festgeschriebene ESP-seitige "Anzeige ausgrauen"-Timeout, ein eigener,
  // vom (nur fuer das runde Display relevanten) Standby-Timeout
  // unabhaengiger Wert.
  bool live = hw_info.ever_received && (now_ms() - hw_info.last_update_ms) < 5000;
  hw_data_unlock();

  for (auto &w : s_datetimeWidgets) {
    lv_label_set_text(w.dateLbl, dateBuf);
    lv_label_set_text(w.timeMainLbl, mainBuf);
    lv_label_set_text(w.timeSecLbl, secBuf);
    lv_obj_set_style_bg_color(w.dot, lv_color_hex(live ? 0x00ff00 : 0x64748B), 0);
  }
}

// lv_bar_set_value(..., LV_ANIM_ON) allein animiert nicht spuerbar weich
// (dieselbe Erfahrung, die schon beim runden Display zum eigenen
// animate_arc_value() fuehrte, siehe display_round.cpp) - deshalb dieselbe
// manuelle lv_anim_t-Technik hier fuer den Balken. lv_bar_set_value() hat
// drei Parameter (Wert + Anim-Flag), passt also nicht direkt als
// lv_anim_exec_xcb_t (erwartet exakt (void*, int32_t)) - set_bar_value_raw()
// ist der noetige Zwischenschritt, mit LV_ANIM_OFF weil die Animation
// bereits ueber lv_anim_t laeuft, nicht zusaetzlich ueber den Balken selbst.
static void set_bar_value_raw(void *bar, int32_t value) {
  lv_bar_set_value((lv_obj_t *)bar, value, LV_ANIM_OFF);
}
static void animate_bar_value(lv_obj_t *bar, int32_t target) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, bar);
  lv_anim_set_exec_cb(&a, set_bar_value_raw);
  lv_anim_set_values(&a, lv_bar_get_value(bar), target);
  lv_anim_set_duration(&a, 300);
  lv_anim_start(&a);
}

static void refresh_hw_card_widgets(void) {
  if (s_hwCardWidgets.empty()) return;
  hw_data_lock();
  float cpuLoad = hw_info.cpu_load, cpuTemp = hw_info.cpu_temp, cpuPower = hw_info.cpu_power;
  float gpuLoad = hw_info.gpu_load, gpuTemp = hw_info.gpu_temp, gpuPower = hw_info.gpu_power;
  hw_data_unlock();

  char buf[16];
  for (auto &w : s_hwCardWidgets) {
    float load = w.isGpu ? gpuLoad : cpuLoad;
    float temp = w.isGpu ? gpuTemp : cpuTemp;
    float power = w.isGpu ? gpuPower : cpuPower;

    // Farbe hier statt nur einmal bei der Erstellung setzen: eine
    // Aenderung von cpu_arc_color/gpu_arc_color im WebUI aktualisiert
    // app_config sofort (siehe web_portal.cpp::handlePostConfig()), ohne
    // dieses Refresh haette das Icon aber erst nach einem manuellen Neustart
    // die neue Farbe gezeigt - Balken faerbt bewusst NICHT mit (fixer
    // gruen->rot-Verlauf nach Last, siehe build_hw_card()).
    lv_obj_set_style_text_color(w.iconLbl, lv_color_hex(w.isGpu ? app_config.gpu_arc_color : app_config.cpu_arc_color), 0);

    int loadPct = (int)(load + 0.5f);
    snprintf(buf, sizeof(buf), "%d%%", loadPct);
    lv_label_set_text(w.valLbl, buf);
    animate_bar_value(w.bar, loadPct);

    int tempR = (int)(temp + 0.5f);
    snprintf(buf, sizeof(buf), "%d\xc2\xb0" "C", tempR);
    lv_label_set_text(w.tempLbl, buf);

    int powerR = (int)(power + 0.5f);
    snprintf(buf, sizeof(buf), "%dW", powerR);
    lv_label_set_text(w.powerLbl, buf);
  }
}

static void refresh_hw_card_flat_widgets(void) {
  if (s_hwCardFlatWidgets.empty()) return;
  hw_data_lock();
  float cpuLoad = hw_info.cpu_load, cpuTemp = hw_info.cpu_temp, cpuPower = hw_info.cpu_power;
  float gpuLoad = hw_info.gpu_load, gpuTemp = hw_info.gpu_temp, gpuPower = hw_info.gpu_power;
  hw_data_unlock();

  char buf[16];
  for (auto &w : s_hwCardFlatWidgets) {
    float load  = w.isGpu ? gpuLoad  : cpuLoad;
    float temp  = w.isGpu ? gpuTemp  : cpuTemp;
    float power = w.isGpu ? gpuPower : cpuPower;

    snprintf(buf, sizeof(buf), "%d%%", (int)(load + 0.5f));
    lv_label_set_text(w.loadLbl, buf);
    snprintf(buf, sizeof(buf), "%dW", (int)(power + 0.5f));
    lv_label_set_text(w.powerLbl, buf);
    snprintf(buf, sizeof(buf), "%d\xc2\xb0" "C", (int)(temp + 0.5f));
    lv_label_set_text(w.tempLbl, buf);
  }
}

static void refresh_weather_card_widgets(void) {
  if (s_weatherCardWidgets.empty() || !weather_info.valid) return;

  uint32_t color = weather_icon_color(weather_info.icon);
  const char *icon = weather_icon_mdi(weather_info.icon);
  int tempR = (int)(weather_info.temp_c + (weather_info.temp_c >= 0 ? 0.5f : -0.5f));
  int windR = (int)(weather_info.wind_speed + 0.5f);
  int rainTenths = (int)(weather_info.rain_1h * 10 + 0.5f);

  char buf[24];
  for (auto &w : s_weatherCardWidgets) {
    lv_obj_set_style_text_color(w.mainIconLbl, lv_color_hex(color), 0);
    lv_label_set_text(w.mainIconLbl, icon);
    snprintf(buf, sizeof(buf), "%d\xc2\xb0" "C", tempR);
    lv_label_set_text(w.mainValLbl, buf);

    snprintf(buf, sizeof(buf), "%dkm/h %s", windR, weather_wind_compass(weather_info.wind_deg));
    lv_label_set_text(w.windValLbl, buf);

    snprintf(buf, sizeof(buf), "%d%%", weather_info.humidity);
    lv_label_set_text(w.humidityValLbl, buf);

    snprintf(buf, sizeof(buf), "%d.%dmm", rainTenths / 10, rainTenths % 10);
    lv_label_set_text(w.rainValLbl, buf);
  }
}

// history_push() aktualisiert cpu_history/gpu_history hoechstens 1x/Sek
// (siehe hw_data.cpp) - dieses Refresh laeuft trotzdem im generischen
// 500ms-Takt wie alle anderen refresh_*-Funktionen (Konsistenz, kein
// Sonderfall), zeichnet dabei im Schnitt jeden zweiten Aufruf identische
// Werte neu. Bei 60 Punkten x maximal 2 Serien ist das auf einem ESP32
// vernachlaessigbar, eine eigene Drossel waere verfrueht.
static void refresh_chart_card_widgets(void) {
  if (s_chartCardWidgets.empty()) return;
  hw_data_lock();
  for (auto &w : s_chartCardWidgets) {
    const history_t *h = w.isGpu ? &gpu_history : &cpu_history;
    for (int i = 0; i < HIST_LEN; i++) {
      switch (w.metric) {
        case ChartMetric::kLoad:
          lv_chart_set_value_by_id(w.chart, w.series1, i, (int32_t)(h->load[i] + 0.5f));
          break;
        case ChartMetric::kTemp:
          lv_chart_set_value_by_id(w.chart, w.series1, i, (int32_t)(h->temp[i] + 0.5f));
          break;
        case ChartMetric::kPower:
          lv_chart_set_value_by_id(w.chart, w.series1, i, (int32_t)(h->power[i] + 0.5f));
          break;
        case ChartMetric::kTempPower:
          lv_chart_set_value_by_id(w.chart, w.series1, i, (int32_t)(h->temp[i] + 0.5f));
          lv_chart_set_value_by_id(w.chart, w.series2, i, (int32_t)(h->power[i] + 0.5f));
          break;
      }
    }
    lv_chart_refresh(w.chart);
  }
  hw_data_unlock();
}

static void refresh_mono_clock_widgets(void) {
  if (s_monoClockWidgets.empty() && s_monoDateWidgets.empty() &&
      s_monoWeekdayWidgets.empty() && s_monoWeatherWidgets.empty()) {
    return;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[24];

  if (!s_monoClockWidgets.empty()) {
    strftime(buf, sizeof(buf), "%H:%M", &t);
    for (auto &w : s_monoClockWidgets) lv_label_set_text(w.lbl, buf);
  }
  if (!s_monoDateWidgets.empty()) {
    strftime(buf, sizeof(buf), "%d.%m.%Y", &t);
    for (auto &w : s_monoDateWidgets) lv_label_set_text(w.lbl, buf);
  }
  if (!s_monoWeekdayWidgets.empty()) {
    // %A = C-Locale-Wochentag (Englisch, im Projekt nirgends setlocale())
    strftime(buf, sizeof(buf), "%A", &t);
    for (auto &w : s_monoWeekdayWidgets) lv_label_set_text(w.lbl, buf);
  }
  if (!s_monoWeatherWidgets.empty()) {
    bool valid = app_config.weather_enabled && weather_info.valid;
    if (valid) {
      int tempR = (int)(weather_info.temp_c + (weather_info.temp_c >= 0 ? 0.5f : -0.5f));
      snprintf(buf, sizeof(buf), "%dC", tempR);
    }
    for (auto &w : s_monoWeatherWidgets) lv_label_set_text(w.lbl, valid ? buf : "--");
  }
}

static void refresh_mono_forecast_widgets(void) {
  if (s_monoForecastWidgets.empty()) return;
  char buf[8];
  for (auto &w : s_monoForecastWidgets) {
    for (int i = 0; i < w.colCount; i++) {
      if (forecast_info.valid && i < forecast_info.day_count) {
        const forecast_day_t &d = forecast_info.days[i];
        // date_epoch ist bereits lokal verschoben (siehe fetch_forecast() in
        // weather_service.cpp) - gmtime_r statt localtime_r, sonst wuerde
        // der Geraete-TZ-Offset ein zweites Mal angewendet.
        time_t t = (time_t)d.date_epoch;
        struct tm tmv;
        gmtime_r(&t, &tmv);
        lv_label_set_text(w.dayLbls[i], weather_weekday_abbr(tmv.tm_wday));
        int hi = (int)(d.temp_max + (d.temp_max >= 0 ? 0.5f : -0.5f));
        int lo = (int)(d.temp_min + (d.temp_min >= 0 ? 0.5f : -0.5f));
        snprintf(buf, sizeof(buf), "%dC", hi);
        lv_label_set_text(w.hiLbls[i], buf);
        snprintf(buf, sizeof(buf), "%dC", lo);
        lv_label_set_text(w.loLbls[i], buf);
      } else {
        lv_label_set_text(w.dayLbls[i], "--");
        lv_label_set_text(w.hiLbls[i], "--");
        lv_label_set_text(w.loLbls[i], "--");
      }
    }
  }
}

// Icon-Holder bekommen nur bei tatsaechlichem Kategoriewechsel einen neuen
// Icon-Pointer + lv_obj_invalidate() (Vergleich gegen w.curIconDrawn/
// dayIconDrawn[i]) - das eigentliche Zeichnen passiert dann beim naechsten
// Render-Durchlauf in mono_weather_icon_draw_event_cb() oben. Spart bei
// unveraendertem Wetter unnoetige Redraws (und damit unnoetige I2C/SPI-
// Flushes zum Display) - diese Funktion laeuft alle 500ms auf unbestimmte
// Zeit (main.cpp).
static void refresh_mono_standby_weather_widgets(void) {
  if (s_monoStandbyWeatherWidgets.empty()) return;
  char buf[8];
  bool curValid = app_config.weather_enabled && weather_info.valid;
  for (auto &w : s_monoStandbyWeatherWidgets) {
    const uint8_t *curIcon = curValid ? mono_weather_icon12_for_owm(weather_info.icon) : nullptr;
    if (curValid) {
      int tempR = (int)(weather_info.temp_c + (weather_info.temp_c >= 0 ? 0.5f : -0.5f));
      snprintf(buf, sizeof(buf), "%dC", tempR);
      lv_label_set_text(w.curTempLbl, buf);
    } else {
      lv_label_set_text(w.curTempLbl, "--");
    }
    if (curIcon != w.curIconDrawn) {
      lv_obj_set_user_data(w.curIconHolder, (void *)curIcon);
      lv_obj_invalidate(w.curIconHolder);
      w.curIconDrawn = curIcon;
    }

    for (int i = 0; i < w.colCount; i++) {
      bool dayValid = forecast_info.valid && i < forecast_info.day_count;
      const uint8_t *dayIcon = dayValid ? mono_weather_icon12_for_owm(forecast_info.days[i].icon) : nullptr;
      if (dayValid) {
        int hi = (int)(forecast_info.days[i].temp_max + (forecast_info.days[i].temp_max >= 0 ? 0.5f : -0.5f));
        snprintf(buf, sizeof(buf), "%dC", hi);
        lv_label_set_text(w.dayTempLbls[i], buf);
        int lo = (int)(forecast_info.days[i].temp_min + (forecast_info.days[i].temp_min >= 0 ? 0.5f : -0.5f));
        snprintf(buf, sizeof(buf), "%dC", lo);
        lv_label_set_text(w.dayLoLbls[i], buf);
      } else {
        lv_label_set_text(w.dayTempLbls[i], "--");
        lv_label_set_text(w.dayLoLbls[i], "--");
      }
      if (dayIcon != w.dayIconDrawn[i]) {
        lv_obj_set_user_data(w.dayIconHolders[i], (void *)dayIcon);
        lv_obj_invalidate(w.dayIconHolders[i]);
        w.dayIconDrawn[i] = dayIcon;
      }
    }
  }
}

static void refresh_mono_stat_widgets(void) {
  if (s_monoStatWidgets.empty()) return;
  hw_data_lock();
  float cpuLoad = hw_info.cpu_load, cpuTemp = hw_info.cpu_temp, cpuPower = hw_info.cpu_power;
  float gpuLoad = hw_info.gpu_load, gpuTemp = hw_info.gpu_temp, gpuPower = hw_info.gpu_power;
  hw_data_unlock();

  char buf[24];
  for (auto &w : s_monoStatWidgets) {
    float load  = w.isGpu ? gpuLoad  : cpuLoad;
    float temp  = w.isGpu ? gpuTemp  : cpuTemp;
    float power = w.isGpu ? gpuPower : cpuPower;

    int loadPct = (int)(load + 0.5f);
    snprintf(buf, sizeof(buf), "%s %d%%", w.isGpu ? "GPU" : "CPU", loadPct);
    lv_label_set_text(w.lbl, buf);
    lv_bar_set_value(w.bar, loadPct, LV_ANIM_OFF);

    int tempR  = (int)(temp + 0.5f);
    int powerR = (int)(power + 0.5f);
    snprintf(buf, sizeof(buf), "%dC %dW", tempR, powerR);
    lv_label_set_text(w.statLbl, buf);
  }
}

static void refresh_mono_hw_card_widgets(void) {
  if (s_monoHwCardWidgets.empty()) return;
  hw_data_lock();
  float cpuLoad = hw_info.cpu_load, cpuTemp = hw_info.cpu_temp, cpuPower = hw_info.cpu_power;
  float gpuLoad = hw_info.gpu_load, gpuTemp = hw_info.gpu_temp, gpuPower = hw_info.gpu_power;
  hw_data_unlock();

  char buf[16];
  for (auto &w : s_monoHwCardWidgets) {
    float load  = w.isGpu ? gpuLoad  : cpuLoad;
    float temp  = w.isGpu ? gpuTemp  : cpuTemp;
    float power = w.isGpu ? gpuPower : cpuPower;

    snprintf(buf, sizeof(buf), "%d%%", (int)(load + 0.5f));
    lv_label_set_text(w.loadLbl, buf);
    snprintf(buf, sizeof(buf), "%dW", (int)(power + 0.5f));
    lv_label_set_text(w.powerLbl, buf);
    snprintf(buf, sizeof(buf), "%dC", (int)(temp + 0.5f));
    lv_label_set_text(w.tempLbl, buf);
  }
}

// props["key"] gegen die 6 festen hw_info-Namen pruefen, sonst Fallback auf
// dyn_values (siehe shared_state.h/hw_data.cpp) - deckt sowohl die
// Grundwerte als auch beliebige weitere vom PC-Client gesendete Felder ab.
static void refresh_mono_label_value_widgets(void) {
  if (s_monoLabelValueWidgets.empty()) return;
  hw_data_lock();
  float cpuLoad = hw_info.cpu_load, cpuTemp = hw_info.cpu_temp, cpuPower = hw_info.cpu_power;
  float gpuLoad = hw_info.gpu_load, gpuTemp = hw_info.gpu_temp, gpuPower = hw_info.gpu_power;

  char buf[24];
  for (auto &w : s_monoLabelValueWidgets) {
    const char *key = w.key.c_str();
    bool found = true;
    float val = 0;
    if (strcmp(key, "cpu_load") == 0)       val = cpuLoad;
    else if (strcmp(key, "cpu_temp") == 0)  val = cpuTemp;
    else if (strcmp(key, "cpu_power") == 0) val = cpuPower;
    else if (strcmp(key, "gpu_load") == 0)  val = gpuLoad;
    else if (strcmp(key, "gpu_temp") == 0)  val = gpuTemp;
    else if (strcmp(key, "gpu_power") == 0) val = gpuPower;
    else {
      found = false;
      for (int i = 0; i < dyn_values_count; i++) {
        if (strncmp(dyn_values[i].key, key, sizeof(dyn_values[i].key)) == 0) {
          val = dyn_values[i].value;
          found = true;
          break;
        }
      }
    }
    if (found) {
      snprintf(buf, sizeof(buf), "%d%s", (int)(val + (val >= 0 ? 0.5f : -0.5f)), w.unit.c_str());
      lv_label_set_text(w.valLbl, buf);
    } else {
      lv_label_set_text(w.valLbl, "--");
    }
  }
  hw_data_unlock();
}

// Standby-Auswertung fuer Mono: dieselbe Formel wie zuvor im hartcodierten
// display_mono.cpp (app_config.standby_timeout_s als Trigger, weiterhin im
// Web-UI editierbar) - baut jetzt aber kein eigenes Widget mehr, sondern
// blendet nur zwischen den beiden vom Layout-Editor gebauten Sets um
// (s_dashboardContentWidgets/s_standbyWidgets), analog zum bestehenden
// Dashboard/Settings-Tab-Umschalter oben in dieser Datei. s_monoStandby/
// s_monoStandbyAuto/s_monoManualToggleRequested sind weiter oben (vor
// layout_apply()) deklariert, siehe dortiger Kommentar.
static void refresh_mono_standby_visibility(void) {
  if (!displayIsMono() || s_standbyWidgets.empty()) return;

  hw_data_lock();
  bool everReceived  = hw_info.ever_received;
  int64_t lastUpdate = hw_info.last_update_ms;
  hw_data_unlock();

  bool autoStandby = app_config.standby_timeout_s > 0 && everReceived &&
                      (now_ms() - lastUpdate) > (int64_t)app_config.standby_timeout_s * 1000;

  if (autoStandby) {
    s_monoStandby = true;
    s_monoStandbyAuto = true;
  } else if (s_monoStandbyAuto) {
    s_monoStandby = false;
    s_monoStandbyAuto = false;
  }
  if (s_monoManualToggleRequested) {
    s_monoManualToggleRequested = false;
    s_monoStandby = !s_monoStandby;
    s_monoStandbyAuto = false;
  }

  for (lv_obj_t *o : s_dashboardContentWidgets) {
    if (s_monoStandby) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  for (lv_obj_t *o : s_standbyWidgets) {
    if (s_monoStandby) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

// BOOT-Taste als manueller Standby-Umschalter, exaktes Gegenstueck zu
// displayRoundButtonPoll() (display_round.cpp) - dort ausfuehrlich
// begruendet: Pin ist chip-abhaengig (C3: GPIO9, ESP32/S3: GPIO0 - auf dem
// real getesteten ESP32-S3-Zero sind BOOT und RESET zwei getrennte
// physische Taster, GPIO0 haengt NICHT an RST). Eigene Debounce-Variablen
// statt Code-Teilung mit displayRoundButtonPoll() - beide Funktionen laufen
// JEDEN loop()-Takt unabhaengig vom aktiven Display (siehe
// displayDrawButtonPoll() in display_draw.cpp), reine Duplikation von ~15
// Zeilen ist hier lesbarer als eine gemeinsame Abstraktion fuer zwei
// Aufrufer.
#if defined(BOARD_GENERIC)

void displayMonoButtonPoll(void) {
#if CONFIG_IDF_TARGET_ESP32C3
  constexpr uint8_t kButtonPin = 9;
#else
  constexpr uint8_t kButtonPin = 0;
#endif
  constexpr uint32_t kDebounceMs = 40;

  static bool initialized = false;
  static bool lastLevel = true; // INPUT_PULLUP: HIGH = losgelassen
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
      s_monoManualToggleRequested = true;
    }
    lastLevel = level;
  }
}

#else

void displayMonoButtonPoll(void) {}

#endif

void layout_refresh_bindings(void) {
  refresh_datetime_widgets();
  refresh_hw_card_widgets();
  refresh_hw_card_flat_widgets();
  refresh_weather_card_widgets();
  refresh_chart_card_widgets();
  refresh_mono_clock_widgets();
  refresh_mono_forecast_widgets();
  refresh_mono_standby_weather_widgets();
  refresh_mono_stat_widgets();
  refresh_mono_hw_card_widgets();
  refresh_mono_label_value_widgets();
  refresh_mono_standby_visibility();
}

// Nur auf der CYD relevant (XPT2046-Touch) - true, solange Touch noch nie
// erfolgreich kalibriert wurde. main.cpp zeigt in diesem Fall den
// Kalibrier-Bildschirm VOR dem normalen Dashboard: der uebliche Weg dahin
// (Footer -> Zahnrad-Icon/Settings-Tab antippen) setzt selbst schon
// einigermassen praezisen Touch voraus, den es beim allerersten Setup per
// Definition noch nicht gibt.
bool touchCalibrationNeeded(void) {
#if defined(BOARD_CYD_2432S028R)
  return !app_config.touch_calibrated;
#else
  return false;
#endif
}

// Zeigt den Kalibrier-Bildschirm direkt auf dem aktiven Screen (beim
// Erstboot ist noch gar kein Dashboard/Footer aufgebaut) - no-op auf Boards
// ohne XPT2046-Touch.
void beginFirstBootTouchCalibration(void) {
#if defined(BOARD_CYD_2432S028R)
  start_touch_calibration(nullptr);
#endif
}

bool displayIsRound(void) {
#if defined(BOARD_GENERIC)
  // GC9A01 ist das einzige runde Display im Sortiment (240x240-Framebuffer,
  // physisch aber rund) - ILI9488/ST7796S sind rechteckig trotz anderer
  // Aufloesung.
  return app_config.display_type == DISPLAY_GENERIC_GC9A01;
#else
  // CYD und JC8048W550 sind die einzigen fest verdrahteten Boards - beide
  // rechteckig.
  return false;
#endif
}

bool displayIsMono(void) {
#if defined(BOARD_GENERIC)
  // SSD1309 und SH1106 sind die monochromen Displays im Sortiment - beide
  // nutzen denselben Treiber (displays/mono_oled_i2c.h) und dieselben
  // mono_*-Widget-Typen ueber den generischen Layout-Editor (siehe
  // kWidgetTypes oben in dieser Datei).
  return app_config.display_type == DISPLAY_GENERIC_SSD1309 ||
         app_config.display_type == DISPLAY_GENERIC_SH1106;
#else
  return false;
#endif
}

bool displaySupportsLayoutEditor(void) {
#if defined(BOARD_GENERIC)
  // DISPLAY_NONE (kein Display gewaehlt/verkabelt) -> false, sonst wuerde
  // der Editor-Tab ohne jedes Display angeboten. Mono hat eigene
  // Widget-Typen (mono_*, siehe kWidgetTypes) und laeuft seitdem ueber
  // denselben layout_apply()-Pfad wie die eckigen Farbdisplays - nur rund
  // bleibt aussen vor (siehe Begruendung in display_layout.h).
  return app_config.display_type != DISPLAY_NONE && !displayIsRound();
#else
  return !displayIsRound();
#endif
}

int16_t displayWidthPx(void) {
#if defined(BOARD_GENERIC)
  return g_lcdWidth;
#else
  return LCD_WIDTH;
#endif
}

int16_t displayHeightPx(void) {
#if defined(BOARD_GENERIC)
  return g_lcdHeight;
#else
  return LCD_HEIGHT;
#endif
}

// Grundlayout fuer Mono (128x64): Dashboard reproduziert 1:1 das alte
// hartcodierte display_mono.cpp-Layout (Datum links/Uhr rechts als
// Kopfzeile, CPU-/GPU-Statuszeile darunter). standby_widgets ist "Variante
// 1" aus der Anfrage (grosse Uhr + Datum + Wochentag, siehe Referenzfoto) -
// "Variante 2" (Uhr+Datum+Wetter) gibt es nur als Preset im Web-Editor
// (dashboard.html), nicht hier, da sie ohne konfiguriertes Wetter-API
// bloss "--" anzeigen wuerde.
static String mono_layout_default_json(void) {
  JsonDocument doc;
  doc["version"] = 1;
  JsonArray widgets = doc["widgets"].to<JsonArray>();

  auto addWidget = [&](JsonArray arr, const char *id, const char *type,
                        int16_t x, int16_t y, int16_t ww, int16_t hh, bool big = false) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = id;
    o["type"] = type;
    o["x"] = x;
    o["y"] = y;
    o["w"] = ww;
    o["h"] = hh;
    if (big) o["props"]["big"] = true;
  };

  addWidget(widgets, "date1", "mono_date",      0,  0, 80, 10);
  addWidget(widgets, "clk1",  "mono_clock",    80,  0, 48, 10);
  addWidget(widgets, "cpu1",  "mono_cpu_stat",  0, 13, 128, 24);
  addWidget(widgets, "gpu1",  "mono_gpu_stat",  0, 38, 128, 24);

  JsonArray standby = doc["standby_widgets"].to<JsonArray>();
  addWidget(standby, "sclk1", "mono_clock",   0,  4, 128, 18, /*big=*/true);
  addWidget(standby, "sdate1","mono_date",    0, 26, 128, 10);
  addWidget(standby, "sday1", "mono_weekday", 0, 40, 128, 10);

  String out;
  serializeJson(doc, out);
  return out;
}

// Grundlayout fuer eckige Farbdisplays: 2x2-Raster (Datum/Uhrzeit + Wetter
// oben, CPU + GPU unten) plus Footer, nachgebaut vom User handgebauten
// Referenzfoto. Fuer die CYD (320x240) an echter Hardware bestaetigte
// Masse: margin=5, gap=10, row1H=80 (Datum/Wetter), row2H=95 (CPU/GPU),
// footerH=40 - ergibt cardW=150 und footerW=310, beides exakt vom User
// vorgegeben. Skaliert linear mit der Bildschirmhoehe (scale = h/240) fuer
// groessere Displays wie das JC8048W550, damit die Proportionen erhalten
// bleiben statt auf 800x480 winzig zu wirken. Im Editor laesst sich danach
// ohnehin jede Karte frei verschieben/skalieren. Mono (128x64) hat ein
// eigenes Grundlayout (mono_layout_default_json() oben, eigene
// mono_*-Widget-Typen).
String layout_default_json(int16_t w, int16_t h) {
  if (w == 128 && h == 64) {
    return mono_layout_default_json();
  }

  JsonDocument doc;
  doc["version"] = 1;
  JsonArray widgets = doc["widgets"].to<JsonArray>();
  doc["standby_widgets"].to<JsonArray>(); // leer - nur Mono nutzt Standby-Layouts

  int16_t margin = 5;
  int16_t gap = 10;
  float scale = (float)h / 240.0f;
  int16_t cardW = (w - 2 * margin - gap) / 2;
  int16_t row1H, row2H, footerH;
  if (w == 800 && h == 480) {
    // JC8048W550: eigene handgetunte Werte statt der linearen Skalierung
    // unten (User-Feedback anhand echter Hardware-Fotos) - reines
    // Hochskalieren liess die CPU/GPU-Karten leer wirken. Datum/Wetter
    // bekommen bewusst WENIGER als der lineare Wert (120 statt 160),
    // CPU/GPU dafuer MEHR (250 statt 190) - der Freiraum ist fuer einen
    // spaeter geplanten Verlaufsgraphen gedacht. Zusammen mit cardW=390
    // (siehe oben) geht exakt in 800x480 auf, kein Rest.
    row1H = 120;
    row2H = 250;
    footerH = 80;
  } else {
    row1H = (int16_t)(80 * scale + 0.5f);
    row2H = (int16_t)(95 * scale + 0.5f);
    footerH = (int16_t)(40 * scale + 0.5f);
  }
  int16_t row1Y = margin;
  int16_t row2Y = row1Y + row1H + gap;
  int16_t footerY = h - margin - footerH;
  int16_t footerW = w - 2 * margin;

  auto addWidget = [&](const char *id, const char *type, int16_t x, int16_t y, int16_t ww, int16_t hh) {
    JsonObject o = widgets.add<JsonObject>();
    o["id"] = id;
    o["type"] = type;
    o["x"] = x;
    o["y"] = y;
    o["w"] = ww;
    o["h"] = hh;
  };

  addWidget("dt1",      "datetime_card", margin,               row1Y,   cardW,   row1H);
  addWidget("weather1", "weather_card",  margin + cardW + gap, row1Y,   cardW,   row1H);
  addWidget("cpu1",     "cpu_card",      margin,               row2Y,   cardW,   row2H);
  addWidget("gpu1",     "gpu_card",      margin + cardW + gap, row2Y,   cardW,   row2H);
  addWidget("footer1",  "footer",        margin,               footerY, footerW, footerH);

  String out;
  serializeJson(doc, out);
  return out;
}
