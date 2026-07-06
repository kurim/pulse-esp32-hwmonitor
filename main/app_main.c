#include "shared_state.h"
#include "config_store.h"
#include "display_ui.h"
#include "web_portal.h"
#include "mqtt_handler.h"
#include "serial_handler.h"
#include "time_service.h"
#include "weather_service.h"

#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "#####    #    #        #####  ######");
    ESP_LOGI(TAG, "#    #   #    #   #     #     #     ");
    ESP_LOGI(TAG, "#####    #    #   #      #### #####");
    ESP_LOGI(TAG, "#         #    #  #         # #     ");
    ESP_LOGI(TAG, "#          #### ######  #####  ######");
    ESP_LOGI(TAG, "=== Pulse ESP32 Hardware-Monitor (Version %s) ===", FW_VERSION);

    // Konfiguration laden (Defaults -> NVS-Overrides).
    config_store_init();
    config_set_defaults(&app_config);
    config_store_load(&app_config);

    // Netzwerk-Grundgeruest (einmalig).
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Display zuerst, damit beim Boot Status sichtbar ist (AP-SSID/IP etc.).
    display_ui_begin();

    web_portal_begin();      // WLAN verbinden bzw. Setup-AP + Webserver
    time_service_begin();    // SNTP + Zeitzone

    // Hardwaredaten-Quelle: nur einer der beiden Wege gleichzeitig aktiv,
    // umschaltbar per Dropdown im Webportal (app_config.hw_source).
    if (app_config.hw_source == HW_SOURCE_USB) {
        serial_handler_begin(); // Hardwaredaten per USB/UART0
    } else {
        mqtt_handler_begin();   // Hardwaredaten per MQTT
    }

    weather_service_begin(); // optionaler Wetter-Abruf

    ESP_LOGI(TAG, "Setup abgeschlossen.");
    // Keine Endlosschleife noetig: LVGL-Task, esp-mqtt, Wetter-Task und der
    // Webserver laufen eigenstaendig weiter.
}
