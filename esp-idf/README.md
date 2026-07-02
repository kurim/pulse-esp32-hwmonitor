# CYD Hardware-Monitor — ESP-IDF-Port

Portierung der Arduino-Firmware (`../src`) auf **pures ESP-IDF** (kein Arduino
Core). Gleiche Funktion: Uhrzeit/Datum, optional Wetter, CPU/GPU-Auslastung,
-Temperatur und -Leistung per MQTT, Verlaufsdiagramme per Antippen der Kacheln,
WLAN-Setup über offenen Access Point mit Config-Webportal und OTA-Update.

> ⚠️ **Auf Hardware verifiziert und iteriert.** Baut und flasht mit
> ESP-IDF 6.0.x. Farbinversion, Display-Rotation (`rotation=1`) und das
> Top-Bar-Layout wurden anhand von Fotos vom laufenden Gerät korrigiert.
> Die Material-Design-Icons-Font zeigte auf Hardware zweimal in Folge **gar
> keine** Icons (siehe Abschnitt "Icons") — im dritten Anlauf auf Codepoints
> im selben 3-Byte-UTF-8-Bereich wie LVGLs eigene `LV_SYMBOL_*`-Zeichen
> umgestellt, noch nicht gegengeprüft.

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
| `TFT_eSPI` | `esp_lcd` (ILI9341) + **LVGL 9** via `esp_lvgl_port` (`display_ui.c`) |
| `XPT2046_Touchscreen` | eigener SPI-Treiber (`touch_xpt2046.c`) |
| `ArduinoJson` | `cJSON` (Core bei IDF <6.0, managed component `espressif/cjson` ab 6.0) |

## Voraussetzungen

- **ESP-IDF 5.1 – 6.x** (mit `idf.py` im PATH, `. $IDF_PATH/export.sh`).
- Internetzugang beim ersten Build: Managed Components werden automatisch aus
  dem ESP Component Registry geladen (LVGL, esp_lvgl_port, esp_lcd_ili9341
  sowie ab **ESP-IDF v6.0** auch `espressif/mqtt` und `espressif/cjson`, da
  esp-mqtt und cJSON dort nicht mehr Teil des Cores sind).

## Bauen & Flashen

```bash
cd esp-idf
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Ersteinrichtung wie beim Arduino-Port: offener AP **`CYD-Setup-XXXX`** →
`http://192.168.4.1` → WLAN/MQTT/Zeitzone eintragen → speichern → Neustart.

## Auf Hardware zu prüfen / ggf. anzupassen

Diese Punkte sind board-abhängig und ließen sich ohne Gerät nicht verifizieren:

1. **Display-Farben** — `display_ui.c`, `esp_lcd_panel_invert_color(panel, false)`.
   Auf Hardware verifiziert: `true` ergab ein komplett invertiertes Bild.
   Bei falscher Rot/Blau-Vertauschung `LCD_RGB_ELEMENT_ORDER_BGR` ↔ `_RGB`
   tauschen (noch nicht beobachtet, aber board-abhängig möglich).
2. **Display-Rotation / Spiegelung** — `lcd_init()` setzt `swap_xy/mirror_x/
   mirror_y` je `rotation`. Für `rotation=1` (Default) auf Hardware
   verifiziert und korrigiert (war horizontal gespiegelt, jetzt behoben);
   `rotation=3` als 180°-Gegenstück mitgezogen, aber nicht einzeln getestet.
   `rotation=0/2` (Portrait) sind zusätzlich unlayoutet, da die UI fest auf
   320×240 Landscape ausgelegt ist. Steht das Bild weiterhin gespiegelt/
   kopfüber, die `mirror_*`-Flags des betreffenden `case` anpassen.
3. **Touch-Kalibrierung** — `touch_xpt2046.c`: `TOUCH_RAW_*`-Grenzen und die
   Achsen-Zuordnung pro `rotation`. Bei versetztem/gespiegeltem Touch dort
   nachziehen (analog zur Arduino-Version; `rotation=3` ist bereits beide
   Achsen gespiegelt).
4. **LVGL-Komponenten-Versionen** — `main/idf_component.yml`. Falls der
   Component Manager andere Versionen erwartet, dort die Ranges anpassen.
   Die UI ist gegen die LVGL-9-API geschrieben.
5. **Icons (Material Design Icons)** — `main/font_mdi_icons_20.c` +
   `main/mdi_icons.h`. Chip/Thermometer/Sonne/Wind/Regen/Zahnrad/Zurück-
   Pfeil/WLAN kommen jetzt aus einer echten Material-Design-Icons-Schrift
   statt aus handgezeichneten Formen (siehe Abschnitt "Icons" unten) — noch
   nicht auf Hardware gegengeprüft. Power-/Trend-Symbole nutzen weiterhin
   LVGLs eingebaute `LV_SYMBOL_*`-Glyphen (bereits verifiziert), da sie mit
   Fließtext in einem Label gemischt werden und Font-Wechsel mitten im String
   nicht ohne Weiteres möglich sind.
6. **Layout-Feinjustage** — Text kann bei sehr langen Wetterbeschreibungen
   oder extremen Werten (z.B. 3-stellige °C) knapp werden; Positionen sind
   mit Sicherheitsabständen (feste Breiten + `CLIP`) versehen, aber nicht
   jede Kombination wurde getestet.

## Design

Angelehnt an ein vom Nutzer bereitgestelltes Referenz-Layout (Karten mit
Farbverlauf-Balken, Wetter-/Wind-/Regen-Anzeige, Trend-Pfeile, Settings-Knopf):

- **Icons**: Material Design Icons (siehe eigener Abschnitt unten) für
  Chip/Thermometer/Sonne/Wind/Regen/Zahnrad/Zurück-Pfeil/WLAN; eingebaute
  LVGL-Symbole (`LV_SYMBOL_CHARGE`, `LV_SYMBOL_UP`/`_DOWN`) für Power/Trend,
  da diese mit Fließtext in einem Label gemischt sind.
- **Wetter-Block**: nutzt weiterhin nur die bereits eingebundene
  OpenWeatherMap-"Current Weather"-API, jetzt inkl. gefühlter Temperatur,
  Luftfeuchte, Windgeschwindigkeit/-richtung (`weather_wind_compass()`,
  8-Punkte-Kompass) und Regenmenge (`rain.1h`, 0 falls kein Regen gemeldet).
  Keine neue Konfiguration nötig, alles über den bestehenden `weather_api_key`.
- **Settings-Screen** (Zahnrad-Knopf auf Haupt- und Detailschirm): zeigt
  Firmware-Version, IP-Adresse, WLAN-/MQTT-Status und freien Speicher. Der
  Knopf "Neustart in Setup-AP" (`web_portal_force_ap()`) schaltet **zur
  Laufzeit ohne Geräte-Neustart** vom WLAN auf den offenen Setup-AP um, ohne
  die gespeicherten WLAN-Zugangsdaten zu löschen — nach dem nächsten normalen
  Neustart verbindet sich das Gerät wieder regulär. Erfordert zur Bestätigung
  zwei Taps (verhindert versehentliches Trennen). Volle WLAN-/MQTT-/Wetter-
  Konfiguration bleibt bewusst im Webportal, nicht am Touchscreen (keine
  Bildschirmtastatur nötig).
- **Trend-Pfeile**: vergleichen den aktuellen Auslastungswert mit dem Wert
  vor bis zu 10 Messungen (`compute_trend()` in `display_ui.c`); Schwelle
  ±3 Prozentpunkte für steigend/fallend, sonst "stabil" (Strich).
- **Chart-Achsenbeschriftung**: `lv_chart_set_axis_tick()`/`LV_PART_TICKS`
  gibt es in LVGL 9 nicht mehr (in 9.0 entfernt; Ersatz wäre ein separates
  `lv_scale`-Widget neben dem Chart). Die Verlaufsdiagramme zeigen daher nur
  Gitterlinien ohne Zahlenbeschriftung (wie in der zuvor bestätigten Version).

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

**Auf Hardware verifizierte Probleme + Fix** (zwei Anlaeufe scheiterten,
bevor der dritte funktionierte — vollstaendig dokumentiert, da beim
Ergaenzen weiterer Icons derselbe Fehler droht):

1. Erste Fassung: 12 Codepoints inkl. `lightning-bolt` (U+F140B), weit
   entfernt von den uebrigen 11 (U+F004D–U+F061A). Zeigte auf dem Geraet
   **kein einziges** MDI-Icon, obwohl `lv_font_conv` fehlerfrei durchlief.
   Vermutung: bekannter `lv_font_conv`-Bug bei der "sparse tiny"-Cmap mit
   grossen Codepoint-Luecken ([Issue #62](https://github.com/lvgl/lv_font_conv/issues/62)).
2. Zweite Fassung: `lightning-bolt` entfernt, nur noch die 8 tatsaechlich
   genutzten, eng beieinanderliegenden Original-Codepoints (Bereich 1486
   statt 5055). **Ebenfalls weiterhin kein einziges Icon sichtbar** — die
   Cmap-Luecke war also nicht die (alleinige) Ursache.
3. Dritte, funktionierende Fassung: alle 8 MDI-Originalcodepoints liegen
   oberhalb U+FFFF und brauchen daher 4-Byte-UTF-8; LVGLs eigene
   `LV_SYMBOL_*`-Zeichen (die nachweislich funktionieren) liegen dagegen alle
   unterhalb U+FFFF (3-Byte-UTF-8). Per `lv_font_conv`-Remapping
   (`-r 'quelle=>ziel'`) auf U+E001–U+E008 (Basic Multilingual Plane,
   Private-Use-Area) verschoben — selber Codepoint-Bereich/Byte-Laenge wie
   `LV_SYMBOL_*`. Nebeneffekt: `lv_font_conv` waehlt dafuer automatisch das
   einfachere, zusammenhaengende `FORMAT0_TINY`-Cmap-Format statt des
   fehleranfaelligen `SPARSE_TINY`. Die `MDI_*`-Makros in `mdi_icons.h`
   enthalten die *remappten* Codepoints, nicht die MDI-Originalwerte.

**Wichtige Einschränkung**: Ein MDI-Glyph kann nur in einem Label gerendert
werden, dessen Font auf `&mdi_icons_20` gesetzt ist — er lässt sich *nicht*
mitten in einen mit `&lv_font_montserrat_*` gerenderten Text einbetten (im
Gegensatz zu LVGLs `LV_SYMBOL_*`, die Teil jeder Montserrat-Font-Größe sind).
Deshalb sind MDI-Icons im Code immer eigene, separate Label-Objekte neben dem
Text, nie in einen gemeinsamen String eingebettet.

**Weitere Icons ergänzen**: Icon-Name in `.../package/css/materialdesignicons.css`
nachschlagen (Codepoint hinter `content: "\FXXXXX"`), dann mit Remapping auf
einen freien Codepoint ab `0xE009` aufwaerts (Basic Multilingual Plane,
**nicht** den MDI-Originalcodepoint direkt verwenden!) neu generieren. Danach
im erzeugten `.c`-File pruefen, dass `.type` `FORMAT0_TINY` oder
`FORMAT0_FULL` ist (nicht `SPARSE_*`) und `range_start`/`range_length`
plausibel sind:

```bash
npm pack @mdi/font@7.4.47 && tar xzf mdi-font-7.4.47.tgz
npx lv_font_conv --font package/fonts/materialdesignicons-webfont.ttf \
  -r '0xF004D=>0xE001,0xF0493=>0xE002,0xF050F=>0xE003,0xF0597=>0xE004,0xF0599=>0xE005,0xF059D=>0xE006,0xF05A9=>0xE007,0xF061A=>0xE008,<original>=>0xE009' \
  --size 20 --bpp 4 --format lvgl --lv-font-name mdi_icons_20 \
  -o font_mdi_icons_20.c
```

Den `#ifdef LV_LVGL_H_INCLUDE_SIMPLE`-Include-Block danach durch ein einfaches
`#include "lvgl.h"` ersetzen (passend zum Rest des Projekts).

## Struktur

```
esp-idf/
  CMakeLists.txt, sdkconfig.defaults, partitions.csv
  main/
    app_main.c          Boot/Verdrahtung
    shared_state.[ch]    globale Zustände, Ringpuffer
    config_store.[ch]    NVS-Persistenz
    web_portal.[ch]      WLAN (STA/offener AP) + HTTP-Portal + OTA
    mqtt_handler.[ch]    esp-mqtt + JSON-Parsing
    time_service.[ch]    SNTP + Zeitzone
    weather_service.[ch] OpenWeatherMap-Abruf
    display_ui.[ch]      esp_lcd + LVGL-Oberfläche
    font_mdi_icons_20.c  generierte Material-Design-Icons-Font (12 Glyphen)
    mdi_icons.h           Font-Deklaration + MDI_*-Zeichen-Makros
    touch_xpt2046.[ch]   XPT2046-SPI-Treiber
    bsp_pins.h           CYD-Pinbelegung
```
