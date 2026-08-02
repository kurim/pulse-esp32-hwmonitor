#pragma once

// Stoesst die WLAN-Verbindung an, NICHT blockierend (siehe wifi_provision_loop()
// fuer die eigentliche Zeitsteuerung):
// 1. Per app_config.wifi_ssid/wifi_pass, falls vorhanden, sonst per vom
//    ESP-SDK gemerkten Zugangsdaten (WiFi.begin() ohne Args).
// 2. Waehrenddessen, nur wenn app_config.hw_source == HW_SOURCE_MQTT (siehe
//    .cpp - der serielle Port wird sonst schon von serial_handler.cpp fuer
//    HW_SOURCE_USB-Daten gebraucht), zusaetzlich per Improv WiFi ueber
//    Serial erreichbar (improv-wifi.com bzw. kompatible Tools).
// 3. Falls das innerhalb des Zeitfensters nicht verbindet und
//    -DIMPROV_WIFI_BLE_ENABLED gesetzt ist: Improv WiFi ueber BLE (30s
//    Fenster). Bewusst NICHT parallel zu Schritt 1 gestartet - BLEDevice::
//    init() waehrend eines laufenden WiFi.begin()-Versuchs fuehrt auf
//    ESP32/S3 zu Radio-Coexistence-Fehlern ("esp_wifi_set_ps failed").
// 4. Falls auch das nicht verbindet: Fallback auf einen Captive-Portal-AP
//    (martinverges/ESP32-Wifi-Manager) zum Konfigurieren im Browser. Laeuft
//    danach autonom in einer eigenen Task weiter.
//
// Vor mqtt_handler_begin()/time_service_begin()/weather_service_begin() aus
// setup() aufrufen, nach config_store_load().
void wifi_provision_begin(void);

// Muss in jeder loop()-Iteration aufgerufen werden - treibt die Zeitfenster-
// Uebergaenge zwischen den obigen Stufen. Absichtlich nicht-blockierend
// (im Gegensatz zur fruehren Version): eine blockierende Warteschleife in
// setup() haette verhindert, dass lv_timer_handler() je laeuft, wodurch der
// Boot-Screen (Logo/Log-Konsole) nie tatsaechlich gerendert worden waere -
// LVGL baut den Widget-Baum zwar sofort auf, das Zeichnen auf den
// Bildschirm passiert aber ausschliesslich in lv_timer_handler().
void wifi_provision_loop(void);

// Grobzustand der Provisionierung, fuer die Statuszeile auf dem Boot-Screen
// (siehe main.cpp loop()). Bewusst feiner als ein einfaches "fertig oder
// nicht": SETUP_MODE (Captive-Portal-Fallback laeuft autonom weiter, aber es
// besteht noch keine WLAN-Verbindung) darf NICHT zum Wechsel auf das normale
// Dashboard fuehren - dafuer fehlen dann noch die Hardwaredaten.
typedef enum {
    WIFI_PROVISION_CONNECTING, // WiFi.begin()/Improv/BLE laufen noch
    WIFI_PROVISION_SETUP_MODE, // Captive-Portal-Fallback aktiv, noch nicht verbunden
    WIFI_PROVISION_CONNECTED,  // WL_CONNECTED, unabhaengig vom Verbindungsweg
} wifi_provision_phase_t;

wifi_provision_phase_t wifi_provision_get_phase(void);

// Loescht die gespeicherten WLAN-Zugangsdaten (WiFiManager-eigener NVS-
// Namespace "pulsewifi", getrennt von app_config - siehe wifi_provision.cpp)
// OHNE app_config anzufassen, im Gegensatz zu einem vollen Werksreset (siehe
// web_portal.cpp::handleFactoryReset(), das intern dieselbe Funktion nutzt).
// Startet NICHT selbst neu - der naechste Boot faellt mangels Zugangsdaten
// automatisch in den Captive-Portal-Setup-Modus (siehe wifi_provision_begin()).
void wifi_provision_forget(void);
