#pragma once
#include <stdint.h>

// Startet den Hintergrund-Task, der periodisch (alle 10 Min) das Wetter von
// OpenWeatherMap abruft, sofern in app_config aktiviert. Nutzt die Current
// Weather Data API (kostenloser Standard-Plan, Stadtname statt Lat/Lon,
// kein Subscription-Opt-in wie bei One Call 3.0/4.0 noetig).
void weather_service_begin(void);

// Wandelt eine Windrichtung in Grad (0-360) in eine 8-Punkte-Kompassangabe
// um (deutsch oder englisch je nach app_config.language).
const char *weather_wind_compass(int deg);

// 2-Buchstaben-Wochentagsabkuerzung (deutsch oder englisch je nach
// app_config.language), wday in struct-tm-Konvention (0=Sonntag/Sunday).
// Fuer den Forecast (forecast_info in shared_state.h) - dessen date_epoch
// ist bereits lokal verschoben, siehe Kommentar in weather_service.cpp
// fetch_forecast().
const char *weather_weekday_abbr(int wday);

// Ordnet einen OpenWeatherMap-Icon-Code (weather_info.icon, z.B. "01d",
// "10n" - 2-stellige Bedingungs-ID + Tag/Nacht-Suffix) dem passendsten MDI-
// Weather-Icon aus mdi_icons.h zu. Nur 5 Wetter-Icons vorhanden - kein
// eigenes fuer Wolken (02-04) oder Nebel (50), dafuer MDI_WEATHER_HAIL als
// naechstliegender Ersatz statt ein sechstes Icon zu erzeugen. Zentral hier
// statt lokal in display_draw.cpp, damit jede Stelle, die Wetter mit Icon
// zeigen will (z.B. spaeter ein Dashboard-Editor-Widget), dieselbe Zuordnung
// nutzt.
const char *weather_icon_mdi(const char *owmCode);

// Passende Akzentfarbe (0xRRGGBB) zum selben OWM-Icon-Code, fuer farblich
// deutlichere Wetteranzeigen (z.B. per LVGL-Recolor-Span "#RRGGBB text#").
// uint32_t statt lv_color_t, damit dieses rein netzwerkseitige Modul kein
// LVGL einbinden muss - der Aufrufer wandelt via lv_color_hex() um.
uint32_t weather_icon_color(const char *owmCode);
