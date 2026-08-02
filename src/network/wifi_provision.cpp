#include "wifi_provision.h"
#include "shared_state.h"
#include "config_store.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ImprovWiFiLibrary.h>
#ifdef IMPROV_WIFI_BLE_ENABLED
#include <ImprovWiFiBLE.h>
#endif
#include <ESPAsyncWebServer.h>
#include <wifimanager.h>
#include "web_portal.h"

// s_improv liest bei Bedarf direkt von Serial - siehe wifi_provision_loop():
// nur aktiv, wenn serial_handler.cpp den Port nicht schon fuer
// HW_SOURCE_USB-Daten besitzt. Beide gleichzeitig aktiv wuerde um dieselben
// Bytes konkurrieren (ImprovWiFi bietet keine API, um ihm einzelne, bereits
// gelesene Bytes zuzufuehren - siehe Doku dort). Dies ist der EINZIGE Ort im
// Projekt, der ImprovWiFi(&Serial) instanziiert - ein zweites Serial-Improv-
// Objekt anderswo (z.B. main.cpp) wuerde um dieselben Bytes konkurrieren und
// jede USB-Provisionierung unbrauchbar machen.
static ImprovWiFi s_improv(&Serial);
static bool        s_improv_active;

#ifdef IMPROV_WIFI_BLE_ENABLED
// s_improvBle wird erst gestartet, nachdem das Zeitfenster fuer den
// WiFi.begin()-Versuch oben bereits abgelaufen ist (siehe
// wifi_provision_loop()) - setDeviceInfo() ruft intern BLEDevice::init()
// auf und startet sofort das Advertising. Parallel zu einem noch laufenden
// WiFi.begin()-Verbindungsversuch fuehrt das auf ESP32/S3 zu einem
// Radio-Coexistence-Konflikt (beobachtet als "esp_wifi_set_ps failed" in
// STA.cpp) - siehe auch das Referenzbeispiel der Fork-Doku
// (examples/arduino-BLE/src/main.cpp), das BLE ebenfalls erst nach einem
// gescheiterten WiFi.begin()-Versuch startet.
static ImprovWiFiBLE s_improvBle;
#endif

// Fallback: eigener Captive-Portal-AP, nur gestartet wenn weder Improv noch
// bekannte Zugangsdaten zu einer Verbindung fuehren. Eigene NVS-Namespace/
// Zugangsdatenliste, unabhaengig von app_config - siehe wifi_provision.h.
// Nutzt den gemeinsamen AsyncWebServer aus web_portal.cpp statt einer
// eigenen Instanz - zwei Server auf Port 80 wuerden beim .begin()
// kollidieren, sobald es neben dem Dashboard auch noch das Captive Portal
// gibt (web_portal_begin() laeuft immer, nicht nur im Fallback).
static WIFIMANAGER s_wm("pulsewifi");

#if CONFIG_IDF_TARGET_ESP32S3
static constexpr ImprovTypes::ChipFamily kChipFamily = ImprovTypes::ChipFamily::CF_ESP32_S3;
#elif CONFIG_IDF_TARGET_ESP32C3
static constexpr ImprovTypes::ChipFamily kChipFamily = ImprovTypes::ChipFamily::CF_ESP32_C3;
#else
static constexpr ImprovTypes::ChipFamily kChipFamily = ImprovTypes::ChipFamily::CF_ESP32;
#endif

// Zustandsautomat, von wifi_provision_loop() angetrieben - ersetzt die
// fruehere blockierende Warteschleife (siehe wifi_provision.h fuer die
// Begruendung: lv_timer_handler() muss waehrenddessen weiterlaufen koennen).
enum class ProvisionState {
    kTryingCreds, // WiFi.begin() laeuft, ggf. parallel Serial-Improv
    kTryingBle,   // nur erreicht wenn IMPROV_WIFI_BLE_ENABLED
    kDone,        // verbunden ODER Captive-Portal-Fallback laeuft autonom
};

static ProvisionState s_state         = ProvisionState::kDone;
static uint32_t       s_stateStartMs  = 0;
static uint32_t       s_credsWindowMs = 5000;

static void onWiFiEvent(WiFiEvent_t event)
{
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        wifi_connected = true;
        // Falls die Verbindung ueber den WiFiManager-Fallback zustande kam
        // (andere Zugangsdaten als zuletzt in app_config gespeichert), hier
        // nachziehen, damit app_config die aktuell genutzten Credentials
        // widerspiegelt. Bei Improv/SDK-Pfad sind SSID/PSK bereits identisch,
        // der Vergleich verhindert unnoetige Flash-Schreibzugriffe.
        if (WiFi.SSID() != app_config.wifi_ssid || WiFi.psk() != app_config.wifi_pass) {
            strlcpy(app_config.wifi_ssid, WiFi.SSID().c_str(), sizeof(app_config.wifi_ssid));
            strlcpy(app_config.wifi_pass, WiFi.psk().c_str(), sizeof(app_config.wifi_pass));
            config_store_save(&app_config);
        }
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        wifi_connected = false;
        break;
    default:
        break;
    }
}

static void onImprovError(ImprovTypes::Error /*err*/)
{
    // Provisionierung abgebrochen/fehlgeschlagen - keine Aktion noetig.
}

static void onImprovConnected(const char *ssid, const char *password)
{
    strlcpy(app_config.wifi_ssid, ssid, sizeof(app_config.wifi_ssid));
    strlcpy(app_config.wifi_pass, password, sizeof(app_config.wifi_pass));
    config_store_save(&app_config);
}

static void startCaptivePortalFallback()
{
    // WiFiManager (tzapu) ist dafuer nicht nutzbar (bricht die Network-Core-
    // Kompilierung unter arduino-esp32 3.x/pioarduino) - stattdessen
    // martinverges/ESP32-Wifi-Manager: nutzt ESPAsyncWebServer (ohnehin
    // Projekt-Dependency), eigene NVS-Verwaltung, kein WiFiManager-Konflikt.
    log_w("Kein WLAN verbunden - starte Captive-Portal-Fallback (AP '%s-Setup').", BOARD_NAME);

    if (app_config.wifi_ssid[0] != '\0') {
        s_wm.addWifi(app_config.wifi_ssid, app_config.wifi_pass);
    }
    s_wm.fallbackToSoftAp(true);
    s_wm.startBackgroundTask(String(BOARD_NAME) + "-Setup");

    // Verbindung/AP laufen ab hier autonom in einer eigenen Task weiter
    // (WIFIMANAGER::loop()) - wifi_provision_loop() hat danach nichts mehr
    // selbst zu tun. Neue Zugangsdaten, die der Nutzer ueber die Captive-
    // Portal-UI eintraegt, werden ueber onWiFiEvent()/
    // ARDUINO_EVENT_WIFI_STA_GOT_IP nach app_config uebernommen.
}

void wifi_provision_begin(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(onWiFiEvent);

    // Registriert /wifi + /api/wifi/* auf dem gemeinsamen Server, unabhaengig
    // davon ob spaeter ueberhaupt der Captive-Portal-Fallback ausgeloest
    // wird - so ist die WLAN-Sektion des Web-Dashboards auch im Normalbetrieb
    // (Verbindung ueber gespeicherte Zugangsdaten) erreichbar. Das reine
    // Registrieren der Routen startet noch kein AP, das macht weiterhin
    // ausschliesslich startCaptivePortalFallback() ueber fallbackToSoftAp().
    s_wm.attachWebServer(&web_portal_server());
    s_wm.attachUI();

    // Serial nur fuer Improv beanspruchen, wenn es nicht schon fuer
    // Hardwaredaten reserviert ist (siehe serial_handler.cpp).
    s_improv_active = (app_config.hw_source == HW_SOURCE_MQTT);
    s_credsWindowMs = 5000;

    if (s_improv_active) {
        s_improv.setDeviceInfo(kChipFamily, "PulseMQTT", FW_VERSION, BOARD_NAME);
        s_improv.onImprovError(onImprovError);
        s_improv.onImprovConnected(onImprovConnected);
        s_credsWindowMs = 20000; // laenger, da hier aktiv auf Improv-Befehle gewartet wird
    }

    // Erst mit den in app_config gespeicherten Zugangsdaten versuchen; ohne
    // die (frischer Flash, noch nichts konfiguriert) auf die vom ESP-SDK
    // gemerkten Credentials zurueckfallen.
    if (app_config.wifi_ssid[0] != '\0') {
        WiFi.begin(app_config.wifi_ssid, app_config.wifi_pass);
    } else {
        WiFi.begin();
    }

    s_state        = ProvisionState::kTryingCreds;
    s_stateStartMs = millis();
}

void wifi_provision_loop(void)
{
    // Unabhaengig vom State: Improv-ueber-Serial soll jederzeit erreichbar
    // sein, nicht nur waehrend des kurzen kTryingCreds-Zeitfensters direkt
    // nach dem Boot. Sonst verpasst man als Nutzer das Fenster fast immer -
    // WiFi.begin() mit bereits gespeicherten Zugangsdaten verbindet oft
    // innerhalb von Millisekunden, danach ist der State laengst kDone und
    // handleSerial() wuerde nie wieder aufgerufen (Symptom: Browser-Tool
    // findet den Port, aber das Geraet antwortet nie auf Improv-Kommandos).
    if (s_improv_active) {
        s_improv.handleSerial();
    }

    switch (s_state) {
    case ProvisionState::kTryingCreds:
        if (WiFi.status() == WL_CONNECTED) {
            s_state = ProvisionState::kDone;
            break;
        }
        if (millis() - s_stateStartMs < s_credsWindowMs) {
            break;
        }
#ifdef IMPROV_WIFI_BLE_ENABLED
        // Naechste Stufe: Improv-ueber-BLE. Bewusst erst hier gestartet,
        // nicht parallel zur vorigen Stufe - siehe Kommentar bei
        // s_improvBle weiter oben.
        log_w("Kein WLAN verbunden - starte Improv-BLE-Fallback.");
        s_improvBle.setDeviceInfo(kChipFamily, "PulseMQTT", FW_VERSION, BOARD_NAME);
        s_improvBle.onImprovError(onImprovError);
        s_improvBle.onImprovConnected(onImprovConnected);
        s_state        = ProvisionState::kTryingBle;
        s_stateStartMs = millis();
#else
        startCaptivePortalFallback();
        s_state = ProvisionState::kDone;
#endif
        break;

#ifdef IMPROV_WIFI_BLE_ENABLED
    case ProvisionState::kTryingBle:
        if (WiFi.status() == WL_CONNECTED) {
            s_state = ProvisionState::kDone;
            break;
        }
        if (millis() - s_stateStartMs < 30000) {
            break;
        }
        startCaptivePortalFallback();
        s_state = ProvisionState::kDone;
        break;
#endif

    case ProvisionState::kDone:
    default:
        break;
    }
}

wifi_provision_phase_t wifi_provision_get_phase(void)
{
    if (wifi_connected) {
        return WIFI_PROVISION_CONNECTED;
    }
    // s_state == kDone ohne wifi_connected kann nur ueber
    // startCaptivePortalFallback() erreicht worden sein (der andere Weg
    // dorthin - WL_CONNECTED - ist oben bereits abgefangen).
    return (s_state == ProvisionState::kDone) ? WIFI_PROVISION_SETUP_MODE
                                               : WIFI_PROVISION_CONNECTING;
}

void wifi_provision_forget(void)
{
    Preferences p;
    if (p.begin("pulsewifi", false)) {
        p.clear();
        p.end();
    }
}
