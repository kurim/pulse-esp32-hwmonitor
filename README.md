# ESP32 Hardware-Monitor (ESP-IDF)

Reine **ESP-IDF**-Firmware (kein Arduino Core) fuer einen kleinen
WLAN-Hardware-Monitor: Uhrzeit/Datum, optional Wetter, sowie CPU/GPU-
Auslastung, -Temperatur und -Leistung, die per MQTT von einem PC-Client
geliefert werden. WLAN-Setup ueber offenen Access Point mit Config-Webportal
und OTA-Update.

> Dies ist ein eigener Dauerbranch fuer die ESP-IDF-Variante, getrennt von der
> Arduino/PlatformIO-Variante auf `main` (dort liegen Arduino-Firmware und
> ESP-IDF-Port nebeneinander in Unterordnern). Hier auf `esp-idf` liegt der
> ESP-IDF-Code direkt im Repo-Root, ohne Arduino-Dateien daneben.

## Displayauswahl: ein Image fuer mehrere Boards/Panels

Es gibt **keinen** display- oder chipspezifischen Build mehr: eine Firmware
enthaelt alle unten aufgefuehrten Panel-Treiber. Welches Display tatsaechlich
verbaut ist, waehlt man **nach dem Flashen im Webportal** (Karte "Display" →
Dropdown "Displaytyp"); die Auswahl landet in NVS, das Geraet startet danach
neu und initialisiert das gewaehlte Panel. Das Webportal zeigt zur gewaehlten
Option automatisch die passende Pin-Tabelle an (`GET /api/displays`, aus
`main/board_profiles.c` generiert - keine doppelte Pflege von Pinbelegungen).

**Die Auswahl ist build-spezifisch gefiltert** (`board_profile_is_available()`
in `board_profiles.c`, ausgewertet ueber `CONFIG_IDF_TARGET_ESP32`): auf einem
Build fuer den klassischen ESP32 (`idf.py set-target esp32`) gibt es nur das
ESP32-2432S028-Profil - das Webportal blendet das Dropdown dort aus und zeigt
stattdessen nur "Fest verbaut: ...". Auf jedem anderen Chip (`esp32s3`,
`esp32c3`, ...) erscheinen nur die vier generischen Profile im Dropdown,
das ESP32-2432S028-Profil (feste Werksverdrahtung dieses einen Boards) taucht
dort gar nicht erst auf.

| Displaytyp | Aufloesung | Bus | Touch | UI |
|---|---|---|---|---|
| ESP32-2432S028, ILI9341 | 320x240 | SPI | XPT2046 | volle Kachel-UI (Verlauf, Settings) |
| ILI9488 | 480x320 | SPI | nein | Kachel-UI, skaliert auf die Aufloesung |
| ST7796S | 480x320 | SPI | nein | Kachel-UI, skaliert auf die Aufloesung |
| GC9A01 (rund) | 240x240 | SPI | nein (BOOT-Taste als Navigation) | **Platzhalter**-UI (Arcs/Wetter + Text, noch nicht final) |
| SSD1309 | 128x64, monochrom | I2C | nein | **Platzhalter**-UI (nur Text, kein Farbverlauf) |

**Wichtige Einschraenkungen dieser ersten Umsetzung:**

- Nur das erste Profil (ESP32-2432S028) hat Touch. GC9A01 hat stattdessen die
  BOOT-Taste des Devboards als einfache Navigation (Screen wechseln,
  siehe unten); ILI9488/ST7796S/SSD1309 haben gar keine Navigation am Geraet.
  Konfiguration laeuft in jedem Fall vollstaendig ueber das Webportal.
- Rotation (`app_config.rotation`) wirkt nur auf die rechteckige Kachel-UI
  (ESP32-2432S028/ILI9488/ST7796S). GC9A01 und SSD1309 ignorieren die
  Einstellung.
- Die Pin-Zuordnungen fuer ILI9488/ST7796S/GC9A01/SSD1309 sind
  **Standard-Verdrahtungsvorschlaege** fuer einen generischen ESP32/S3/C3-
  Aufbau (siehe Tabelle unten), keine Werksverdrahtung - bei abweichender
  eigener Verdrahtung im Webportal unter "Pin-Belegung" pro Feld anpassbar
  (leer lassen = Default aus der Tabelle), kein Neubauen/Neuflashen noetig.
  Siehe Abschnitt "Pin-Overrides" unten.
- **Nur ILI9341 (ESP32-2432S028) ist auf echter Hardware verifiziert.** Die
  anderen vier Panel-Treiber, das Rundlayout und das Mono-Layout sind neu und
  noch nicht gegengeprueft (siehe Abschnitt "Auf Hardware zu pruefen" unten).

### Pin-Tabelle (Standard-Verdrahtung)

**ESP32-2432S028 (ILI9341 + XPT2046, Werksverdrahtung):**

| Funktion | GPIO |
|---|---|
| TFT MOSI/MISO/SCLK | 13 / 12 / 14 |
| TFT CS/DC | 15 / 2 |
| TFT Backlight | 21 |
| Touch CS/IRQ | 33 / 36 |
| Touch MOSI/MISO/CLK | 32 / 39 / 25 |

**ILI9488 / ST7796S (generische SPI-Verdrahtung, kein Touch):**

Unterschiedlich je Zielchip (`board_profiles.c`, `#if CONFIG_IDF_TARGET_ESP32C3`),
da der C3 nur GPIO0-21 hat und GPIO23 dort nicht existiert. Das Webportal
(`/api/displays`) zeigt immer die fuer den tatsaechlich geflashten Build
gueltigen Pins an - hier beide Varianten zum Nachschlagen:

| Funktion | ESP32 / ESP32-S3 | ESP32-C3 |
|---|---|---|
| MOSI/MISO/SCLK | 23 / 19 / 18 | 4 / 5 / 6 |
| CS/DC | 5 / 17 | 7 / 10 |
| RESET | 16 | 3 |
| Backlight | 4 | 1 |

**GC9A01 (fest, chipunabhaengig, kein MISO/Touch):**

| Funktion | GPIO |
|---|---|
| SDA (MOSI) | 3 |
| SCL (SCLK) | 4 |
| CS | 1 |
| DC | 10 |
| RST | 0 |
| Backlight | kein separater Pin (fest verdrahtet/immer an) |
| Navigationstaste | BOOT-Taste des Devboards: GPIO9 (C3/C6/H2) bzw. GPIO0 (ESP32/S3) - auf ESP32/S3 identisch mit RST, dort daher deaktiviert (`board_profiles.c`) |

### Pin-Overrides (eigene Verdrahtung ohne Neubauen)

Alle Pins oben sind Defaults (`board_profiles.c`), aber im Webportal unter
"Pin-Belegung (optional anpassen)" pro Feld ueberschreibbar - z.B. wenn die
eigene Verdrahtung von den Vorschlaegen abweicht. Leeres Feld = Default
verwenden. Nach dem Speichern startet das Geraet neu und uebernimmt die
neuen Pins.

Design-Entscheidung: Es gibt **ein** Override-Set, nicht eins pro
Displaytyp - ein Geraet wird in der Praxis dauerhaft mit einem physischen
Display betrieben, eine Verwaltung von Overrides fuer fuenf gleichzeitig nie
genutzte Profile waere unnoetiger Aufwand gewesen. Wechselt man im Webportal
den Displaytyp, werden alle Pin-Overrides automatisch zurueckgesetzt (alte
Pins - z.B. Touch-Pins vom CYD-Profil - passen sonst i.d.R. nicht zum neuen
Panel/Bus). Implementiert in `board_profiles.h/.c` (`pin_override_t`,
`board_profile_apply_overrides()`) und `display_ui.c` (`lcd_init()` merged
Override + Default zu `s_profile`, bevor irgendein GPIO angefasst wird).

Da eine fehlerhafte Pin-Eingabe dank `LCD_CHECK` (siehe oben) nicht mehr zur
Boot-Schleife fuehrt, sondern nur die Display-Init ueberspringt, ist das
Risiko einer verunglueckten manuellen Anpassung gering - das Webportal
bleibt so oder so erreichbar, um es zu korrigieren.

### GC9A01: Screens, Standby, Navigationstaste

Das runde Minimal-UI hat zwei per Taste umschaltbare Screens (`display_ui.c`:
`s_round_screen`, `ROUND_SCR_*`):

- **Overview** (Default): Uhrzeit + zwei Arcs fuer CPU-/GPU-Auslastung,
  gleicher Radius, rechte Haelfte CPU / linke Haelfte GPU (ein gemeinsamer,
  geteilter Ring statt zwei ineinander verschachtelter Kreise), je mit
  Chip-Icon + Prozentzahl darunter.
- **Wetter**: Uhrzeit + Temperatur/gefuehlte Temperatur (Thermometer-Icon) /
  Luftfeuchte / Wind (Icon) / Regen (Icon) (erfordert `weather_enabled` im
  Webportal).

Ein Druck auf die BOOT-Taste schaltet zwischen beiden um. Da dieses Panel
keinen Backlight-Pin hat (`bl = -1`), laesst sich der Standby nicht per
Software abdunkeln - stattdessen wird der Bildschirminhalt reduziert: im
Standby werden **immer eine groessere, weiter oben stehende Uhrzeit + der
komplette Wetterblock** (dieselben vier Zeilen wie auf dem Wetter-Screen)
angezeigt, unabhaengig vom zuletzt gewaehlten Screen (Overview/CPU-GPU-Arcs
sind im Standby immer ausgeblendet). Ein Tastendruck im Standby weckt nur
auf (zeigt wieder den zuletzt gewaehlten Screen), loest aber keinen
Screen-Wechsel aus - analog zum Touch-Wakeup beim CYD-Profil.

Die Arc-Widgets sind eigentlich Schieberegler und zeichnen ohne
`lv_obj_remove_style(..., LV_PART_KNOB)` einen dicken Knob an der aktuellen
Werteposition - sah wie ein Glitch/Fremdkoerper auf dem duennen Ring aus,
ist jetzt entfernt (`build_round_ui()` in `display_ui.c`).

**Standby-Timeout ist im Webportal einstellbar** (Karte "Display" →
"Standby nach ... Sekunden", `app_config.standby_timeout_s`, 0 = deaktiviert,
Default 120s). Gilt fuer alle Displaytypen: bei Panels mit Backlight-Pin
wirkt er wie bisher (Backlight aus), bei GC9A01 wie oben beschrieben
(Inhalt reduzieren statt Helligkeit).

**SSD1309 (I2C, monochrom, kein Touch):**

| Funktion | GPIO/Wert |
|---|---|
| SDA | 8 |
| SCL | 9 |
| VCC | 3.3V |
| GND | GND |
| I2C-Adresse | 0x3C |

## Framework-Abbildung (Arduino → ESP-IDF)

| Arduino | ESP-IDF |
|---|---|
| `WiFi.h` | `esp_wifi` + `esp_netif` |
| `Preferences` | `nvs_flash` (`config_store.c`) |
| `ESPAsyncWebServer` | `esp_http_server` (`web_portal.c`) |
| `PubSubClient` | `esp-mqtt` (`mqtt_handler.c`) |
| NTP über `configTzTime` | `esp_sntp` (`time_service.c`) |
| `HTTPClient` | `esp_http_client` (`weather_service.c`) |
| `Update` (OTA) | `esp_ota_ops` (`web_portal.c`, Raw-Binary-Upload) |
| `TFT_eSPI` | `esp_lcd` (mehrere Panel-Treiber) + **LVGL 9** via `esp_lvgl_port` (`display_ui.c`, `board_profiles.c`) |
| `XPT2046_Touchscreen` | eigener SPI-Treiber (`touch_xpt2046.c`) |
| `ArduinoJson` | `cJSON` (Core bei IDF <6.0, managed component `espressif/cjson` ab 6.0) |

## Voraussetzungen

- **ESP-IDF 5.1 – 6.x** (mit `idf.py` im PATH, `. $IDF_PATH/export.sh`).
- Internetzugang beim ersten Build: Managed Components werden automatisch aus
  dem ESP Component Registry geladen (LVGL, esp_lvgl_port, mehrere
  esp_lcd-Panel-Treiber, ab **ESP-IDF v6.0** auch `espressif/mqtt` und
  `espressif/cjson`, da esp-mqtt und cJSON dort nicht mehr Teil des Cores sind).

## Bauen & Flashen

```bash
idf.py set-target esp32       # oder esp32s3 / esp32c3, siehe Hinweis unten
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Ersteinrichtung: offener AP **`ESP32-HWMon-XXXX`** → `http://192.168.4.1` →
WLAN eintragen → speichern → Neustart. Das Webportal zeigt beim ersten
Aufruf nur die WLAN-Karte (SSID/Passwort, mit Button "WLAN-Netzwerke
suchen" fuer eine Liste der in Reichweite gefundenen Netze statt manueller
SSID-Eingabe); alle weiteren Einstellungen (MQTT/Zeitzone/Displaytyp/
Pin-Belegung/OTA) liegen hinter "Erweiterte Einstellungen anzeigen" verborgen.
Diese Trennung vermeidet, dass mobile Browser beim Ausfuellen des langen
Formulars zwischenzeitlich den Fokus verlieren und zurueck aufs SSID-Feld
springen.

### CI-Build (GitHub Actions)

`.github/workflows/build-idf.yml` baut die Firmware im offiziellen
`espressif/idf`-Docker-Image (`espressif/esp-idf-ci-action`) und fasst
Bootloader + Partitionstabelle + App-Image per `idf.py merge-bin` zu
**einer** flashbaren Datei zusammen (`esp32-hwmonitor-vX.X.X-idf[-<target>].bin`).
Diese Datei laesst sich per Web-Flasher (z.B. ESP Web Tools/esptool-js) oder
`esptool.py write_flash 0x0 <datei>` in einem Rutsch flashen, statt
Bootloader/Partitionstabelle/App einzeln an drei Offsets schreiben zu muessen.

Zwei Ausloeser:

- **Manuell** (Tab "Actions" → "ESP-IDF Build" → "Run workflow"): Ziel-Chip
  (esp32/esp32s3/esp32c3) und ESP-IDF-Docker-Tag waehlen, baut nur diesen
  einen Chip. Ergebnis liegt als Artifact am Workflow-Run, `FW_VERSION` kommt
  unveraendert aus `main/shared_state.h`.
- **Tag-Push** (`git tag v1.0.0-idf && git push origin v1.0.0-idf`): baut alle
  drei Chips (esp32/esp32s3/esp32c3) und legt anschliessend automatisch ein
  GitHub-Release zum Tag an, mit allen drei `.bin`-Dateien als Anhang.
  `FW_VERSION` wird dabei **aus dem Tag** gesetzt (fuehrendes "v" abgeschnitten,
  `v1.0.0-idf` → `FW_VERSION "1.0.0-idf"`) statt aus `main/shared_state.h` -
  der im Repo eingetragene Wert dient nur als Default fuer manuelle Builds.

> **Captive-Portal-Erkennung des Handys kann kurzzeitig Sockets ausschoepfen**:
> iOS/Android/Windows pruefen die Internetverbindung im Setup-AP ueber mehrere
> parallele Anfragen (`hotspot-detect.html`, `generate_204`,
> `connecttest.txt`, ...). `CONFIG_LWIP_MAX_SOCKETS` ist deshalb auf 16 erhoeht
> (`httpd_config_t.max_open_sockets=13` in `web_portal.c`) und die bekanntesten
> Erkennungspfade sind direkt registriert statt nur ueber den generischen
> 404-Handler zu laufen. Aeussert sich unbehandelt als `error in accept (23)`/
> `error in recv: 104` im Log und als springender Fokus/Neuladen im
> Config-Formular auf dem Handy.

> **Hinweis zu ESP32-S3/-C3**: Der Code selbst ist chip-unabhaengig (die
> GPIO-Nummern in `board_profiles.c` sind reine Zahlen, keine ESP32-classic-
> Spezifika mehr - mit Ausnahme des ESP32-2432S028-Profils, das die feste
> Werksverdrahtung dieses Boards beschreibt und nur auf klassischem ESP32
> Sinn ergibt). Fuer S3/C3 einfach mit `idf.py set-target esp32s3` bzw.
> `esp32c3` bauen und die generischen SPI-/I2C-Displays entsprechend
> Pin-Tabelle oben verdrahten. **Nicht separat getestet** - insbesondere
> ESP32-C3 hat weniger SRAM und kein PSRAM; ein 480x320-Framebuffer
> (ILI9488/ST7796S) kann dort eng werden (siehe
> `lvgl_port_display_cfg_t.buffer_size` in `display_ui.c`, ggf. reduzieren).
> Der C3 hat ausserdem nur einen General-Purpose-SPI-Controller (`SPI2_HOST`,
> kein `SPI3_HOST`) - betrifft nur den (dort ohnehin nicht waehlbaren)
> CYD-Touch-Bus, `board_profiles.c` waehlt dafuer automatisch per
> `SOC_SPI_PERIPH_NUM` einen kompilierbaren Platzhalter. Der C3 hat zudem nur
> GPIO0-21 (22 Pins statt bis zu 39/48 bei ESP32/S3) - der generische
> SPI-Pinsatz ist deshalb chipabhaengig definiert (siehe Pin-Tabelle oben).

> **Falsche/unpassende Displaywahl fuehrt nicht mehr zur Boot-Schleife**:
> Panel-Init-Fehler (z.B. eine auf dem Zielchip nicht existierende GPIO-Nummer)
> loesen seit `LCD_CHECK` in `display_ui.c` keinen `ESP_ERROR_CHECK`-Abort mehr
> aus, sondern werden geloggt; das Geraet startet trotzdem WLAN/Webportal, nur
> ohne Bildschirmausgabe. So bleibt die Displayauswahl im Webportal immer
> erreichbar, um einen Fehlgriff zu korrigieren - auch nach einem
> Chip-/Profilwechsel, bei dem die in NVS gespeicherte alte Auswahl auf dem
> neuen Chip nicht mehr passt.

> **Hinweis bei bereits vorhandener lokaler `sdkconfig`**: `sdkconfig.defaults`
> wird nur bei einer *neuen* `sdkconfig` angewendet, nicht bei einem
> bestehenden Build-Verzeichnis. Betrifft nicht nur fehlende LVGL-Features
> (`lv_font_montserrat_24`, `lv_arc_create`, ...), sondern auch die
> **Partitionstabelle**: ohne `CONFIG_PARTITION_TABLE_CUSTOM=y` aus einer
> aktuellen `sdkconfig` landet der Build auf dem ESP-IDF-Standardlayout
> ("factory", 1 MB) statt auf den beiden 1.75-MB-OTA-Slots aus
> `partitions.csv` - der Build bricht dann mit "app partition is too small"
> ab, obwohl das Image eigentlich passt. Bei Problemen dieser Art immer
> zuerst die lokale `sdkconfig` loeschen und neu bauen lassen, statt einzelne
> Optionen von Hand zu suchen.

> **Image-Groesse**: Ein Image mit allen 5 Displaytreibern kommt je nach
> Zielchip auf bis zu ~1.6 MB. Die OTA-Partitionen sind daher auf 1.75 MB
> je Slot ausgelegt (`partitions.csv`) und der Build ist auf Groesse statt
> Geschwindigkeit optimiert (`CONFIG_COMPILER_OPTIMIZATION_SIZE=y`). Falls
> "app partition is too small" trotz aktueller `sdkconfig` weiterhin
> auftritt, in `partitions.csv` die `ota_0`/`ota_1`-Groessen weiter erhoehen
> (4 MB Flash abzueglich `nvs`/`otadata`/`phy_init` erlauben rechnerisch bis
> knapp unter 2 MB je Slot) oder ungenutzte Displaytreiber testweise aus
> `main/idf_component.yml`/`display_ui.c` entfernen.

## Auf Hardware zu pruefen / ggf. anzupassen

Diese Punkte sind board-abhängig und ließen sich ohne Gerät nicht verifizieren:

1. **Display-Farben** — Farbinversion ist jetzt im Webportal umschaltbar
   (Karte "Display" → "Farben invertieren (Dark Mode)", `app_config.color_invert`,
   Default aus/false) statt fest in `display_ui.c` verdrahtet. Fuer ILI9341
   (ESP32-2432S028) ist "aus" (Default) auf Hardware verifiziert korrekt; beim
   GC9A01 war der Hintergrund ohne Inversion hell statt dunkel - dort "an"
   waehlen. Falls stattdessen Rot/Blau vertauscht sind (anderes Symptom als
   heller/dunkler Hintergrund): das hilft `color_invert` nicht, dafuer
   `board_profile_t.bgr` in `board_profiles.c` anpassen.
2. **Neue Panel-Treiber-Komponenten** — `main/idf_component.yml` referenziert
   `atanisoft/esp_lcd_ili9488` (Community-Komponente, kein offizieller
   `espressif/`-Namespace-Eintrag existiert dafuer) sowie `espressif/esp_lcd_st7796`
   und `espressif/esp_lcd_gc9a01` (beide offiziell, gegen die Registry-Eintraege
   geprueft). SSD1306/SSD1309 brauchen **keinen** Registry-Eintrag - der Treiber
   ist bereits Teil des Core-`esp_lcd`-Components (`esp_lcd_panel_vendor.h`).
   Ein frueherer Stand dieses Branches hatte faelschlich einen nicht
   existierenden `espressif/esp_lcd_panel_ssd1306`-Eintrag, der den Build mit
   "Version solving failed" abbrach - das ist behoben.
3. **SSD1309 ueber I2C** — `display_ui.c: lcd_init_mono_i2c()` nutzt den
   Core-SSD1306-Treiber (SSD1309 spricht dasselbe Protokoll, aber ggf. mit
   abweichenden Kontrast-/Multiplex-Defaults) ueber den neuen `i2c_master`-
   Treiber (`driver/i2c_master.h`, `i2c_new_master_bus()` +
   `esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t, ...)`) - die alte
   `driver/i2c.h`-API liefert auf ESP-IDF ≥5.2/6.x nicht mehr den dafuer
   erwarteten Bus-Handle-Typ. Noch nicht auf Hardware verifiziert.
4. **GC9A01-Rundlayout** — `display_ui.c: build_round_ui()` ist bewusst ein
   einfacher Platzhalter (Arcs/Text, zwei Screens, siehe Abschnitt oben), kein
   ausgearbeitetes rundes Design. Auf einem ersten Testaufbau bestaetigt
   (180°-Korrektur noetig, siehe Punkt 5, sowie Layout/Screens/Boot-Taste
   grundsaetzlich funktionsfaehig) - Feinschliff (Schriftgroessen, Icons im
   Wetter-Screen, ggf. weitere Screens) noch offen.
5. **Display-Rotation / Spiegelung** — `app_config.rotation` hat je nach
   Panelform eine andere Bedeutung; das Webportal zeigt dafuer nur die pro
   Displaytyp sinnvollen Werte an (`renderRotationOptions()` in
   `web_portal.c`):
   - LCD_SHAPE_RECT (ESP32-2432S028/ILI9488/ST7796S): waehlt zwischen den vier
     Landscape/Portrait-Faellen, das Webportal bietet hier aber nur 1 und 3
     an (jeweils Landscape, 180° zueinander) - 0/2 schalten intern auf
     Portrait um, was die fest auf Landscape gezeichnete Kachel-UI zerreisst.
     Fuer ILI9341 rotation=1 (Default) auf Hardware verifiziert, rotation=3
     ebenfalls (siehe Touch-Hinweis unten); die anderen Panels nicht einzeln
     getestet.
   - LCD_SHAPE_ROUND (GC9A01): waehlt direkt zwischen den vier moeglichen
     `mirror_x`/`mirror_y`-Kombinationen (`lcd_init_color_spi()` in
     `display_ui.c`), da nicht vorhersagbar ist, welche Kombination bei
     gegebener Verbauung/Panel-Charge Text weder kopfueber noch
     seitenverkehrt darstellt - **auf dem ersten Testaufbau reichte die
     180°-Kombination (`rotation=3`) nicht aus, das Bild blieb
     seitenverkehrt** (mirror_x/mirror_y zusammen entspricht nicht
     zwangslaeufig "richtig herum", sondern haengt von der Scan-Richtung
     des jeweiligen Panels ab). Einfach 0-3 durchprobieren, kein Neuflashen
     noetig (nur Neustart nach dem Speichern).
6. **Touch-Kalibrierung** — `touch_xpt2046.c`: `TOUCH_RAW_*`-Grenzen und die
   Achsen-Zuordnung pro `rotation`, nur fuer das ESP32-2432S028-Profil
   relevant. `rotation=3` (180°) mappte die Touch-Koordinaten bis vor Kurzem
   identisch zu `rotation=1` - der Touchpunkt war dadurch bei gedrehtem Text
   oben/unten und links/rechts vertauscht. Beide Achsen fuer `rotation=3`
   jetzt gegenueber `rotation=1` invertiert.
7. **LVGL-Komponenten-Versionen** — `main/idf_component.yml`. Falls der
   Component Manager andere Versionen erwartet, dort die Ranges anpassen.
   Die UI ist gegen die LVGL-9-API geschrieben.
8. **Icons (Material Design Icons)** — `main/font_mdi_icons_20.c` +
   `main/mdi_icons.h`, nur von der Kachel-UI genutzt. Auf ESP32-2432S028-
   Hardware verifiziert (siehe Abschnitt "Icons" unten).

## Design

Angelehnt an ein vom Nutzer bereitgestelltes Referenz-Layout (Karten mit
Farbverlauf-Balken, Wetter-/Wind-/Regen-Anzeige, Trend-Pfeile, Settings-Knopf):

- **Icons**: Material Design Icons (siehe eigener Abschnitt unten) für
  Chip/Thermometer/Sonne/Wind/Regen/Zahnrad/Zurück-Pfeil/WLAN; eingebaute
  LVGL-Symbole (`LV_SYMBOL_CHARGE`, `LV_SYMBOL_UP`/`_DOWN`) für Power/Trend,
  da diese mit Fließtext in einem Label gemischt sind.
- **Wetter-Block**: nutzt weiterhin nur die bereits eingebundene
  OpenWeatherMap-"Current Weather"-API, inkl. gefühlter Temperatur,
  Luftfeuchte, Windgeschwindigkeit/-richtung (`weather_wind_compass()`,
  8-Punkte-Kompass) und Regenmenge (`rain.1h`, 0 falls kein Regen gemeldet).
  Nur auf der Kachel-UI sichtbar. Keine neue Konfiguration nötig, alles über
  den bestehenden `weather_api_key`.
- **Settings-Screen** (Zahnrad-Knopf, nur Kachel-UI): zeigt Firmware-Version,
  IP-Adresse, WLAN-/MQTT-Status und freien Speicher. Der Knopf "Neustart in
  Setup-AP" (`web_portal_force_ap()`) schaltet **zur Laufzeit ohne
  Geräte-Neustart** vom WLAN auf den offenen Setup-AP um, ohne die
  gespeicherten WLAN-Zugangsdaten zu löschen. Erfordert zur Bestätigung zwei
  Taps (verhindert versehentliches Trennen).
- **Trend-Pfeile**: vergleichen den aktuellen Auslastungswert mit dem Wert
  vor bis zu 10 Messungen (`compute_trend()` in `display_ui.c`); Schwelle
  ±3 Prozentpunkte für steigend/fallend, sonst "stabil" (Strich).
- **Standby** (`display_ui.c`, `check_standby()`/`enter_standby()`/
  `exit_standby()`): greift, wenn seit `app_config.standby_timeout_s`
  (im Webportal einstellbar, Default 120s, 0 = deaktiviert) keine neuen
  MQTT-Hardwaredaten eingetroffen sind. Bei Panels mit Backlight-Pin geht
  die Beleuchtung per LEDC auf Duty 0; bei Panels ohne Backlight-Pin
  (GC9A01, SSD1309) bleibt die Beleuchtung unveraendert (OLED ist
  selbstleuchtend, GC9A01 haengt bei diesem Board fest an VCC) - beim
  GC9A01 wird stattdessen der Inhalt auf Uhrzeit+Wetter reduziert (siehe
  GC9A01-Abschnitt oben). Aufwecken automatisch, sobald wieder Daten
  eintreffen, oder per Touch (ESP32-2432S028) bzw. Boot-Taste (GC9A01).

## Icons (Material Design Icons)

`main/font_mdi_icons_20.c` ist eine mit
[`lv_font_conv`](https://github.com/lvgl/lv_font_conv) aus dem npm-Paket
`@mdi/font` (Material Design Icons, Pictogrammers, Apache-2.0-Lizenz)
generierte LVGL-Font — **nur die 9 tatsächlich genutzten Glyphen** bei 20px/
4bpp, nicht der komplette Icon-Satz (der hätte mehrere MB). `main/mdi_icons.h`
deklariert die Font (`extern const lv_font_t mdi_icons_20;`) sowie ein
`#define MDI_<NAME>` je Icon (UTF-8-codierter Codepoint als C-String), analog
zu LVGLs eigenen `LV_SYMBOL_*`-Makros nutzbar: `make_label(parent, MDI_COG,
&mdi_icons_20, farbe)`.

Verwendet werden `chip`, `thermometer`, `weather-sunny`, `weather-windy`,
`weather-rainy`, `water-percent`, `cog`, `arrow-left`, `wifi` (alle in
`mdi_icons.h` dokumentiert mit Original-Namen und Unicode-Codepoint).
`water-percent` (Tropfen mit Prozent) wurde nachtraeglich fuer die
Luftfeuchte-Anzeige im runden GC9A01-UI ergaenzt (Codepoint U+F058E,
remapped auf U+E009) - Regenerierungsbefehl siehe unten, inklusive dieses
neunten Eintrags im `-r`-Remapping.

**Wichtige Einschränkung**: Ein MDI-Glyph kann nur in einem Label gerendert
werden, dessen Font auf `&mdi_icons_20` gesetzt ist — er lässt sich *nicht*
mitten in einen mit `&lv_font_montserrat_*` gerenderten Text einbetten (im
Gegensatz zu LVGLs `LV_SYMBOL_*`, die Teil jeder Montserrat-Font-Größe sind).
Deshalb sind MDI-Icons im Code immer eigene, separate Label-Objekte neben dem
Text, nie in einen gemeinsamen String eingebettet.

**Weitere Icons ergänzen**: Icon-Name in `.../package/css/materialdesignicons.css`
nachschlagen (Codepoint hinter `content: "\FXXXXX"`), dann mit Remapping auf
einen freien Codepoint ab `0xE00A` aufwaerts (Basic Multilingual Plane,
**nicht** den MDI-Originalcodepoint direkt verwenden!) neu generieren - dabei
immer **alle bisherigen Remaps mit angeben**, sonst verschieben sich die
bestehenden Icons:

```bash
npm pack @mdi/font@7.4.47 && tar xzf mdi-font-7.4.47.tgz
npx lv_font_conv --font package/fonts/materialdesignicons-webfont.ttf \
  -r '0xF004D=>0xE001,0xF0493=>0xE002,0xF050F=>0xE003,0xF0597=>0xE004,0xF0599=>0xE005,0xF059D=>0xE006,0xF05A9=>0xE007,0xF061A=>0xE008,0xF058E=>0xE009,<original>=>0xE00A' \
  --size 20 --bpp 4 --format lvgl --lv-font-name mdi_icons_20 --no-compress --no-prefilter \
  -o font_mdi_icons_20.c
```

Den `#ifdef LV_LVGL_H_INCLUDE_SIMPLE`-Include-Block danach durch ein einfaches
`#include "lvgl.h"` ersetzen (passend zum Rest des Projekts).

## MQTT-Datenformat

Der PC-Client publiziert ein JSON-Objekt auf das konfigurierte Topic
(Standard: `pulsemqtt/hwinfo`):

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
`tools/pc_bridge_example.py` zeigt beispielhaft, wie ein Python-Skript auf
dem PC Werte per MQTT veröffentlicht.

## Struktur

```
CMakeLists.txt, sdkconfig.defaults, partitions.csv
main/
  app_main.c           Boot/Verdrahtung
  shared_state.[ch]    globale Zustände, Ringpuffer
  config_store.[ch]    NVS-Persistenz
  board_profiles.[ch]  Displaytyp-Enum + Pin-/Bus-/UI-Profile (Kern der Multi-Display-Unterstuetzung)
  web_portal.[ch]      WLAN (STA/offener AP) + HTTP-Portal (inkl. Displayauswahl) + OTA
  mqtt_handler.[ch]    esp-mqtt + JSON-Parsing
  time_service.[ch]    SNTP + Zeitzone
  weather_service.[ch] OpenWeatherMap-Abruf
  display_ui.[ch]      esp_lcd (mehrere Panel-Treiber) + LVGL-Oberflaeche (Kachel-/Mono-/Rund-UI)
  font_mdi_icons_20.c  generierte Material-Design-Icons-Font
  mdi_icons.h          Font-Deklaration + MDI_*-Zeichen-Makros
  touch_xpt2046.[ch]   XPT2046-SPI-Treiber (nur ESP32-2432S028-Profil)
tools/
  pc_bridge_example.py Beispiel PC->MQTT Bridge
```
