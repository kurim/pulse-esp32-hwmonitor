#pragma once
#include <stdbool.h>

// Verbindet mit dem konfigurierten WLAN oder oeffnet - falls das fehlschlaegt -
// einen offenen Setup-AP mit Captive-Portal. Startet anschliessend den
// Webserver (Config-Seite, /api/*, OTA).
void web_portal_begin(void);

// Muss regelmaessig aus loop() aufgerufen werden (DNS-Server-Polling im
// Setup-AP-Modus - ESPAsyncWebServer selbst braucht kein Polling).
void web_portal_loop(void);

bool        web_portal_ap_mode(void);   // true = Setup-AP aktiv
const char *web_portal_ip(void);        // aktuelle IP als String
const char *web_portal_ap_ssid(void);   // SSID des Setup-AP (im AP-Modus)

// Schaltet zur Laufzeit vom WLAN (STA) auf den offenen Setup-AP um, ohne
// Neustart und ohne die gespeicherten WLAN-Zugangsdaten zu loeschen.
void web_portal_force_ap(void);
