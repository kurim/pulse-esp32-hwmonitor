#include "config_store.h"
#include <Preferences.h>

static Preferences prefs;
static const char *NS = "cydcfg";

namespace ConfigStore {

void begin() {
  // nichts persistentes hier nötig, Preferences wird je Aufruf geöffnet
}

void load(AppConfig &cfg) {
  prefs.begin(NS, true); // read-only
  prefs.getString("wifi_ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
  prefs.getString("wifi_pass", cfg.wifi_pass, sizeof(cfg.wifi_pass));
  prefs.getString("mqtt_host", cfg.mqtt_host, sizeof(cfg.mqtt_host));
  cfg.mqtt_port = prefs.getUShort("mqtt_port", cfg.mqtt_port);
  prefs.getString("mqtt_user", cfg.mqtt_user, sizeof(cfg.mqtt_user));
  prefs.getString("mqtt_pass", cfg.mqtt_pass, sizeof(cfg.mqtt_pass));
  prefs.getString("mqtt_topic", cfg.mqtt_topic, sizeof(cfg.mqtt_topic));
  prefs.getString("tz", cfg.tz, sizeof(cfg.tz));
  prefs.getString("ntp_server", cfg.ntp_server, sizeof(cfg.ntp_server));
  cfg.weather_enabled = prefs.getBool("w_en", cfg.weather_enabled);
  prefs.getString("w_key", cfg.weather_api_key, sizeof(cfg.weather_api_key));
  prefs.getString("w_city", cfg.weather_city, sizeof(cfg.weather_city));
  prefs.getString("w_units", cfg.weather_units, sizeof(cfg.weather_units));
  cfg.brightness = prefs.getUChar("bright", cfg.brightness);
  cfg.rotation   = prefs.getUChar("rot", cfg.rotation);
  prefs.end();
}

void save(const AppConfig &cfg) {
  prefs.begin(NS, false); // read-write
  prefs.putString("wifi_ssid", cfg.wifi_ssid);
  prefs.putString("wifi_pass", cfg.wifi_pass);
  prefs.putString("mqtt_host", cfg.mqtt_host);
  prefs.putUShort("mqtt_port", cfg.mqtt_port);
  prefs.putString("mqtt_user", cfg.mqtt_user);
  prefs.putString("mqtt_pass", cfg.mqtt_pass);
  prefs.putString("mqtt_topic", cfg.mqtt_topic);
  prefs.putString("tz", cfg.tz);
  prefs.putString("ntp_server", cfg.ntp_server);
  prefs.putBool("w_en", cfg.weather_enabled);
  prefs.putString("w_key", cfg.weather_api_key);
  prefs.putString("w_city", cfg.weather_city);
  prefs.putString("w_units", cfg.weather_units);
  prefs.putUChar("bright", cfg.brightness);
  prefs.putUChar("rot", cfg.rotation);
  prefs.end();
}

void resetToDefaults() {
  prefs.begin(NS, false);
  prefs.clear();
  prefs.end();
}

} // namespace ConfigStore
