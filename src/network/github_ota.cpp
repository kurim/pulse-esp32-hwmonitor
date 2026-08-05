#include "github_ota.h"
#include "shared_state.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>

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
char s_error[96] = "";
bool s_updateAvailable = false;

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
    snprintf(s_error, sizeof(s_error), "JSON-Parse-Fehler: %s (Antwortlaenge %u Bytes)", err.c_str(), (unsigned)body.length());
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

bool github_ota_perform_update(void) {
  if (!s_assetUrl[0]) {
    strlcpy(s_error, "Kein Asset bekannt - erst github_ota_check() aufrufen", sizeof(s_error));
    return false;
  }
  if (!wifi_connected) {
    strlcpy(s_error, "Kein WLAN verbunden", sizeof(s_error));
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  https.setTimeout(15000);
  if (!https.begin(client, s_assetUrl)) {
    strlcpy(s_error, "Verbindungsaufbau zum Asset-Download fehlgeschlagen", sizeof(s_error));
    return false;
  }
  https.addHeader("User-Agent", "pulse-esp32-hwmonitor");

  int status = https.GET();
  if (status != 200) {
    snprintf(s_error, sizeof(s_error), "Asset-Download antwortete mit HTTP %d", status);
    https.end();
    return false;
  }

  // Dieselbe Absicherung wie beim lokalen Upload in web_portal.cpp
  // handleOtaUpload() - ein vorheriger, nie abgeschlossener Versuch
  // (egal ob lokal oder ueber GitHub) wuerde Update.begin() sonst
  // dauerhaft mit "already running" scheitern lassen.
  if (Update.isRunning()) {
    Update.abort();
  }

  // getSize() ist -1, wenn der Server kein Content-Length sendet (z.B. bei
  // Chunked Transfer Encoding) - UPDATE_SIZE_UNKNOWN ist dann der richtige
  // Fallback, exakt wie beim lokalen Upload in web_portal.cpp.
  int contentLen = https.getSize();
  bool began = Update.begin(contentLen > 0 ? (size_t)contentLen : UPDATE_SIZE_UNKNOWN);
  if (!began) {
    snprintf(s_error, sizeof(s_error), "Update.begin() fehlgeschlagen: %s", Update.errorString());
    https.end();
    return false;
  }

  // Pausiert den Wetter-Task fuer die Dauer des Downloads/Flashens
  // (dieselbe Begruendung wie beim lokalen Upload, siehe shared_state.h).
  ota_in_progress = true;

  auto *stream = https.getStreamPtr();
  uint8_t buf[1024];
  size_t totalWritten = 0;
  bool writeError = false;
  // Kanonisches Streaming-Download-Muster (siehe Arduino-ESP32-Beispiele
  // fuer HTTPUpdate/OTAWebUpdater): solange verbunden UND (Restlaenge
  // bekannt und noch nicht erreicht) ODER Laenge unbekannt (-1) weiterlesen.
  while (https.connected() && (contentLen > 0 || contentLen == -1)) {
    size_t avail = stream->available();
    if (!avail) {
      delay(2);
      continue;
    }
    size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
    int got = stream->readBytes(buf, toRead);
    if (got <= 0) break;
    size_t written = Update.write(buf, (size_t)got);
    totalWritten += written;
    if (written != (size_t)got) {
      snprintf(s_error, sizeof(s_error), "Update.write() Kurzschreibung: %s", Update.errorString());
      writeError = true;
      break;
    }
    if (contentLen > 0) contentLen -= got;
  }
  https.end();
  ota_in_progress = false;

  if (writeError) {
    Update.abort();
    return false;
  }
  if (contentLen > 0) {
    // Schleife wurde verlassen, obwohl noch Bytes fehlten (Verbindung weg).
    snprintf(s_error, sizeof(s_error), "Download abgebrochen, %u Bytes fehlen", (unsigned)contentLen);
    Update.abort();
    return false;
  }

  bool ok = Update.end(true);
  if (!ok) {
    snprintf(s_error, sizeof(s_error), "Update.end() fehlgeschlagen: %s", Update.errorString());
    return false;
  }
  (void)totalWritten;
  return true;
}
