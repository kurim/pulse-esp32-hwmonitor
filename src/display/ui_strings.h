#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------
// Uebersetzbare Texte des Geraete-Displays (LVGL-UI, main/display_ui.c).
// Screens werden nur einmal beim Boot gebaut (display_ui_begin()) - eine
// Sprachaenderung greift daher erst nach einem Neustart, wie auch andere
// Display-Einstellungen (display_type/rotation/brightness).
// ----------------------------------------------------------------
typedef enum {
    UI_STR_HISTORY_TITLE = 0,   // "Verlauf"
    UI_STR_SETTINGS_TITLE,      // "Einstellungen"
    UI_STR_WAITING_MQTT,        // "Warte auf MQTT-Daten..."
    UI_STR_WAITING_DATA,        // "Warte auf Daten..."
    UI_STR_WEATHER_OFF,         // "Wetter aus"
    UI_STR_CPU_HISTORY,         // "CPU Verlauf"
    UI_STR_GPU_HISTORY,         // "GPU Verlauf"
    UI_STR_USAGE_FMT,           // "Auslastung (Usage): %.1f %%"
    UI_STR_TEMP_FMT,            // "Temperatur (Temp): %.1f C"
    UI_STR_POWER_FMT,           // "Leistung (Power): %.1f W"
    UI_STR_USAGE_CHART_CAPTION, // "Auslastung (%) - Verlauf"
    UI_STR_TEMP_CHART_CAPTION,  // "Temperatur (C) - Verlauf"
    UI_STR_QUICK_CPU_FMT,       // "CPU: %.0fC %.0fW %s"
    UI_STR_QUICK_GPU_FMT,       // "GPU: %.0fC %.0fW %s"
    UI_STR_FW_FMT,              // "Firmware: %s"
    UI_STR_IP_FMT,              // "IP-Adresse: %s%s"
    UI_STR_IP_SETUP_AP_SUFFIX,  // " (Setup-AP)"
    UI_STR_WIFI_FMT,            // "WLAN: %s"
    UI_STR_WIFI_SETUP_AP,       // "Setup-AP aktiv"
    UI_STR_CONNECTED,           // "verbunden"
    UI_STR_DISCONNECTED,        // "getrennt"
    UI_STR_MQTT_FMT,            // "MQTT: %s"
    UI_STR_USB_FMT,             // "USB/Seriell: %s"
    UI_STR_FREE_HEAP_FMT,       // "Freier Speicher: %u KB"
    UI_STR_SETTINGS_HINT,       // "WLAN/MQTT/Wetter werden weiterhin ueber das Webportal konfiguriert."
    UI_STR_AP_BTN_IDLE,         // "Neustart in Setup-AP"
    UI_STR_AP_BTN_CONFIRM,      // "Wirklich? Nochmal tippen"
    UI_STR_AP_BTN_SWITCHING,    // "Wechsle in Setup-AP..."
    UI_STR_MONO_PCT_FMT,        // "%d%%"
    UI_STR_MONO_POWER_FMT,      // "%dW"
    UI_STR_MONO_TEMP_FMT,       // "%dC"
    UI_STR_ROUND_CPU_FMT,       // "CPU %d%%"
    UI_STR_ROUND_GPU_FMT,       // "GPU %d%%"
    UI_STR_COUNT
} ui_str_id_t;

// Setzt die aktuelle UI-Sprache ("de"/"en", alles andere faellt auf "de"
// zurueck). Vor dem ersten build_*()-Aufruf in display_ui_begin() setzen.
void ui_set_language(const char *lang_code);

// Liefert den Text/Format-String fuer id in der aktuell gesetzten Sprache.
const char *ui_str(ui_str_id_t id);

#ifdef __cplusplus
}
#endif
