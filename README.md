# ESP32-2432S028 (CYD) Hardware-Monitor

Firmware für die "Cheap Yellow Display"-Boards (ESP32-WROOM-32 + 2.8" ILI9341 +
XPT2046 Touch). Zeigt Uhrzeit, optional Wetter sowie CPU/GPU-Auslastung,
-Temperatur und -Leistungsaufnahme an, die per MQTT von einem PC-Client
geliefert werden. Tippen auf die CPU- oder GPU-Kachel öffnet ein
Verlaufsdiagramm.

## Zwei Varianten

- **Arduino/PlatformIO** (dieses Verzeichnis, `src/` + `platformio.ini`) — die
  primäre, auf Hardware erprobte Firmware.
- **ESP-IDF-Port** (`esp-idf/`) — dieselbe Funktion auf purem ESP-IDF (LVGL +
  `esp_lcd`, `esp_wifi`, `esp-mqtt`, …), baubar mit `idf.py`. Siehe
  [`esp-idf/README.md`](esp-idf/README.md). Noch nicht auf Hardware verifiziert.

## Warum PlatformIO statt Arduino IDE?

PlatformIO wurde gewählt, weil `TFT_eSPI` für CYD-Boards exakte Pin-Defines
braucht, die hier sauber per `build_flags` in `platformio.ini` gesetzt werden
(keine manuelle `User_Setup.h`-Bearbeitung in der Library nötig), und weil das
Projekt mehrere Dateien sowie aktuelle Libraries (`ESPAsyncWebServer`-Fork,
`ArduinoJson` v7) mit gepinnten Versionen nutzt. Das lässt sich in der
Arduino IDE deutlich umständlicher reproduzieren.

## Setup

1. Projekt in VS Code mit PlatformIO-Extension öffnen.
2. Board per USB anschließen, `pio run -t upload` (oder Upload-Button).
3. Beim ersten Start (kein gespeichertes WLAN) öffnet das Board einen
   **offenen** Access Point `CYD-Setup-XXXX` (ohne Passwort). Mit Handy/PC
   verbinden, Captive-Portal öffnet automatisch die Konfigurationsseite
   (sonst manuell `192.168.4.1` aufrufen). Der AP läuft nur zur Ersteinrichtung
   und ist rein lokal erreichbar; ein Passwort entfällt bewusst, da der
   WPA2-Handshake der ESP32-SoftAP auf vielen Handys als „falsches Passwort"
   fehlschlägt.
4. WLAN-Zugangsdaten, MQTT-Broker, Zeitzone und optional Wetter-API-Key
   eintragen, speichern. Das Board startet neu und verbindet sich mit dem WLAN.
5. Webinterface ist danach dauerhaft unter der im seriellen Monitor
   angezeigten IP erreichbar, um Einstellungen jederzeit zu ändern.

## MQTT-Datenformat

Der PC-Client publiziert ein JSON-Objekt auf das konfigurierte Topic
(Standard: `cyd/hwinfo`):

```json
{
  "cpu_load": 42.5,
  "cpu_temp": 58.0,
  "cpu_power": 65.0,
  "gpu_load": 12.0,
  "gpu_temp": 40.0,
  "gpu_power": 25.0
}
```

Alle Felder sind optional - nur enthaltene Werte werden aktualisiert.
Sinnvolle Publish-Rate: 1-2 Sekunden. Das Board sammelt unabhängig davon
max. 1x/Sekunde einen Wert für die Verlaufsdiagramme (60 Werte = 1 Minute
Verlauf, in `include/shared_state.h` über `HIST_LEN` änderbar).

`tools/pc_bridge_example.py` zeigt beispielhaft, wie ein Python-Skript auf
dem PC Werte (z.B. aus LibreHardwareMonitor oder HWiNFO64) per MQTT
veröffentlicht - als Startpunkt, nicht produktionsfertig (CPU-Temperatur via
`psutil` funktioniert unter Windows i.d.R. nicht zuverlässig; GPU-Werte sind
Platzhalter und müssen an die jeweilige Quelle angebunden werden).

## Wetter (optional)

Nutzt die OpenWeatherMap "Current Weather"-API (kostenloser Tier reicht).
API-Key auf https://openweathermap.org/api erstellen, im Webinterface unter
"Wetter" eintragen und Ort als `Stadt,Länderkürzel` angeben (z.B.
`Duesseldorf,DE`).

## Pinbelegung (Standard-CYD)

| Funktion      | GPIO |
|---------------|------|
| TFT MOSI      | 13   |
| TFT MISO      | 12   |
| TFT SCLK      | 14   |
| TFT CS        | 15   |
| TFT DC        | 2    |
| TFT Backlight | 21   |
| Touch CS      | 33   |

Falls Farben invertiert erscheinen: in `platformio.ini` `ILI9341_DRIVER`
durch `ILI9341_2_DRIVER` ersetzen. Falls Touch-Koordinaten nicht passen:
Kalibrierwerte (`calData[]`) in `src/display_ui.cpp` per `TFT_eSPI`
Kalibrier-Sketch neu ermitteln.

## Projektstruktur

```
platformio.ini
include/
  shared_state.h     Konfig-/Datenstrukturen, globale Variablen
  config_store.h
  display_ui.h
  mqtt_handler.h
  web_portal.h
  weather_service.h
  time_service.h
src/
  main.cpp            Setup/Loop, verdrahtet alle Module
  config_store.cpp     NVS-Persistenz (Preferences)
  display_ui.cpp       TFT-Darstellung, Touch-Handling, Graphen
  mqtt_handler.cpp     MQTT-Subscribe + JSON-Parsing
  web_portal.cpp       WLAN-Verbindung/AP-Fallback, Webserver, Config-API
  weather_service.cpp  OpenWeatherMap-Abruf
  time_service.cpp     NTP/Zeitzone
tools/
  pc_bridge_example.py Beispiel PC->MQTT Bridge
```

## Mögliche Erweiterungen

- OTA-Updates (`ArduinoOTA` einbinden)
- Mehrere Verlaufsmetriken gleichzeitig (z.B. Temperatur-Graph einblendbar)
- Captive-Portal mit DNS-Wildcard für alle Betriebssysteme robuster gestalten
- Persistente Verlaufsdaten über Neustarts (aktuell nur im RAM)
