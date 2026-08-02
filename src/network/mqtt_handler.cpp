#include "mqtt_handler.h"
#include "shared_state.h"
#include "hw_data.h"
#include <WiFi.h>
#include <espMqttClient.h>

// espMqttClient (bertmelis) statt PubSubClient - siehe CLAUDE.md
// "Bibliotheks-Entscheidungen" (groesserer Buffer, non-blocking connect()
// dank interner Task/State-Machine, kein separates .loop() noetig).
static espMqttClient s_client;
static uint32_t      s_last_reconnect_attempt_ms;
static char          s_client_id[32];
static bool          s_enabled;

static void onMqttConnect(bool /*sessionPresent*/)
{
    mqtt_connected = true;
    s_client.subscribe(app_config.mqtt_topic, 0); // QoS 0, siehe CLAUDE.md
}

static void onMqttDisconnect(espMqttClientTypes::DisconnectReason /*reason*/)
{
    mqtt_connected = false;
}

static void onMqttMessage(const espMqttClientTypes::MessageProperties & /*properties*/,
                           const char * /*topic*/, const uint8_t *payload, size_t len,
                           size_t /*index*/, size_t /*total*/)
{
    hw_data_apply_json((const char *)payload, (int)len);
    hw_rx_count = hw_rx_count + 1;
}

void mqtt_handler_begin(void)
{
    // Exklusiv zu HW_SOURCE_USB (siehe serial_handler.cpp, hw_source_t in
    // shared_state.h) - ohne diese Pruefung verbindet sich der MQTT-Client
    // auch bei hw_source==USB, sobald ein mqtt_host konfiguriert ist, und
    // onMqttMessage() erhoeht denselben geteilten hw_rx_count wie der
    // Serial-Pfad. Ergebnis: "USB"-Ticks im Dashboard steigen tatsaechlich
    // durch MQTT-Nachrichten, waehrend usb.connected (serial_connected)
    // korrekt false bleibt, weil nie echte Bytes auf Serial ankommen.
    if (app_config.hw_source != HW_SOURCE_MQTT || strlen(app_config.mqtt_host) == 0) {
        s_enabled = false;
        return;
    }
    s_enabled = true;

    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_client_id, sizeof(s_client_id), "esp32hwmon-%02x%02x%02x", mac[3], mac[4], mac[5]);

    s_client.onConnect(onMqttConnect);
    s_client.onDisconnect(onMqttDisconnect);
    s_client.onMessage(onMqttMessage);
    s_client.setClientId(s_client_id);
    s_client.setServer(app_config.mqtt_host, app_config.mqtt_port);
    if (strlen(app_config.mqtt_user) > 0) {
        s_client.setCredentials(app_config.mqtt_user, app_config.mqtt_pass);
    }
}

void mqtt_handler_loop(void)
{
    if (!s_enabled || !wifi_connected || s_client.connected()) return;

    // Alle 5s ein Reconnect-Versuch - connect() blockt hier nicht (siehe oben).
    uint32_t now = millis();
    if (now - s_last_reconnect_attempt_ms >= 5000) {
        s_last_reconnect_attempt_ms = now;
        s_client.connect();
    }
}
