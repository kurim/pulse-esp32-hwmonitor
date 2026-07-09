#pragma once
#include <stdbool.h>

// Startet WiFiManager (tzapu/WiFiManager, nicht-blockierend): verbindet mit
// zuvor gespeicherten Zugangsdaten oder oeffnet einen offenen Setup-AP mit
// Captive-Portal. Kehrt sofort zurueck, unabhaengig vom Ausgang - der
// eigentliche Verbindungsaufbau/Portal-Betrieb passiert in web_portal_loop().
// Der eigene Webserver (Config-Seite, /api/*, OTA) startet NICHT hier,
// sondern erst in web_portal_loop(), sobald eine WLAN-Verbindung steht.
void web_portal_begin(void);

// Muss regelmaessig aus loop() aufgerufen werden: bedient WiFiManagers
// Setup-Portal (DNS/Webserver) und startet einmalig unseren eigenen
// Webserver, sobald WiFi.status() == WL_CONNECTED ist.
void web_portal_loop(void);

bool        web_portal_ap_mode(void);   // true = WiFiManager-Setup-Portal aktiv
const char *web_portal_ip(void);        // aktuelle IP als String (AP- oder STA-IP)
const char *web_portal_ap_ssid(void);   // SSID des Setup-AP

// Loescht die von WiFiManager gespeicherten WLAN-Zugangsdaten und startet
// neu - das Geraet oeffnet danach beim Boot automatisch wieder das
// Setup-Portal. Anders als zuvor kein Live-Wechsel ohne Neustart mehr,
// da WiFiManager keinen Wechsel in ein bereits laufendes Portal ohne
// eigenen (mit unserem AsyncWebServer kollidierenden) Webserver anbietet.
void web_portal_force_ap(void);
