#include "web_portal.h"
#include "shared_state.h"
#include "config_store.h"
#include "display_layout.h"
#include "layout_store.h"
#include "wifi_provision.h"
#include "github_ota.h"
#include "mqtt_handler.h"
#include "serial_handler.h"
#include "time_service.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Update.h>
#include <time.h>

#if defined(BOARD_GENERIC)
// Fuer die eingebauten Default-Pins je Displaytyp (siehe handleGetConfig()) -
// die Konstanten (disp_gc9a01::DEFAULT_MOSI etc.) sind pro Target bereits
// korrekt aufgeloest (siehe CLAUDE.md-Fallstrick zu GPIO23 auf dem C3).
#include "displays/display_factory.h"
#include "displays/mono_oled_i2c.h"
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
String s_importBody; // dieselbe Chunk-Sammel-Logik, eigener Puffer fuer /api/config/import

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

    // Alle zusaetzlich vom PC-Client gesendeten Felder (siehe hw_data.cpp/
    // shared_state.h dyn_values) - Basis fuer die Key-Vorschlagsliste des
    // "Label - Wert"-Widgets im Mono-Layout-Editor (dashboard.html).
    JsonObject extra = doc["extra"].to<JsonObject>();
    for (int i = 0; i < dyn_values_count; i++) {
        extra[dyn_values[i].key] = dyn_values[i].value;
    }
    hw_data_unlock();

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// Gemeinsam von handleGetConfig() (includeSecrets=false, siehe dortige
// "leer = unveraendert" Konvention in handlePostConfig()) und
// handleConfigExport() (includeSecrets = Opt-in-Checkbox im Dashboard)
// genutzt, damit das Feld-Mapping nur an einer Stelle gepflegt wird.
void buildConfigDoc(JsonDocument &doc, bool includeSecrets)
{
    doc["mqtt_host"] = app_config.mqtt_host;
    doc["mqtt_port"] = app_config.mqtt_port;
    doc["mqtt_user"] = app_config.mqtt_user;
    if (includeSecrets) {
        doc["mqtt_pass"] = app_config.mqtt_pass;
    }
    doc["mqtt_topic"] = app_config.mqtt_topic;
    doc["hw_source"] = (uint8_t)app_config.hw_source;
    doc["tz"] = app_config.tz;
    doc["ntp_server"] = app_config.ntp_server;
    doc["weather_enabled"] = app_config.weather_enabled;
    if (includeSecrets) {
        doc["weather_api_key"] = app_config.weather_api_key;
    }
    doc["weather_city"] = app_config.weather_city;
    doc["weather_units"] = app_config.weather_units;
    doc["brightness"] = app_config.brightness;
    doc["rotation"] = app_config.rotation;
    // Steuert im Dashboard, welche Rotation-Optionen angeboten werden -
    // nicht jedes Board vertraegt einen Breite/Hoehe-Tausch bei 90/270 Grad
    // (siehe cyd_2432s028r.h/display_factory.h):
    // "full" - alle 4 Werte (BOARD_GENERIC-Farbdisplays: GC9A01/ILI9488/
    //          ST7796S, keines davon hat Touch)
    // "flip" - nur 0/180 Grad (CYD: hat Touch, Gehaeuse fest fuer
    //          Querformat gebaut)
    // "none" - Feld wird ausgeblendet (JC8048W550: RGB-Panel, kein
    //          Arduino_GFX-setRotation() dafuer verdrahtet; Mono-I2C-OLEDs:
    //          GDDRAM-Adressierung unterstuetzt kein Portrait; kein Display
    //          gewaehlt)
#if defined(BOARD_CYD_2432S028R)
    doc["rotation_mode"] = "flip";
#elif defined(BOARD_JC8048W550)
    doc["rotation_mode"] = "none";
#elif defined(BOARD_GENERIC)
    doc["rotation_mode"] = (app_config.display_type == DISPLAY_NONE || displayIsMono()) ? "none" : "full";
#else
    doc["rotation_mode"] = "none";
#endif
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

    // SSD1309/SH1106 (I2C statt SPI) teilen sich eine eigene, kleinere
    // Pin-Form - eigenes JSON-Objekt statt die 7 SPI-Felder oben mit
    // ungenutzten Werten zu ueberladen. i2c_addr 0 = kein Override (0x00 ist
    // keine gueltige I2C-Adresse), analog zu PIN_UNSET bei den Pin-Feldern.
    JsonObject pinsI2c = doc["pin_overrides_i2c"].to<JsonObject>();
    auto putPinI2c = [&](const char *key, int8_t v) {
        if (v == PIN_UNSET) pinsI2c[key] = nullptr; else pinsI2c[key] = v;
    };
    putPinI2c("sda", app_config.mono_i2c_pins.sda);
    putPinI2c("scl", app_config.mono_i2c_pins.scl);
    putPinI2c("rst", app_config.mono_i2c_pins.rst);
    if (app_config.mono_i2c_pins.i2c_addr == 0) pinsI2c["addr"] = nullptr;
    else pinsI2c["addr"] = app_config.mono_i2c_pins.i2c_addr;

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

    // SSD1309/SH1106 - eigene Default-Tabelle, andere Pin-Form (I2C statt
    // SPI, siehe pin_overrides_i2c oben). Beide teilen sich dieselbe
    // Verkabelung/denselben Treiber-Header (mono_oled_i2c.h), daher
    // identische Werte unter beiden display_type-Schluesseln.
    JsonObject defaultsI2c = doc["display_defaults_i2c"].to<JsonObject>();
    auto addDefaultsI2c = [&](const char *key) {
        JsonObject o = defaultsI2c[key].to<JsonObject>();
        o["sda"]  = disp_mono_oled_i2c::DEFAULT_SDA;
        o["scl"]  = disp_mono_oled_i2c::DEFAULT_SCL;
        o["rst"]  = disp_mono_oled_i2c::DEFAULT_RST;
        o["addr"] = disp_mono_oled_i2c::DEFAULT_ADDR;
    };
    addDefaultsI2c("7"); // DISPLAY_GENERIC_SSD1309
    addDefaultsI2c("8"); // DISPLAY_GENERIC_SH1106
#endif
}

void handleGetConfig(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    buildConfigDoc(doc, /*includeSecrets=*/false);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// Partial-Merge: nur Felder uebernehmen, die im Body tatsaechlich enthalten
// sind. Gemeinsam von handlePostConfig() und handleConfigImport() genutzt
// (Import wendet denselben Merge auf ein zusaetzlich aus einer Datei
// geladenes doc an), damit das Feld-Mapping nur an einer Stelle gepflegt
// wird.
//
// MQTT/hw_source und Zeit/NTP werden am Ende sofort angewendet (kein Reboot
// noetig - mqtt_handler_apply_config()/time_service_begin() sind dieselben
// Funktionen, die main.cpp auch beim Boot aufruft, beide gefahrlos erneut
// aufrufbar). Anzeige/Pins bleiben bewusst reboot-pflichtig - ein Live-
// Reinit des SPI-Displaytreibers/LVGL waere ein deutlich groesseres
// Absturzrisiko fuer wenig Komfortgewinn.
void applyConfigFields(JsonDocument &doc)
{
    bool mqttChanged = false;
    if (doc["mqtt_host"].is<const char *>())  { strlcpy(app_config.mqtt_host,  doc["mqtt_host"],  sizeof(app_config.mqtt_host)); mqttChanged = true; }
    if (doc["mqtt_port"].is<uint16_t>())      { app_config.mqtt_port = doc["mqtt_port"]; mqttChanged = true; }
    if (doc["mqtt_user"].is<const char *>())  { strlcpy(app_config.mqtt_user,  doc["mqtt_user"],  sizeof(app_config.mqtt_user)); mqttChanged = true; }
    if (doc["mqtt_topic"].is<const char *>()) { strlcpy(app_config.mqtt_topic, doc["mqtt_topic"], sizeof(app_config.mqtt_topic)); mqttChanged = true; }
    // Leeres Passwort im Body = "unveraendert lassen" (das Frontend laesst
    // das Feld beim Laden bewusst leer, siehe handleGetConfig()).
    if (doc["mqtt_pass"].is<const char *>() && strlen(doc["mqtt_pass"]) > 0) {
        strlcpy(app_config.mqtt_pass, doc["mqtt_pass"], sizeof(app_config.mqtt_pass));
        mqttChanged = true;
    }
    if (doc["hw_source"].is<uint8_t>()) {
        app_config.hw_source = (hw_source_t)(uint8_t)doc["hw_source"];
        // serial_handler_begin() liest hw_source neu (setzt nur ein Flag,
        // siehe serial_handler.cpp) - mqtt_handler_apply_config() unten
        // prueft hw_source ebenfalls selbst erneut.
        serial_handler_begin();
        mqttChanged = true;
    }

    bool timeChanged = false;
    if (doc["tz"].is<const char *>())         { strlcpy(app_config.tz,         doc["tz"],         sizeof(app_config.tz)); timeChanged = true; }
    if (doc["ntp_server"].is<const char *>()) { strlcpy(app_config.ntp_server, doc["ntp_server"], sizeof(app_config.ntp_server)); timeChanged = true; }

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

    // SSD1309/SH1106-Pins (I2C) - eigenes Objekt, eigene Feldnamen, siehe
    // handleGetConfig()/pin_overrides_i2c. "addr" ist uint8_t statt int8_t
    // (0 = kein Override) statt PIN_UNSET, eigener Zweig statt setPin().
    if (doc["pin_overrides_i2c"].is<JsonObject>()) {
        JsonObject po = doc["pin_overrides_i2c"];
        auto setPin = [&](const char *key, int8_t &field) {
            JsonVariant v = po[key];
            field = v.is<int>() ? (int8_t)v.as<int>() : PIN_UNSET;
        };
        setPin("sda", app_config.mono_i2c_pins.sda);
        setPin("scl", app_config.mono_i2c_pins.scl);
        setPin("rst", app_config.mono_i2c_pins.rst);
        JsonVariant addr = po["addr"];
        app_config.mono_i2c_pins.i2c_addr = addr.is<int>() ? (uint8_t)addr.as<int>() : 0;
    }

    // Erst hier anwenden, nachdem app_config vollstaendig aktualisiert ist -
    // mqtt_handler_apply_config()/time_service_begin() lesen die Felder aus
    // app_config, nicht aus doc.
    if (mqttChanged) mqtt_handler_apply_config();
    if (timeChanged) time_service_begin();
}

void handlePostConfig(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_configBody);
    s_configBody = "";
    if (err) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }

    applyConfigFields(doc);
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

// Speichert + wendet ein Layout-JSON sofort an (ueber die Thread-Safe-Queue,
// siehe display_layout.h - handlePostLayout() laeuft im AsyncTCP-Task, darf
// LVGL niemals direkt anfassen). Gemeinsam von handlePostLayout() und
// handleConfigImport() genutzt. Liefert false nur bei "zu gross fuer NVS"
// (siehe layout_store_save()).
//
// Ein explizit gespeichertes LEERES Widgets-Array ("Alles entfernen" +
// Speichern) wuerde sonst dauerhaft einen leeren Screen persistieren - von
// aussen nicht mehr von "noch nie gespeichert" unterscheidbar, aber OHNE den
// Fallback auf das generierte Grundlayout, da der JSON-String selbst nicht
// leer ist (siehe layout_store_load()). Nur "leer", wenn BEIDE Arrays leer
// sind - ein Standby-only oder Dashboard-only gespeichertes Layout (nur auf
// Mono ueberhaupt relevant) darf nicht faelschlich als Reset gewertet
// werden. Stattdessen wird der Store in diesem Fall geleert, damit GET
// /api/layout und der naechste Boot wieder auf das Grundlayout
// zurueckfallen - konsistent mit "Auf Grundlayout zuruecksetzen" statt
// einer Sackgasse.
bool applyLayoutJson(const String &json)
{
    JsonDocument doc;
    bool isEmpty = deserializeJson(doc, json) == DeserializationError::Ok &&
                   doc["widgets"].as<JsonArrayConst>().size() == 0 &&
                   doc["standby_widgets"].as<JsonArrayConst>().size() == 0;
    if (isEmpty) {
        layout_store_clear();
        String defaultJson = layout_default_json(displayWidthPx(), displayHeightPx());
        layout_queue_push_apply(defaultJson);
        return true;
    }

    if (!layout_store_save(json)) {
        return false;
    }
    layout_queue_push_apply(json);
    return true;
}

void handlePostLayout(AsyncWebServerRequest *request)
{
    if (!displaySupportsLayoutEditor()) {
        s_layoutBody = "";
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"layout editor not supported on this display\"}");
        return;
    }

    bool ok = applyLayoutJson(s_layoutBody);
    s_layoutBody = "";
    if (!ok) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"layout too large\"}");
        return;
    }
    request->send(200, "application/json", "{\"ok\":true}");
}

// Buendelt Konfiguration + (falls vorhanden) Layout in eine einzige
// herunterladbare JSON-Datei (Issue #63 - Backup/Restore). include_secrets
// ist ein bewusstes Opt-in im Dashboard (Checkbox "gefaehrlich") - ohne
// diesen Parameter enthaelt die Datei weder mqtt_pass noch weather_api_key.
void handleConfigExport(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    buildConfigDoc(doc, /*includeSecrets=*/request->hasParam("include_secrets"));

    // Layout nur auf Displays mit Editor mit exportieren (runde Displays
    // haben keine Widget-Liste, siehe displaySupportsLayoutEditor()) -
    // dieselbe Grundlayout-Fallback-Logik wie handleGetLayout().
    if (displaySupportsLayoutEditor()) {
        String layoutJson = layout_store_load();
        if (layoutJson.length() == 0) {
            layoutJson = layout_default_json(displayWidthPx(), displayHeightPx());
        }
        deserializeJson(doc["layout"], layoutJson);
    }

    String out;
    serializeJson(doc, out);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", out);
    // Loest im Browser direkt den nativen Datei-Download aus, kein Blob-/JS-
    // Umweg im Dashboard noetig.
    response->addHeader("Content-Disposition", "attachment; filename=\"pulse-config.json\"");
    request->send(response);
}

// Gegenstueck zu handleConfigExport() - erwartet dieselbe Datei zurueck.
// Wendet Config-Felder wie handlePostConfig() an (kein Live-Apply, manueller
// Neustart empfohlen, siehe dashboard.html), das Layout dagegen sofort (wie
// handlePostLayout()). Ein "layout"-Schluessel wird still ignoriert, wenn
// dieses Display keinen Layout-Editor hat (z.B. Import einer CYD-Config auf
// ein rundes Display) - kein Fehlerfall.
void handleConfigImport(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_importBody);
    s_importBody = "";
    if (err) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }

    applyConfigFields(doc);
    config_store_save(&app_config);

    bool layoutApplied = false;
    if (doc["layout"].is<JsonObject>() && displaySupportsLayoutEditor()) {
        String layoutJson;
        serializeJson(doc["layout"], layoutJson);
        layoutApplied = applyLayoutJson(layoutJson);
    }

    JsonDocument resp;
    resp["ok"] = true;
    resp["layout_applied"] = layoutApplied;
    String out;
    serializeJson(resp, out);
    request->send(200, "application/json", out);
}

void handleReboot(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
}

// Dieselbe (einzige) Seite fuer "/", "/settings", "/backup", "/fota" und
// "/editor" - es ist eine Single-Page-App, das JS entscheidet anhand von
// location.pathname beim Laden und history.pushState() beim Tab-Wechsel,
// welcher Tab aktiv ist (siehe dashboard.html "--- Tabs ---"). So sind alle
// Tabs direkt per URL erreichbar/verlinkbar/reload-fest, ohne eine zweite
// Kopie der Seite zu brauchen.
void handleDashboard(AsyncWebServerRequest *request)
{
    AsyncWebServerResponse *response = request->beginResponse(
        200, "text/html",
        web_dashboard_html_gz_start,
        web_dashboard_html_gz_end - web_dashboard_html_gz_start);
    response->addHeader("Content-Encoding", "gzip");
    // Ohne diesen Header darf der Browser eine ALTE HTML/JS-Version zwischen-
    // speichern und bei einem normalen Reload (nicht Hard-Refresh) weiter
    // ausliefern, obwohl die Firmware auf dem Geraet laengst aktueller ist -
    // fuehrte konkret dazu, dass der Editor mit veraltetem JS gegen einen
    // neuen Server sprach (z.B. kein "standby_widgets" im POST-Body) und
    // Aktionen wie "Auf Grundlayout zuruecksetzen" scheinbar wirkungslos
    // blieben. Die /api/*-Endpunkte hatten dieses Problem schon vorher nicht
    // (siehe deren eigene "Cache-Control: no-store"), nur die Seite selbst
    // (HTML+JS) war ungeschuetzt.
    response->addHeader("Cache-Control", "no-store");
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

// Bisher komplett unbeobachtet (kein einziger log_*-Aufruf in diesem
// Handler) - ein Fehlschlag zeigte sich im Serial-Log als nichts, nur als
// "Upload fehlgeschlagen" im Browser (dashboard.html). Jetzt an jedem
// Schritt geloggt, der stillschweigend fehlschlagen kann: Update.begin()
// (z.B. zu wenig Platz in der Ziel-OTA-Partition - siehe partitions_4mb.csv,
// die App-Partitionen sind mit den letzten Erweiterungen (Mono-Layout-
// Editor, Forecast) auf ueber 90% Auslastung gewachsen, das wird knapp),
// Update.write() (Rueckgabewert < len ist ein Fehler, Update.write() wirft
// nicht) und Update.end().
void handleOtaUpload(AsyncWebServerRequest * /*request*/, String filename, size_t index,
                      uint8_t *data, size_t len, bool final)
{
    if (index == 0) {
        // Falls ein vorheriger Upload nie den "final"-Chunk erreicht hat
        // (Client gibt auf/Verbindung reisst ab, z.B. weil der bisherige
        // 15s-Timeout in dashboard.html bei einem ~1.8MB-Image mit
        // gleichzeitigem Flash-Schreiben zu knapp war) bleibt Update.begin()
        // fuer diese eine, GLOBALE Update-Instanz auf "laeuft noch" stehen -
        // jeder weitere Versuch scheitert dann dauerhaft mit "already
        // running" (Updater.cpp), bis zum naechsten Reboot. Explizit
        // abbrechen statt das stillschweigend zu blockieren, bevor neu
        // begonnen wird.
        if (Update.isRunning()) {
            log_w("Vorheriger OTA-Upload war noch offen (nie 'final' erreicht) - breche ab und starte neu.");
            Update.abort();
        }
        // Pausiert den Wetter-Task fuer die Dauer des Uploads (siehe
        // shared_state.h) - unabhaengig vom Ausgang unten wieder freigegeben,
        // sonst bliebe er nach einem fehlgeschlagenen Update dauerhaft aus.
        ota_in_progress = true;
        bool began = Update.begin(UPDATE_SIZE_UNKNOWN);
        log_w("OTA-Upload gestartet: Datei='%s', freier Heap=%u Bytes, Update.begin()=%s",
              filename.c_str(), ESP.getFreeHeap(), began ? "ok" : "FEHLGESCHLAGEN");
        if (!began) {
            log_e("Update.begin() fehlgeschlagen: %s", Update.errorString());
        }
    }
    if (len) {
        size_t written = Update.write(data, len);
        if (written != len) {
            log_e("Update.write() Kurzschreibung bei Offset %u: %u von %u Bytes, Fehler: %s",
                  (unsigned)index, (unsigned)written, (unsigned)len, Update.errorString());
        }
    }
    if (final) {
        bool ok = Update.end(true);
        log_w("OTA-Upload abgeschlossen bei Offset %u: Update.end()=%s%s",
              (unsigned)(index + len), ok ? "ok" : "FEHLGESCHLAGEN",
              ok ? "" : (String(", Fehler: ") + Update.errorString()).c_str());
        ota_in_progress = false;
    }
}

void handleOtaDone(AsyncWebServerRequest *request)
{
    bool ok = !Update.hasError();
    if (!ok) {
        log_e("handleOtaDone: Update.hasError() -> %s", Update.errorString());
    }
    AsyncWebServerResponse *response = request->beginResponse(
        ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    response->addHeader("Connection", "close");
    request->send(response);
    if (ok) {
        delay(200);
        ESP.restart();
    }
}

// Liest nur das Ergebnis des letzten github_ota_check()-Aufrufs (egal ob der
// aus diesem oder einem frueheren Boot stammt, siehe ota_boot_run_pending_
// action() in github_ota.cpp) - loest selbst KEINEN Check aus, siehe
// handleFotaCheckTrigger() unten dafuer. has_result unterscheidet "noch nie
// geprueft" (leere current_version/error) von einem tatsaechlichen Ergebnis,
// dashboard.html zeigt im ersten Fall einen neutralen Hinweis statt "kein
// Update verfuegbar (neueste Version: v)".
void handleFotaCheckResult(AsyncWebServerRequest *request)
{
    bool hasResult = github_ota_latest_version()[0] != '\0' || github_ota_error()[0] != '\0';
    bool ok = github_ota_error()[0] == '\0';
    JsonDocument doc;
    doc["ok"] = ok;
    doc["has_result"] = hasResult;
    doc["current_version"] = FW_VERSION;
    doc["latest_version"] = github_ota_latest_version();
    doc["update_available"] = github_ota_update_available();
    if (!ok) {
        doc["error"] = github_ota_error();
    }
    String out;
    serializeJson(doc, out);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", out);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

#if defined(BOARD_HAS_PSRAM) || defined(FOTA_DIRECT_CHECK)
// Boards mit PSRAM haben genug freien Heap fuer den GitHub-TLS-Handshake
// auch im laufenden Betrieb (siehe github_ota.h fuer die Gegenseite) - Check
// und Update laufen deshalb hier direkt im Request-Handler, kein Reboot-
// Umweg noetig. Antwortform bewusst OHNE "rebooting" - dashboard.html
// unterscheidet daran diesen Fall vom Reboot-Fall unten.
//
// FOTA_DIRECT_CHECK: Opt-in-Flag fuer denselben direkten Weg auf einem Board
// OHNE PSRAM. Auf dem CYD getestet (nachdem github_ota_check() auf den
// leichtgewichtigen github.com-Redirect umgestellt wurde statt der ~14-20 KB
// grossen api.github.com-JSON-Antwort) und live wieder mit mbedtls -10368
// "X509 - Allocation of memory failed" gescheitert - der im laufenden
// Betrieb (WiFiManager/AsyncWebServer/MQTT/LVGL-Pool bereits reserviert)
// verbleibende ~35 KB grosse zusammenhaengende Block reicht selbst fuer den
// minimalen Check-Handshake nicht, das war keine Eigenschaft der grossen
// JSON-Antwort, sondern strukturell. Deshalb fuer cyd_base bewusst NICHT
// gesetzt (siehe platformio.ini) - bleibt als Flag erhalten, falls es sich
// auf einem anderen zukuenftigen Board ohne PSRAM lohnt, das erneut zu
// pruefen.
void handleFotaCheckTrigger(AsyncWebServerRequest *request)
{
    bool ok = github_ota_check();
    JsonDocument doc;
    doc["ok"] = ok;
    doc["current_version"] = FW_VERSION;
    doc["latest_version"] = github_ota_latest_version();
    doc["update_available"] = github_ota_update_available();
    if (!ok) {
        doc["error"] = github_ota_error();
    }
    String out;
    serializeJson(doc, out);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", out);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

// Startet Download+Flash nur noch (github_ota_start_update() spawnt einen
// eigenen Task und kehrt sofort zurueck) - Fortschritt/Ergebnis holt sich
// dashboard.html per Polling ueber handleFotaProgress() unten.
void handleFotaUpdateTrigger(AsyncWebServerRequest *request)
{
    bool started = github_ota_start_update();
    JsonDocument doc;
    doc["ok"] = started;
    if (!started) {
        doc["error"] = github_ota_error();
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}
#else
// Boards OHNE PSRAM (CYD, ESP32-C3): stoesst KEINEN Check hier im laufenden
// Betrieb an, sondern nur noch beim NAECHSTEN Boot, VOR Display-/LVGL-/MQTT-
// Init (siehe github_ota.h, ota_boot_run_pending_action()) - der GitHub-TLS-
// Handshake braucht mehr zusammenhaengenden Heap, als im laufenden Betrieb
// frei ist (live beobachtet: mbedtls X509/BIGNUM-Allokationsfehler trotz
// ausgereizter TLS-Puffer-Tuning, siehe platformio.ini). dashboard.html
// pollt nach dem Reboot /api/status, bis das Geraet wieder antwortet, und
// liest dann per GET auf denselben Pfad (handleFotaCheckResult oben) das
// frische Ergebnis.
void handleFotaCheckTrigger(AsyncWebServerRequest *request)
{
    github_ota_set_pending_boot_action(OTA_BOOT_ACTION_CHECK);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    response->addHeader("Connection", "close");
    request->send(response);
    delay(200);
    ESP.restart();
}

// Dieselbe Begruendung wie handleFotaCheckTrigger() oben - Download+Flash
// passieren jetzt ebenfalls erst im naechsten Boot, VOR Display-/LVGL-/MQTT-
// Init (ota_boot_run_pending_action() ruft dort selbst github_ota_check()
// erneut auf, um ein zwischenzeitlich neues Release/Asset zu erkennen, dann
// bei Bedarf github_ota_start_update()). Live-Fortschritt (Bytes) ueber
// handleFotaProgress() ist waehrend dieses Downloads NICHT erreichbar - der
// Webserver startet ja selbst erst danach - dashboard.html zeigt
// stattdessen nur einen "laedt, Geraet startet neu" Hinweis und erkennt
// Erfolg/Misserfolg nach dem Reboot am esp.fw_version-Feld von /api/status
// bzw. am state/error von handleFotaProgress().
void handleFotaUpdateTrigger(AsyncWebServerRequest *request)
{
    if (!github_ota_update_available()) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"Kein Update bekannt - zuerst pruefen\"}");
        return;
    }
    github_ota_set_pending_boot_action(OTA_BOOT_ACTION_UPDATE);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    response->addHeader("Connection", "close");
    request->send(response);
    delay(200);
    ESP.restart();
}
#endif

void handleFotaProgress(AsyncWebServerRequest *request)
{
    GithubOtaState state = github_ota_state();
    JsonDocument doc;
    doc["state"] = (int)state;
    doc["done"] = (uint32_t)github_ota_bytes_done();
    doc["total"] = (uint32_t)github_ota_bytes_total();
    if (state == GITHUB_OTA_ERROR) {
        doc["error"] = github_ota_error();
    }
    String out;
    serializeJson(doc, out);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", out);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
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
    s_server.on("/backup", HTTP_GET, handleDashboard);
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
    // Wie bei /api/layout weiter unten: die spezifischeren /api/config/...-
    // Pfade muessen VOR dem einfachen String-URI "/api/config" registriert
    // werden, da ESPAsyncWebServer letzteren als "BackwardCompatible"-Matcher
    // behandelt (regex-aequivalent zu ^/api/config(/.*)?$) und sonst jede
    // Anfrage an /api/config/export bzw. /api/config/import faelschlich von
    // handleGetConfig()/handlePostConfig() beantwortet wuerde.
    s_server.on("/api/config/export", HTTP_GET, handleConfigExport);
    s_server.on(
        "/api/config/import", HTTP_POST, handleConfigImport, nullptr,
        [](AsyncWebServerRequest * /*request*/, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) s_importBody = "";
            s_importBody.concat((const char *)data, len);
            (void)total;
        });
    s_server.on("/api/config", HTTP_GET, handleGetConfig);
    s_server.on(
        "/api/config", HTTP_POST, handlePostConfig, nullptr,
        [](AsyncWebServerRequest * /*request*/, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) s_configBody = "";
            s_configBody.concat((const char *)data, len);
            (void)total;
        });
    // Reihenfolge wichtig: ESPAsyncWebServer behandelt einen einfachen
    // String-URI wie "/api/layout" als "BackwardCompatible"-Matcher, der
    // regex-aequivalent zu ^/api/layout(/.*)?$ ist - passt also OHNE
    // weiteres Zutun auch auf "/api/layout/default". Handler werden in
    // Registrierungsreihenfolge geprueft, der erste Treffer gewinnt (siehe
    // AsyncWebServer::_attachHandler()) - stuende "/api/layout" zuerst,
    // wuerde JEDE Anfrage an "/api/layout/default" von handleGetLayout()
    // beantwortet (liefert das GESPEICHERTE statt des generierten
    // Grundlayouts) und handleGetLayoutDefault() waere faktisch toter Code.
    // Der spezifischere Pfad muss deshalb zuerst registriert werden.
    s_server.on("/api/layout/default", HTTP_GET, handleGetLayoutDefault);
    s_server.on("/api/layout", HTTP_GET, handleGetLayout);
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
    s_server.on("/api/fota/check", HTTP_GET, handleFotaCheckResult);
    s_server.on("/api/fota/check", HTTP_POST, handleFotaCheckTrigger);
    s_server.on("/api/fota/update", HTTP_POST, handleFotaUpdateTrigger);
    s_server.on("/api/fota/progress", HTTP_GET, handleFotaProgress);

    s_server.begin();
}
