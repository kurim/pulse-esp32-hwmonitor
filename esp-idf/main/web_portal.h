#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Verbindet mit dem konfigurierten WLAN oder oeffnet - falls das fehlschlaegt -
// einen offenen Setup-AP mit Captive-Portal. Startet anschliessend den
// Webserver (Config-Seite, /api/*, OTA).
void web_portal_begin(void);

bool        web_portal_ap_mode(void);   // true = Setup-AP aktiv
const char *web_portal_ip(void);        // aktuelle IP als String
const char *web_portal_ap_ssid(void);   // SSID des Setup-AP (im AP-Modus)

#ifdef __cplusplus
}
#endif
