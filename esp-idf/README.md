# CYD Hardware-Monitor — ESP-IDF-Port

Portierung der Arduino-Firmware (`../src`) auf **pures ESP-IDF** (kein Arduino
Core). Gleiche Funktion: Uhrzeit/Datum, optional Wetter, CPU/GPU-Auslastung,
-Temperatur und -Leistung per MQTT, Verlaufsdiagramme per Antippen der Kacheln,
WLAN-Setup über offenen Access Point mit Config-Webportal und OTA-Update.

> ⚠️ **Teilweise auf Hardware verifiziert.** Baut und flasht erfolgreich mit
> ESP-IDF 6.0.x. Die Display-Orientierung für `rotation=1` (Default) wurde
> auf echter Hardware korrigiert (siehe Punkt 2 unten). Das überarbeitete
> Design (Punkt 5 unten) ist **noch nicht auf Hardware verifiziert** — Layout/
> Icon-Feinheiten (v.a. Textumbruch im Wetter-Block, Chart-Achsenbeschriftung)
> ggf. nach dem ersten Blick auf dem Geräte nachjustieren.

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

1. **Display-Farben** — `display_ui.c`, `esp_lcd_panel_invert_color(panel, true)`.
   Wirkt das Bild farbverfälscht/negativ, auf `false` setzen. Bei falscher
   Rot/Blau-Vertauschung `LCD_RGB_ELEMENT_ORDER_BGR` ↔ `_RGB` tauschen.
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
5. **Design/Layout** — `display_ui.c` wurde auf ein Karten-Design mit
   Farbverlauf-Balken, Wetter-/Wind-/Regen-Block, Trend-Pfeilen und einem
   Settings-Screen umgestellt (siehe Abschnitt "Design" unten). Icons sind
   bewusst einfache LVGL-Vektorformen (Kreise/Rechtecke, kein Bild-/SD-Karten-
   Bedarf) und daher eine Annäherung an den gewünschten Look, keine
   Pixelkopie. Feinjustage der Positionen (`lv_obj_set_pos`-Werte in
   `build_main()`/`build_detail()`) ist nach dem ersten Blick auf dem Gerät
   wahrscheinlich nötig — Text kann bei sehr langen Wetterbeschreibungen oder
   extremen Werten (z.B. 3-stellige °C) knapp werden.

## Design

Angelehnt an ein vom Nutzer bereitgestelltes Referenz-Layout (Karten mit
Farbverlauf-Balken, Wetter-/Wind-/Regen-Anzeige, Trend-Pfeile, Settings-Knopf):

- **Icons**: einfache LVGL-Vektorformen (Kreise/abgerundete Rechtecke) für
  Chip/Thermometer/Sonne/Wind/Regen sowie eingebaute LVGL-Symbole
  (`LV_SYMBOL_SETTINGS`, `LV_SYMBOL_LEFT`, `LV_SYMBOL_WIFI`,
  `LV_SYMBOL_CHARGE`, `LV_SYMBOL_UP`/`_DOWN`) für Zahnrad/Zurück/WLAN/Power/
  Trend. Bewusste Entscheidung gegen Bitmap-Icons von SD-Karte, um ohne
  zusätzliche Hardware (SD-Kartenslot-Verkabelung) und ohne Bild-Decoder-Code
  auszukommen. Wer stattdessen echte Icon-Grafiken von SD-Karte laden möchte,
  kann das nachrüsten (LVGL-Bilddecoder + SD/SPI-Treiber nötig).
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
    touch_xpt2046.[ch]   XPT2046-SPI-Treiber
    bsp_pins.h           CYD-Pinbelegung
```
