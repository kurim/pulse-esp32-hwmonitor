#include "time_service.h"
#include "shared_state.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <stdlib.h>

static const char *TAG = "time";

void time_service_begin(void)
{
    // POSIX-Zeitzone setzen (z.B. "CET-1CEST,M3.5.0,M10.5.0/3").
    setenv("TZ", app_config.tz, 1);
    tzset();

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, app_config.ntp_server);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP gestartet (%s), TZ=%s", app_config.ntp_server, app_config.tz);
}
