# PulseMQTT

Überträgt Hardware-Sensordaten vom PC per MQTT an ein ESP-Display.
PC-Seite: LibreHardwareMonitor + PawnIO (.NET). ESP-Seite: Arduino-Framework, LVGL 9.

## Vorhandene Hardware

- **CYD** (ESP32-2432S028R, ILI9341, XPT2046-Touch) — Hauptzielgerät
- **ESP32-C3 + GC9A01** (240x240 rund, selbst verdrahtet: RST 0, CS 1, DC 10, MOSI 3, SCLK 4)
- **Guition JC8048W550** (ESP32-S3, ST7262 800x480 RGB-Parallel, GT911-Touch)
- C3 + SSD1309 existiert, liegt aber bei einer anderen Person — nicht testbar, Env auskommentiert

## Bibliotheks-Entscheidungen (nicht ohne Grund umwerfen)

- **Arduino_GFX statt LovyanGFX.** LovyanGFX machte im alten esp-idf-Branch zu viele
  Probleme.
- **espMqttClient (bertmelis) statt PubSubClient.** PubSubClient: 256-Byte-Default-Buffer,
  QoS 0 beim Publish, blockierender Connect. Nicht zu verwechseln mit
  `plapointe6/EspMQTTClient` — das ist nur ein Wrapper *um* PubSubClient und erbt alle Limits.
- **MQTT 3.1.1, kein MQTT 5.** Für LAN-lokale Telemetrie bringt 5.0 kaum etwas und
  kostet einen kompletten IDF-Rebuild via `custom_sdkconfig`.

## Architektur

Panels werden als **Daten** beschrieben, nicht als `#ifdef`-Kaskade: welches Board
(`board_config.h`, compile-time über `BOARD_CYD_2432S028R`/`BOARD_JC8048W550`/
`BOARD_GENERIC`) und — nur bei `BOARD_GENERIC` — welcher Display-Treiber (`DisplayType`
in `display_factory.h`, zur Laufzeit aus NVS). Ein Firmware-Image enthält alle Treiber.

- `DISPLAY_NONE` ist der Default: headless booten, Webportal erreichbar, Display danach
  konfigurieren. Rettet bei jedem Fehlgriff in der Pin-Konfiguration.
- `lcd_ui_shape_t` ist eine eigene Achse. Mono/rund/wide sind unterschiedliche UIs,
  nicht dieselbe UI in anderer Größe.
- `PIN_UNSET` (nicht `-1`) markiert "kein Override" — `-1` ist ein gültiges "nicht verdrahtet".
- Enum-Werte werden **ans Ende angehängt**, nie einsortiert (der Wert liegt im NVS).

## Fallstricke

**PlatformIO**
- `build_flags` in einer Env **ersetzt** den Wert aus `[env]`, es merged nicht.
  Immer `${env.build_flags}` bzw. `${<section>.build_flags}` explizit voranstellen.
- Gleiches gilt für `extends`.

**Arduino_GFX**
- `spi_num`-Default ist `FSPI` — auf dem klassischen ESP32 ist das der **Flash-Bus**.
  Beim CYD muss `HSPI` explizit übergeben werden. Auf C3/S3 ist `FSPI` korrekt.
- `is_shared_interface = false` beim CYD (Display allein auf HSPI) — spart pro Transfer
  die Transaction-Verwaltung. Bei geteiltem Bus zwingend `true`, sonst sporadisch
  verschobene Farben.
- SPI-Takt kommt über `gfx->begin(speed)`, nicht über den Bus-Konstruktor.
- `rgb_data[16]` folgt ESP-IDF-Konvention (Index = Bitposition): `[0..4]` = B0–B4,
  `[5..10]` = G0–G5, `[11..15]` = R0–R4. `Arduino_ESP32RGBPanel` nimmt die Pins dagegen
  in Reihenfolge R, G, B. Hier vertauscht man Rot und Blau schnell.
- Die README behauptet, `ESP32RGBPanel` werde ab arduino-esp32 3.0 nicht mehr unterstützt.
  Läuft auf 3.3.x nachweislich (JC8048W550 in Betrieb, inkl. Backlight-PWM-Tuning am
  echten Gerät) — die README-Warnung trifft hier nicht zu.

**Hardware**
- CYD gibt es mit ILI9341 (v1/v2) und ST7789 (v3, zwei USB-Ports). v3 braucht zusätzlich
  invertierte Farben.
- CYD: frei nutzbare GPIOs sind nur 22, 27 und 35 (35 nur Eingang).
- CYD: Touch hängt auf einem eigenen SPI-Bus (VSPI), Display auf HSPI.
- ESP32-C3: Strapping-Pins 2, 8, 9 freihalten. GPIO 9 ist die BOOT-Taste und wird als
  Nav-Button für Profile ohne Touch genutzt.
- ESP32-C3: 400 KB SRAM, kein PSRAM. Ein Vollbild-Framebuffer für GC9A01 wären 112 KB —
  zu viel neben LVGL, MQTT und AsyncWebServer. Partielle Draw-Buffer verwenden.

**Flash / Partitionen**
- Getrennte CSVs je Flash-Größe, Dual-OTA. Auf 4 MB **kein** Dateisystem: nach zwei
  App-Slots blieben ~128 KB, für LittleFS zu wenig zum Wear-Levelling. Konfiguration
  gehört ins NVS (`Preferences`), statische Web-Assets via `board_build.embed_files`.
- Auf 8 MB gibt es eine LittleFS-Partition. Sie heißt `littlefs`, aber `LittleFS.begin()`
  sucht per Default das Label `spiffs` — Label explizit übergeben.
- `phy_init` wird im Arduino-Build nie gelesen (`CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION`
  ist aus) und fehlt daher absichtlich.
- Partitionstabellen lassen sich nicht per OTA ausrollen. Layout-Änderungen sind ein
  Kabel-Termin.

**ESP32-S3 / C3**
- Am nativen USB-Port braucht es `ARDUINO_USB_CDC_ON_BOOT=1`, sonst geht `Serial` auf UART0.
- Bei einem Crash verschwindet der CDC-Port, bevor der Exception-Decoder den Backtrace
  sieht. Zum Debuggen entweder am UART-Port hängen oder die Coredump-Partition auslesen.

## MQTT-Design

- **Ein Topic, ein JSON-Snapshot, retained.** Nicht 20 Einzeltopics. Atomarer Zustand,
  ein Paket, und der ESP hat nach jedem Reboot sofort das volle Bild.
- **QoS 0.** Ein erneut zugestellter Messwert von vor zwei Sekunden ist wertlos.
- **LWT vom PC-Agent**, nicht vom ESP (`pulse/pc/status = offline`). Zusätzlich auf
  ESP-Seite ein Timeout: kein Update seit >5 s → Anzeige ausgrauen bzw. Backlight aus.
- Update-Rate 1–2 Hz.

## Nicht anfassen

Treibergröße ist kein Thema — alle SPI-Treiber gemeinsam liegen im niedrigen zweistelligen
KB-Bereich gegen einen 1,9-MB-App-Slot. Präprozessor-Guards nur dort, wo Code auf einem
Target **nicht übersetzt** (RGB-Pfad außerhalb ESP32-S3), nicht zur Platzersparnis.
Das war bei LovyanGFX ein Thema, bei Arduino_GFX nicht.
