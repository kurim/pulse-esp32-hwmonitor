#include "ui_strings.h"
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

// Format-Strings muessen zwischen STR_DE/STR_EN dieselben printf-Spezifizierer
// (Typ und Reihenfolge) verwenden, da dieselben snprintf()-Aufrufe in
// display_ui.c beide Varianten mit denselben Argumenten fuellen.

static const char *STR_DE[UI_STR_COUNT] = {
    [UI_STR_HISTORY_TITLE]     = "Verlauf",
    [UI_STR_SETTINGS_TITLE]    = "Einstellungen",
    [UI_STR_WAITING_MQTT]      = "Warte auf MQTT-Daten...",
    [UI_STR_WAITING_DATA]      = "Warte auf Daten...",
    [UI_STR_WEATHER_OFF]       = "Wetter aus",
    [UI_STR_CPU_HISTORY]       = "CPU Verlauf",
    [UI_STR_GPU_HISTORY]       = "GPU Verlauf",
    [UI_STR_USAGE_FMT]         = "Auslastung (Usage): %.1f %%",
    [UI_STR_TEMP_FMT]          = "Temperatur (Temp): %.1f C",
    [UI_STR_POWER_FMT]         = "Leistung (Power): %.1f W",
    [UI_STR_USAGE_CHART_CAPTION] = "Auslastung (%) - Verlauf",
    [UI_STR_TEMP_CHART_CAPTION]  = "Temperatur (C) - Verlauf",
    [UI_STR_QUICK_CPU_FMT]     = "CPU: %.0fC %.0fW %s",
    [UI_STR_QUICK_GPU_FMT]     = "GPU: %.0fC %.0fW %s",
    [UI_STR_FW_FMT]            = "Firmware: %s",
    [UI_STR_IP_FMT]            = "IP-Adresse: %s%s",
    [UI_STR_IP_SETUP_AP_SUFFIX] = " (Setup-AP)",
    [UI_STR_WIFI_FMT]          = "WLAN: %s",
    [UI_STR_WIFI_SETUP_AP]     = "Setup-AP aktiv",
    [UI_STR_CONNECTED]         = "verbunden",
    [UI_STR_DISCONNECTED]      = "getrennt",
    [UI_STR_MQTT_FMT]          = "MQTT: %s",
    [UI_STR_FREE_HEAP_FMT]     = "Freier Speicher: %u KB",
    [UI_STR_SETTINGS_HINT]     = "WLAN/MQTT/Wetter werden weiterhin ueber das Webportal konfiguriert.",
    [UI_STR_AP_BTN_IDLE]       = "Neustart in Setup-AP",
    [UI_STR_AP_BTN_CONFIRM]    = "Wirklich? Nochmal tippen",
    [UI_STR_AP_BTN_SWITCHING]  = "Wechsle in Setup-AP...",
    [UI_STR_MONO_CPU_FMT]      = "CPU %d%% %dC",
    [UI_STR_MONO_GPU_FMT]      = "GPU %d%% %dC",
    [UI_STR_ROUND_CPU_FMT]     = "CPU %d%%",
    [UI_STR_ROUND_GPU_FMT]     = "GPU %d%%",
};

static const char *STR_EN[UI_STR_COUNT] = {
    [UI_STR_HISTORY_TITLE]     = "History",
    [UI_STR_SETTINGS_TITLE]    = "Settings",
    [UI_STR_WAITING_MQTT]      = "Waiting for MQTT data...",
    [UI_STR_WAITING_DATA]      = "Waiting for data...",
    [UI_STR_WEATHER_OFF]       = "Weather off",
    [UI_STR_CPU_HISTORY]       = "CPU History",
    [UI_STR_GPU_HISTORY]       = "GPU History",
    [UI_STR_USAGE_FMT]         = "Usage: %.1f %%",
    [UI_STR_TEMP_FMT]          = "Temperature: %.1f C",
    [UI_STR_POWER_FMT]         = "Power: %.1f W",
    [UI_STR_USAGE_CHART_CAPTION] = "Usage (%) - history",
    [UI_STR_TEMP_CHART_CAPTION]  = "Temperature (C) - history",
    [UI_STR_QUICK_CPU_FMT]     = "CPU: %.0fC %.0fW %s",
    [UI_STR_QUICK_GPU_FMT]     = "GPU: %.0fC %.0fW %s",
    [UI_STR_FW_FMT]            = "Firmware: %s",
    [UI_STR_IP_FMT]            = "IP address: %s%s",
    [UI_STR_IP_SETUP_AP_SUFFIX] = " (setup AP)",
    [UI_STR_WIFI_FMT]          = "WiFi: %s",
    [UI_STR_WIFI_SETUP_AP]     = "Setup AP active",
    [UI_STR_CONNECTED]         = "connected",
    [UI_STR_DISCONNECTED]      = "disconnected",
    [UI_STR_MQTT_FMT]          = "MQTT: %s",
    [UI_STR_FREE_HEAP_FMT]     = "Free memory: %u KB",
    [UI_STR_SETTINGS_HINT]     = "WiFi/MQTT/weather are still configured via the web portal.",
    [UI_STR_AP_BTN_IDLE]       = "Restart into setup AP",
    [UI_STR_AP_BTN_CONFIRM]    = "Really? Tap again",
    [UI_STR_AP_BTN_SWITCHING]  = "Switching to setup AP...",
    [UI_STR_MONO_CPU_FMT]      = "CPU %d%% %dC",
    [UI_STR_MONO_GPU_FMT]      = "GPU %d%% %dC",
    [UI_STR_ROUND_CPU_FMT]     = "CPU %d%%",
    [UI_STR_ROUND_GPU_FMT]     = "GPU %d%%",
};

static bool s_use_en = false;

void ui_set_language(const char *lang_code)
{
    s_use_en = (lang_code && strcmp(lang_code, "en") == 0);
}

const char *ui_str(ui_str_id_t id)
{
    if ((size_t)id >= UI_STR_COUNT) return "";
    return (s_use_en ? STR_EN : STR_DE)[id];
}
