#include "time_service.h"
#include "shared_state.h"
#include <Arduino.h>

void time_service_begin(void)
{
    // configTzTime setzt sowohl die POSIX-TZ (z.B. "CET-1CEST,M3.5.0,M10.5.0/3")
    // als auch den SNTP-Server und startet SNTP - Arduino-Aequivalent zu
    // setenv("TZ",...)+tzset()+esp_sntp_*() im esp-idf-Original.
    configTzTime(app_config.tz, app_config.ntp_server);

    // TEMP-DEBUG: Nutzer berichtet Uhrzeit auf dem Standby-Screen ist um 2h
    // falsch (genau der Unterschied zwischen UTC und CEST/Sommerzeit) - hier
    // pruefen ob app_config.tz tatsaechlich den erwarteten Wert enthaelt
    // (z.B. durch einen alten/leeren NVS-Wert aus frueheren Tests ueberschrieben)
    // statt zu raten. Nach der Fehlersuche wieder entfernen.
    log_i("TEMP-DEBUG time_service_begin(): tz='%s' ntp_server='%s'",
          app_config.tz, app_config.ntp_server);
}
