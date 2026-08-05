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

// Laedt das von github_ota_check() gefundene Asset herunter und flasht es
// (dieselbe Update.begin()/write()/end()-Maschinerie wie handleOtaUpload()
// in web_portal.cpp fuer lokale Uploads) - blockierend, startet das Geraet
// bei Erfolg neu. Setzt einen vorherigen erfolgreichen github_ota_check()
// mit github_ota_update_available()==true voraus.
bool github_ota_perform_update(void);
