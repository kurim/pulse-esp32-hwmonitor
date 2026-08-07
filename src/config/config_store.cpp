#include "config/config_store.h"
#include <Preferences.h>
#include <string.h>

static Preferences s_prefs;
static const char *NS = "cydcfg";

void config_store_init(void)
{
    // Preferences kapselt nvs_flash_init() inkl. Erase-bei-Versionswechsel
    // intern - kein expliziter Aufruf hier noetig.
}

// Liest einen String-Schluessel; laesst dst unveraendert, wenn nicht vorhanden.
static void get_str(Preferences &p, const char *key, char *dst, size_t dst_size)
{
    if (p.isKey(key)) {
        p.getString(key, dst, dst_size);
    }
}

void config_store_load(app_config_t *cfg)
{
    if (!s_prefs.begin(NS, /*readOnly=*/false)) {
        return;
    }

    get_str(s_prefs, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
    get_str(s_prefs, "wifi_pass", cfg->wifi_pass, sizeof(cfg->wifi_pass));

    cfg->hw_source = (hw_source_t)s_prefs.getUChar("hw_src", (uint8_t)cfg->hw_source);

    get_str(s_prefs, "mqtt_host",  cfg->mqtt_host,  sizeof(cfg->mqtt_host));
    cfg->mqtt_port = s_prefs.getUShort("mqtt_port", cfg->mqtt_port);
    get_str(s_prefs, "mqtt_user",  cfg->mqtt_user,  sizeof(cfg->mqtt_user));
    get_str(s_prefs, "mqtt_pass",  cfg->mqtt_pass,  sizeof(cfg->mqtt_pass));
    get_str(s_prefs, "mqtt_topic", cfg->mqtt_topic, sizeof(cfg->mqtt_topic));
    get_str(s_prefs, "tz",         cfg->tz,         sizeof(cfg->tz));
    get_str(s_prefs, "ntp_server", cfg->ntp_server, sizeof(cfg->ntp_server));

    cfg->weather_enabled = s_prefs.getUChar("w_en", cfg->weather_enabled ? 1 : 0) != 0;
    get_str(s_prefs, "w_key",   cfg->weather_api_key, sizeof(cfg->weather_api_key));
    get_str(s_prefs, "w_city",  cfg->weather_city,    sizeof(cfg->weather_city));
    get_str(s_prefs, "w_units", cfg->weather_units,   sizeof(cfg->weather_units));

    cfg->brightness = s_prefs.getUChar("bright", cfg->brightness);
    cfg->rotation   = s_prefs.getUChar("rot",    cfg->rotation);
    cfg->display_type = (display_type_t)s_prefs.getUChar("disp_type", (uint8_t)cfg->display_type);
    cfg->standby_timeout_s = s_prefs.getUShort("standby_s", cfg->standby_timeout_s);
    cfg->cpu_arc_color = s_prefs.getUInt("cpu_col", cfg->cpu_arc_color);
    cfg->gpu_arc_color = s_prefs.getUInt("gpu_col", cfg->gpu_arc_color);

    get_str(s_prefs, "language", cfg->language, sizeof(cfg->language));

    // Pin-Overrides als Blob - Groessenpruefung, damit ein spaeter geaendertes
    // struct pin_override_t nicht stillschweigend falsch interpretiert wird.
    if (s_prefs.isKey("pin_ov")) {
        size_t len = s_prefs.getBytesLength("pin_ov");
        if (len == sizeof(cfg->pin_overrides)) {
            s_prefs.getBytes("pin_ov", &cfg->pin_overrides, len);
        } else {
            pin_override_set_defaults(&cfg->pin_overrides);
        }
    }
    if (s_prefs.isKey("pin_ov_i2c")) {
        size_t len = s_prefs.getBytesLength("pin_ov_i2c");
        if (len == sizeof(cfg->mono_i2c_pins)) {
            s_prefs.getBytes("pin_ov_i2c", &cfg->mono_i2c_pins, len);
        } else {
            pin_override_i2c_set_defaults(&cfg->mono_i2c_pins);
        }
    }

    cfg->touch_x_min = s_prefs.getShort("touch_xmin", cfg->touch_x_min);
    cfg->touch_x_max = s_prefs.getShort("touch_xmax", cfg->touch_x_max);
    cfg->touch_y_min = s_prefs.getShort("touch_ymin", cfg->touch_y_min);
    cfg->touch_y_max = s_prefs.getShort("touch_ymax", cfg->touch_y_max);
    cfg->touch_calibrated = s_prefs.getUChar("touch_cal", cfg->touch_calibrated ? 1 : 0) != 0;

    s_prefs.end();
}

void config_store_save(const app_config_t *cfg)
{
    if (!s_prefs.begin(NS, /*readOnly=*/false)) {
        return;
    }

    s_prefs.putString("wifi_ssid",  cfg->wifi_ssid);
    s_prefs.putString("wifi_pass",  cfg->wifi_pass);
    s_prefs.putUChar ("hw_src",     (uint8_t)cfg->hw_source);
    s_prefs.putString("mqtt_host",  cfg->mqtt_host);
    s_prefs.putUShort("mqtt_port",  cfg->mqtt_port);
    s_prefs.putString("mqtt_user",  cfg->mqtt_user);
    s_prefs.putString("mqtt_pass",  cfg->mqtt_pass);
    s_prefs.putString("mqtt_topic", cfg->mqtt_topic);
    s_prefs.putString("tz",         cfg->tz);
    s_prefs.putString("ntp_server", cfg->ntp_server);
    s_prefs.putUChar ("w_en",       cfg->weather_enabled ? 1 : 0);
    s_prefs.putString("w_key",      cfg->weather_api_key);
    s_prefs.putString("w_city",     cfg->weather_city);
    s_prefs.putString("w_units",    cfg->weather_units);
    s_prefs.putUChar ("bright",     cfg->brightness);
    s_prefs.putUChar ("rot",        cfg->rotation);
    s_prefs.putUChar ("disp_type",  (uint8_t)cfg->display_type);
    s_prefs.putUShort("standby_s",  cfg->standby_timeout_s);
    s_prefs.putUInt  ("cpu_col",    cfg->cpu_arc_color);
    s_prefs.putUInt  ("gpu_col",    cfg->gpu_arc_color);
    s_prefs.putString("language",   cfg->language);
    s_prefs.putBytes ("pin_ov",     &cfg->pin_overrides, sizeof(cfg->pin_overrides));
    s_prefs.putBytes ("pin_ov_i2c", &cfg->mono_i2c_pins, sizeof(cfg->mono_i2c_pins));

    s_prefs.putShort ("touch_xmin", cfg->touch_x_min);
    s_prefs.putShort ("touch_xmax", cfg->touch_x_max);
    s_prefs.putShort ("touch_ymin", cfg->touch_y_min);
    s_prefs.putShort ("touch_ymax", cfg->touch_y_max);
    s_prefs.putUChar ("touch_cal",  cfg->touch_calibrated ? 1 : 0);

    s_prefs.end();
}

void config_store_factory_reset(void)
{
    if (!s_prefs.begin(NS, /*readOnly=*/false)) {
        return;
    }
    s_prefs.clear();
    s_prefs.end();
}
