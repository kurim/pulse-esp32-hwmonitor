#pragma once

#include <ESPAsyncWebServer.h>

// Einziger AsyncWebServer der Firmware (Port 80). Lazy-init Singleton, damit
// sowohl web_portal.cpp (Dashboard/REST-API) als auch wifi_provision.cpp
// (WiFiManager-Captive-Portal-Routen) dieselbe Instanz nutzen - zwei
// AsyncWebServer auf Port 80 wuerden beim .begin() kollidieren.
AsyncWebServer &web_portal_server(void);

// Registriert die Dashboard- und REST-API-Routen und startet den Server
// (.begin()). Einmalig in setup() aufrufen, NACH wifi_provision_begin()
// (das ruft WiFi.mode(WIFI_STA) auf - AsyncTCP braucht das bereits
// angelegte Netif, bevor .begin() seine interne Queue anlegen kann, sonst
// "assert failed: xQueueSemaphoreTake" noch in setup()). wifi_provision.cpp
// haengt seine eigenen Routen vorher schon an dieselbe Instanz (Registrieren
// vor .begin() ist unproblematisch).
void web_portal_begin(void);
