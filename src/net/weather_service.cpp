#include "weather_service.h"
#include "shared_state.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define FETCH_INTERVAL_MS (10 * 60 * 1000)

const char *weather_wind_compass(int deg)
{
    static const char *dirs_de[] = { "N", "NO", "O", "SO", "S", "SW", "W", "NW" };
    static const char *dirs_en[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    const char **dirs = (strcmp(app_config.language, "en") == 0) ? dirs_en : dirs_de;
    int idx = ((deg % 360) + 360) % 360;
    idx = (idx + 22) / 45; // auf 8 Sektoren runden (je 45 Grad)
    return dirs[idx % 8];
}

// Loggt Erfolg/Fehlschlag explizit - ohne das blieb weather_info.valid bei
// einem Fehler einfach false, ohne erkennbar zu machen ob die Anfrage nie
// rausging, der Server einen Fehler zurueckgab (z.B. falscher API-Key/
// Stadtname -> HTTP 401/404) oder die Antwort nicht wie erwartet aussah.
static void fetch_weather(void)
{
    if (!app_config.weather_enabled || strlen(app_config.weather_api_key) == 0) return;
    if (!wifi_connected) return;

    const char *owm_lang = (strcmp(app_config.language, "en") == 0) ? "en" : "de";
    char url[320];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s&lang=%s",
             app_config.weather_city, app_config.weather_api_key, app_config.weather_units, owm_lang);

    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(url)) {
        log_e("http.begin() fehlgeschlagen fuer Stadt='%s'", app_config.weather_city);
        return;
    }

    int status = http.GET();
    log_i("GET Wetter-API -> HTTP %d (Stadt='%s')", status, app_config.weather_city);
    if (status == 200) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (err == DeserializationError::Ok) {
            JsonVariant temp = doc["main"]["temp"];
            if (!temp.isNull()) {
                weather_info.temp_c = temp.as<float>();
                weather_info.valid  = true;
                weather_info.last_fetch_ms = now_ms();

                weather_info.feels_like_c = doc["main"]["feels_like"] | weather_info.feels_like_c;
                weather_info.humidity     = doc["main"]["humidity"]   | weather_info.humidity;

                JsonVariant speed = doc["wind"]["speed"];
                if (!speed.isNull()) {
                    float s = speed.as<float>();
                    // OpenWeatherMap liefert m/s bei "metric", mph bei "imperial".
                    weather_info.wind_speed =
                        (strcmp(app_config.weather_units, "metric") == 0) ? s * 3.6f : s;
                }
                weather_info.wind_deg = doc["wind"]["deg"] | weather_info.wind_deg;

                weather_info.rain_1h = doc["rain"]["1h"] | 0.0f;

                const char *desc = doc["weather"][0]["description"];
                const char *icon = doc["weather"][0]["icon"];
                if (desc) strlcpy(weather_info.description, desc, sizeof(weather_info.description));
                if (icon) strlcpy(weather_info.icon, icon, sizeof(weather_info.icon));

                log_i("Wetter aktualisiert: %.1fC, %d%%, Wind %.1f", weather_info.temp_c,
                      weather_info.humidity, weather_info.wind_speed);
            } else {
                log_e("Antwort ohne main.temp - unerwartete JSON-Struktur");
            }
        } else {
            log_e("JSON-Parse-Fehler: %s", err.c_str());
        }
    } else {
        String body = http.getString();
        log_e("HTTP-Fehler %d, Antwort: %s", status, body.c_str());
    }
    http.end();
}

static void weather_task(void *arg)
{
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
