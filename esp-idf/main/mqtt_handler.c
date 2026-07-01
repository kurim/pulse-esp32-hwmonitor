#include "mqtt_handler.h"
#include "shared_state.h"
#include "mqtt_client.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client;
static int64_t s_last_history_push;
#define HISTORY_PUSH_INTERVAL_MS 1000

static void parse_payload(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse error");
        return;
    }

    cJSON *v;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "cpu_load"))))  hw_info.cpu_load  = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "cpu_temp"))))  hw_info.cpu_temp  = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "cpu_power")))) hw_info.cpu_power = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "gpu_load"))))  hw_info.gpu_load  = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "gpu_temp"))))  hw_info.gpu_temp  = (float)v->valuedouble;
    if (cJSON_IsNumber((v = cJSON_GetObjectItem(root, "gpu_power")))) hw_info.gpu_power = (float)v->valuedouble;
    cJSON_Delete(root);

    hw_info.last_update_ms = now_ms();
    hw_info.ever_received  = true;

    // Verlauf hoechstens 1x/Sek aktualisieren (unabhaengig von Publish-Rate).
    if (now_ms() - s_last_history_push >= HISTORY_PUSH_INTERVAL_MS) {
        s_last_history_push = now_ms();
        history_push(&cpu_history, hw_info.cpu_load, hw_info.cpu_temp, hw_info.cpu_power);
        history_push(&gpu_history, hw_info.gpu_load, hw_info.gpu_temp, hw_info.gpu_power);
    }
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_connected = true;
            esp_mqtt_client_subscribe(s_client, app_config.mqtt_topic, 0);
            ESP_LOGI(TAG, "verbunden, abonniert: %s", app_config.mqtt_topic);
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            break;
        case MQTT_EVENT_DATA:
            parse_payload(event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

void mqtt_handler_begin(void)
{
    if (strlen(app_config.mqtt_host) == 0) {
        ESP_LOGI(TAG, "Kein Broker konfiguriert - MQTT deaktiviert");
        return;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%u", app_config.mqtt_host, app_config.mqtt_port);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    static char client_id[32];
    snprintf(client_id, sizeof(client_id), "CYD-%02x%02x%02x", mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri       = uri,
        .credentials.client_id    = client_id,
        .buffer.size              = 1024,
    };
    if (strlen(app_config.mqtt_user) > 0) {
        cfg.credentials.username = app_config.mqtt_user;
        cfg.credentials.authentication.password = app_config.mqtt_pass;
    }

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init fehlgeschlagen");
        return;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "Client gestartet: %s (id %s)", uri, client_id);
}
