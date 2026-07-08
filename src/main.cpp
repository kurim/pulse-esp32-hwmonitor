// LVGL9_MIN_TEST (siehe platformio.ini env:esp32_lvgl9_min_test) ersetzt
// setup()/loop() komplett durch den isolierten LVGL9-Minimaltest in
// lvgl9_min_test.cpp - die eigentliche App (unten) wird dafuer ausgeblendet.
#ifndef LVGL9_MIN_TEST

#include <Arduino.h>
#include "shared_state.h"
#include "config_store.h"
#include "display/display_ui.h"
#include "net/web_portal.h"
#include "net/mqtt_handler.h"
#include "net/serial_handler.h"
#include "net/time_service.h"
#include "net/weather_service.h"

// Nur einmal beim Boot aus setup() aufgerufen, NICHT aus loop() - loop()
// laeuft in einer Endlosschleife (tausende Male/Sekunde), ein Aufruf dort
// wuerde das Banner ununterbrochen spammen statt es einmal pro Neustart zu
// zeigen.
static void bootlogo(void)
{
    log_i("   ___  _ __ __    ___  ___");
    log_i("  / o |/// // /  ,' _/ / _/");
    log_i(" / _,'/ U // /_ _\\ `. / _/ ");
    log_i("/_/   \\_,'/___//___,'/___/ ");
    log_i("Pulse ESP32 Hardware-Monitor (Version %s)", FW_VERSION);
}

void setup(void)
{
    Serial.begin(115200);
    // Kurze Wartezeit, damit ein per "pio device monitor" erst NACH dem
    // Flashen geoeffneter serieller Monitor Zeit hat, sich (re-)zu verbinden,
    // bevor das Boot-Banner geschrieben wird - sonst ist es beim Verbinden
    // oft schon vorbei. Zuverlaessiger/einfacher als auf eine (bei UART-USB-
    // Bruecken wie CP2102/CH340 ohnehin nicht vorhandene) DTR-Handshake-
    // Erkennung zu warten. Am besten trotzdem gleich mit
    // "pio run -t upload -t monitor" flashen UND monitoren, statt beides als
    // getrennte Befehle nacheinander auszufuehren.
    delay(300);
    bootlogo();

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

#endif // LVGL9_MIN_TEST
