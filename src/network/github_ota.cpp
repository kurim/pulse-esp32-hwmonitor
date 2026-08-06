#include "github_ota.h"
#include "shared_state.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
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

const char *kReleaseApiUrl = "https://api.github.com/repos/kurim/pulse-esp32-hwmonitor/releases/latest";

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

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  https.setTimeout(10000);
  if (!https.begin(client, kReleaseApiUrl)) {
    strlcpy(s_error, "Verbindungsaufbau zur GitHub-API fehlgeschlagen", sizeof(s_error));
    return false;
  }
  // GitHub verlangt zwingend einen User-Agent-Header, sonst HTTP 403.
  https.addHeader("User-Agent", "pulse-esp32-hwmonitor");
  https.addHeader("Accept", "application/vnd.github+json");

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
    snprintf(s_error, sizeof(s_error), "GitHub-API antwortete mit HTTP %d (%s, freier Heap: %u B, groesster Block: %u B)",
             status, tlsErr[0] ? tlsErr : "kein TLS-Fehlerdetail", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    https.end();
    return false;
  }
  if (status != 200) {
    snprintf(s_error, sizeof(s_error), "GitHub-API antwortete mit HTTP %d", status);
    https.end();
    return false;
  }

  // getString() statt direkt von https.getStream() zu parsen: die GitHub-
  // API liefert ohne Content-Length per Chunked Transfer-Encoding (bei
  // dieser Release-Groesse, 14 Assets, mehrere KB JSON) - deserializeJson()
  // direkt vom Stream brach dabei mit "IncompleteInput" ab, bevor die
  // komplette (entchunkte) Antwort gelesen war. getString() sammelt die
  // vollstaendige Antwort zuverlaessig ein, bevor geparst wird - kostet
  // kurzzeitig etwas mehr Heap (Antwort liegt einmal als String, einmal als
  // JsonDocument vor), aber bei ein paar KB unproblematisch.
  String body = https.getString();
  https.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err != DeserializationError::Ok) {
    // Antwortlaenge 0 bei HTTP 200 ist kein Protokollfehler, sondern typisch
    // fuer eine fehlgeschlagene Heap-Allokation waehrend https.getString()
    // beim Einsammeln der gechunkten Antwort (String() faengt OOM ab und
    // wird dann leer statt zu crashen) - freier Heap gehoert daher mit in
    // die Fehlermeldung, nicht nur die Bytezahl.
    snprintf(s_error, sizeof(s_error), "JSON-Parse-Fehler: %s (Antwortlaenge %u Bytes, freier Heap: %u B, groesster Block: %u B)",
             err.c_str(), (unsigned)body.length(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return false;
  }

  const char *tag = doc["tag_name"] | "";
  if (!tag[0]) {
    strlcpy(s_error, "Antwort ohne tag_name - unerwartete API-Struktur", sizeof(s_error));
    return false;
  }
  const char *tagVersion = (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag; // "v1.2.3" -> "1.2.3"
  strlcpy(s_latestVersion, tagVersion, sizeof(s_latestVersion));

  // Passendes Asset suchen: Name endet auf "-pio-<FW_OTA_ENV_SLUG>-ota.bin"
  // (siehe .github/workflows/build.yaml) - Suffix-Suche statt den vollen
  // Dateinamen samt Versionsnummer nachzubauen, robuster gegen minimal
  // abweichende Formatierung.
  char suffix[48];
  snprintf(suffix, sizeof(suffix), "-pio-%s-ota.bin", FW_OTA_ENV_SLUG);
  size_t suffixLen = strlen(suffix);

  for (JsonObjectConst asset : doc["assets"].as<JsonArrayConst>()) {
    const char *name = asset["name"] | "";
    size_t nameLen = strlen(name);
    if (nameLen >= suffixLen && strcmp(name + nameLen - suffixLen, suffix) == 0) {
      const char *url = asset["browser_download_url"] | "";
      strlcpy(s_assetUrl, url, sizeof(s_assetUrl));
      break;
    }
  }
  if (!s_assetUrl[0]) {
    snprintf(s_error, sizeof(s_error), "Kein Asset fuer '%s' im Release %s gefunden", FW_OTA_ENV_SLUG, tag);
    return false;
  }

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
