# CYD Hardware-Monitor — ESP-IDF-Port

Portierung der Arduino-Firmware (`../src`) auf **pures ESP-IDF** (kein Arduino
Core). Gleiche Funktion: Uhrzeit/Datum, optional Wetter, CPU/GPU-Auslastung,
-Temperatur und -Leistung per MQTT, Verlaufsdiagramme per Antippen der Kacheln,
WLAN-Setup über offenen Access Point mit Config-Webportal und OTA-Update.

> ⚠️ **Ungetestet auf Hardware.** Dieser Port wurde ohne Board erstellt.
> Er ist ein vollständiger, idiomatischer Startpunkt — vor produktivem Einsatz
> bitte auf einem echten CYD bauen, flashen und die unten markierten
> Kalibrier-/Board-Punkte prüfen.

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
| `ArduinoJson` | `cJSON` (im IDF enthalten) |

## Voraussetzungen

- **ESP-IDF ≥ 5.1** (mit `idf.py` im PATH, `. $IDF_PATH/export.sh`).
- Internetzugang beim ersten Build: Managed Components (LVGL, esp_lvgl_port,
  esp_lcd_ili9341) werden automatisch aus dem ESP Component Registry geladen.

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
   mirror_y` je `rotation`. Wenn das Bild gespiegelt/kopfüber steht, die
   `mirror_*`-Flags des betreffenden `case` anpassen.
3. **Touch-Kalibrierung** — `touch_xpt2046.c`: `TOUCH_RAW_*`-Grenzen und die
   Achsen-Zuordnung pro `rotation`. Bei versetztem/gespiegeltem Touch dort
   nachziehen (analog zur Arduino-Version; `rotation=3` ist bereits beide
   Achsen gespiegelt).
4. **LVGL-Komponenten-Versionen** — `main/idf_component.yml`. Falls der
   Component Manager andere Versionen erwartet, dort die Ranges anpassen.
   Die UI ist gegen die LVGL-9-API geschrieben.

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
