#include <Arduino.h>
#include <Wire.h>
#include "shared_state.h"
#include "config_store.h"
#include "display_ui.h"
#include "web_portal.h"
#include "mqtt_handler.h"
#include "weather_service.h"
#include "time_service.h"

// Globale Zustände (siehe shared_state.h)
AppConfig appConfig;
HWInfo hwInfo;
WeatherInfo weatherInfo;
History cpuHistory;
History gpuHistory;
bool wifiConnected = false;
bool mqttConnected = false;

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== CYD Hardware-Monitor ===");

  ConfigStore::begin();
  ConfigStore::load(appConfig);

  Wire.begin(33, 32); // typische I2C-Pins bei kapazitiven CYD-Varianten (SDA=33, SCL=32)
  Serial.println("I2C-Scan...");
  for (byte addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
    {
      Serial.printf("I2C-Gerät gefunden: 0x%02X\n", addr);
    }
  }
  // Display zuerst, damit beim Boot Status sichtbar ist (z.B. AP-SSID/IP)
  DisplayUI::begin();

  WebPortal::begin();      // verbindet WLAN bzw. startet Config-AP + Webserver
  TimeService::begin();    // NTP + Zeitzone
  MqttHandler::begin();    // MQTT-Verbindung zu Hardwaredaten
  WeatherService::begin(); // optionaler erster Wetter-Abruf

  Serial.println("Setup abgeschlossen.");
}

void loop()
{
  WebPortal::loop();
  MqttHandler::loop();
  WeatherService::loop();
  DisplayUI::loop();
}
