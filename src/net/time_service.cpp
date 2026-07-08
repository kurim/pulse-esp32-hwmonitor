#include "time_service.h"
#include "shared_state.h"
#include <Arduino.h>

void time_service_begin(void)
{
    // configTzTime setzt sowohl die POSIX-TZ (z.B. "CET-1CEST,M3.5.0,M10.5.0/3")
    // als auch den SNTP-Server und startet SNTP - Arduino-Aequivalent zu
    // setenv("TZ",...)+tzset()+esp_sntp_*() im esp-idf-Original.
    configTzTime(app_config.tz, app_config.ntp_server);
}
