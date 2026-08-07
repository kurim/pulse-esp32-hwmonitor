#include "github_ota.h"
#include "shared_state.h"
#include "mqtt_handler.h"
#include "wifi_provision.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef FW_OTA_ENV_SLUG
// Sollte ueber platformio.ini (build_flags je [env:*]) immer gesetzt sein -
// dieser Fallback verhindert nur, dass ein versehentlich fehlender Flag
// stillschweigend JEDEN Asset-Namen matcht (leerer Suffix wuerde das tun),
// sondern stattdessen zuverlaessig KEIN Asset findet.
#define FW_OTA_ENV_SLUG "unknown-env"
#endif

namespace {

// Web-UI-Redirect statt REST-API: github.com/{owner}/{repo}/releases/latest
// antwortet mit HTTP 302 auf .../releases/tag/{tag} - das ist alles, was wir
// brauchen (der Tag), ohne die ~14-20 KB grosse JSON-Antwort der API
// (api.github.com/.../releases/latest, 14 Assets samt Metadaten) jemals in
// EINEM zusammenhaengenden Heap-Block halten zu muessen. Auf Boards ohne
// PSRAM (CYD) reichte selbst ein auf ~35 KB reduzierter groesster freier
// Block dafuer nicht zuverlaessig aus (live beobachtet: "JSON-Parse-Fehler:
// IncompleteInput" bei ueber 13 KB empfangenen, aber nicht vollstaendigen
// Bytes - vermutlich ein Realloc-Fehlschlag mitten in String::getString()).
// Den Asset-Namen muessen wir dafuer selbst zusammenbauen (siehe unten),
// statt ihn aus der assets[]-Liste der API-Antwort zu lesen - das Format
// ist durch .github/workflows/build.yaml deterministisch vorgegeben.
const char *kReleaseRedirectUrl = "https://github.com/kurim/pulse-esp32-hwmonitor/releases/latest";
const char *kRepoUrl = "https://github.com/kurim/pulse-esp32-hwmonitor";

char s_latestVersion[16] = "";
char s_assetUrl[256] = "";
char s_error[160] = "";
bool s_updateAvailable = false;

volatile GithubOtaState s_state = GITHUB_OTA_IDLE;
volatile size_t s_bytesDone = 0;
volatile size_t s_bytesTotal = 0;

// Grobe SemVer-Zerlegung ("1.2.3" -> {1,2,3}) - FW_VERSION/Release-Tags in
// diesem Projekt nutzen kein Pre-Release-Suffix, ein numerischer
// Dreifach-Vergleich reicht.
void parseVersion(const char *v, int out[3]) {
  out[0] = out[1] = out[2] = 0;
  sscanf(v, "%d.%d.%d", &out[0], &out[1], &out[2]);
}

bool isNewer(const char *latest, const char *current) {
  int a[3], b[3];
  parseVersion(latest, a);
  parseVersion(current, b);
  for (int i = 0; i < 3; i++) {
    if (a[i] != b[i]) return a[i] > b[i];
  }
  return false;
}

} // namespace

bool github_ota_check(void) {
  s_latestVersion[0] = '\0';
  s_assetUrl[0] = '\0';
  s_error[0] = '\0';
  s_updateAvailable = false;

  if (!wifi_connected) {
    strlcpy(s_error, "Kein WLAN verbunden", sizeof(s_error));
    return false;
  }

  // Gibt den vom MQTT-Client gehaltenen Speicher fuer die Dauer des TLS-
  // Handshakes frei (siehe mqtt_handler.h) - RAII-Guard statt manuellem
  // resume() vor jedem der mehreren return-Pfade unten. Auf Boards ohne
  // PSRAM reicht der freie Heap fuer github.com sonst nicht zuverlaessig
  // (live beobachtet: mbedtls X509/BIGNUM-Allokationsfehler auf dem CYD).
  struct MqttPauseGuard {
    MqttPauseGuard() { mqtt_handler_pause(); }
    ~MqttPauseGuard() { mqtt_handler_resume(); }
  } mqttPauseGuard;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  // Explizit NICHT folgen - wir wollen die Location-Kopfzeile selbst lesen
  // (https.getLocation(), siehe unten), nicht die Zielseite (eine HTML-Seite,
  // die wir nicht brauchen und die den Heap nur unnoetig belasten wuerde).
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  https.setTimeout(10000);
  if (!https.begin(client, kReleaseRedirectUrl)) {
    strlcpy(s_error, "Verbindungsaufbau zu github.com fehlgeschlagen", sizeof(s_error));
    return false;
  }
  https.addHeader("User-Agent", "pulse-esp32-hwmonitor");

  int status = https.GET();
  if (status <= 0) {
    // status<=0 kommt aus dem TCP/TLS-Layer (HTTPClient-eigener Fehlercode,
    // z.B. HTTPC_ERROR_CONNECTION_REFUSED=-1), nicht vom HTTP-Protokoll -
    // https.errorToString() liefert dafuer nur einen generischen Text.
    // client.lastError() gibt den mbedTLS/Socket-Fehler dahinter aus (siehe
    // ssl_client.cpp), und der freie Heap zum Zeitpunkt des Fehlschlags ist
    // auf Boards ohne PSRAM (CYD, C3) der naheliegendste Verdaechtige, da der
    // TLS-Handshake einen einzelnen grossen zusammenhaengenden Block braucht.
    char tlsErr[128];
    client.lastError(tlsErr, sizeof(tlsErr));
    snprintf(s_error, sizeof(s_error), "github.com antwortete mit HTTP %d (%s, freier Heap: %u B, groesster Block: %u B)",
             status, tlsErr[0] ? tlsErr : "kein TLS-Fehlerdetail", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    https.end();
    return false;
  }
  // 302 (o.ae.) mit Location-Kopfzeile erwartet - alles andere ist
  // unerwartet (z.B. 404, falls Repo/Pfad sich je aendern sollten).
  String location = https.getLocation();
  https.end();
  if (status < 300 || status >= 400 || location.length() == 0) {
    snprintf(s_error, sizeof(s_error), "github.com/.../releases/latest antwortete mit HTTP %d statt einer Weiterleitung", status);
    return false;
  }

  // location sieht aus wie ".../releases/tag/v0.4.8" - der Tag ist das
  // letzte Pfadsegment.
  int slashIdx = location.lastIndexOf('/');
  if (slashIdx < 0 || slashIdx + 1 >= (int)location.length()) {
    strlcpy(s_error, "Unerwartetes Location-Format in der Weiterleitung", sizeof(s_error));
    return false;
  }
  String tag = location.substring(slashIdx + 1);
  const char *tagVersion = (tag[0] == 'v' || tag[0] == 'V') ? tag.c_str() + 1 : tag.c_str(); // "v1.2.3" -> "1.2.3"
  strlcpy(s_latestVersion, tagVersion, sizeof(s_latestVersion));

  // Asset-URL deterministisch nachbauen statt aus der assets[]-Liste der
  // API-Antwort zu lesen (siehe Kommentar bei kReleaseRedirectUrl oben) -
  // Name+Pfad exakt wie in .github/workflows/build.yaml erzeugt:
  // "esp32-hwmonitor-v<VERSION>-pio-<ENV>-ota.bin" unter
  // ".../releases/download/<tag>/<name>". Dieselbe github.com/.../download/
  // -URL, die auch "browser_download_url" in der API-Antwort enthalten
  // haette (nicht der direkte CDN-Link) - github_ota_start_update() folgt
  // ihrer eigenen Weiterleitung dorthin bereits mit HTTPC_FORCE_FOLLOW_REDIRECTS.
  snprintf(s_assetUrl, sizeof(s_assetUrl), "%s/releases/download/%s/esp32-hwmonitor-v%s-pio-%s-ota.bin",
           kRepoUrl, tag.c_str(), s_latestVersion, FW_OTA_ENV_SLUG);

  s_updateAvailable = isNewer(s_latestVersion, FW_VERSION);
  return true;
}

const char *github_ota_latest_version(void) { return s_latestVersion; }
const char *github_ota_error(void) { return s_error; }
bool        github_ota_update_available(void) { return s_updateAvailable; }

namespace {

// Lief frueher synchron im aufrufenden Task (siehe Git-Historie) - bei
// AsyncWebServer ist das der AsyncTCP-Request-Handler-Task selbst, der
// dabei fuer die GESAMTE Download+Flash-Dauer blockiert wurde. Das hielt
// nur, solange Download+Flash unter CONFIG_ESP_TASK_WDT_TIMEOUT_S (siehe
// platformio.ini) blieben - live beobachtet: ein ~1.8MB-Image ueberschritt
// das (~70s), der Task-Watchdog schlug fuer "async_tcp" zu und rebootete
// das Geraet mitten im Update. Jetzt ein eigener Task (siehe
// github_ota_start_update()), der AsyncTCP nie beruehrt und daher auch
// dessen Watchdog nie gefaehrdet - unabhaengig davon, wie lange Download+
// Flash tatsaechlich dauern.
void ota_update_task(void *) {
  s_bytesDone = 0;
  s_bytesTotal = 0;
  s_error[0] = '\0';
  ota_in_progress = true;
  // Siehe github_ota_check() oben - derselbe TLS-Heap-Engpass gilt fuer den
  // Download-Handshake. mqtt_handler_resume() unten deckt nur den Fehlerfall
  // ab; bei Erfolg startet ESP.restart() ohnehin neu.
  mqtt_handler_pause();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  https.setConnectTimeout(10000);
  https.setTimeout(15000);

  bool ok = false;
  bool fatal = false;
  size_t written = 0;
  int contentLen = -1;
  bool sizeKnown = false;

  // Bis zu 20 Versuche, jeweils per HTTP-Range ab der zuletzt geschriebenen
  // Position fortgesetzt - ein WLAN-Aussetzer oder ein abgebrochener TLS-
  // Stream mitten im Download (bei einem 1-2MB-Image ueber mehrere zehn
  // Sekunden nicht unwahrscheinlich) muss so nicht den gesamten Download
  // neu beginnen. Ignoriert der Server Range (Status 200 statt 206 trotz
  // Range-Header), ist Resume unmoeglich - dann sofort abbrechen statt
  // wiederholt von vorn zu laden.
  const int kMaxAttempts = 20;
  for (int attempt = 0; attempt < kMaxAttempts; attempt++) {
    if (attempt > 0) {
      delay(attempt * 1000 < 5000 ? (uint32_t)attempt * 1000 : 5000);
    }

    if (!https.begin(client, s_assetUrl)) {
      snprintf(s_error, sizeof(s_error), "Verbindungsaufbau fehlgeschlagen (Versuch %d/%d)", attempt + 1, kMaxAttempts);
      continue;
    }
    https.addHeader("User-Agent", "pulse-esp32-hwmonitor");
    if (written > 0) {
      https.addHeader("Range", String("bytes=") + written + "-");
    }

    int status = https.GET();
    int expected = written > 0 ? 206 : 200;
    if (status != expected) {
      if (written > 0 && status == 200) {
        snprintf(s_error, sizeof(s_error), "Server ignoriert Range-Resume bei %u Bytes - Abbruch", (unsigned)written);
        fatal = true;
      } else {
        snprintf(s_error, sizeof(s_error), "Asset-Download antwortete mit HTTP %d (Versuch %d/%d)", status, attempt + 1, kMaxAttempts);
      }
      https.end();
      if (fatal) break;
      continue;
    }

    if (written == 0) {
      // getSize() ist -1, wenn der Server kein Content-Length sendet (z.B.
      // bei Chunked Transfer Encoding) - UPDATE_SIZE_UNKNOWN ist dann der
      // richtige Fallback, exakt wie beim lokalen Upload in web_portal.cpp.
      // Ohne bekannte Groesse ist Range-Resume unten ausgeschlossen (siehe
      // dortiger Kommentar), das reicht aber fuer GitHub-Release-Assets in
      // der Praxis nicht - die liefern zuverlaessig ein Content-Length.
      contentLen = https.getSize();
      sizeKnown = contentLen > 0;
      s_bytesTotal = sizeKnown ? (size_t)contentLen : 0;

      // Dieselbe Absicherung wie beim lokalen Upload in web_portal.cpp
      // handleOtaUpload() - ein vorheriger, nie abgeschlossener Versuch
      // (egal ob lokal oder ueber GitHub) wuerde Update.begin() sonst
      // dauerhaft mit "already running" scheitern lassen.
      if (Update.isRunning()) {
        Update.abort();
      }
      if (!Update.begin(sizeKnown ? (size_t)contentLen : UPDATE_SIZE_UNKNOWN)) {
        snprintf(s_error, sizeof(s_error), "Update.begin() fehlgeschlagen: %s", Update.errorString());
        https.end();
        fatal = true;
        break;
      }
    }

    auto *stream = https.getStreamPtr();
    uint8_t buf[1024];
    uint32_t lastData = millis();
    bool attemptOk = false;
    bool attemptRetryable = false;
    while (!sizeKnown || written < (size_t)contentLen) {
      int got = stream->read(buf, sizeof(buf));
      if (got > 0) {
        size_t w = Update.write(buf, (size_t)got);
        if (w != (size_t)got) {
          snprintf(s_error, sizeof(s_error), "Update.write() Kurzschreibung: %s", Update.errorString());
          fatal = true;
          break;
        }
        written += (size_t)got;
        s_bytesDone = written;
        lastData = millis();
      } else if (!stream->connected() && stream->available() == 0) {
        if (!sizeKnown) {
          // Ohne Content-Length ist ein sauberes Ende nicht von einem
          // Abbruch zu unterscheiden - wie in der vorherigen Implementierung
          // als abgeschlossen werten.
          attemptOk = true;
        } else {
          snprintf(s_error, sizeof(s_error), "Verbindung abgebrochen bei %u/%d Bytes - erneuter Versuch", (unsigned)written, contentLen);
          attemptRetryable = true;
        }
        break;
      } else if (millis() - lastData > 20000) {
        snprintf(s_error, sizeof(s_error), "Download haengt bei %u Bytes seit 20s (freier Heap: %u B) - erneuter Versuch",
                 (unsigned)written, (unsigned)ESP.getFreeHeap());
        attemptRetryable = true;
        break;
      } else {
        delay(2);
      }
    }
    https.end();
    if (fatal) break;
    if (sizeKnown && written >= (size_t)contentLen) attemptOk = true;
    if (attemptOk) {
      ok = true;
      break;
    }
    if (!attemptRetryable) break; // Schleife nur ueber die while-Bedingung verlassen, kein bekannter Grund - nicht endlos weiterversuchen
  }

  ota_in_progress = false;

  if (ok) {
    ok = Update.end(true);
    if (!ok) {
      snprintf(s_error, sizeof(s_error), "Update.end() fehlgeschlagen: %s", Update.errorString());
    }
  } else {
    Update.abort();
    if (!s_error[0]) {
      snprintf(s_error, sizeof(s_error), "Download nach %d Versuchen abgebrochen", kMaxAttempts);
    }
  }

  s_state = ok ? GITHUB_OTA_SUCCESS : GITHUB_OTA_ERROR;

  if (ok) {
    delay(200);
    ESP.restart();
  }
  mqtt_handler_resume();
  vTaskDelete(nullptr);
}

} // namespace

bool github_ota_start_update(void) {
  if (s_state == GITHUB_OTA_RUNNING) {
    strlcpy(s_error, "Update laeuft bereits", sizeof(s_error));
    return false;
  }
  if (!s_assetUrl[0]) {
    strlcpy(s_error, "Kein Asset bekannt - erst github_ota_check() aufrufen", sizeof(s_error));
    return false;
  }
  if (!wifi_connected) {
    strlcpy(s_error, "Kein WLAN verbunden", sizeof(s_error));
    return false;
  }

  s_error[0] = '\0';
  s_bytesDone = 0;
  s_bytesTotal = 0;
  s_state = GITHUB_OTA_RUNNING;
  if (xTaskCreate(ota_update_task, "github_ota", 8192, nullptr, 4, nullptr) != pdPASS) {
    s_state = GITHUB_OTA_ERROR;
    strlcpy(s_error, "Task konnte nicht gestartet werden", sizeof(s_error));
    return false;
  }
  return true;
}

GithubOtaState github_ota_state(void) { return s_state; }
size_t         github_ota_bytes_done(void) { return s_bytesDone; }
size_t         github_ota_bytes_total(void) { return s_bytesTotal; }

namespace {
// Eigener NVS-Namespace statt "cydcfg" (config_store.cpp) - dies ist
// fluechtiger Boot-Zustand, keine Nutzerkonfiguration, und soll vom
// Werksreset dort unberuehrt bleiben.
const char *kOtaBootNs  = "otaboot";
const char *kOtaBootKey = "action";
} // namespace

void github_ota_set_pending_boot_action(OtaBootAction action) {
  Preferences p;
  if (p.begin(kOtaBootNs, false)) {
    p.putUChar(kOtaBootKey, (uint8_t)action);
    p.end();
  }
}

void ota_boot_run_pending_action(void) {
  OtaBootAction action = OTA_BOOT_ACTION_NONE;
  {
    Preferences p;
    if (p.begin(kOtaBootNs, false)) {
      action = (OtaBootAction)p.getUChar(kOtaBootKey, OTA_BOOT_ACTION_NONE);
      if (action != OTA_BOOT_ACTION_NONE) {
        // Sofort loeschen, nicht erst am Ende - ein Haenger/Crash waehrend
        // des Checks/Updates soll nicht dazu fuehren, dass JEDER folgende
        // Boot wieder in diesem Zweig landet.
        p.putUChar(kOtaBootKey, (uint8_t)OTA_BOOT_ACTION_NONE);
      }
      p.end();
    }
  }
  if (action == OTA_BOOT_ACTION_NONE) return;

  log_w("ota_boot_run_pending_action: Aktion %d - warte auf WLAN...", (int)action);

  uint32_t start = millis();
  while (wifi_provision_get_phase() != WIFI_PROVISION_CONNECTED) {
    wifi_provision_loop();
    if (millis() - start > 20000) {
      strlcpy(s_error, "WLAN-Verbindung fuer den Update-Check nicht rechtzeitig hergestellt", sizeof(s_error));
      log_e("ota_boot_run_pending_action: %s", s_error);
      return;
    }
    delay(50);
  }

  if (!github_ota_check() || action == OTA_BOOT_ACTION_CHECK) {
    return;
  }

  if (!github_ota_update_available()) {
    // Zwischen Klick und diesem Boot hat sich nichts (mehr) geaendert -
    // github_ota_error() bleibt leer, github_ota_update_available() liefert
    // korrekt false, das Dashboard zeigt "kein Update verfuegbar" an.
    return;
  }

  if (!github_ota_start_update()) {
    return; // s_error bereits gesetzt (github_ota_start_update())
  }
  while (github_ota_state() == GITHUB_OTA_RUNNING) {
    delay(100);
  }
  // Bei Erfolg hat der Update-Task bereits ESP.restart() ausgeloest - hier
  // nur noch im Fehlerfall erreichbar, s_error/s_state sind bereits gesetzt.
}
