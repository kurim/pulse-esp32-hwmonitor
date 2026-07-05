# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/), versioning follows
`vX.Y.Z-idf` tags (see `.github/workflows/build-idf.yml`).

## [Unreleased]

- **USB/serial hardware data source**: hardware data (CPU/GPU load, temp,
  power) can now be received over USB/serial (UART0, 115200 baud) as an
  alternative to MQTT, switchable via a dropdown in the web portal - no
  broker/WiFi required for this path. `tools/pc_bridge_example.py` gained a
  `--serial` option to send data this way instead of `--host` (MQTT).

## [v1.0.0-idf] - First release

First official release of the ESP-IDF port. One firmware image per target
chip (ESP32/ESP32-S3/ESP32-C3), display type is chosen at runtime in the web
portal - no reflashing needed to switch displays.

### Supported hardware

- **Chips**: ESP32 (classic), ESP32-S3, ESP32-C3.
- **Displays**: ILI9341 320x240 + XPT2046 touch (ESP32-2432S028 "CYD",
  factory wiring), ILI9488 480x320, ST7796S 480x320, GC9A01 240x240 round,
  SSD1309 128x64 monochrome (I2C) - all five contained in a single firmware
  image.
- **Pin assignment**: overridable per field in the web portal for all
  generically-wired profiles (everything except ESP32-2432S028), with
  sensible defaults; changing the display type automatically resets the
  overrides.

### Features

- **Web portal configuration**: WiFi (with network scan as a clickable list
  instead of a plain input field), MQTT broker/topic, NTP/timezone, weather
  (OpenWeatherMap), display type + pin overrides, brightness, rotation/
  mirroring, standby timeout, color inversion - the WiFi card deliberately
  separated from the remaining (advanced) settings to avoid form issues on
  mobile browsers.
- **OTA firmware update** via the web portal (`.bin` upload), two OTA
  partitions (1.75 MB each).
- **MQTT data source**: CPU/GPU load, temperature, power draw from the PC as
  JSON on a configurable topic (default `pulsemqtt/hwinfo`, example bridge
  under `tools/pc_bridge_example.py`).
- **UI per panel shape**: rectangular tile UI with history charts and trend
  arrows (ILI9341/ILI9488/ST7796S), reduced round layout with two screens
  (GC9A01, navigation via the BOOT button), minimal monochrome layout
  (SSD1309).
- **Standby mode** after configurable inactivity (0 = disabled), shows
  time/date + weather.
- **Material Design Icons** (thermometer, sun, wind, rain, humidity, gear,
  WiFi, back arrow) as a dedicated LVGL font, no vector graphics.
- **CI**: manual or tag-based GitHub Actions build
  (`espressif/esp-idf-ci-action`), combines bootloader/partition table/app
  into a single file flashable via a web flasher, using `idf.py merge-bin`.
  A tag push (`vX.Y.Z-idf`) builds all three chips and automatically
  publishes a GitHub release with all three binaries.

### Known limitations

- Only ESP32-2432S028 (ILI9341) is thoroughly verified on real hardware;
  GC9A01 and SSD1309 have been confirmed on first test setups, ILI9488/
  ST7796S/ESP32-S3/ESP32-C3 not yet cross-checked on physical hardware (see
  README, "To verify on hardware" section).
- Rotation is limited to the two sensible landscape values for rectangular
  panels (0/2 would switch the tile UI, which is fixed for landscape, to
  portrait); for the round panel, the right mirror combination may need to
  be tried depending on the panel batch.
- Only the ESP32-2432S028 profile has touch; GC9A01 uses the BOOT button as
  simple navigation, the remaining profiles have no input on the device
  (configuration runs entirely through the web portal there).
