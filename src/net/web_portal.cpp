#include "web_portal.h"
#include "../shared_state.h"
#include "../config_store.h"
#include "../display/board_profiles.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <ArduinoJson.h>

static AsyncWebServer s_server(80);
static WiFiManager    s_wm;
static bool           s_our_server_started;
static char           s_ap_ssid[24];

// ------------------------------------------------------------------
// Eingebettete Config-Seite - woertlich aus dem esp-idf-Original
// uebernommen (main/net/web_portal.c: INDEX_HTML), da reines HTML/JS/CSS
// ohne ESP-IDF-Abhaengigkeit. Die WLAN-Karte (SSID/Passwort/Netzwerksuche)
// ist seit dem Wechsel auf tzapu/WiFiManager raus - WLAN wird jetzt
// ausschliesslich ueber WiFiManagers eigenes Setup-Portal konfiguriert,
// dieser Server startet gar nicht erst, bevor eine WLAN-Verbindung steht
// (siehe web_portal_loop()).
// ------------------------------------------------------------------
#include "web_portal_html.inc"

// ------------------------------------------------------------------
// WLAN (alexhopeoconnor/WiFiManager v2.0.19 statt eigener STA/AP-Logik)
// ------------------------------------------------------------------
// WiFiManager haelt seine eigenen Zugangsdaten in der WLAN-Treiber-eigenen
// NVS-Ablage (WiFi.begin()-Persistenz), unabhaengig von app_config/
// config_store.cpp - app_config hat deshalb keine wifi_ssid/wifi_pass-Felder
// mehr.
//
// Diese Fork-Version ist intern durchgehend async (nativ auf
// ESPAsyncWebServer aufgebaut) und kennt kein setConfigPortalBlocking()
// mehr (im Original/aelteren Versionen vorhanden) - autoConnect() startet
// bei fehlgeschlagener STA-Verbindung das Setup-Portal (offener AP +
// Webserver + DNS, alles von WiFiManager selbst verwaltet) und kehrt sofort
// zurueck, ohne auf dessen Ende zu warten (verifiziert im Fork-Quellcode:
// startConfigPortal() blockiert nicht). web_portal_loop() muss dafuer jeden
// Durchlauf wm.process() aufrufen - ohne diesen Aufruf passiert im Portal
// schlicht nichts. Wichtig ist das aus demselben Grund wie zuvor: waere der
// Verbindungsaufbau blockierend, wuerde loop() (und damit
// display_ui_loop()/lv_timer_handler()) waehrend des gesamten
// Setup-Portal-Wartens nicht laufen - das Display wuerde einfrieren (keine
// Uhr, kein Touch, kein Refresh), obwohl display_ui_begin() bereits vor
// web_portal_begin() lief, gerade damit AP-SSID/IP live sichtbar bleiben.
//
// Unser eigener AsyncWebServer (Config-Seite/API/OTA) teilt sich Port 80
// mit WiFiManagers Portal-Webserver - beide gleichzeitig zu binden wuerde
// kollidieren. Er startet deshalb bewusst NICHT hier in web_portal_begin(),
// sondern erst einmalig in web_portal_loop(), sobald WiFi.status() ==
// WL_CONNECTED und WiFiManagers Portal nicht mehr aktiv ist.
void web_portal_begin(void)
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ESP32-HWMon-%02x%02x", mac[4], mac[5]);

    s_wm.autoConnect(s_ap_ssid); // offener AP (kein Passwort), wie zuvor
}

// ------------------------------------------------------------------
// HTTP-Handler
// ------------------------------------------------------------------
static void h_status(AsyncWebServerRequest *req)
{
    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"wifi\":%s,\"mqtt\":%s,\"serial\":%s,\"hw_source\":%d,\"ip\":\"%s\",\"cpu_load\":%.1f,\"gpu_load\":%.1f,"
        "\"cpu_temp\":%.1f,\"gpu_temp\":%.1f,\"cpu_power\":%.1f,\"gpu_power\":%.1f,"
        "\"fw_version\":\"%s\",\"free_heap\":%u}",
        wifi_connected ? "true" : "false", mqtt_connected ? "true" : "false",
        serial_connected ? "true" : "false", (int)app_config.hw_source, web_portal_ip(),
        hw_info.cpu_load, hw_info.gpu_load, hw_info.cpu_temp, hw_info.gpu_temp,
        hw_info.cpu_power, hw_info.gpu_power, FW_VERSION,
        (unsigned)ESP.getFreeHeap());
    req->send(200, "application/json", String(buf, n));
}

static void add_pin_or_null(JsonObject &d, const char *key, int16_t v)
{
    if (v == PIN_UNSET) d[key] = nullptr;
    else d[key] = v;
}

static void h_config_get(AsyncWebServerRequest *req)
{
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["hw_source"] = (int)app_config.hw_source;
    d["mqtt_host"] = app_config.mqtt_host;
    d["mqtt_port"] = app_config.mqtt_port;
    d["mqtt_user"] = app_config.mqtt_user;
    d["mqtt_pass"] = "";
    d["mqtt_topic"] = app_config.mqtt_topic;
    d["ntp_server"] = app_config.ntp_server;
    d["tz"] = app_config.tz;
    d["weather_enabled"] = app_config.weather_enabled;
    d["weather_api_key"] = "";
    d["weather_city"] = app_config.weather_city;
    d["weather_units"] = app_config.weather_units;
    d["brightness"] = app_config.brightness;
    d["rotation"] = app_config.rotation;
    d["display_type"] = board_profile_key(app_config.display_type);
    d["standby_timeout_s"] = app_config.standby_timeout_s;
    d["language"] = app_config.language;

    const pin_override_t *ov = &app_config.pin_overrides;
    add_pin_or_null(d, "pin_mosi", ov->mosi);
    add_pin_or_null(d, "pin_miso", ov->miso);
    add_pin_or_null(d, "pin_sclk", ov->sclk);
    add_pin_or_null(d, "pin_cs",   ov->cs);
    add_pin_or_null(d, "pin_dc",   ov->dc);
    add_pin_or_null(d, "pin_rst",  ov->rst);
    add_pin_or_null(d, "pin_bl",   ov->bl);
    add_pin_or_null(d, "pin_touch_cs",   ov->touch_cs);
    add_pin_or_null(d, "pin_touch_irq",  ov->touch_irq);
    add_pin_or_null(d, "pin_touch_mosi", ov->touch_mosi);
    add_pin_or_null(d, "pin_touch_miso", ov->touch_miso);
    add_pin_or_null(d, "pin_touch_clk",  ov->touch_clk);
    add_pin_or_null(d, "pin_i2c_sda", ov->i2c_sda);
    add_pin_or_null(d, "pin_i2c_scl", ov->i2c_scl);
    if (ov->i2c_addr == 0) d["pin_i2c_addr"] = nullptr;
    else d["pin_i2c_addr"] = ov->i2c_addr;
    add_pin_or_null(d, "pin_nav_button", ov->nav_button);
    add_pin_or_null(d, "pin_touch_rst", ov->touch_rst);
    add_pin_or_null(d, "pin_touch_int", ov->touch_int);

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

// Liefert alle unterstuetzten Displaytypen samt Pinbelegung als JSON-Array.
static void h_displays(AsyncWebServerRequest *req)
{
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < DISPLAY_TYPE_COUNT; i++) {
        if (!board_profile_is_available((display_type_t)i)) continue;
        const board_profile_t *p = board_profile_get((display_type_t)i);
        JsonObject d = arr.add<JsonObject>();
        d["key"]  = board_profile_key((display_type_t)i);
        d["name"] = p->name;
        d["bus"]  = i == DISPLAY_NONE ? "none"
            : (p->bus == LCD_BUS_I2C ? "i2c" : (p->bus == LCD_BUS_RGB ? "rgb" : "spi"));
        d["shape"] = p->shape == LCD_SHAPE_ROUND ? "round" : (p->shape == LCD_SHAPE_MONO ? "mono"
            : (p->shape == LCD_SHAPE_WIDE ? "wide" : "rect"));
        d["has_touch"] = p->has_touch;
        d["h_res"] = p->h_res;
        d["v_res"] = p->v_res;

        JsonObject pins = d["pins"].to<JsonObject>();
        if (i == DISPLAY_NONE) {
            // keine Pins
        } else if (p->bus == LCD_BUS_I2C) {
            pins["sda"] = p->i2c_sda;
            pins["scl"] = p->i2c_scl;
            pins["addr"] = p->i2c_addr;
        } else if (p->bus == LCD_BUS_RGB) {
            pins["sda"] = p->i2c_sda;
            pins["scl"] = p->i2c_scl;
            pins["addr"] = p->i2c_addr;
            pins["touch_rst"] = p->touch_rst;
            pins["touch_int"] = p->touch_int;
        } else {
            pins["mosi"] = p->mosi;
            pins["miso"] = p->miso;
            pins["sclk"] = p->sclk;
            pins["cs"]   = p->cs;
            pins["dc"]   = p->dc;
            pins["rst"]  = p->rst;
            pins["bl"]   = p->bl;
            if (p->has_touch) {
                pins["touch_cs"]   = p->touch_cs;
                pins["touch_irq"]  = p->touch_irq;
                pins["touch_mosi"] = p->touch_mosi;
                pins["touch_miso"] = p->touch_miso;
                pins["touch_clk"]  = p->touch_clk;
            }
        }
        pins["nav_button"] = p->nav_button;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

static void cfg_str(JsonObject &root, const char *key, char *dst, size_t sz, bool skip_empty)
{
    if (!root[key].is<const char *>()) return;
    const char *v = root[key];
    if (skip_empty && strlen(v) == 0) return;
    strlcpy(dst, v, sz);
}

// Hinweis: ArduinoJson unterscheidet "Feld fehlt" nicht von "Feld ist JSON
// null" (beides isNull()==true) - das Webportal-JS sendet aber immer alle
// Pin-Felder mit (siehe PIN_FIELDS im eingebetteten JS), der Unterschied
// kommt in der Praxis nie vor.
static void cfg_pin(JsonObject &root, const char *key, int16_t *dst)
{
    if (!root[key].isNull() && !root[key].is<int>()) return;
    if (root[key].isNull()) *dst = PIN_UNSET;
    else *dst = (int16_t)root[key].as<int>();
}

static void cfg_pin_addr(JsonObject &root, const char *key, uint8_t *dst)
{
    if (root[key].isNull()) *dst = 0;
    else if (root[key].is<int>()) *dst = (uint8_t)root[key].as<int>();
}

// Wie im esp-idf-Original auf 4096 Bytes begrenzt (das Config-JSON inkl. aller
// Pin-Overrides passt deutlich darunter) - Puffer sammelt die (meist einzige)
// Chunk-Uebertragung ein, bevor geparst wird.
#define CONFIG_BODY_MAX 4096
static uint8_t s_config_body[CONFIG_BODY_MAX];

static void h_config_post_body(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                                size_t index, size_t total)
{
    if (total > CONFIG_BODY_MAX || index + len > CONFIG_BODY_MAX) {
        req->send(400, "application/json", "{\"error\":\"too large\"}");
        return;
    }
    memcpy(s_config_body + index, data, len);
    if (index + len != total) return; // weitere Chunks abwarten

    JsonDocument doc;
    if (deserializeJson(doc, s_config_body, total) != DeserializationError::Ok) {
        req->send(200, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }
    JsonObject root = doc.as<JsonObject>();

    if (root["hw_source"].is<int>()) app_config.hw_source = (hw_source_t)root["hw_source"].as<int>();
    cfg_str(root, "mqtt_host",  app_config.mqtt_host,  sizeof(app_config.mqtt_host),  false);
    if (root["mqtt_port"].is<int>()) app_config.mqtt_port = root["mqtt_port"].as<uint16_t>();
    cfg_str(root, "mqtt_user",  app_config.mqtt_user,  sizeof(app_config.mqtt_user),  false);
    cfg_str(root, "mqtt_pass",  app_config.mqtt_pass,  sizeof(app_config.mqtt_pass),  true);
    cfg_str(root, "mqtt_topic", app_config.mqtt_topic, sizeof(app_config.mqtt_topic), false);
    cfg_str(root, "ntp_server", app_config.ntp_server, sizeof(app_config.ntp_server), false);
    cfg_str(root, "tz",         app_config.tz,         sizeof(app_config.tz),         false);
    if (root["weather_enabled"].is<bool>()) app_config.weather_enabled = root["weather_enabled"].as<bool>();
    cfg_str(root, "weather_api_key", app_config.weather_api_key, sizeof(app_config.weather_api_key), true);
    cfg_str(root, "weather_city",    app_config.weather_city,    sizeof(app_config.weather_city),    false);
    cfg_str(root, "weather_units",   app_config.weather_units,   sizeof(app_config.weather_units),   false);
    if (root["brightness"].is<int>()) app_config.brightness = root["brightness"].as<uint8_t>();
    if (root["rotation"].is<int>())   app_config.rotation   = root["rotation"].as<uint8_t>();
    if (root["display_type"].is<const char *>()) {
        display_type_t new_type = board_profile_from_key(root["display_type"].as<const char *>());
        if (new_type != app_config.display_type) {
            pin_override_set_defaults(&app_config.pin_overrides);
        }
        app_config.display_type = new_type;
    }
    if (root["standby_timeout_s"].is<int>()) app_config.standby_timeout_s = root["standby_timeout_s"].as<uint16_t>();
    cfg_str(root, "language", app_config.language, sizeof(app_config.language), false);

    pin_override_t *ov = &app_config.pin_overrides;
    cfg_pin(root, "pin_mosi", &ov->mosi);
    cfg_pin(root, "pin_miso", &ov->miso);
    cfg_pin(root, "pin_sclk", &ov->sclk);
    cfg_pin(root, "pin_cs",   &ov->cs);
    cfg_pin(root, "pin_dc",   &ov->dc);
    cfg_pin(root, "pin_rst",  &ov->rst);
    cfg_pin(root, "pin_bl",   &ov->bl);
    cfg_pin(root, "pin_touch_cs",   &ov->touch_cs);
    cfg_pin(root, "pin_touch_irq",  &ov->touch_irq);
    cfg_pin(root, "pin_touch_mosi", &ov->touch_mosi);
    cfg_pin(root, "pin_touch_miso", &ov->touch_miso);
    cfg_pin(root, "pin_touch_clk",  &ov->touch_clk);
    cfg_pin(root, "pin_i2c_sda", &ov->i2c_sda);
    cfg_pin(root, "pin_i2c_scl", &ov->i2c_scl);
    cfg_pin_addr(root, "pin_i2c_addr", &ov->i2c_addr);
    cfg_pin(root, "pin_nav_button", &ov->nav_button);
    cfg_pin(root, "pin_touch_rst", &ov->touch_rst);
    cfg_pin(root, "pin_touch_int", &ov->touch_int);

    config_store_save(&app_config);
    req->send(200, "application/json", "{\"ok\":true}");
    // Arduino Update-Bibliothek erlaubt keinen Timer-Restart wie
    // esp_timer_start_once() im Original - kleine Verzoegerung, damit die
    // HTTP-Antwort noch abgeschickt wird, dann Neustart.
    delay(50);
    ESP.restart();
}

// Raw-Binary-OTA: der Request-Body IST die .bin (Arduino Update-Bibliothek).
static void h_update_body(AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total)
{
    if (index == 0) {
        if (!Update.begin(total)) {
            req->send(500, "text/plain", "ota begin");
            return;
        }
    }
    if (Update.write(data, len) != len) {
        req->send(500, "text/plain", "ota write");
        return;
    }
    if (index + len == total) {
        if (!Update.end(true)) {
            req->send(500, "text/plain", "ota finalize");
            return;
        }
        req->send(200, "text/plain", "OK");
        log_i("OTA erfolgreich, Neustart folgt");
        delay(50);
        ESP.restart();
    }
}

static void h_not_found(AsyncWebServerRequest *req)
{
    req->send(404, "text/plain", "Not found");
}

// Startet unseren eigenen Server erst, nachdem WiFiManager fertig ist (siehe
// Kommentar bei web_portal_begin()) - Port 80 ist bis dahin exklusiv
// WiFiManagers Portal-Webserver vorbehalten.
static void start_our_server(void)
{
    log_i("WLAN verbunden, IP %s", web_portal_ip());

    s_server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", INDEX_HTML);
    });
    s_server.on("/api/status", HTTP_GET, h_status);
    s_server.on("/api/config", HTTP_GET, h_config_get);
    s_server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest *req) { /* Body kommt ueber onBody */ },
        nullptr,
        h_config_post_body);
    s_server.on("/api/displays", HTTP_GET, h_displays);
    s_server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest *req) { /* Antwort erfolgt in h_update_body */ },
        nullptr,
        h_update_body);
    s_server.onNotFound(h_not_found);

    s_server.begin();
    s_our_server_started = true;
}

void web_portal_loop(void)
{
    s_wm.process();

    if (!s_our_server_started && WiFi.status() == WL_CONNECTED && !s_wm.getConfigPortalActive()) {
        wifi_connected = true;
        start_our_server();
    }
}

bool web_portal_ap_mode(void) { return s_wm.getConfigPortalActive(); }

const char *web_portal_ip(void)
{
    static char buf[16];
    IPAddress ip = s_wm.getConfigPortalActive() ? WiFi.softAPIP() : WiFi.localIP();
    strcpy(buf, ip.toString().c_str());
    return buf;
}

const char *web_portal_ap_ssid(void) { return s_ap_ssid; }

// Loescht die von WiFiManager gespeicherten Zugangsdaten und startet neu -
// beim naechsten Boot findet autoConnect() dann keine gespeicherte SSID
// mehr und oeffnet automatisch das Setup-Portal. Anders als zuvor (sofortiger
// Live-Wechsel in den AP-Modus ohne Neustart) bedeutet das jetzt einen
// kurzen Reboot, in der Praxis kaum ein Unterschied: die aktuelle
// WLAN-Verbindung geht in beiden Faellen sofort verloren.
void web_portal_force_ap(void)
{
    wifi_connected = false;
    mqtt_connected  = false;
    s_wm.resetSettings();
    delay(50);
    ESP.restart();
}
