#include "mqtt_handler.h"
#include "shared_state.h"
#include "hw_data.h"
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient   s_tcp;
static PubSubClient s_client(s_tcp);
static uint32_t     s_last_reconnect_attempt_ms;
static char         s_client_id[32];
static bool         s_enabled;

static void mqtt_callback(char *topic, uint8_t *payload, unsigned int length)
{
    hw_data_apply_json((const char *)payload, (int)length);
}

static bool mqtt_try_connect(void)
{
    bool ok;
    if (strlen(app_config.mqtt_user) > 0) {
        ok = s_client.connect(s_client_id, app_config.mqtt_user, app_config.mqtt_pass);
    } else {
        ok = s_client.connect(s_client_id);
    }
    if (ok) {
        s_client.subscribe(app_config.mqtt_topic);
        mqtt_connected = true;
    }
    return ok;
}

void mqtt_handler_begin(void)
{
    if (strlen(app_config.mqtt_host) == 0) {
        s_enabled = false;
        return;
    }
    s_enabled = true;

    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_client_id, sizeof(s_client_id), "esp32hwmon-%02x%02x%02x", mac[3], mac[4], mac[5]);

    s_client.setServer(app_config.mqtt_host, app_config.mqtt_port);
    s_client.setBufferSize(1024);
    s_client.setCallback(mqtt_callback);

    mqtt_try_connect();
}

void mqtt_handler_loop(void)
{
    if (!s_enabled) return;

    if (!s_client.connected()) {
        mqtt_connected = false;
        uint32_t now = millis();
        // Alle 5s einen Reconnect-Versuch, kein Blockieren des loop().
        if (now - s_last_reconnect_attempt_ms >= 5000) {
            s_last_reconnect_attempt_ms = now;
            mqtt_try_connect();
        }
        return;
    }
    s_client.loop();
}
