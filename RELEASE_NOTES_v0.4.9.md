# Pulse ESP32 Hardware Monitor — v0.4.9

Erstes Release mit vollständigen Notes für diesen PlatformIO/Arduino-Rewrite. Frühere Tags (`v0.3.10` – `v0.4.8`) hatten kein beschriebenes GitHub-Release; diese Notes fassen den gesamten aktuellen Funktionsumfang zusammen, nicht nur den Unterschied zum letzten Tag.

## Was ist das?

Ein kleines WLAN-Hardware-Monitor-Display für den Schreibtisch: Uhrzeit/Datum, optional Wetter, sowie CPU/GPU-Auslastung, -Temperatur und -Leistung, geliefert von einem PC-Client über MQTT oder USB/Serial. Läuft headless mit Web-Portal, sobald kein Display konfiguriert ist — kein Bricking-Risiko durch eine falsche Pin-Konfiguration.

## Unterstützte Hardware

Ein Firmware-Image pro Board/Chip enthält jeweils **alle** dafür verfügbaren Display-Treiber — welcher tatsächlich verwendet wird, wird nach dem Flashen im Web-Portal gewählt (`BOARD_GENERIC`-Envs) bzw. steht bei den beiden CYD-Varianten und dem JC8048W550 durch die feste Werksverdrahtung von vornherein fest.

| PlatformIO-Env | Chip | Display(s) | Touch/Bedienung |
|---|---|---|---|
| `cyd` | ESP32 (klassisch) | ESP32-2432S028R, ILI9341, 320x240 SPI | XPT2046-Touch |
| `cyd-v3` | ESP32 (klassisch) | ESP32-2432S028R v3, ST7789 (invertierte Farben), 320x240 SPI | XPT2046-Touch |
| `jc8048w550` | ESP32-S3 (PSRAM) | Guition JC8048W550, ST7262, 800x480 RGB-Parallel | GT911-Touch |
| `esp32` | ESP32 (klassisch) | zur Laufzeit wählbar: GC9A01 (rund, 240x240) / ILI9488 (480x320) / ST7796S (480x320) / SSD1309 (128x64, mono, I2C) / SH1106 (128x64, mono, I2C) / headless | GC9A01: BOOT-Taste als Navigation; sonst keine |
| `esp32c3` | ESP32-C3 (kein PSRAM) | s.o. | s.o. |
| `esp32s3-4mb` | ESP32-S3, 4 MB Flash/2 MB PSRAM | s.o. | s.o. |
| `esp32s3-8mb` | ESP32-S3, 8 MB Flash/Octal-PSRAM | s.o. | s.o. |

Alle Pin-Zuordnungen für die generischen Displays lassen sich im Web-Portal ohne Neu-Flash überschreiben ("Pin assignment" — leeres Feld = Treiber-Default, `-1` = bewusst nicht verdrahtet).

**Auf echter Hardware bestätigt:** ESP32-2432S028R (CYD, ILI9341), ESP32-S3 + JC8048W550, ESP32-C3 + GC9A01 (rundes UI, inkl. Standby-Ansicht mit animierten Ringen), sowie SSD1309/SH1106 (128x64 mono OLED) auf ESP32-C3 und ESP32-S3-4MB.
**Noch nicht auf echter Hardware verifiziert:** ILI9488, ST7796S, sowie die CYD-v3-Variante (ST7789).

## Features

- **Datenquelle umschaltbar**: MQTT (retained JSON-Snapshot, QoS 0) oder direkt per USB/Serial — beides läuft über dasselbe JSON-Format, keine zwei Firmware-Varianten nötig.
- **Web-Portal** für die komplette Konfiguration: WLAN (Captive-Portal-Fallback + Improv WiFi über Serial und, wo Flash-Budget reicht, BLE), MQTT, Zeitzone/NTP, Wetter, Display-Typ/Rotation/Helligkeit/Pin-Zuordnung, Sprache.
- **Wetter** (OpenWeatherMap): aktuelle Werte + mehrtägige Vorhersage, Beschreibungstexte auf Deutsch oder Englisch umschaltbar.
- **Dashboard-Editor** (Tile-UI): Drag & Drop, freie Platzierung/Größe von Widgets (Charts, Wert-Anzeigen, Wetter-Karten, Standby-Vorlagen) statt eines festen Layouts.
- **Mono-UI** (SSD1309/SH1106): eigene kompakte Widget-Auswahl inkl. Vollbild-Standby-Wettervorlage mit Tages-Icons.
- **Standby/Screensaver**: automatisch bei Signalverlust, konfigurierbares Timeout; auf Boards mit Backlight-Pin wird das Backlight gedimmt, auf GC9A01/Mono-Displays wird stattdessen der Inhalt auf Uhrzeit+Wetter reduziert.
- **Mehrsprachig** (Web-Portal + Wetterbeschreibungen): Deutsch/Englisch.
- **FOTA** (GitHub-Release-basiertes Over-the-Air-Update) — siehe eigener Abschnitt unten.
- Manuelles Firmware-Update per `.bin`-Upload im Web-Portal bleibt als Fallback auf allen Boards verfügbar.

## FOTA: Firmware-Updates direkt vom Dashboard

"Auf Updates prüfen" / "Jetzt aktualisieren" im Web-Portal-Tab "FOTA" holen die neueste Firmware direkt von den GitHub-Releases dieses Repos — kein manuelles Herunterladen/Flashen nötig.

Da ein TLS-Handshake zu GitHub auf Boards ohne PSRAM (CYD, ESP32-C3) im laufenden Betrieb (WiFi-Manager, Web-Server, ggf. MQTT und die Display-UI belegen dort bereits fast den gesamten verfügbaren Speicher) nicht zuverlässig genug Speicher findet, läuft der Check/das Update dort über einen kurzen automatischen Neustart in einen schlanken Boot-Zustand (Display/MQTT werden dafür kurz übersprungen, ca. 5–90 Sekunden nicht erreichbar je nach Aktion). Auf Boards mit PSRAM (S3, JC8048W550) läuft beides direkt im laufenden Betrieb, inklusive Live-Fortschrittsanzeige beim Download.

## Installation

Erstinstallation per USB — entweder per PlatformIO (`pio run -e <env> -t upload`, siehe README) oder browserbasiert ganz ohne Toolchain-Installation über [ESP Web Tools](https://web.esphome.io/) bzw. jeden anderen esptool-js-basierten Flasher: die zu jedem Release beigefügte `esp32-hwmonitor-v<VERSION>-pio-<ENV>.bin` ist das vollständige Image (Bootloader + Partitionstabelle + App), einfach bei Offset `0x0` schreiben. Danach sind alle weiteren Updates über FOTA oder manuellen `.bin`-Upload im Web-Portal möglich, kein erneuter USB-Zugriff nötig.
