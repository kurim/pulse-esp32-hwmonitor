#include "web_portal.h"
#include "shared_state.h"
#include "config_store.h"
#include "mqtt_handler.h"
#include "time_service.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>

static AsyncWebServer server(80);
static DNSServer dnsServer;
static bool apMode = false;
static unsigned long restartAtMs = 0;
static const byte DNS_PORT = 53;

// ------------------------------------------------------------------
// Eingebettete Config-Seite (kein LittleFS-Upload nötig)
// ------------------------------------------------------------------
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="de"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CYD Hardware-Monitor</title>
<style>
:root{--bg:#10141c;--card:#1a2030;--accent:#3fd0e0;--text:#e8edf4;--sub:#8893a8;--border:#2a3142;}
*{box-sizing:border-box;}
body{background:var(--bg);color:var(--text);font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:0;padding:24px 16px;}
.wrap{max-width:560px;margin:0 auto;}
h1{font-size:1.4rem;margin-bottom:4px;}
.sub{color:var(--sub);margin-bottom:24px;font-size:.9rem;}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:18px 20px;margin-bottom:16px;}
.card h2{font-size:1rem;margin:0 0 14px;color:var(--accent);}
label{display:block;font-size:.82rem;color:var(--sub);margin:10px 0 4px;}
input[type=text],input[type=password],input[type=number],select{
 width:100%;padding:9px 10px;border-radius:6px;border:1px solid var(--border);
 background:#0d111a;color:var(--text);font-size:.95rem;}
.row{display:flex;gap:12px;}
.row>div{flex:1;}
.toggle{display:flex;align-items:center;gap:10px;margin:10px 0;}
button{background:var(--accent);color:#04222a;border:none;border-radius:8px;
 padding:12px 18px;font-size:1rem;font-weight:600;cursor:pointer;width:100%;margin-top:6px;}
button:hover{opacity:.9;}
#status{margin-top:14px;font-size:.85rem;color:var(--sub);}
.statbar{display:flex;gap:16px;font-size:.85rem;color:var(--sub);margin-bottom:18px;flex-wrap:wrap;}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;}
</style></head>
<body><div class="wrap">
<h1>CYD Hardware-Monitor</h1>
<div class="sub">Konfiguration für ESP32-2432S028</div>

<div class="statbar" id="livebar">Lade Status...</div>

<form id="cfgForm">
  <div class="card"><h2>WLAN</h2>
    <label>SSID</label><input type="text" id="wifi_ssid" maxlength="32">
    <label>Passwort</label><input type="password" id="wifi_pass" maxlength="64" placeholder="unverändert lassen = leer">
  </div>

  <div class="card"><h2>MQTT (Hardwaredaten vom PC)</h2>
    <label>Broker-Host</label><input type="text" id="mqtt_host" maxlength="64">
    <div class="row">
      <div><label>Port</label><input type="number" id="mqtt_port" min="1" max="65535"></div>
      <div><label>Topic</label><input type="text" id="mqtt_topic" maxlength="64"></div>
    </div>
    <div class="row">
      <div><label>Benutzer</label><input type="text" id="mqtt_user" maxlength="32"></div>
      <div><label>Passwort</label><input type="password" id="mqtt_pass" maxlength="64" placeholder="unverändert lassen = leer"></div>
    </div>
  </div>

  <div class="card"><h2>Zeit</h2>
    <label>NTP-Server</label><input type="text" id="ntp_server" maxlength="64">
    <label>POSIX-Zeitzone</label><input type="text" id="tz" maxlength="64">
  </div>

  <div class="card"><h2>Wetter (optional, OpenWeatherMap)</h2>
    <div class="toggle"><input type="checkbox" id="weather_enabled"><label style="margin:0">Aktivieren</label></div>
    <label>API-Key</label><input type="text" id="weather_api_key" maxlength="40" placeholder="unverändert lassen = leer">
    <div class="row">
      <div><label>Ort (Stadt,Land)</label><input type="text" id="weather_city" maxlength="64"></div>
      <div><label>Einheit</label>
        <select id="weather_units"><option value="metric">°C</option><option value="imperial">°F</option></select>
      </div>
    </div>
  </div>

  <div class="card"><h2>Display</h2>
    <div class="row">
      <div><label>Helligkeit (0-255)</label><input type="number" id="brightness" min="0" max="255"></div>
      <div><label>Rotation (0-3)</label><input type="number" id="rotation" min="0" max="3"></div>
    </div>
  </div>

  <button type="submit">Speichern &amp; Neustart</button>
  <div id="status"></div>
</form>

<div class="card">
  <h2>Firmware-Update (OTA)</h2>
  <div class="sub" style="margin-bottom:10px">Aktuelle Version: <span id="fwVersion">-</span></div>
  <input type="file" id="fwFile" accept=".bin">
  <button type="button" onclick="uploadFirmware()" style="background:#e0a53f">.bin hochladen &amp; flashen</button>
  <progress id="otaProgress" value="0" max="100" style="width:100%;margin-top:10px;display:none"></progress>
  <div id="otaStatus" style="margin-top:8px;font-size:.85rem;color:var(--sub)"></div>
</div>
</div>

<script>
async function loadCfg(){
  const r = await fetch('/api/config'); const c = await r.json();
  for (const k in c){ const el=document.getElementById(k); if(!el) continue;
    if(el.type==='checkbox') el.checked = !!c[k]; else el.value = c[k]; }
}
async function loadStatus(){
  try{
    const r = await fetch('/api/status'); const s = await r.json();
    document.getElementById('fwVersion').innerText = s.fw_version + ' (freier Speicher: ' + Math.round(s.free_heap/1024) + ' KB)';
    document.getElementById('livebar').innerHTML =
      '<span><span class="dot" style="background:'+(s.wifi?'#3fd0e0':'#e05a5a')+'"></span>WLAN</span>'+
      '<span><span class="dot" style="background:'+(s.mqtt?'#3fd0e0':'#e05a5a')+'"></span>MQTT</span>'+
      '<span>CPU '+s.cpu_load.toFixed(0)+'%</span><span>GPU '+s.gpu_load.toFixed(0)+'%</span>'+
      '<span>IP '+s.ip+'</span>';
  }catch(e){}
}
document.getElementById('cfgForm').addEventListener('submit', async (e)=>{
  e.preventDefault();
  const ids=['wifi_ssid','wifi_pass','mqtt_host','mqtt_port','mqtt_user','mqtt_pass','mqtt_topic',
    'ntp_server','tz','weather_enabled','weather_api_key','weather_city','weather_units','brightness','rotation'];
  const payload={};
  ids.forEach(id=>{const el=document.getElementById(id);
    payload[id]= el.type==='checkbox'? el.checked : (el.type==='number'? Number(el.value): el.value);});
  document.getElementById('status').innerText='Speichere...';
  await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
  document.getElementById('status').innerText='Gespeichert. Gerät startet neu...';
});
loadCfg(); loadStatus(); setInterval(loadStatus, 2000);

function uploadFirmware(){
  const fileInput = document.getElementById('fwFile');
  const file = fileInput.files[0];
  if(!file){ alert('Bitte zuerst eine .bin-Datei auswählen.'); return; }
  if(!confirm('Firmware "'+file.name+'" jetzt aufspielen? Das Gerät startet danach automatisch neu.')) return;

  const progress = document.getElementById('otaProgress');
  const status = document.getElementById('otaStatus');
  progress.style.display = 'block';
  progress.value = 0;
  status.innerText = 'Lade hoch...';

  const formData = new FormData();
  formData.append('firmware', file, file.name);

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);
  xhr.upload.onprogress = function(e){
    if (e.lengthComputable){
      const pct = Math.round((e.loaded / e.total) * 100);
      progress.value = pct;
      status.innerText = 'Lade hoch... ' + pct + '%';
    }
  };
  xhr.onload = function(){
    if (xhr.status === 200){
      status.innerText = 'Update erfolgreich - Gerät startet neu. Seite in Kürze neu laden.';
    } else {
      status.innerText = 'Fehler beim Update: ' + xhr.responseText;
    }
  };
  xhr.onerror = function(){
    status.innerText = 'Verbindungsfehler beim Hochladen.';
  };
  xhr.send(formData);
}

</script>
</body></html>
)HTML";

// ------------------------------------------------------------------
static void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", INDEX_HTML);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["wifi_ssid"]   = appConfig.wifi_ssid;
    doc["wifi_pass"]   = "";              // Passwörter nie zurücksenden
    doc["mqtt_host"]   = appConfig.mqtt_host;
    doc["mqtt_port"]   = appConfig.mqtt_port;
    doc["mqtt_user"]   = appConfig.mqtt_user;
    doc["mqtt_pass"]   = "";
    doc["mqtt_topic"]  = appConfig.mqtt_topic;
    doc["ntp_server"]  = appConfig.ntp_server;
    doc["tz"]          = appConfig.tz;
    doc["weather_enabled"] = appConfig.weather_enabled;
    doc["weather_api_key"] = "";
    doc["weather_city"]    = appConfig.weather_city;
    doc["weather_units"]   = appConfig.weather_units;
    doc["brightness"]  = appConfig.brightness;
    doc["rotation"]    = appConfig.rotation;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["wifi"] = wifiConnected;
    doc["mqtt"] = mqttConnected;
    doc["ip"]   = wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["cpu_load"]  = hwInfo.cpuLoad;
    doc["gpu_load"]  = hwInfo.gpuLoad;
    doc["cpu_temp"]  = hwInfo.cpuTemp;
    doc["gpu_temp"]  = hwInfo.gpuTemp;
    doc["cpu_power"] = hwInfo.cpuPower;
    doc["gpu_power"] = hwInfo.gpuPower;
    doc["fw_version"] = FW_VERSION;
    doc["free_heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // POST /api/config mit JSON-Body (Async-Body-Handler)
  server.on("/api/config", HTTP_POST,
    [](AsyncWebServerRequest *req) { /* wird in onBody fertig beantwortet */ },
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      static String body;
      if (index == 0) body = "";
      body += String((char*)data).substring(0, len);
      if (index + len != total) return; // noch nicht komplett

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
      }

      if (doc["wifi_ssid"].is<const char*>()) strlcpy(appConfig.wifi_ssid, doc["wifi_ssid"], sizeof(appConfig.wifi_ssid));
      if (doc["wifi_pass"].is<const char*>() && strlen(doc["wifi_pass"]) > 0)
        strlcpy(appConfig.wifi_pass, doc["wifi_pass"], sizeof(appConfig.wifi_pass));
      if (doc["mqtt_host"].is<const char*>()) strlcpy(appConfig.mqtt_host, doc["mqtt_host"], sizeof(appConfig.mqtt_host));
      if (doc["mqtt_port"].is<int>()) appConfig.mqtt_port = doc["mqtt_port"];
      if (doc["mqtt_user"].is<const char*>()) strlcpy(appConfig.mqtt_user, doc["mqtt_user"], sizeof(appConfig.mqtt_user));
      if (doc["mqtt_pass"].is<const char*>() && strlen(doc["mqtt_pass"]) > 0)
        strlcpy(appConfig.mqtt_pass, doc["mqtt_pass"], sizeof(appConfig.mqtt_pass));
      if (doc["mqtt_topic"].is<const char*>()) strlcpy(appConfig.mqtt_topic, doc["mqtt_topic"], sizeof(appConfig.mqtt_topic));
      if (doc["ntp_server"].is<const char*>()) strlcpy(appConfig.ntp_server, doc["ntp_server"], sizeof(appConfig.ntp_server));
      if (doc["tz"].is<const char*>()) strlcpy(appConfig.tz, doc["tz"], sizeof(appConfig.tz));
      if (doc["weather_enabled"].is<bool>()) appConfig.weather_enabled = doc["weather_enabled"];
      if (doc["weather_api_key"].is<const char*>() && strlen(doc["weather_api_key"]) > 0)
        strlcpy(appConfig.weather_api_key, doc["weather_api_key"], sizeof(appConfig.weather_api_key));
      if (doc["weather_city"].is<const char*>()) strlcpy(appConfig.weather_city, doc["weather_city"], sizeof(appConfig.weather_city));
      if (doc["weather_units"].is<const char*>()) strlcpy(appConfig.weather_units, doc["weather_units"], sizeof(appConfig.weather_units));
      if (doc["brightness"].is<int>()) appConfig.brightness = doc["brightness"];
      if (doc["rotation"].is<int>()) appConfig.rotation = doc["rotation"];

      ConfigStore::save(appConfig);
      req->send(200, "application/json", "{\"ok\":true}");
      restartAtMs = millis() + 1500; // kurze Verzögerung, damit Antwort noch rausgeht
    }
  );

  // ---- OTA Firmware-Upload ----
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      bool ok = !Update.hasError();
      AsyncWebServerResponse *response = req->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK" : "Update fehlgeschlagen");
      response->addHeader("Connection", "close");
      req->send(response);
      if (ok) restartAtMs = millis() + 1500;
    },
    [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        Serial.printf("OTA-Update gestartet: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
      }
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }
      if (final) {
        if (Update.end(true)) {
          Serial.printf("OTA-Update erfolgreich: %u Bytes\n", (unsigned)(index + len));
        } else {
          Update.printError(Serial);
        }
      }
    }
  );

  // Captive-Portal Fallback: alles auf / umleiten wenn im AP-Modus
  server.onNotFound([](AsyncWebServerRequest *req) {
    if (apMode) {
      req->redirect("/");
    } else {
      req->send(404, "text/plain", "Not found");
    }
  });
}

static bool connectWifiSTA() {
  if (strlen(appConfig.wifi_ssid) == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(appConfig.wifi_ssid, appConfig.wifi_pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

static void startApMode() {
  apMode = true;

  // Sauberer Übergang in den AP-Modus: Modus setzen und dem WLAN-Treiber kurz
  // Zeit zum Initialisieren geben. Ohne diese Verzögerung kann softAP()
  // zurückkehren, bevor die AP-Konfiguration vollständig angewendet wurde.
  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.setSleep(false); // Radio wach halten -> zuverlässigerer Verbindungsaufbau

  String apName = "CYD-Setup-" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(0, 4);

  // Offener Setup-AP (kein Passwort, nullptr = keine Verschlüsselung).
  // Grund: Der WPA2/WPA3-Handshake der ESP32-SoftAP schlägt auf vielen Handys
  // (v.a. neuere Android-Geräte mit PMF) sowie bei knapper USB-Stromversorgung
  // reproduzierbar fehl und wird dort als "falsches Passwort" gemeldet. Der AP
  // läuft nur zur Ersteinrichtung und ist ausschließlich lokal erreichbar.
  bool ok = WiFi.softAP(apName.c_str(), nullptr, 1 /* Kanal */);
  if (!ok) {
    delay(200); // ein Wiederholungsversuch, falls der erste Start fehlschlägt
    ok = WiFi.softAP(apName.c_str(), nullptr, 1);
  }

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.printf("AP-Modus %s: SSID '%s' (offen, ohne Passwort), IP %s\n",
                ok ? "aktiv" : "FEHLGESCHLAGEN", apName.c_str(),
                WiFi.softAPIP().toString().c_str());
}

namespace WebPortal {

void begin() {
  bool connected = connectWifiSTA();
  if (connected) {
    apMode = false;
    wifiConnected = true;
    Serial.printf("WLAN verbunden, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    wifiConnected = false;
    startApMode();
  }

  setupRoutes();
  server.begin();
}

void loop() {
  if (apMode) {
    dnsServer.processNextRequest();
  } else {
    wifiConnected = (WiFi.status() == WL_CONNECTED);
  }

  if (restartAtMs != 0 && millis() > restartAtMs) {
    ESP.restart();
  }
}

} // namespace WebPortal
