#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config_store";
static const char *NS  = "cydcfg";

void config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// Liest einen String-Schluessel; laesst dst unveraendert, wenn nicht vorhanden.
static void get_str(nvs_handle_t h, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;
    esp_err_t err = nvs_get_str(h, key, dst, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "get_str %s: %s", key, esp_err_to_name(err));
    }
}

void config_store_load(app_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Kein gespeichertes Config-Namespace - nutze Defaults");
        return;
    }

    get_str(h, "wifi_ssid",  cfg->wifi_ssid,  sizeof(cfg->wifi_ssid));
    get_str(h, "wifi_pass",  cfg->wifi_pass,  sizeof(cfg->wifi_pass));
    get_str(h, "mqtt_host",  cfg->mqtt_host,  sizeof(cfg->mqtt_host));
    nvs_get_u16(h, "mqtt_port", &cfg->mqtt_port);
    get_str(h, "mqtt_user",  cfg->mqtt_user,  sizeof(cfg->mqtt_user));
    get_str(h, "mqtt_pass",  cfg->mqtt_pass,  sizeof(cfg->mqtt_pass));
    get_str(h, "mqtt_topic", cfg->mqtt_topic, sizeof(cfg->mqtt_topic));
    get_str(h, "tz",         cfg->tz,         sizeof(cfg->tz));
    get_str(h, "ntp_server", cfg->ntp_server, sizeof(cfg->ntp_server));

    uint8_t w_en = cfg->weather_enabled ? 1 : 0;
    nvs_get_u8(h, "w_en", &w_en);
    cfg->weather_enabled = (w_en != 0);

    get_str(h, "w_key",   cfg->weather_api_key, sizeof(cfg->weather_api_key));
    get_str(h, "w_city",  cfg->weather_city,    sizeof(cfg->weather_city));
    get_str(h, "w_units", cfg->weather_units,   sizeof(cfg->weather_units));

    nvs_get_u8(h, "bright", &cfg->brightness);
    nvs_get_u8(h, "rot",    &cfg->rotation);

    uint8_t disp_type = (uint8_t)cfg->display_type;
    nvs_get_u8(h, "disp_type", &disp_type);
    cfg->display_type = (display_type_t)disp_type;

    nvs_get_u16(h, "standby_s", &cfg->standby_timeout_s);

    uint8_t inv = cfg->color_invert ? 1 : 0;
    nvs_get_u8(h, "inv", &inv);
    cfg->color_invert = (inv != 0);

    nvs_close(h);
}

void config_store_save(const app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (rw) fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_str(h, "wifi_ssid",  cfg->wifi_ssid);
    nvs_set_str(h, "wifi_pass",  cfg->wifi_pass);
    nvs_set_str(h, "mqtt_host",  cfg->mqtt_host);
    nvs_set_u16(h, "mqtt_port",  cfg->mqtt_port);
    nvs_set_str(h, "mqtt_user",  cfg->mqtt_user);
    nvs_set_str(h, "mqtt_pass",  cfg->mqtt_pass);
    nvs_set_str(h, "mqtt_topic", cfg->mqtt_topic);
    nvs_set_str(h, "tz",         cfg->tz);
    nvs_set_str(h, "ntp_server", cfg->ntp_server);
    nvs_set_u8 (h, "w_en",       cfg->weather_enabled ? 1 : 0);
    nvs_set_str(h, "w_key",      cfg->weather_api_key);
    nvs_set_str(h, "w_city",     cfg->weather_city);
    nvs_set_str(h, "w_units",    cfg->weather_units);
    nvs_set_u8 (h, "bright",     cfg->brightness);
    nvs_set_u8 (h, "rot",        cfg->rotation);
    nvs_set_u8 (h, "disp_type",  (uint8_t)cfg->display_type);
    nvs_set_u16(h, "standby_s",  cfg->standby_timeout_s);
    nvs_set_u8 (h, "inv",        cfg->color_invert ? 1 : 0);

    err = nvs_commit(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "nvs_commit: %s", esp_err_to_name(err));
    nvs_close(h);
}
