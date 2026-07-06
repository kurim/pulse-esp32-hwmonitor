#include "shared_state.h"
#include <string.h>
#include <Arduino.h>

app_config_t   app_config;
hw_info_t      hw_info;
weather_info_t weather_info;
history_t      cpu_history;
history_t      gpu_history;
volatile bool  wifi_connected = false;
volatile bool  mqtt_connected = false;
volatile bool  serial_connected = false;

int64_t now_ms(void)
{
    return (int64_t)millis();
}

void config_set_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->hw_source = HW_SOURCE_MQTT; // = 0, explizit fuer Lesbarkeit
    cfg->mqtt_port = 1883;
    strcpy(cfg->mqtt_topic, "pulsemqtt/hwinfo");
    strcpy(cfg->tz, "CET-1CEST,M3.5.0,M10.5.0/3");   // Europe/Berlin
    strcpy(cfg->ntp_server, "pool.ntp.org");
    cfg->weather_enabled = false;
    strcpy(cfg->weather_city, "Duesseldorf,DE");
    strcpy(cfg->weather_units, "metric");
    cfg->brightness = 255;
    cfg->rotation   = 1;
    cfg->display_type = board_profile_default();
    cfg->standby_timeout_s = 120; // 2 Minuten
    cfg->color_invert = false;
    strcpy(cfg->language, "de");
    pin_override_set_defaults(&cfg->pin_overrides);
}

void history_push(history_t *h, float load, float temp, float power)
{
    for (int i = 0; i < HIST_LEN - 1; i++) {
        h->load[i]  = h->load[i + 1];
        h->temp[i]  = h->temp[i + 1];
        h->power[i] = h->power[i + 1];
    }
    h->load[HIST_LEN - 1]  = load;
    h->temp[HIST_LEN - 1]  = temp;
    h->power[HIST_LEN - 1] = power;
    if (h->count < HIST_LEN) h->count++;
}
