#pragma once
#include <Arduino.h>

// Prueft die neueste GitHub-Release (kurim/pulse-esp32-hwmonitor) und findet
// das zum aktuell laufenden Board passende Asset (Name endet auf
// "-pio-<FW_OTA_ENV_SLUG>-ota.bin", siehe .github/workflows/build.yaml und
// die build_flags je Env in platformio.ini). Blockierend (ein HTTPS-Request,
// mehrere hundert ms bis wenige Sekunden) - nur aus einem Web-Handler bei
// explizitem Nutzer-Klick aufrufen ("Auf Updates pruefen"), nie automatisch/
// periodisch. TLS-Zertifikatspruefung bewusst deaktiviert (setInsecure() in
// github_ota.cpp) - User-Entscheidung: kein Zertifikat im Firmware-Image zu
// pflegen/kein Bricking-Risiko bei einer CA-Rotation durch GitHub, dieselbe
// pragmatische Haltung wie beim frei rotierbaren Wetter-API-Key. Sicherheit
// kommt stattdessen daher, dass Update.end() das heruntergeladene Image
// ohnehin als gueltiges Firmware-Image validiert.
bool github_ota_check(void);

// Ergebnis des letzten github_ota_check()-Aufrufs. Leere Strings, wenn noch
// nie erfolgreich geprueft.
const char *github_ota_latest_version(void);
const char *github_ota_error(void);
bool        github_ota_update_available(void);

enum GithubOtaState {
    GITHUB_OTA_IDLE = 0,
    GITHUB_OTA_RUNNING = 1,
    GITHUB_OTA_SUCCESS = 2,
    GITHUB_OTA_ERROR = 3,
};

// Startet Download+Flash des von github_ota_check() gefundenen Assets in
// einem eigenen FreeRTOS-Task (dieselbe Update.begin()/write()/end()-
// Maschinerie wie handleOtaUpload() in web_portal.cpp fuer lokale Uploads).
// Kehrt SOFORT zurueck - blockiert NICHT den aufrufenden Task. Das ist
// bewusst anders als github_ota_check(): eine vorherige Version fuehrte
// Download+Flash synchron im AsyncTCP-Request-Handler aus, was diesen Task
// fuer die gesamte Dauer blockierte - bei einem Image, dessen Download+
// Flash-Schreiben laenger als CONFIG_ESP_TASK_WDT_TIMEOUT_S (siehe
// platformio.ini) dauerte, loeste das den Task-Watchdog fuer "async_tcp"
// aus und rebootete das Geraet mitten im Update (live beobachtet: ~70s
// Laufzeit fuer ein ~1.8MB-Image). Fortschritt/Ergebnis ueber
// github_ota_state()/github_ota_bytes_done()/github_ota_bytes_total()/
// github_ota_error() abfragen (Polling, siehe handleFotaProgress() in
// web_portal.cpp). Setzt bei Erfolg selbst ESP.restart(), bei Misserfolg
// GITHUB_OTA_ERROR. Setzt einen vorherigen erfolgreichen github_ota_check()
// mit github_ota_update_available()==true voraus.
bool github_ota_start_update(void);

// Ein einzelner Abbruch/Stall wird bis zu 20x per HTTP-Range-Request ab der
// zuletzt geschriebenen Byte-Position fortgesetzt (siehe github_ota.cpp) -
// GitHub-Release-Assets liefern dafuer zuverlaessig ein Content-Length, ein
// Server, der Range ignoriert, bricht sofort ab statt weiter zu versuchen.
GithubOtaState github_ota_state(void);
size_t         github_ota_bytes_done(void);
size_t         github_ota_bytes_total(void);

// Aktion, die ota_boot_run_pending_action() beim naechsten Boot ausfuehren
// soll (in NVS persistiert, siehe github_ota.cpp) - siehe dortiger Kommentar
// fuer die Begruendung (TLS-Heap-Engpass auf Boards ohne PSRAM, CYD/C3).
enum OtaBootAction {
    OTA_BOOT_ACTION_NONE   = 0,
    OTA_BOOT_ACTION_CHECK  = 1,
    OTA_BOOT_ACTION_UPDATE = 2,
};

// Persistiert 'action' in NVS, OHNE selbst neuzustarten - der Aufrufer (ein
// Web-Handler, siehe web_portal.cpp) muss vorher noch seine HTTP-Antwort
// verschicken, der eigentliche ESP.restart() passiert dort danach, exakt wie
// bei handleOtaDone() fuer den lokalen Upload.
void github_ota_set_pending_boot_action(OtaBootAction action);

// Muss als ALLERERSTES in setup() aufgerufen werden, noch vor jeglicher
// Display-/LVGL-/WiFiManager-AP-/MQTT-Initialisierung, direkt nach
// wifi_provision_begin() (siehe main.cpp). Liest die oben per
// github_ota_set_pending_boot_action() persistierte Aktion; ist keine
// gesetzt, kehrt die Funktion sofort zurueck (No-Op, der normale Bootpfad
// aendert sich fuer den ueblichen Fall in keiner Weise).
//
// Ist eine Aktion gesetzt: wartet synchron (max. 20s) auf eine WLAN-
// Verbindung, fuehrt dann github_ota_check() aus und - bei
// OTA_BOOT_ACTION_UPDATE und verfuegbarem Update - den Download+Flash
// (github_ota_start_update(), synchron abgewartet). Der Grund, warum das
// hier oben in setup() passieren muss statt wie frueher aus einem laufenden
// Web-Handler heraus: der GitHub-TLS-Handshake braucht mehr zusammen-
// haengenden Heap, als auf Boards ohne PSRAM (CYD, ESP32-C3, siehe
// CLAUDE.md) im laufenden Betrieb frei ist, sobald LVGLs 48-KB-Speicherpool
// (lv_conf.h), WiFiManager/AsyncWebServer und ggf. der MQTT-Client bereits
// reserviert sind (live gemessen: nur ~35 KB zusammenhaengend frei, mbedtls
// X509/BIGNUM-Allokationsfehler beim Handshake mit api.github.com) - an
// diesem Punkt in setup(), vor all dem, ist der Speicher dagegen noch
// (fast) komplett frei.
//
// Kehrt bei erfolgreichem Update NIE zurueck (ESP.restart() passiert bereits
// innerhalb von github_ota_start_update()'s Task). Bei einer reinen Pruefung
// oder einem fehlgeschlagenen Update kehrt sie zurueck, und setup() faehrt
// mit der Display-/LVGL-/MQTT-Initialisierung wie gewohnt fort - Ergebnis/
// Fehler stehen dann bereits ueber github_ota_error()/github_ota_state()
// fuer das Dashboard bereit, ohne dass ein erneuter Check noetig waere.
void ota_boot_run_pending_action(void);
