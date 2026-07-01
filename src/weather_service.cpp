#include "weather_service.h"
#include "shared_state.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const unsigned long FETCH_INTERVAL_MS = 10UL * 60UL * 1000UL; // 10 Min

static void fetchWeather() {
  if (!appConfig.weather_enabled || strlen(appConfig.weather_api_key) == 0) return;
  if (!wifiConnected) return;

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(appConfig.weather_city) +
               "&appid=" + String(appConfig.weather_api_key) +
               "&units=" + String(appConfig.weather_units) +
               "&lang=de";

  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      weatherInfo.tempC = doc["main"]["temp"] | 0.0f;
      weatherInfo.description = String((const char*)(doc["weather"][0]["description"] | ""));
      weatherInfo.icon = String((const char*)(doc["weather"][0]["icon"] | ""));
      weatherInfo.valid = true;
      weatherInfo.lastFetchMs = millis();
    } else {
      Serial.printf("Wetter JSON Fehler: %s\n", err.c_str());
    }
  } else {
    Serial.printf("Wetter HTTP Fehler: %d\n", code);
  }
  http.end();
}

namespace WeatherService {

void begin() {
  weatherInfo.valid = false;
  if (appConfig.weather_enabled) fetchWeather();
}

void loop() {
  if (!appConfig.weather_enabled) return;
  if (millis() - weatherInfo.lastFetchMs > FETCH_INTERVAL_MS || weatherInfo.lastFetchMs == 0) {
    fetchWeather();
  }
}

} // namespace WeatherService
