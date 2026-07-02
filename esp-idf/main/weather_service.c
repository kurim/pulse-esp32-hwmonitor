#include "weather_service.h"
#include "shared_state.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "weather";

#define FETCH_INTERVAL_MS (10 * 60 * 1000)
#define RESP_BUF_SIZE     2048

static char s_resp[RESP_BUF_SIZE];
static int  s_resp_len;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (s_resp_len + evt->data_len < RESP_BUF_SIZE - 1) {
            memcpy(s_resp + s_resp_len, evt->data, evt->data_len);
            s_resp_len += evt->data_len;
            s_resp[s_resp_len] = '\0';
        }
    }
    return ESP_OK;
}

// Wandelt eine Windrichtung in Grad in eine 8-Punkte-Kompassangabe um.
const char *weather_wind_compass(int deg)
{
    static const char *dirs[] = { "N", "NO", "O", "SO", "S", "SW", "W", "NW" };
    int idx = ((deg % 360) + 360) % 360;
    idx = (idx + 22) / 45; // auf 8 Sektoren runden (je 45 Grad)
    return dirs[idx % 8];
}

static void fetch_weather(void)
{
    if (!app_config.weather_enabled || strlen(app_config.weather_api_key) == 0) return;
    if (!wifi_connected) return;

    char url[320];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s&lang=de",
             app_config.weather_city, app_config.weather_api_key, app_config.weather_units);

    s_resp_len = 0;
    s_resp[0]  = '\0';

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_event,
        .timeout_ms    = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err == ESP_OK && status == 200) {
        cJSON *root = cJSON_Parse(s_resp);
        if (root) {
            cJSON *main = cJSON_GetObjectItem(root, "main");
            cJSON *temp = main ? cJSON_GetObjectItem(main, "temp") : NULL;
            if (cJSON_IsNumber(temp)) {
                weather_info.temp_c = (float)temp->valuedouble;
                weather_info.valid  = true;
                weather_info.last_fetch_ms = now_ms();

                cJSON *feels = cJSON_GetObjectItem(main, "feels_like");
                if (cJSON_IsNumber(feels)) weather_info.feels_like_c = (float)feels->valuedouble;
                cJSON *hum = cJSON_GetObjectItem(main, "humidity");
                if (cJSON_IsNumber(hum)) weather_info.humidity = hum->valueint;

                cJSON *wind = cJSON_GetObjectItem(root, "wind");
                if (wind) {
                    cJSON *speed = cJSON_GetObjectItem(wind, "speed");
                    if (cJSON_IsNumber(speed)) {
                        float s = (float)speed->valuedouble;
                        // OpenWeatherMap liefert m/s bei "metric", mph bei "imperial".
                        weather_info.wind_speed =
                            (strcmp(app_config.weather_units, "metric") == 0) ? s * 3.6f : s;
                    }
                    cJSON *deg = cJSON_GetObjectItem(wind, "deg");
                    if (cJSON_IsNumber(deg)) weather_info.wind_deg = deg->valueint;
                }

                cJSON *rain = cJSON_GetObjectItem(root, "rain");
                weather_info.rain_1h = 0.0f;
                if (rain) {
                    cJSON *r1h = cJSON_GetObjectItem(rain, "1h");
                    if (cJSON_IsNumber(r1h)) weather_info.rain_1h = (float)r1h->valuedouble;
                }

                cJSON *arr = cJSON_GetObjectItem(root, "weather");
                cJSON *w0  = arr ? cJSON_GetArrayItem(arr, 0) : NULL;
                if (w0) {
                    cJSON *desc = cJSON_GetObjectItem(w0, "description");
                    cJSON *icon = cJSON_GetObjectItem(w0, "icon");
                    if (cJSON_IsString(desc))
                        strlcpy(weather_info.description, desc->valuestring, sizeof(weather_info.description));
                    if (cJSON_IsString(icon))
                        strlcpy(weather_info.icon, icon->valuestring, sizeof(weather_info.icon));
                }
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG, "JSON parse fehlgeschlagen");
        }
    } else {
        ESP_LOGW(TAG, "HTTP Fehler: err=%s status=%d", esp_err_to_name(err), status);
    }
    esp_http_client_cleanup(client);
}

static void weather_task(void *arg)
{
    // Erster Abruf, sobald WLAN steht.
    for (;;) {
        if (app_config.weather_enabled) {
            if (weather_info.last_fetch_ms == 0 ||
                now_ms() - weather_info.last_fetch_ms > FETCH_INTERVAL_MS) {
                fetch_weather();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30 * 1000)); // alle 30 s pruefen, Abruf nur alle 10 Min
    }
}

void weather_service_begin(void)
{
    weather_info.valid = false;
    xTaskCreate(weather_task, "weather", 6144, NULL, 4, NULL);
}
