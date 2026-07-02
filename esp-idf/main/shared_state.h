#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FW_VERSION "2.0.0-idf"

// Ringpuffer-Laenge fuer Verlaufsdiagramme (60 Samples = ~1 Min bei 1 Wert/Sek)
#define HIST_LEN 60

// ----------------------------------------------------------------
// Konfiguration (per Webportal gesetzt, in NVS persistiert)
// ----------------------------------------------------------------
typedef struct {
    char     wifi_ssid[33];
    char     wifi_pass[65];

    char     mqtt_host[65];
    uint16_t mqtt_port;
    char     mqtt_user[33];
    char     mqtt_pass[65];
    char     mqtt_topic[65];

    char     tz[65];
    char     ntp_server[65];

    bool     weather_enabled;
    char     weather_api_key[41];
    char     weather_city[65];
    char     weather_units[9];   // "metric" | "imperial"

    uint8_t  brightness;         // 0-255 (PWM)
    uint8_t  rotation;           // 0-3
} app_config_t;

// ----------------------------------------------------------------
// Live-Hardwarewerte (per MQTT vom PC-Client geliefert)
// ----------------------------------------------------------------
typedef struct {
    float   cpu_load, gpu_load;   // %
    float   cpu_temp, gpu_temp;   // Grad C
    float   cpu_power, gpu_power; // W
    int64_t last_update_ms;
    bool    ever_received;
} hw_info_t;

typedef struct {
    bool    valid;
    float   temp_c;
    float   feels_like_c;
    int     humidity;      // %
    float   wind_speed;    // km/h (metric) bzw. mph (imperial), s. app_config.weather_units
    int     wind_deg;      // Windrichtung in Grad (0-360)
    float   rain_1h;       // mm/h, 0 wenn kein Regen gemeldet
    char    description[48];
    char    icon[8];
    int64_t last_fetch_ms;
} weather_info_t;

typedef struct {
    float   load[HIST_LEN];
    float   temp[HIST_LEN];
    float   power[HIST_LEN];
    uint8_t count;
} history_t;

// Globale Instanzen (definiert in shared_state.c)
extern app_config_t   app_config;
extern hw_info_t      hw_info;
extern weather_info_t weather_info;
extern history_t      cpu_history;
extern history_t      gpu_history;
extern volatile bool  wifi_connected;
extern volatile bool  mqtt_connected;

// Millisekunden seit Boot (Arduino-millis()-Aequivalent).
int64_t now_ms(void);

// Setzt cfg auf die Werksvorgaben.
void config_set_defaults(app_config_t *cfg);

// Schiebt einen Wert in den Ringpuffer (aeltester faellt raus).
void history_push(history_t *h, float load, float temp, float power);

#ifdef __cplusplus
}
#endif
