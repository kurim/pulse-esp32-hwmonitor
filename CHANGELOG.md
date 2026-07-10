# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/), versioning follows
`vX.Y.Z-idf` tags (see `.github/workflows/build-idf.yml`).

## [Unreleased]

- **Fix: firmware no longer fit the OTA app partition after adding
  WiFiManager** (`program size (1977299 bytes) is greater than maximum
  allowed (1966080 bytes)`, ~11 KB over) - the library alone adds roughly
  120 KB of code, more than the ~110 KB of headroom the I8 boot logo
  encoding had freed up. Raised both OTA app slots in `partitions.csv` from
  1.875 MB to 1.9375 MB (+64 KB each) and **removed the `coredump`
  partition** to free the flash for it (its only purpose was silencing a
  harmless `esp_core_dump_flash: No core dump partition found!` boot log
  line - no functional loss). This uses the **entire** 4 MB flash chip with
  0 bytes to spare (2x 1.9375 MB + nvs/otadata/phy_init exactly fill it) -
  there is no more room to grow within this shared 4 MB partition table
  (esp32/esp32s3/esp32c3 all use the same `partitions.csv`). The next
  overflow will need an actual code-size reduction or a per-chip partition
  table for boards with more flash, not another size bump.
- **Replaced the hand-rolled WiFi STA/AP logic with tzapu/WiFiManager**
  (`src/net/web_portal.cpp`): `wifi_connect_sta()`/`start_ap()` and the
  custom captive-portal-detection routes are gone, `WiFiManager` now owns
  WiFi connect-or-fallback-to-AP entirely, including its own scan/SSID-entry
  portal UI (so `/api/wifi_scan` and the "WLAN" card in the embedded config
  page - SSID/password fields and the "Scan WiFi networks" button - are
  removed too; `app_config` no longer has `wifi_ssid`/`wifi_pass` fields,
  WiFiManager persists credentials itself via the WiFi driver's own storage).
  Run in **non-blocking** mode (`setConfigPortalBlocking(false)` +
  `wm.process()` from `web_portal_loop()`) specifically so the LCD stays
  responsive (clock/touch/refresh) while WiFiManager's setup portal is open -
  a blocking `autoConnect()` call would stall `loop()`, and with it
  `lv_timer_handler()`, for as long as the portal is up. Our own
  `AsyncWebServer` (config page, `/api/*`, OTA) now starts lazily, once
  `WiFi.status() == WL_CONNECTED` and WiFiManager's own portal server has
  stepped aside, since both would otherwise fight over port 80.
  `web_portal_force_ap()` (the "Restart into setup AP" button) now clears
  WiFiManager's stored credentials and reboots instead of switching into AP
  mode live in place - a reboot either way, since the button already drops
  the current connection immediately in both versions.
  Adds `tzapu/WiFiManager@^2.0.17` to `platformio.ini`.
- **Added an image-based boot logo screen** (`scr_boot`, `build_boot()` in
  `src/display/display_ui_rect.cpp`): the device now shows a dedicated,
  centered splash screen (Pulse logo image + the previous "Warte auf
  Daten..."/"Warte auf MQTT-Daten..." status text) instead of the homescreen
  right after panel init, and only switches to `scr_main` once the first
  message from the configured source arrives - same trigger
  (`hw_info.ever_received`) and therefore the same effective duration as the
  status text alone had before. Since `ever_received` never resets to
  `false` again, the boot screen only ever shows once per boot and doesn't
  reappear on a later, short-lived data stream interruption. The image
  itself (`src/bootlogo.c`/`bootlogo.h`, `lv_image_dsc_t bootlogo_img`) was
  down-scaled from the originally checked-in 800x480 asset (750 KB
  uncompressed, ~43% of the 1.75 MB OTA app partition on its own) to 200x120
  (~47 KB) so it reads clearly as a centered logo on the 320x240 CYD panel
  without being excessive.
- **Re-encoded the boot logo as an indexed (I8) image** instead of raw
  RGB565: `src/bootlogo.c` now stores a 256-color palette (1024 bytes, B/G/R/A
  per entry) plus one palette-index byte per pixel instead of 2 raw color
  bytes per pixel - 25,024 bytes total vs. 48,000 before (~48% smaller), no
  visible quality loss for this particular image (solid black background +
  a smooth two-color gradient, well within 256 colors). LVGL's built-in
  decoder (`lv_bin_decoder.c`) expands indexed images to a temporary
  ARGB8888 buffer (200x120x4 = ~94 KB) at draw time since the software
  renderer can't blit indexed pixels directly - but since `LV_CACHE_DEF_SIZE`
  in `lv_conf.h` is left at its default of `0`, that buffer is released
  immediately after each draw rather than held for as long as the boot
  screen is shown, so this is a brief one-time allocation spike at first
  paint, not a sustained RAM cost. Combined with the partition bump below,
  this leaves ~110 KB of headroom in each 1.875 MB OTA slot instead of ~87 KB.
- **Fix: firmware no longer fit the OTA app partition** after adding the
  boot logo image (`program size (1876603 bytes) is greater than maximum
  allowed (1835008 bytes)`, ~41 KB over on top of the driver code for 5
  display types already sharing the one firmware image). Raised both OTA
  app slots in `partitions.csv` from 1.75 MB to 1.875 MB (+128 KB each),
  using flash space that was already unallocated at the end of the 4 MB
  chip (~320 KB free tail before this change, ~64 KB after) - the sensitive
  `nvs`/`otadata`/`phy_init` offsets (see comment above them, `otadata`'s
  fixed `0xe000` in particular) are untouched. **Changing the partition
  table means the usual "just re-upload" won't boot** - do a full chip
  erase first (`pio run -e esp32 -t erase`, per-env for `esp32s3`/`esp32c3`)
  and reflash, exactly as already documented in the README for partition
  table changes.
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
