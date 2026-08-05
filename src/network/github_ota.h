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
