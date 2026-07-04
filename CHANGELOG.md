# Changelog

Alle nennenswerten Aenderungen dieses Projekts werden hier dokumentiert.
Format lose an [Keep a Changelog](https://keepachangelog.com/) angelehnt,
Versionierung folgt `vX.Y.Z-idf`-Tags (siehe `.github/workflows/build-idf.yml`).

## [v1.0.0-idf] - Erstes Release

Erstes offizielles Release des ESP-IDF-Ports. Ein Firmware-Image je Ziel-Chip
(ESP32/ESP32-S3/ESP32-C3), Displaytyp wird zur Laufzeit im Webportal gewaehlt
- kein Neuflashen bei Displaywechsel noetig.

### Unterstuetzte Hardware

- **Chips**: ESP32 (klassisch), ESP32-S3, ESP32-C3.
- **Displays**: ILI9341 320x240 + XPT2046-Touch (ESP32-2432S028 "CYD",
  Werksverdrahtung), ILI9488 480x320, ST7796S 480x320, GC9A01 240x240 rund,
  SSD1309 128x64 monochrom (I2C) - alle fuenf in einem einzigen Firmware-Image
  enthalten.
- **Pin-Belegung**: fuer alle generisch verdrahteten Profile (alles ausser
  ESP32-2432S028) im Webportal pro Feld ueberschreibbar, mit sinnvollen
  Defaults; Wechsel des Displaytyps setzt die Overrides automatisch zurueck.

### Features

- **Webportal-Konfiguration**: WLAN (mit Netzwerk-Scan als klickbare Liste
  statt eines Eingabefelds), MQTT-Broker/Topic, NTP/Zeitzone, Wetter
  (OpenWeatherMap), Displaytyp + Pin-Overrides, Helligkeit, Rotation/
  Spiegelung, Standby-Timeout, Farbinversion - WLAN-Karte bewusst von den
  uebrigen (erweiterten) Einstellungen getrennt, um Formularprobleme auf
  mobilen Browsern zu vermeiden.
- **OTA-Firmware-Update** ueber das Webportal (`.bin`-Upload), zwei
  OTA-Partitionen (je 1.75 MB).
- **MQTT-Datenquelle**: CPU/GPU-Auslastung, -Temperatur, -Leistungsaufnahme
  vom PC per JSON auf konfigurierbarem Topic (Standard `pulsemqtt/hwinfo`,
  Beispiel-Bridge unter `tools/pc_bridge_example.py`).
- **UI je Panelform**: rechteckige Kachel-UI mit Verlaufsdiagrammen und
  Trend-Pfeilen (ILI9341/ILI9488/ST7796S), reduziertes Rund-Layout mit
  zwei Screens (GC9A01, Navigation ueber die BOOT-Taste), minimales
  Monochrom-Layout (SSD1309).
- **Standby-Modus** nach konfigurierbarer Inaktivitaet (0 = deaktiviert),
  zeigt Uhrzeit/Datum + Wetter.
- **Material-Design-Icons** (Thermometer, Sonne, Wind, Regen, Luftfeuchte,
  Zahnrad, WLAN, Zurueck-Pfeil) als eigene LVGL-Font, keine Vektorgrafiken.
- **CI**: manueller oder Tag-basierter GitHub-Actions-Build
  (`espressif/esp-idf-ci-action`), fasst Bootloader/Partitionstabelle/App
  per `idf.py merge-bin` zu einer einzigen, per Web-Flasher flashbaren Datei
  zusammen. Tag-Push (`vX.Y.Z-idf`) baut alle drei Chips und veroeffentlicht
  automatisch ein GitHub-Release mit allen drei Binaries.

### Bekannte Einschraenkungen

- Nur ESP32-2432S028 (ILI9341) ist auf echter Hardware ausfuehrlich
  verifiziert; GC9A01 und SSD1309 wurden auf ersten Testaufbauten bestaetigt,
  ILI9488/ST7796S/ESP32-S3/ESP32-C3 noch nicht auf physischer Hardware
  gegengeprueft (siehe README, Abschnitt "Auf Hardware zu pruefen").
- Rotation ist fuer rechteckige Panels auf die beiden sinnvollen Landscape-
  Werte begrenzt (0/2 wuerden die fest auf Landscape ausgelegte Kachel-UI auf
  Portrait umschalten); bei rundem Panel muss die richtige Spiegel-Kombination
  je nach Panel-Charge ggf. durchprobiert werden.
- Nur das ESP32-2432S028-Profil hat Touch; GC9A01 nutzt die BOOT-Taste als
  einfache Navigation, die uebrigen Profile haben keine Eingabe am Geraet
  (Konfiguration laeuft dort vollstaendig ueber das Webportal).
