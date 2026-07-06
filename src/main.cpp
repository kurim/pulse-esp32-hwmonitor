#include <Arduino.h>
#include "shared_state.h"
#include "config_store.h"
#include "display/display_ui.h"
#include "net/web_portal.h"
#include "net/mqtt_handler.h"
#include "net/serial_handler.h"
#include "net/time_service.h"
#include "net/weather_service.h"

void setup(void)
{
    Serial.begin(115200);
    log_i("=== ESP32 Hardware-Monitor (Arduino/pioarduino %s) ===", FW_VERSION);

    // Konfiguration laden (Defaults -> Preferences/NVS-Overrides).
    config_store_init();
    config_set_defaults(&app_config);
    config_store_load(&app_config);

    // Display zuerst, damit beim Boot Status sichtbar ist (AP-SSID/IP etc.).
    display_ui_begin();

    web_portal_begin();      // WLAN verbinden bzw. Setup-AP + Webserver
    time_service_begin();    // SNTP + Zeitzone

    // Hardwaredaten-Quelle: nur einer der beiden Wege gleichzeitig aktiv,
    // umschaltbar per Dropdown im Webportal (app_config.hw_source).
    if (app_config.hw_source == HW_SOURCE_USB) {
        serial_handler_begin(); // Hardwaredaten per USB/Serial
    } else {
        mqtt_handler_begin();   // Hardwaredaten per MQTT
    }

    weather_service_begin(); // optionaler Wetter-Abruf (eigener FreeRTOS-Task)

    log_i("Setup abgeschlossen.");
}

void loop(void)
{
    // Anders als im esp-idf-Original (esp_lvgl_port/esp-mqtt liefen dort in
    // eigenen Tasks) laufen LVGL-Timer, WLAN-Portal-DNS und MQTT/Seriell-
    // Polling hier zentral im Arduino-loop() - keiner der Aufrufe blockiert
    // laenger als ein paar Millisekunden.
    display_ui_loop();
    web_portal_loop();

    if (app_config.hw_source == HW_SOURCE_USB) {
        serial_handler_loop();
    } else {
        mqtt_handler_loop();
    }
}
