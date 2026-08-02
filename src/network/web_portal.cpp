#include "web_portal.h"
#include "shared_state.h"
#include "config_store.h"
#include "display_layout.h"
#include "layout_store.h"
#include "wifi_provision.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Update.h>
#include <time.h>

#if defined(BOARD_GENERIC)
// Fuer die eingebauten Default-Pins je Displaytyp (siehe handleGetConfig()) -
// die Konstanten (disp_gc9a01::DEFAULT_MOSI etc.) sind pro Target bereits
// korrekt aufgeloest (siehe CLAUDE.md-Fallstrick zu GPIO23 auf dem C3).
#include "displays/display_factory.h"
#include "displays/ssd1309.h"
#endif

// Von extra_script_gzip_web.py aus web/dashboard.html erzeugt und ueber
// board_build.embed_files (siehe platformio.ini) eingebettet. Symbolnamen
// folgen PlatformIOs Konvention fuer embed_files: _binary_<pfad-mit-
// unterstrichen>_start/_end.
extern const uint8_t web_dashboard_html_gz_start[] asm("_binary_web_dashboard_html_gz_start");
extern const uint8_t web_dashboard_html_gz_end[]   asm("_binary_web_dashboard_html_gz_end");
extern const uint8_t web_favicon_svg_gz_start[] asm("_binary_web_favicon_svg_gz_start");
extern const uint8_t web_favicon_svg_gz_end[]   asm("_binary_web_favicon_svg_gz_end");

namespace {

AsyncWebServer s_server(80);

// Sammelt den Body von POST /api/config, da AsyncWebServer JSON-Bodies in
// mehreren Chunks liefern kann (siehe onBody-Signatur unten).
String s_configBody;
String s_layoutBody; // dieselbe Chunk-Sammel-Logik, eigener Puffer fuer /api/layout

void handleStatus(AsyncWebServerRequest *request)
{
    JsonDocument doc;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = wifi_connected;
    wifi["ssid"] = WiFi.SSID();
    wifi["ip"] = WiFi.localIP().toString();
    wifi["rssi"] = wifi_connected ? WiFi.RSSI() : 0;

    doc["source"] = (app_config.hw_source == HW_SOURCE_MQTT) ? "mqtt" : "usb";
    if (app_config.hw_source == HW_SOURCE_MQTT) {
        JsonObject mqtt = doc["mqtt"].to<JsonObject>();
        mqtt["connected"] = mqtt_connected;
        mqtt["host"] = app_config.mqtt_host;
        mqtt["port"] = app_config.mqtt_port;
        mqtt["rx_count"] = hw_rx_count;
    } else {
        JsonObject usb = doc["usb"].to<JsonObject>();
        usb["connected"] = serial_connected;
        usb["rx_count"] = hw_rx_count;
    }

    JsonObject weather = doc["weather"].to<JsonObject>();
    weather["connected"] = app_config.weather_enabled && weather_info.valid;

    // Steuert im Dashboard, ob der "Dashboard Editor"-Tab ueberhaupt
    // angeboten wird - nicht auf runden Displays (GC9A01), siehe
    // display_layout.h.
    JsonObject disp = doc["display"].to<JsonObject>();
    disp["layout_editor"] = displaySupportsLayoutEditor();
    disp["w"] = displayWidthPx();
    disp["h"] = displayHeightPx();

    time_t now = time(nullptr);
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char iso[32];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &tmInfo);
    JsonObject t = doc["time"].to<JsonObject>();
    t["iso"] = iso;
    t["tz"] = app_config.tz;

    JsonObject esp = doc["esp"].to<JsonObject>();
#if !CONFIG_IDF_TARGET_ESP32
    esp["temp_c"] = temperatureRead();
#else
    esp["temp_c"] = nullptr; // ESP32 (original) hat keinen internen Temp-Sensor
#endif
    esp["free_heap"] = ESP.getFreeHeap();
    esp["uptime_s"] = millis() / 1000;
    esp["fw_version"] = FW_VERSION;
    esp["board_name"] = BOARD_NAME;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleHwData(AsyncWebServerRequest *request)
{
    JsonDocument doc;

    hw_data_lock();
    JsonObject cpu = doc["cpu"].to<JsonObject>();
    cpu["load"] = hw_info.cpu_load;
    cpu["temp"] = hw_info.cpu_temp;
    cpu["power"] = hw_info.cpu_power;
    JsonObject gpu = doc["gpu"].to<JsonObject>();
    gpu["load"] = hw_info.gpu_load;
    gpu["temp"] = hw_info.gpu_temp;
    gpu["power"] = hw_info.gpu_power;
    hw_data_unlock();

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void handleGetConfig(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["mqtt_host"] = app_config.mqtt_host;
    doc["mqtt_port"] = app_config.mqtt_port;
    doc["mqtt_user"] = app_config.mqtt_user;
    // mqtt_pass bewusst NICHT gesendet - siehe handlePostConfig() fuer die
    // "leer = unveraendert" Konvention beim Speichern.
    doc["mqtt_topic"] = app_config.mqtt_topic;
    doc["hw_source"] = (uint8_t)app_config.hw_source;
    doc["tz"] = app_config.tz;
    doc["ntp_server"] = app_config.ntp_server;
    doc["weather_enabled"] = app_config.weather_enabled;
    doc["weather_api_key"] = app_config.weather_api_key;
    doc["weather_city"] = app_config.weather_city;
    doc["weather_units"] = app_config.weather_units;
    doc["brightness"] = app_config.brightness;
    doc["rotation"] = app_config.rotation;
    doc["language"] = app_config.language;
    doc["board_name"] = BOARD_NAME;
#ifdef BOARD_HAS_BACKLIGHT_PWM
    doc["has_backlight_pwm"] = true;
#else
    doc["has_backlight_pwm"] = false;
#endif
    doc["display_type"] = (uint8_t)app_config.display_type;
    doc["display_round"] = displayIsRound();
    doc["display_mono"] = displayIsMono();
    // "#rrggbb" statt Zahl - passt direkt auf <input type="color"> im
    // Dashboard, kein Formatieren im JS noetig.
    char colBuf[8];
    snprintf(colBuf, sizeof(colBuf), "#%06lX", (unsigned long)app_config.cpu_arc_color);
    doc["cpu_arc_color"] = colBuf;
    snprintf(colBuf, sizeof(colBuf), "#%06lX", (unsigned long)app_config.gpu_arc_color);
    doc["gpu_arc_color"] = colBuf;
    doc["standby_timeout_s"] = app_config.standby_timeout_s;
#if defined(BOARD_GENERIC)
    // Nur auf generischen Devkits (ESP32/S3/C3) waehlt der Nutzer das Panel
    // selbst - fest verdrahtete Boards (CYD, JC8048W550) kennen ihr Display
    // bereits zur Compile-Zeit, das Dashboard blendet den Picker sonst aus.
    doc["dynamic_display"] = true;
#else
    doc["dynamic_display"] = false;
#endif

    // PIN_UNSET (siehe pin_override.h) -> null im JSON, "kein Override,
    // eingebauter Default des Displaytreibers gilt" - unterscheidet sich
    // bewusst von -1 ("Pin nicht verdrahtet", ein gueltiger echter Wert).
    JsonObject pins = doc["pin_overrides"].to<JsonObject>();
    auto putPin = [&](const char *key, int8_t v) {
        if (v == PIN_UNSET) pins[key] = nullptr; else pins[key] = v;
    };
    putPin("mosi", app_config.pin_overrides.mosi);
    putPin("miso", app_config.pin_overrides.miso);
    putPin("sclk", app_config.pin_overrides.sclk);
    putPin("cs",   app_config.pin_overrides.cs);
    putPin("dc",   app_config.pin_overrides.dc);
    putPin("rst",  app_config.pin_overrides.rst);
    putPin("bl",   app_config.pin_overrides.bl);

    // SSD1309 (I2C statt SPI) hat eine eigene, kleinere Pin-Form - eigenes
    // JSON-Objekt statt die 7 SPI-Felder oben mit ungenutzten Werten zu
    // ueberladen. i2c_addr 0 = kein Override (0x00 ist keine gueltige I2C-
    // Adresse), analog zu PIN_UNSET bei den Pin-Feldern.
    JsonObject pinsI2c = doc["pin_overrides_i2c"].to<JsonObject>();
    auto putPinI2c = [&](const char *key, int8_t v) {
        if (v == PIN_UNSET) pinsI2c[key] = nullptr; else pinsI2c[key] = v;
    };
    putPinI2c("sda", app_config.ssd1309_pins.sda);
    putPinI2c("scl", app_config.ssd1309_pins.scl);
    putPinI2c("rst", app_config.ssd1309_pins.rst);
    if (app_config.ssd1309_pins.i2c_addr == 0) pinsI2c["addr"] = nullptr;
    else pinsI2c["addr"] = app_config.ssd1309_pins.i2c_addr;

#if defined(BOARD_GENERIC)
    // Eingebaute Default-Pins je Displaytyp - Schluessel sind die display_type_t-
    // Werte als String (passt direkt auf den <select>-value im Dashboard).
    // Laesst das Pins-Feld leer stehen (= "kein Override"), zeigt den
    // tatsaechlich wirksamen Wert aber als Platzhalter an, siehe dashboard.html.
    JsonObject defaults = doc["display_defaults"].to<JsonObject>();
    auto addDefaults = [&](const char *key, int8_t mosi, int8_t miso, int8_t sclk,
                            int8_t cs, int8_t dc, int8_t rst, int8_t bl) {
        JsonObject o = defaults[key].to<JsonObject>();
        o["mosi"] = mosi;
        o["miso"] = miso;
        o["sclk"] = sclk;
        o["cs"]   = cs;
        o["dc"]   = dc;
        o["rst"]  = rst;
        o["bl"]   = bl;
    };
    addDefaults("4", disp_gc9a01::DEFAULT_MOSI, disp_gc9a01::DEFAULT_MISO, disp_gc9a01::DEFAULT_SCLK,
                disp_gc9a01::DEFAULT_CS, disp_gc9a01::DEFAULT_DC, disp_gc9a01::DEFAULT_RST, disp_gc9a01::DEFAULT_BL);
    addDefaults("5", disp_ili9488::DEFAULT_MOSI, disp_ili9488::DEFAULT_MISO, disp_ili9488::DEFAULT_SCLK,
                disp_ili9488::DEFAULT_CS, disp_ili9488::DEFAULT_DC, disp_ili9488::DEFAULT_RST, disp_ili9488::DEFAULT_BL);
    addDefaults("6", disp_st7796s::DEFAULT_MOSI, disp_st7796s::DEFAULT_MISO, disp_st7796s::DEFAULT_SCLK,
                disp_st7796s::DEFAULT_CS, disp_st7796s::DEFAULT_DC, disp_st7796s::DEFAULT_RST, disp_st7796s::DEFAULT_BL);

    // SSD1309 - eigene Default-Tabelle, andere Pin-Form (I2C statt SPI, siehe
    // pin_overrides_i2c oben).
    JsonObject defaultsI2c = doc["display_defaults_i2c"].to<JsonObject>();
    JsonObject ssd1309Defaults = defaultsI2c["7"].to<JsonObject>();
    ssd1309Defaults["sda"]  = disp_ssd1309::DEFAULT_SDA;
    ssd1309Defaults["scl"]  = disp_ssd1309::DEFAULT_SCL;
    ssd1309Defaults["rst"]  = disp_ssd1309::DEFAULT_RST;
    ssd1309Defaults["addr"] = disp_ssd1309::DEFAULT_ADDR;
#endif

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// Partial-Merge: nur Felder uebernehmen, die im Body tatsaechlich enthalten
// sind. Kein Live-Apply auf laufende Subsysteme (mqtt_handler, time_service,
// ...) - jede Aenderung verlangt einen manuellen Neustart, siehe Plan.
void handlePostConfig(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_configBody);
    s_configBody = "";
    if (err) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }

    if (doc["mqtt_host"].is<const char *>())  strlcpy(app_config.mqtt_host,  doc["mqtt_host"],  sizeof(app_config.mqtt_host));
    if (doc["mqtt_port"].is<uint16_t>())      app_config.mqtt_port = doc["mqtt_port"];
    if (doc["mqtt_user"].is<const char *>())  strlcpy(app_config.mqtt_user,  doc["mqtt_user"],  sizeof(app_config.mqtt_user));
    if (doc["mqtt_topic"].is<const char *>()) strlcpy(app_config.mqtt_topic, doc["mqtt_topic"], sizeof(app_config.mqtt_topic));
    // Leeres Passwort im Body = "unveraendert lassen" (das Frontend laesst
    // das Feld beim Laden bewusst leer, siehe handleGetConfig()).
    if (doc["mqtt_pass"].is<const char *>() && strlen(doc["mqtt_pass"]) > 0) {
        strlcpy(app_config.mqtt_pass, doc["mqtt_pass"], sizeof(app_config.mqtt_pass));
    }
    if (doc["hw_source"].is<uint8_t>())       app_config.hw_source = (hw_source_t)(uint8_t)doc["hw_source"];

    if (doc["tz"].is<const char *>())         strlcpy(app_config.tz,         doc["tz"],         sizeof(app_config.tz));
    if (doc["ntp_server"].is<const char *>()) strlcpy(app_config.ntp_server, doc["ntp_server"], sizeof(app_config.ntp_server));

    if (doc["weather_enabled"].is<bool>())        app_config.weather_enabled = doc["weather_enabled"];
    if (doc["weather_api_key"].is<const char *>()) strlcpy(app_config.weather_api_key, doc["weather_api_key"], sizeof(app_config.weather_api_key));
    if (doc["weather_city"].is<const char *>())    strlcpy(app_config.weather_city,    doc["weather_city"],    sizeof(app_config.weather_city));
    if (doc["weather_units"].is<const char *>())   strlcpy(app_config.weather_units,   doc["weather_units"],   sizeof(app_config.weather_units));

    if (doc["brightness"].is<uint8_t>()) {
        app_config.brightness = doc["brightness"];
        // Helligkeit ist ein reiner GPIO/PWM-Write ohne Nebenwirkungen auf
        // andere Subsysteme - im Gegensatz zu MQTT/Zeit/etc. lohnt sich hier
        // sofortiges Anwenden statt "speichern + Reboot", siehe dashboard.html.
        boardDisplaySetBrightness(app_config.brightness);
    }
    if (doc["rotation"].is<uint8_t>())   app_config.rotation   = doc["rotation"];
    if (doc["display_type"].is<uint8_t>()) app_config.display_type = (display_type_t)(uint8_t)doc["display_type"];
    // "#rrggbb" vom <input type="color"> - fuehrendes '#' ueberspringen,
    // Rest als Hex parsen. Ungueltige/fehlende Werte lassen das Feld
    // unveraendert (strtoul liefert dann 0, deshalb Laengenpruefung vorher).
    if (doc["cpu_arc_color"].is<const char *>()) {
        const char *s = doc["cpu_arc_color"];
        if (strlen(s) == 7 && s[0] == '#') app_config.cpu_arc_color = strtoul(s + 1, nullptr, 16);
    }
    if (doc["gpu_arc_color"].is<const char *>()) {
        const char *s = doc["gpu_arc_color"];
        if (strlen(s) == 7 && s[0] == '#') app_config.gpu_arc_color = strtoul(s + 1, nullptr, 16);
    }
    if (doc["language"].is<const char *>()) strlcpy(app_config.language, doc["language"], sizeof(app_config.language));
    if (doc["standby_timeout_s"].is<uint16_t>()) app_config.standby_timeout_s = doc["standby_timeout_s"];

    // Pin-Overrides kommen als komplettes Objekt (siehe dashboard.html "Pins"-
    // Karte, sendet immer alle 7 Felder zusammen) - jedes Feld ist entweder
    // eine Pin-Nummer oder JSON null ("kein Override", siehe PIN_UNSET).
    if (doc["pin_overrides"].is<JsonObject>()) {
        JsonObject po = doc["pin_overrides"];
        auto setPin = [&](const char *key, int8_t &field) {
            JsonVariant v = po[key];
            field = v.is<int>() ? (int8_t)v.as<int>() : PIN_UNSET;
        };
        setPin("mosi", app_config.pin_overrides.mosi);
        setPin("miso", app_config.pin_overrides.miso);
        setPin("sclk", app_config.pin_overrides.sclk);
        setPin("cs",   app_config.pin_overrides.cs);
        setPin("dc",   app_config.pin_overrides.dc);
        setPin("rst",  app_config.pin_overrides.rst);
        setPin("bl",   app_config.pin_overrides.bl);
    }

    // SSD1309-Pins (I2C) - eigenes Objekt, eigene Feldnamen, siehe
    // handleGetConfig()/pin_overrides_i2c. "addr" ist uint8_t statt int8_t
    // (0 = kein Override) statt PIN_UNSET, eigener Zweig statt setPin().
    if (doc["pin_overrides_i2c"].is<JsonObject>()) {
        JsonObject po = doc["pin_overrides_i2c"];
        auto setPin = [&](const char *key, int8_t &field) {
            JsonVariant v = po[key];
            field = v.is<int>() ? (int8_t)v.as<int>() : PIN_UNSET;
        };
        setPin("sda", app_config.ssd1309_pins.sda);
        setPin("scl", app_config.ssd1309_pins.scl);
        setPin("rst", app_config.ssd1309_pins.rst);
        JsonVariant addr = po["addr"];
        app_config.ssd1309_pins.i2c_addr = addr.is<int>() ? (uint8_t)addr.as<int>() : 0;
    }

    config_store_save(&app_config);
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleGetLayout(AsyncWebServerRequest *request)
{
    String json = layout_store_load();
    if (json.length() == 0) {
        // Noch nichts gespeichert - Editor zeigt dasselbe generierte
        // Grundlayout, das auch ein frischer Boot anwendet (siehe main.cpp),
        // statt eines leeren Canvas.
        json = layout_default_json(displayWidthPx(), displayHeightPx());
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
    // Kein Browser-/Zwischen-Cache fuer dynamische API-Antworten - ohne
    // expliziten Header kam es vor, dass "Auf Grundlayout zuruecksetzen"
    // eine veraltete Antwort zeigte statt des aktuellen Layouts.
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

// Liefert IMMER das generierte Grundlayout, unabhaengig von einem evtl.
// gespeicherten Layout - Backend fuer den "Auf Grundlayout zuruecksetzen"-
// Knopf im Editor (siehe dashboard.html), der das Ergebnis sofort speichert
// und anwendet.
void handleGetLayoutDefault(AsyncWebServerRequest *request)
{
    AsyncWebServerResponse *response = request->beginResponse(
        200, "application/json", layout_default_json(displayWidthPx(), displayHeightPx()));
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

// Speichert + wendet sofort an (ueber die Thread-Safe-Queue, siehe
// display_layout.h - dieser Handler laeuft im AsyncTCP-Task, darf LVGL
// niemals direkt anfassen).
void handlePostLayout(AsyncWebServerRequest *request)
{
    if (!displaySupportsLayoutEditor()) {
        s_layoutBody = "";
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"layout editor not supported on this display\"}");
        return;
    }

    // Ein explizit gespeichertes LEERES Widgets-Array ("Alles entfernen" +
    // Speichern) wuerde sonst dauerhaft einen leeren Screen persistieren -
    // von aussen nicht mehr von "noch nie gespeichert" unterscheidbar, aber
    // OHNE den Fallback auf das generierte Grundlayout, da der JSON-String
    // selbst nicht leer ist (siehe layout_store_load()). Stattdessen wird
    // der Store in diesem Fall geleert, damit GET /api/layout und der
    // naechste Boot wieder auf das Grundlayout zurueckfallen - konsistent
    // mit "Auf Grundlayout zuruecksetzen" statt einer Sackgasse.
    JsonDocument doc;
    bool isEmpty = deserializeJson(doc, s_layoutBody) == DeserializationError::Ok &&
                   doc["widgets"].as<JsonArrayConst>().size() == 0;
    if (isEmpty) {
        layout_store_clear();
        String defaultJson = layout_default_json(displayWidthPx(), displayHeightPx());
        layout_queue_push_apply(defaultJson);
        s_layoutBody = "";
        request->send(200, "application/json", "{\"ok\":true}");
        return;
    }

    if (!layout_store_save(s_layoutBody)) {
        s_layoutBody = "";
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"layout too large\"}");
        return;
    }
    layout_queue_push_apply(s_layoutBody);
    s_layoutBody = "";
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleReboot(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
}

// Dieselbe (einzige) Seite fuer "/", "/settings" und "/fota" - es ist eine
// Single-Page-App, das JS entscheidet anhand von location.pathname beim Laden
// und history.pushState() beim Tab-Wechsel, welcher Tab aktiv ist (siehe
// dashboard.html "--- Tabs ---"). So sind die drei Tabs direkt per URL
// erreichbar/verlinkbar/reload-fest, ohne eine zweite Kopie der Seite zu
// brauchen.
void handleDashboard(AsyncWebServerRequest *request)
{
    AsyncWebServerResponse *response = request->beginResponse(
        200, "text/html",
        web_dashboard_html_gz_start,
        web_dashboard_html_gz_end - web_dashboard_html_gz_start);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void handleFactoryReset(AsyncWebServerRequest *request)
{
    config_store_factory_reset();
    // WiFiManager (martinverges/ESP32 Wifi Manager) verwaltet seine eigenen
    // gespeicherten Zugangsdaten in einem eigenen NVS-Namespace, unabhaengig
    // von app_config - siehe wifi_provision.cpp.
    wifi_provision_forget();
    request->send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
}

void handleOtaUpload(AsyncWebServerRequest * /*request*/, String /*filename*/, size_t index,
                      uint8_t *data, size_t len, bool final)
{
    if (index == 0) {
        // Pausiert den Wetter-Task fuer die Dauer des Uploads (siehe
        // shared_state.h) - unabhaengig vom Ausgang unten wieder freigegeben,
        // sonst bliebe er nach einem fehlgeschlagenen Update dauerhaft aus.
        ota_in_progress = true;
        Update.begin(UPDATE_SIZE_UNKNOWN);
    }
    if (len) {
        Update.write(data, len);
    }
    if (final) {
        Update.end(true);
        ota_in_progress = false;
    }
}

void handleOtaDone(AsyncWebServerRequest *request)
{
    bool ok = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(
        ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    response->addHeader("Connection", "close");
    request->send(response);
    if (ok) {
        delay(200);
        ESP.restart();
    }
}

} // namespace

AsyncWebServer &web_portal_server(void)
{
    return s_server;
}

void web_portal_begin(void)
{
    s_server.on("/", HTTP_GET, handleDashboard);
    s_server.on("/settings", HTTP_GET, handleDashboard);
    s_server.on("/fota", HTTP_GET, handleDashboard);
    s_server.on("/editor", HTTP_GET, handleDashboard);

    s_server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(
            200, "image/svg+xml",
            web_favicon_svg_gz_start,
            web_favicon_svg_gz_end - web_favicon_svg_gz_start);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    s_server.on("/api/status", HTTP_GET, handleStatus);
    s_server.on("/api/hwdata", HTTP_GET, handleHwData);
    s_server.on("/api/config", HTTP_GET, handleGetConfig);
    s_server.on(
        "/api/config", HTTP_POST, handlePostConfig, nullptr,
        [](AsyncWebServerRequest * /*request*/, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) s_configBody = "";
            s_configBody.concat((const char *)data, len);
            (void)total;
        });
    s_server.on("/api/layout", HTTP_GET, handleGetLayout);
    s_server.on("/api/layout/default", HTTP_GET, handleGetLayoutDefault);
    s_server.on(
        "/api/layout", HTTP_POST, handlePostLayout, nullptr,
        [](AsyncWebServerRequest * /*request*/, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) s_layoutBody = "";
            s_layoutBody.concat((const char *)data, len);
            (void)total;
        });
    s_server.on("/api/reboot", HTTP_POST, handleReboot);
    s_server.on("/api/factory_reset", HTTP_POST, handleFactoryReset);
    s_server.on("/api/ota", HTTP_POST, handleOtaDone, handleOtaUpload);

    s_server.begin();
}
