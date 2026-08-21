#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "board_config.h"
#include "pin_override.h"

#define FW_VERSION "1.0.5" // Wird in platformio.ini (build_flags) auch an den C-Compiler uebergeben, siehe src/network/github_ota.cpp

// Ringpuffer-Laenge fuer Verlaufsdiagramme (60 Samples = ~1 Min bei 1 Wert/Sek)
#define HIST_LEN 60

// Quelle der Hardwaredaten (CPU/GPU-Werte): entweder vom MQTT-Broker oder
// direkt per USB/Serial vom PC-Client (siehe src/network/serial_handler.cpp).
// Nur einer der beiden Wege ist gleichzeitig aktiv (siehe main.cpp).
typedef enum {
    HW_SOURCE_MQTT = 0,
    HW_SOURCE_USB  = 1,
} hw_source_t;

// Welches Display-Profil aktiv ist. DISPLAY_NONE ist der Default: headless
// booten, Webportal erreichbar, Display danach konfigurieren (siehe
// CLAUDE.md) - rettet bei jedem Fehlgriff in der Pin-Konfiguration.
// Werte werden ans Ende angehaengt, nie einsortiert (der Wert liegt im NVS).
typedef enum {
    DISPLAY_NONE = 0,
    DISPLAY_CYD_ILI9341,
    DISPLAY_CYD_ST7789,
    DISPLAY_JC8048W550,
    DISPLAY_GENERIC_GC9A01,
    DISPLAY_GENERIC_ILI9488,
    DISPLAY_GENERIC_ST7796S,
    DISPLAY_GENERIC_SSD1309,
    DISPLAY_GENERIC_SH1106,
} display_type_t;

inline display_type_t board_profile_default(void) {
    return DISPLAY_NONE;
}

// Zentrale, aus NVS geladene/persistierte App-Konfiguration. Siehe
// src/config/config_store.{h,cpp} (Laden/Speichern) und
// config_set_defaults() (Defaultwerte, siehe shared_state.cpp).
typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];

    hw_source_t hw_source;

    char     mqtt_host[64];
    uint16_t mqtt_port;
    char     mqtt_user[32];
    char     mqtt_pass[64];
    char     mqtt_topic[64];

    char tz[48];
    char ntp_server[64];

    bool weather_enabled;
    char weather_api_key[40];
    char weather_city[64];
    char weather_units[16];

    uint8_t brightness;
    uint8_t rotation;
    display_type_t display_type;
    uint16_t standby_timeout_s;

    // Nur fuer runde Displays (GC9A01) wirksam - der Dashboard-Editor ist
    // dort per displaySupportsLayoutEditor() bewusst nicht verfuegbar (siehe
    // display_layout.h), als Ersatz sind wenigstens die Arc-Farben ueber das
    // WebUI einstellbar. 0xRRGGBB, Hintergrund-Spur wird davon abgeleitet
    // (gedaempfter Ton derselben Farbe, siehe display_round.cpp).
    uint32_t cpu_arc_color;
    uint32_t gpu_arc_color;

    char language[8];

    pin_override_t pin_overrides;

    // Nur fuer DISPLAY_GENERIC_SSD1309/DISPLAY_GENERIC_SH1106 wirksam (beide
    // teilen sich dieses Feld, siehe pin_override.h) - eigene Struct statt
    // pin_overrides, da I2C eine andere Pin-Form hat (SDA/SCL statt
    // MOSI/MISO/SCLK/CS/DC).
    pin_override_i2c_t mono_i2c_pins;

    // Nur fuer XPT2046-Touch (CYD) wirksam, siehe cyd_2432s028r.h. Rohe
    // ADC-Grenzwerte (0-4095), ueber den "Touch kalibrieren"-Knopf im
    // Einstellungen-Tab gesetzt (siehe display_layout.cpp) statt fest im
    // Code verdrahtet - Ecken-Antippen per Auge war zweimal in Folge zu
    // ungenau (resistive Touchscreens reagieren nahe am physischen Rand oft
    // unzuverlaessig), der Kalibrier-Bildschirm nutzt stattdessen zwei klar
    // definierte, deutlich eingerueckte Zielpunkte.
    int16_t touch_x_min, touch_x_max, touch_y_min, touch_y_max;
    // false bis zur ersten abgeschlossenen Kalibrierung - main.cpp zeigt den
    // Kalibrier-Bildschirm dann VOR dem normalen Dashboard direkt beim
    // Erstboot (siehe display_layout.cpp touchCalibrationNeeded()). Noetig,
    // weil der Weg dahin sonst nur ueber den Footer/Einstellungen-Tab fuehrt,
    // dessen Zahnrad-Icon man ohne kalibrierten Touch kaum treffen kann.
    bool touch_calibrated;
} app_config_t;

// Live-Hardwarewerte vom PC-Client (MQTT oder Serial, siehe hw_data.h).
// Zugriff nur zwischen hw_data_lock()/hw_data_unlock() (wird auch vom
// LVGL-Refresh auf Core 1 und einem Settings-Webserver-Task gelesen).
typedef struct {
    float cpu_load;
    float cpu_temp;
    float cpu_power;
    float gpu_load;
    float gpu_temp;
    float gpu_power;
    int64_t last_update_ms;
    bool  ever_received;
} hw_info_t;

// Spiegelt JEDEN numerischen Top-Level-Key aus dem hw-Data-JSON wider,
// nicht nur die 6 festen cpu_/gpu_-Felder oben - Basis fuer das
// "Label - Wert"-Widget im Mono-Layout-Editor (display_layout.cpp), das an
// beliebige vom PC-Client gesendete Felder binden koennen soll (die App
// liefert mehr als nur die 6 Grundwerte). Fester Cap statt dynamischer
// Allokation - 24*32 Bytes sind auf jedem Zielchip vernachlaessigbar, ein
// std::map waere hier reiner Overhead. Ueberzaehlige Keys (>DYN_VALUES_MAX)
// werden still verworfen, kein Fehlerpfad noetig - dieselbe Haltung wie
// beim 4096-Byte-Limit in layout_store.h. Zugriff ebenfalls nur zwischen
// hw_data_lock()/hw_data_unlock().
#define DYN_VALUES_MAX 24
typedef struct {
    char  key[24];
    float value;
} dyn_value_t;
extern dyn_value_t dyn_values[DYN_VALUES_MAX];
extern int         dyn_values_count;

// Zuletzt abgerufene Wetterdaten (OpenWeatherMap), siehe weather_service.cpp.
typedef struct {
    bool  valid;
    int64_t last_fetch_ms;
    float temp_c;
    float feels_like_c;
    int   humidity;
    float wind_speed;
    int   wind_deg;
    float rain_1h;
    char  description[48];
    char  icon[8];
} weather_info_t;

// Auf Tage verdichteter Forecast (OpenWeatherMap "5 day / 3 hour forecast",
// selber kostenloser Standard-Plan wie die Current-Weather-API oben, siehe
// weather_service.cpp) - die rohen 3h-Schritte werden dort pro Kalendertag
// zu Min/Max verdichtet, hier liegt nur noch das Ergebnis. Kein eigener
// Mutex-Schutz (wie weather_info oben) - ein/derselbe Task schreibt, Leser
// (LVGL-Refresh) lesen nur primitive Felder, ein gelegentlicher zerrissener
// Lesevorgang ist hier folgenlos, dieselbe Haltung wie bei weather_info.
// 5 statt 4 - die OWM-Forecast-Antwort deckt bis zu 5 Kalendertage ab (40
// Eintraege x 3h = 120h), der erste davon ist "heute" (nur die restlichen
// Stunden ab jetzt, meist unvollstaendig). User-Vorgabe: bis zu 4 volle
// Folgetage zusaetzlich zu "heute" sichtbar machen.
#define FORECAST_DAYS 5
typedef struct {
    int32_t date_epoch; // Mitternacht des Tages, lokale Zeit, Unix-Sekunden
    float   temp_min;
    float   temp_max;
    char    icon[8];    // Icon-Code der Mittags-naechsten 3h-Stufe des Tages
} forecast_day_t;
typedef struct {
    bool  valid;
    int64_t last_fetch_ms;
    int   day_count; // wie viele der folgenden Slots gueltig sind (<= FORECAST_DAYS)
    forecast_day_t days[FORECAST_DAYS];
} forecast_info_t;

// Ringpuffer fuer Verlaufsdiagramme, siehe history_push() in shared_state.cpp.
typedef struct {
    float load[HIST_LEN];
    float temp[HIST_LEN];
    float power[HIST_LEN];
    int   count;
} history_t;

extern app_config_t   app_config;
extern hw_info_t      hw_info;
extern weather_info_t weather_info;
extern forecast_info_t forecast_info;
extern history_t      cpu_history;
extern history_t      gpu_history;
extern volatile bool  wifi_connected;
extern volatile bool  mqtt_connected;
extern volatile bool  serial_connected;

// true waehrend eines laufenden FOTA-Uploads (siehe web_portal.cpp
// handleOtaUpload()) - der Wetter-Task (weather_service.cpp) pausiert
// waehrenddessen, da ein HTTPS-Abruf (mbedTLS-Handshake braucht einen
// grossen zusammenhaengenden Heap-Block) sonst mit dem OTA-Update um
// denselben knappen Speicher auf dem klassischen ESP32 (kein PSRAM)
// konkurriert und beides zum Scheitern bringen kann.
extern volatile bool  ota_in_progress;

// Zaehlt jede erfolgreich geparste Hardwaredaten-Nachricht, egal ob sie ueber
// MQTT oder Serial (HW_SOURCE_USB) hereinkam - siehe mqtt_handler.cpp bzw.
// serial_handler.cpp. Fuers Web-Dashboard ("empfangene Ticks").
extern volatile uint32_t hw_rx_count;

// Schuetzt hw_info/cpu_history/gpu_history gegen gleichzeitigen Zugriff aus
// hw_data_apply_json() (Netzwerk-Task), LVGL-Refresh und Settings-Webserver.
void hw_data_lock(void);
void hw_data_unlock(void);

int64_t now_ms(void);

// Fuellt cfg mit Standardwerten. Vor config_store_load() aufrufen, damit
// fehlende NVS-Schluessel (z.B. beim allerersten Boot) sinnvolle Werte
// behalten statt Nullen.
void config_set_defaults(app_config_t *cfg);

void history_push(history_t *h, float load, float temp, float power);
