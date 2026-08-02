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
    {"weather_card", create_weather_card},
    {"chart_card", create_chart_card},
    {"footer", create_footer},
};

enum class LayoutCmdType { kApplyFull };

struct LayoutCmd {
  LayoutCmdType type;
  String *payload; // heap-alloziert, vom Consumer (layout_queue_process) freigegeben
};

QueueHandle_t s_layoutQueue = nullptr;

} // namespace

void layout_apply(JsonArrayConst widgets) {
  lv_obj_t *scr = lv_scr_act();
  // lv_obj_clean() macht alle bisherigen s_widgets-Handles sofort ungueltig -
  // Map/Bindings deshalb VOR dem Neuaufbau leeren, nicht danach.
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  s_widgets.clear();
  s_datetimeWidgets.clear();
  s_hwCardWidgets.clear();
  s_weatherCardWidgets.clear();
  s_chartCardWidgets.clear();
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
      layout_apply(doc["widgets"].as<JsonArrayConst>());
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

void layout_refresh_bindings(void) {
  refresh_datetime_widgets();
  refresh_hw_card_widgets();
  refresh_weather_card_widgets();
  refresh_chart_card_widgets();
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
  // SSD1309 ist aktuell das einzige monochrome Display im Sortiment.
  return app_config.display_type == DISPLAY_GENERIC_SSD1309;
#else
  return false;
#endif
}

bool displaySupportsLayoutEditor(void) {
#if defined(BOARD_GENERIC)
  // DISPLAY_NONE (kein Display gewaehlt/verkabelt) -> false, sonst wuerde
  // der Editor-Tab ohne jedes Display angeboten.
  return app_config.display_type != DISPLAY_NONE && !displayIsRound() && !displayIsMono();
#else
  return !displayIsRound() && !displayIsMono();
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

// Grundlayout fuer eckige Farbdisplays: 2x2-Raster (Datum/Uhrzeit + Wetter
// oben, CPU + GPU unten) plus Footer, nachgebaut vom User handgebauten
// Referenzfoto. Fuer die CYD (320x240) an echter Hardware bestaetigte
// Masse: margin=5, gap=10, row1H=80 (Datum/Wetter), row2H=95 (CPU/GPU),
// footerH=40 - ergibt cardW=150 und footerW=310, beides exakt vom User
// vorgegeben. Skaliert linear mit der Bildschirmhoehe (scale = h/240) fuer
// groessere Displays wie das JC8048W550, damit die Proportionen erhalten
// bleiben statt auf 800x480 winzig zu wirken. Im Editor laesst sich danach
// ohnehin jede Karte frei verschieben/skalieren.
String layout_default_json(int16_t w, int16_t h) {
  JsonDocument doc;
  doc["version"] = 1;
  JsonArray widgets = doc["widgets"].to<JsonArray>();

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
