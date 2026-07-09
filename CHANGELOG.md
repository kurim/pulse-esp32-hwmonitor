# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/), versioning follows
`vX.Y.Z-idf` tags (see `.github/workflows/build-idf.yml`).

## [Unreleased]

- **Removed the "Invert colors (dark mode)" web portal toggle**: the device
  only ever ships in dark mode, so the switch (`app_config.color_invert`,
  the `/api/config` field, its NVS entry, and the checkbox/i18n strings in
  the web portal) was dead weight - dark mode is now simply the hardwired
  default (`cfg.invert=false` in `LGFX_CYD`, `src/display/lgfx_profiles.h`),
  matching how SSD1309 already worked.
- **Fix: CYD "invert colors" toggle was backwards**: checking the web portal's
  "Farben invertieren (Dark Mode...)" toggle made the display turn light, and
  leaving it unchecked (default) showed the correct dark theme - the opposite
  of what the label promises. `display_ui_begin()` now applies
  `!app_config.color_invert` to `invertDisplay()`, and the compiled-in default
  for `color_invert` changed to `true` so a freshly flashed device still boots
  dark with the toggle shown checked.
- **Fix: CYD touch mirrored between "Standard" and "180°" rotation**: the
  touch `offset_rotation` tuned in the previous commit was only verified
  against `rotation=1` ("Standard"); on real hardware, touch at "Standard"
  behaved like "180°" should and vice versa. Changed
  `cfg.offset_rotation` from `4` to `6` in `LGFX_CYD` (`src/display/
  lgfx_profiles.h`), which swaps exactly those two transforms back to their
  correct rotation - see the code comment for the full derivation and the
  remaining fallback candidates if hardware testing shows it still needs
  adjustment.
- **Fix: CYD (ILI9341) speckle noise on real hardware**: solid fills and
  divider lines showed colored speckle noise while text/icons stayed
  readable - a different symptom than the byte-order bug fixed earlier
  (that corrupted the whole image uniformly). Root cause: 40 MHz SPI write
  clock is marginal on the CYD's onboard wiring. Lowered `freq_write` to
  20 MHz in `LGFX_CYD` (`src/display/lgfx_profiles.h`), the value commonly
  reported as stable for this board.
- **Added a `coredump` partition** to `partitions.csv` to silence the
  (harmless but confusing) `esp_core_dump_flash: No core dump partition
  found!` boot log line.
- **Fix: SSD1309 stuck in light-background "normal mode"**: on real hardware
  the mono OLED showed a light background with dark icons, and the web
  portal's "Invert colors" toggle had no effect (it was never applied to the
  SSD1306/1309 I2C init path). Since a permanently lit background risks
  burn-in on a self-lit OLED, the panel now always initializes in inverted
  (dark-background) mode, hardwired rather than user-switchable; the invert
  toggle is hidden in the web portal for this display type.
- **Fix: SSD1309 text/icons invisible on real hardware**: after fixing the
  background above, labels, values and icons still didn't show up. Cause:
  the mono UI used anti-aliased (4bpp) fonts (`lv_font_montserrat_8`,
  `mdi_icons_10`), and LVGL dithers those gray levels down to the panel's
  1bpp framebuffer (`LV_COLOR_FORMAT_I1`) - at 8-10px size, most glyph
  pixels have too little coverage to survive and get dithered away.
  Switched to non-anti-aliased 1bpp fonts (LVGL's built-in `lv_font_unscii_8`
  for text, `mdi_icons_10` regenerated with `--bpp 1`), the path LVGL
  recommends for monochrome displays. The wider monospace font needed a bit
  more horizontal room, so the mono UI now shows time without seconds
  ("HH:MM") and date without year ("TT.MM").
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
