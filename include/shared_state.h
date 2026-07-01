#pragma once
#include <Arduino.h>

#define FW_VERSION "1.2.0"

// ----------------------------------------------------------------
// Konfiguration (wird per Webportal gesetzt, in NVS persistiert)
// ----------------------------------------------------------------
struct AppConfig {
  char wifi_ssid[33]    = "";
  char wifi_pass[65]    = "";

  char mqtt_host[65]    = "";
  uint16_t mqtt_port    = 1883;
  char mqtt_user[33]    = "";
  char mqtt_pass[65]    = "";
  char mqtt_topic[65]   = "cyd/hwinfo";   // Basis-Topic, JSON-Payload

  char tz[65]           = "CET-1CEST,M3.5.0,M10.5.0/3"; // Europe/Berlin
  char ntp_server[65]   = "pool.ntp.org";

  bool weather_enabled  = false;
  char weather_api_key[41] = "";
  char weather_city[65]    = "Duesseldorf,DE";
  char weather_units[9]    = "metric";   // metric | imperial

  uint8_t brightness    = 255;  // 0-255 (PWM)
  uint8_t rotation      = 1;    // TFT_eSPI rotation 0-3
};

// ----------------------------------------------------------------
// Live-Hardwarewerte (per MQTT vom PC-Client geliefert)
// ----------------------------------------------------------------
struct HWInfo {
  float cpuLoad  = 0;   // %
  float gpuLoad  = 0;   // %
  float cpuTemp  = 0;   // °C
  float gpuTemp  = 0;   // °C
  float cpuPower = 0;   // W
  float gpuPower = 0;   // W
  unsigned long lastUpdateMs = 0;
  bool everReceived = false;
};

struct WeatherInfo {
  bool valid = false;
  float tempC = 0;
  String description = "";
  String icon = "";       // OpenWeatherMap icon code
  unsigned long lastFetchMs = 0;
};

// ----------------------------------------------------------------
// Ringpuffer für Verlaufsdiagramme
// ----------------------------------------------------------------
#define HIST_LEN 60   // 60 Samples (z.B. 1 Wert/Sek = 1 Min Verlauf)

struct History {
  float load[HIST_LEN]  = {0};
  float temp[HIST_LEN]  = {0};
  float power[HIST_LEN] = {0};
  uint8_t count = 0; // wie viele gültige Werte bereits vorhanden

  void push(float l, float t, float p) {
    for (int i = 0; i < HIST_LEN - 1; i++) {
      load[i]  = load[i + 1];
      temp[i]  = temp[i + 1];
      power[i] = power[i + 1];
    }
    load[HIST_LEN - 1]  = l;
    temp[HIST_LEN - 1]  = t;
    power[HIST_LEN - 1] = p;
    if (count < HIST_LEN) count++;
  }
};

// Globale Instanzen (definiert in main.cpp)
extern AppConfig appConfig;
extern HWInfo hwInfo;
extern WeatherInfo weatherInfo;
extern History cpuHistory;
extern History gpuHistory;
extern bool wifiConnected;
extern bool mqttConnected;
