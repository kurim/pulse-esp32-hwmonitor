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

| Displaytyp | Aufloesung | Bus | Touch | UI |
|---|---|---|---|---|
| ESP32-2432S028, ILI9341 | 320x240 | SPI | XPT2046 | volle Kachel-UI (Verlauf, Settings) |
| ILI9488 | 480x320 | SPI | nein | Kachel-UI, skaliert auf die Aufloesung |
| ST7796S | 480x320 | SPI | nein | Kachel-UI, skaliert auf die Aufloesung |
| GC9A01 (rund) | 240x240 | SPI | nein | **Platzhalter**-UI (Arcs + Text, noch nicht final) |
| SSD1309 | 128x64, monochrom | I2C | nein | **Platzhalter**-UI (nur Text, kein Farbverlauf) |

**Wichtige Einschraenkungen dieser ersten Umsetzung:**

- Nur das erste Profil (ESP32-2432S028) hat Touch. Bei den anderen vier
  Profilen gibt es keine Navigation am Geraet (Verlaufs-/Settings-Screen);
  Konfiguration laeuft vollstaendig ueber das Webportal.
- Rotation (`app_config.rotation`) wirkt nur auf die rechteckige Kachel-UI
  (ESP32-2432S028/ILI9488/ST7796S). GC9A01 und SSD1309 ignorieren die
  Einstellung.
- Die Pin-Zuordnungen fuer ILI9488/ST7796S/GC9A01/SSD1309 sind
  **Standard-Verdrahtungsvorschlaege** fuer einen generischen ESP32/S3/C3-
  Aufbau (siehe Tabelle unten), keine Werksverdrahtung. Bei abweichender
  eigener Verdrahtung muessen die Defines in `main/board_profiles.c`
  angepasst werden - eine Pin-Konfiguration ueber das Webportal ist (noch)
  nicht vorgesehen.
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

**ILI9488 / ST7796S / GC9A01 (generische SPI-Verdrahtung, kein Touch):**

| Funktion | GPIO |
|---|---|
| MOSI/MISO/SCLK | 23 / 19 / 18 |
| CS/DC | 5 / 17 |
| RESET | 16 |
| Backlight | 4 |

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
WLAN/MQTT/Zeitzone/Displaytyp eintragen → speichern → Neustart.

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

> **Hinweis bei bereits vorhandener lokaler `sdkconfig`**: `sdkconfig.defaults`
> wird nur bei einer *neuen* `sdkconfig` angewendet, nicht bei einem
> bestehenden Build-Verzeichnis. Bei Fehlern zu fehlenden LVGL-Features
> (`lv_font_montserrat_24`, `lv_arc_create`, ...) die lokale `sdkconfig`
> loeschen oder die Optionen per `idf.py menuconfig` nachtragen.

## Auf Hardware zu pruefen / ggf. anzupassen

Diese Punkte sind board-abhängig und ließen sich ohne Gerät nicht verifizieren:

1. **Display-Farben** — `display_ui.c`, `esp_lcd_panel_invert_color(panel, false)`
   und `board_profile_t.bgr`. Fuer ILI9341 (ESP32-2432S028) auf Hardware
   verifiziert; die anderen vier Panels noch nicht - bei falschen/invertierten
   Farben `bgr`-Flag in `board_profiles.c` bzw. `invert_color` in
   `display_ui.c` anpassen.
2. **Neue Panel-Treiber-Komponenten** — `main/idf_component.yml` listet
   `espressif/esp_lcd_ili9488`, `esp_lcd_st7796`, `esp_lcd_gc9a01` und
   `esp_lcd_panel_ssd1306` mit den ueblichen Espressif-Namenskonventionen.
   **Noch nicht gegen den echten ESP Component Registry-Eintrag verifiziert**
   - falls `idf.py build` eine dieser Abhaengigkeiten nicht aufloesen kann,
     den exakten Namen unter https://components.espressif.com nachschlagen.
3. **SSD1309 ueber I2C** — `display_ui.c: lcd_init_mono_i2c()` nutzt den
   SSD1306-kompatiblen Treiber (SSD1309 spricht dasselbe Protokoll, aber ggf.
   mit abweichenden Kontrast-/Multiplex-Defaults). Noch nicht auf Hardware
   verifiziert.
4. **GC9A01-Rundlayout** — `display_ui.c: build_round_ui()` ist bewusst ein
   einfacher Platzhalter (zwei `lv_arc`s + Text), kein ausgearbeitetes rundes
   Design. Layout/Positionierung noch nicht auf Hardware geprueft.
5. **Display-Rotation / Spiegelung** — nur fuer die rechteckige Kachel-UI
   relevant (ESP32-2432S028/ILI9488/ST7796S). Fuer ILI9341 rotation=1
   (Default) auf Hardware verifiziert; die anderen Panels/Rotationen nicht
   einzeln getestet. Portrait (rotation 0/2) ist zusaetzlich unlayoutet, da
   die Kachel-UI fest auf Landscape ausgelegt ist.
6. **Touch-Kalibrierung** — `touch_xpt2046.c`: `TOUCH_RAW_*`-Grenzen und die
   Achsen-Zuordnung pro `rotation`, nur fuer das ESP32-2432S028-Profil
   relevant.
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
  `exit_standby()`): Backlight geht per LEDC auf Duty 0, wenn seit
  `STANDBY_TIMEOUT_MS` (Default 2 Minuten) keine neuen MQTT-Hardwaredaten
  eingetroffen sind. Nur fuer Panels mit Backlight-Pin relevant (OLED
  entfaellt, da selbstleuchtend). Aufwecken automatisch, sobald wieder Daten
  eintreffen, oder per Touch (nur ESP32-2432S028-Profil).

## Icons (Material Design Icons)

`main/font_mdi_icons_20.c` ist eine mit
[`lv_font_conv`](https://github.com/lvgl/lv_font_conv) aus dem npm-Paket
`@mdi/font` (Material Design Icons, Pictogrammers, Apache-2.0-Lizenz)
generierte LVGL-Font — **nur die 8 tatsächlich genutzten Glyphen** bei 20px/
4bpp, nicht der komplette Icon-Satz (der hätte mehrere MB). `main/mdi_icons.h`
deklariert die Font (`extern const lv_font_t mdi_icons_20;`) sowie ein
`#define MDI_<NAME>` je Icon (UTF-8-codierter Codepoint als C-String), analog
zu LVGLs eigenen `LV_SYMBOL_*`-Makros nutzbar: `make_label(parent, MDI_COG,
&mdi_icons_20, farbe)`.

Verwendet werden `chip`, `thermometer`, `weather-sunny`, `weather-windy`,
`weather-rainy`, `cog`, `arrow-left`, `wifi` (alle in `mdi_icons.h`
dokumentiert mit Original-Namen und Unicode-Codepoint).

**Wichtige Einschränkung**: Ein MDI-Glyph kann nur in einem Label gerendert
werden, dessen Font auf `&mdi_icons_20` gesetzt ist — er lässt sich *nicht*
mitten in einen mit `&lv_font_montserrat_*` gerenderten Text einbetten (im
Gegensatz zu LVGLs `LV_SYMBOL_*`, die Teil jeder Montserrat-Font-Größe sind).
Deshalb sind MDI-Icons im Code immer eigene, separate Label-Objekte neben dem
Text, nie in einen gemeinsamen String eingebettet.

**Weitere Icons ergänzen**: Icon-Name in `.../package/css/materialdesignicons.css`
nachschlagen (Codepoint hinter `content: "\FXXXXX"`), dann mit Remapping auf
einen freien Codepoint ab `0xE009` aufwaerts (Basic Multilingual Plane,
**nicht** den MDI-Originalcodepoint direkt verwenden!) neu generieren:

```bash
npm pack @mdi/font@7.4.47 && tar xzf mdi-font-7.4.47.tgz
npx lv_font_conv --font package/fonts/materialdesignicons-webfont.ttf \
  -r '0xF004D=>0xE001,0xF0493=>0xE002,0xF050F=>0xE003,0xF0597=>0xE004,0xF0599=>0xE005,0xF059D=>0xE006,0xF05A9=>0xE007,0xF061A=>0xE008,<original>=>0xE009' \
  --size 20 --bpp 4 --format lvgl --lv-font-name mdi_icons_20 \
  -o font_mdi_icons_20.c
```

Den `#ifdef LV_LVGL_H_INCLUDE_SIMPLE`-Include-Block danach durch ein einfaches
`#include "lvgl.h"` ersetzen (passend zum Rest des Projekts). Bitmap-Format
ohne Kompression generieren (`--no-compress --no-prefilter`).

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
