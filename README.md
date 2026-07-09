# Pulse ESP32 Hardware Monitor (PlatformIO/Arduino)

PlatformIO/[pioarduino](https://github.com/pioarduino/platform-espressif32)
(Arduino core on ESP-IDF 5.5.4) firmware for a small WiFi hardware monitor:
time/date, optional weather, plus CPU/GPU load, temperature and power,
delivered from a PC client via MQTT or USB/serial (switchable in the web
portal). WiFi setup via an open access point with a config web portal and
OTA update.

> This branch is a from-scratch Arduino/PlatformIO rewrite (see `src/`,
> `platformio.ini`), ported over feature-for-feature from this project's
> separate `esp-idf` branch (pure ESP-IDF, no Arduino) - that branch remains
> the reference implementation/fallback and is not affected by this rewrite.
> **Note:** most of the prose below was written for the ESP-IDF version and
> still references `idf.py`/`main/*.c` paths in places - a full documentation
> pass for this branch is a planned follow-up.

See [CHANGELOG.md](CHANGELOG.md) for changes per release.

## Display selection: one image for multiple boards/panels

There is **no** display- or chip-specific build anymore: a single firmware
image contains all panel drivers listed below. Which display is actually
installed is chosen **after flashing, in the web portal** (card "Display" →
"Display type" dropdown); the choice is stored in NVS, the device then
reboots and initializes the selected panel. The web portal automatically
shows the matching pin table for the selected option (`GET /api/displays`,
generated from `main/display/board_profiles.c` - no duplicate maintenance of pin
assignments).

**The selection is filtered per build** (`board_profile_is_available()` in
`board_profiles.c`, evaluated via `CONFIG_IDF_TARGET_ESP32`): on a build for
the classic ESP32 (`idf.py set-target esp32`), only the ESP32-2432S028
profile is available - the web portal hides the dropdown there and instead
shows only "Fixed: ...". On any other chip (`esp32s3`, `esp32c3`, ...), only
the four generic profiles appear in the dropdown; the ESP32-2432S028 profile
(fixed factory wiring of that one board) doesn't show up there at all.

| Display type | Resolution | Bus | Touch | UI |
|---|---|---|---|---|
| ESP32-2432S028, ILI9341 | 320x240 | SPI | XPT2046 | full tile UI (history, settings) |
| ILI9488 | 480x320 | SPI | no | tile UI, scaled to resolution |
| ST7796S | 480x320 | SPI | no | tile UI, scaled to resolution |
| GC9A01 (round) | 240x240 | SPI | no (BOOT button as navigation) | **placeholder** UI (arcs/weather + text, not final yet) |
| SSD1309 | 128x64, monochrome | I2C | no | **placeholder** UI (text only, no gradient) |

**Important limitations of this first implementation:**

- Only the first profile (ESP32-2432S028) has touch. GC9A01 instead uses the
  devboard's BOOT button as simple navigation (switch screens, see below);
  ILI9488/ST7796S/SSD1309 have no on-device navigation at all. Configuration
  in all cases runs entirely through the web portal.
- Rotation (`app_config.rotation`) only affects the rectangular tile UI
  (ESP32-2432S028/ILI9488/ST7796S). GC9A01 and SSD1309 ignore the setting.
- The pin assignments for ILI9488/ST7796S/GC9A01/SSD1309 are **suggested
  default wiring** for a generic ESP32/S3/C3 setup (see table below), not
  factory wiring - if your own wiring differs, it's adjustable per field in
  the web portal under "Pin assignment" (leave empty = default from the
  table), no rebuilding/reflashing needed. See the "Pin overrides" section
  below.
- **Only ILI9341 (ESP32-2432S028) is verified on real hardware.** The other
  four panel drivers, the round layout and the mono layout are new and not
  yet cross-checked (see the "To verify on hardware" section below).

### Pin table (default wiring)

**ESP32-2432S028 (ILI9341 + XPT2046, factory wiring):**

| Function | GPIO |
|---|---|
| TFT MOSI/MISO/SCLK | 13 / 12 / 14 |
| TFT CS/DC | 15 / 2 |
| TFT Backlight | 21 |
| Touch CS/IRQ | 33 / 36 |
| Touch MOSI/MISO/CLK | 32 / 39 / 25 |

**ILI9488 / ST7796S (generic SPI wiring, no touch):**

Differs per target chip (`board_profiles.c`, `#if CONFIG_IDF_TARGET_ESP32C3`),
since the C3 only has GPIO0-21 and GPIO23 doesn't exist there. The web portal
(`/api/displays`) always shows the pins valid for the build actually flashed
- both variants are listed here for reference:

| Function | ESP32 / ESP32-S3 | ESP32-C3 |
|---|---|---|
| MOSI/MISO/SCLK | 23 / 19 / 18 | 4 / 5 / 6 |
| CS/DC | 5 / 17 | 7 / 10 |
| RESET | 16 | 3 |
| Backlight | 4 | 1 |

**GC9A01 (fixed, chip-independent, no MISO/touch):**

| Function | GPIO |
|---|---|
| SDA (MOSI) | 3 |
| SCL (SCLK) | 4 |
| CS | 1 |
| DC | 10 |
| RST | 0 |
| Backlight | no separate pin (hardwired/always on) |
| Nav button | devboard BOOT button: GPIO9 (C3/C6/H2) or GPIO0 (ESP32/S3) - identical to RST on ESP32/S3, therefore disabled there (`board_profiles.c`) |

### Pin overrides (custom wiring without rebuilding)

All pins above are defaults (`board_profiles.c`), but overridable per field
in the web portal under "Pin assignment (optional)" - e.g. if your own
wiring differs from the suggestions. Empty field = use default. After
saving, the device reboots and picks up the new pins.

Design decision: there is **one** override set, not one per display type -
in practice a device is permanently operated with one physical display, so
managing overrides for five simultaneously-never-used profiles would have
been unnecessary effort. Changing the display type in the web portal
automatically resets all pin overrides (old pins - e.g. touch pins from the
CYD profile - otherwise generally wouldn't fit the new panel/bus).
Implemented in `board_profiles.h/.c` (`pin_override_t`,
`board_profile_apply_overrides()`) and `display_ui.c` (`lcd_init()` merges
override + default into `s_profile` before any GPIO is touched).

Since an invalid pin entry no longer causes a boot loop thanks to
`LCD_CHECK` (see above) but only skips display init, the risk of a botched
manual adjustment is low - the web portal stays reachable either way to
correct it.

### GC9A01: screens, standby, nav button

The round minimal UI has two screens switchable via button (`display_ui.c`:
`s_round_screen`, `ROUND_SCR_*`):

- **Overview** (default): time + two arcs for CPU/GPU load, same radius,
  right half CPU / left half GPU (one shared, split ring instead of two
  nested circles), each with a chip icon + percentage below.
- **Weather**: time + temperature/feels-like temperature (thermometer icon)
  / humidity / wind (icon) / rain (icon) (requires `weather_enabled` in the
  web portal).

Pressing the BOOT button switches between the two. Since this panel has no
backlight pin (`bl = -1`), standby can't be dimmed in software - instead the
screen content is reduced: in standby, **a larger, higher-positioned clock +
the full weather block** (the same four lines as on the Weather screen) is
always shown, regardless of the last-selected screen (Overview/CPU-GPU arcs
are always hidden in standby). A button press in standby only wakes up
(shows the last-selected screen again) without triggering a screen switch -
analogous to touch-wakeup on the CYD profile.

The arc widgets are actually sliders and, without
`lv_obj_remove_style(..., LV_PART_KNOB)`, draw a thick knob at the current
value position - looked like a glitch/foreign object on the thin ring, now
removed (`build_round_ui()` in `display_ui.c`).

**Standby timeout is configurable in the web portal** (card "Display" →
"Standby after ... seconds", `app_config.standby_timeout_s`, 0 = disabled,
default 120s). Applies to all display types: on panels with a backlight pin
it works as before (backlight off), on GC9A01 as described above (reduce
content instead of brightness).

**SSD1309 (I2C, monochrome, no touch):**

| Function | GPIO/value |
|---|---|
| SDA | 8 |
| SCL | 9 |
| VCC | 3.3V |
| GND | GND |
| I2C address | 0x3C |

## Framework mapping (Arduino → ESP-IDF)

| Arduino | ESP-IDF |
|---|---|
| `WiFi.h` | `esp_wifi` + `esp_netif` |
| `Preferences` | `nvs_flash` (`config_store.c`) |
| `ESPAsyncWebServer` | `esp_http_server` (`web_portal.c`) |
| `PubSubClient` | `esp-mqtt` (`mqtt_handler.c`) |
| NTP via `configTzTime` | `esp_sntp` (`time_service.c`) |
| `HTTPClient` | `esp_http_client` (`weather_service.c`) |
| `Update` (OTA) | `esp_ota_ops` (`web_portal.c`, raw binary upload) |
| `TFT_eSPI` | `esp_lcd` (multiple panel drivers) + **LVGL 9** via `esp_lvgl_port` (`display_ui.c`, `board_profiles.c`) |
| `XPT2046_Touchscreen` | custom SPI driver (`touch_xpt2046.c`) |
| `ArduinoJson` | `cJSON` (core on IDF <6.0, managed component `espressif/cjson` from 6.0 on) |

## Prerequisites

- **ESP-IDF 5.1 – 6.x** (with `idf.py` in PATH, `. $IDF_PATH/export.sh`).
- Internet access on first build: managed components are automatically
  fetched from the ESP Component Registry (LVGL, esp_lvgl_port, several
  esp_lcd panel drivers, and from **ESP-IDF v6.0** on also `espressif/mqtt`
  and `espressif/cjson`, since esp-mqtt and cJSON are no longer part of the
  core there).

## Building & flashing

This branch is a PlatformIO/[pioarduino](https://github.com/pioarduino/platform-espressif32)
project (Arduino core on top of ESP-IDF 5.5.4) - all firmware source lives
under `src/`, build config in `platformio.ini`.

```bash
pio run -e esp32              # or esp32s3 / esp32c3
pio run -e esp32 -t upload
pio device monitor
```

pioarduino also auto-generates `.pio/build/esp32/firmware.factory.bin`
(bootloader + partition table + otadata initializer + app combined into one
image) on every build - flash this single file at offset `0x0` for the
initial flash, e.g. via [ESP Web Tools](https://esphome.github.io/esp-web-tools/)/
esptool-js in the browser, or `esptool.py write_flash 0x0 firmware.factory.bin`,
instead of writing bootloader/partition table/app separately. If the board
was previously flashed with a *different* partition table/OTA scheme and
boot fails right after flashing (`OTA app partition slot 1 is not
bootable`), do one full chip erase first: `pio run -e esp32 -t erase` then
reflash.

Initial setup: WiFi connect/fallback-to-AP is handled by
[tzapu/WiFiManager](https://github.com/tzapu/WiFiManager), not this
project's own code - open AP **`ESP32-HWMon-XXXX`** → `http://192.168.4.1`
→ pick/enter WiFi in WiFiManager's own portal → device connects and
reboots into normal operation. Our own web portal (config page, `/api/*`,
OTA) only starts once a WiFi connection is up - it no longer has a WiFi
card at all (WiFiManager already handled that before this page ever loads).
All its settings (MQTT/timezone/display type/pin assignment/OTA) are hidden
behind "Show advanced settings" by default, matching the previous
progressive-disclosure behavior minus the WiFi step. To reconfigure WiFi
later, use "Restart into setup AP" (card "Settings") - this clears the
stored WiFi credentials and reboots into WiFiManager's portal again.

### CI build (GitHub Actions)

Not set up yet on this branch - the previous ESP-IDF-based CI workflow
(`idf.py`/Docker) was removed since it no longer matches this PlatformIO/
Arduino codebase. A PlatformIO-based replacement (`pio run -e ...` per
target, matrix over esp32/esp32s3/esp32c3, release artifact upload analogous
to the old workflow) is a planned follow-up, not yet implemented.

> **Captive portal detection on phones can briefly exhaust sockets**:
> iOS/Android/Windows check internet connectivity in the setup AP via
> several parallel requests (`hotspot-detect.html`, `generate_204`,
> `connecttest.txt`, ...). `CONFIG_LWIP_MAX_SOCKETS` is therefore raised to
> 16 (`httpd_config_t.max_open_sockets=13` in `web_portal.c`) and the
> best-known detection paths are registered directly instead of only going
> through the generic 404 handler. Unhandled, this shows up as
> `error in accept (23)`/`error in recv: 104` in the log and as jumping
> focus/reloading in the config form on the phone.

> **Note on ESP32-S3/-C3**: The code itself is chip-independent (the GPIO
> numbers in `board_profiles.c` are plain numbers, no more ESP32-classic
> specifics - except for the ESP32-2432S028 profile, which describes the
> fixed factory wiring of that one board and only makes sense on classic
> ESP32). For S3/C3, simply build with `idf.py set-target esp32s3` or
> `esp32c3` and wire the generic SPI/I2C displays per the pin table above.
> **Not separately tested** - in particular ESP32-C3 has less SRAM and no
> PSRAM; a 480x320 framebuffer (ILI9488/ST7796S) can get tight there (see
> `lvgl_port_display_cfg_t.buffer_size` in `display_ui.c`, reduce if
> needed). The C3 also only has one general-purpose SPI controller
> (`SPI2_HOST`, no `SPI3_HOST`) - this only affects the (there anyway
> unselectable) CYD touch bus, `board_profiles.c` automatically selects a
> compilable placeholder for it via `SOC_SPI_PERIPH_NUM`. The C3 also only
> has GPIO0-21 (22 pins instead of up to 39/48 on ESP32/S3) - the generic
> SPI pin set is therefore defined per chip (see pin table above).

> **A wrong/unsuitable display choice no longer causes a boot loop**: panel
> init failures (e.g. a GPIO number that doesn't exist on the target chip)
> no longer trigger an `ESP_ERROR_CHECK` abort since `LCD_CHECK` in
> `display_ui.c`, but are logged instead; the device still starts WiFi/the
> web portal, just without screen output. This keeps the display selection
> in the web portal always reachable to fix a wrong choice - even after a
> chip/profile change where the old selection stored in NVS no longer fits
> the new chip.

> **Note on an existing local `sdkconfig`**: `sdkconfig.defaults` is only
> applied to a *new* `sdkconfig`, not to an existing build directory. This
> affects not only missing LVGL features (`lv_font_montserrat_24`,
> `lv_arc_create`, ...) but also the **partition table**: without
> `CONFIG_PARTITION_TABLE_CUSTOM=y` from a current `sdkconfig`, the build
> ends up on the ESP-IDF default layout ("factory", 1 MB) instead of the two
> 1.75 MB OTA slots from `partitions.csv` - the build then fails with
> "app partition is too small" even though the image would actually fit.
> For issues like this, always delete the local `sdkconfig` and rebuild
> first, instead of hunting for individual options by hand.

> **Image size**: an image with all 5 display drivers comes in at up to
> ~1.6 MB depending on the target chip. The OTA partitions are therefore
> sized at 1.75 MB per slot (`partitions.csv`) and the build is optimized
> for size rather than speed (`CONFIG_COMPILER_OPTIMIZATION_SIZE=y`). If
> "app partition is too small" still occurs despite a current `sdkconfig`,
> further increase the `ota_0`/`ota_1` sizes in `partitions.csv` (4 MB flash
> minus `nvs`/`otadata`/`phy_init` allows for just under 2 MB per slot) or,
> as a test, remove unused display drivers from
> `main/idf_component.yml`/`display_ui.c`.

## To verify on hardware / adjust if needed

These points are board-dependent and couldn't be verified without a device:

1. **Display colors** — there is no user-facing dark/light mode switch; every
   panel is hardwired to boot with a dark background, fixed per-profile via
   `cfg.invert` in each panel's LGFX config (e.g. `LGFX_CYD` in
   `src/display/lgfx_profiles.h`). For ILI9341 (ESP32-2432S028),
   `cfg.invert=false` is verified correct (dark background) on real
   hardware. If a not-yet-verified panel boots with a light background
   instead of dark, flip that panel's `cfg.invert`. If red/blue are swapped
   instead (a different symptom than light/dark background), adjust
   `board_profile_t.bgr` in `board_profiles.c` instead.
   **SSD1309** needed exactly this fix on real hardware (it showed a light
   background with dark icons) - since a light background isn't just wrong
   but actively harmful (burn-in risk on a self-lit OLED that's on
   continuously), it now also initializes hardwired to a dark background,
   same as every other profile.
2. **SSD1309 text/icon legibility** — also confirmed on real hardware: after
   the background fix above, text, values and icons still didn't render.
   Root cause was the font, not the panel or resolution: `display_ui_mono.c`
   used anti-aliased (4bpp) fonts (`lv_font_montserrat_8`, and `mdi_icons_10`
   generated with `--bpp 4`). LVGL dithers those gray levels down to the
   panel's 1bpp framebuffer (`LV_COLOR_FORMAT_I1`), and at 8-10px font size
   most glyph pixels have too little coverage to survive the dithering
   threshold - they render as background instead of foreground. Card borders
   stayed visible because they're drawn as solid, fully-opaque lines, not
   anti-aliased glyphs. Fixed by switching to non-anti-aliased 1bpp fonts,
   which is what LVGL recommends for monochrome displays: LVGL's built-in
   `lv_font_unscii_8` (`CONFIG_LV_FONT_UNSCII_8` in `sdkconfig.defaults`) for
   all text, and `mdi_icons_10` regenerated with `--bpp 1` (see the updated
   generation command in `main/display/mdi_icons_10.h`). `unscii_8` is
   monospace and wider per character than the previous proportional font, so
   the mono UI now shows time without seconds ("HH:MM") and date without
   year ("TT.MM") to keep everything on one row at 128px width.
3. **New panel driver components** — `main/idf_component.yml` references
   `atanisoft/esp_lcd_ili9488` (community component, no official
   `espressif/` namespace entry exists for it) as well as
   `espressif/esp_lcd_st7796` and `espressif/esp_lcd_gc9a01` (both official,
   checked against the registry entries). SSD1306/SSD1309 need **no**
   registry entry - the driver is already part of the core `esp_lcd`
   component (`esp_lcd_panel_vendor.h`). An earlier state of this branch
   incorrectly had a non-existent `espressif/esp_lcd_panel_ssd1306` entry
   that broke the build with "Version solving failed" - that's fixed.
4. **SSD1309 over I2C** — `display_ui.c: lcd_init_mono_i2c()` uses the core
   SSD1306 driver (SSD1309 speaks the same protocol, but possibly with
   different contrast/multiplex defaults) via the new `i2c_master` driver
   (`driver/i2c_master.h`, `i2c_new_master_bus()` +
   `esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t, ...)`) - the old
   `driver/i2c.h` API no longer provides the expected bus handle type on
   ESP-IDF ≥5.2/6.x. Not yet verified on hardware.
5. **GC9A01 round layout** — `display_ui.c: build_round_ui()` is
   deliberately a simple placeholder (arcs/text, two screens, see section
   above), not a polished round design. Confirmed on a first test setup
   (180° correction needed, see point 5, and layout/screens/boot button
   fundamentally working) - polish (font sizes, icons on the weather
   screen, possibly more screens) still open.
6. **Display rotation / mirroring** — `app_config.rotation` has a different
   meaning depending on panel shape; the web portal only shows the values
   that make sense per display type (`renderRotationOptions()` in
   `web_portal.c`):
   - LCD_SHAPE_RECT (ESP32-2432S028/ILI9488/ST7796S): chooses between the
     four landscape/portrait cases, but the web portal only offers 1 and 3
     here (both landscape, 180° apart) - 0/2 internally switch to portrait,
     which tears apart the tile UI that's drawn fixed for landscape. For
     ILI9341, rotation=1 (default) is verified on hardware, rotation=3 as
     well (see touch note below); the other panels not individually tested.
   - LCD_SHAPE_ROUND (GC9A01): chooses directly between the four possible
     `mirror_x`/`mirror_y` combinations (`lcd_init_color_spi()` in
     `display_ui.c`), since it's unpredictable which combination displays
     text neither upside down nor mirrored for a given build/panel batch -
     **on the first test setup, the 180° combination (`rotation=3`) wasn't
     enough, the image stayed mirrored** (mirror_x/mirror_y together doesn't
     necessarily mean "right way up", it depends on the scan direction of
     the particular panel). Simply try 0-3, no reflashing needed (only a
     reboot after saving).
7. **Touch calibration** — `touch_xpt2046.c`: `TOUCH_RAW_*` bounds and the
   axis mapping per `rotation`, relevant only for the ESP32-2432S028
   profile. Until recently, `rotation=3` (180°) mapped touch coordinates
   identically to `rotation=1` - the touch point was therefore swapped
   top/bottom and left/right relative to the rotated text. Both axes for
   `rotation=3` are now inverted relative to `rotation=1`.
8. **LVGL component versions** — `main/idf_component.yml`. If the component
   manager expects different versions, adjust the ranges there. The UI is
   written against the LVGL 9 API.
9. **Icons (Material Design Icons)** — `main/display/font_mdi_icons_20.c` +
   `main/display/mdi_icons.h`, only used by the tile UI. Verified on ESP32-2432S028
   hardware (see "Icons" section below).

## Design

Modeled after a reference layout provided by the user (cards with gradient
bars, weather/wind/rain display, trend arrows, settings button):

- **Icons**: Material Design Icons (see dedicated section below) for
  chip/thermometer/sun/wind/rain/gear/back-arrow/WiFi; built-in LVGL symbols
  (`LV_SYMBOL_CHARGE`, `LV_SYMBOL_UP`/`_DOWN`) for power/trend, since these
  are mixed with running text in one label.
- **Weather block**: still only uses the already-integrated OpenWeatherMap
  "Current Weather" API, including feels-like temperature, humidity, wind
  speed/direction (`weather_wind_compass()`, 8-point compass) and rain
  amount (`rain.1h`, 0 if no rain reported). Only visible on the tile UI. No
  new configuration needed, everything runs through the existing
  `weather_api_key`.
- **Settings screen** (gear button, tile UI only): shows firmware version,
  IP address, WiFi/MQTT status and free memory. The "Restart into setup AP"
  button (`web_portal_force_ap()`) clears the WiFi credentials WiFiManager
  has stored and reboots into the open setup AP. Requires two taps to
  confirm (prevents accidental disconnection).
- **Trend arrows**: compare the current load value against the value up to
  10 measurements ago (`compute_trend()` in `display_ui.c`); threshold ±3
  percentage points for rising/falling, otherwise "stable" (dash).
- **Standby** (`display_ui.c`, `check_standby()`/`enter_standby()`/
  `exit_standby()`): kicks in when no new hardware data (MQTT or USB/serial) has arrived for
  `app_config.standby_timeout_s` (configurable in the web portal, default
  120s, 0 = disabled). On panels with a backlight pin, the backlight goes
  to duty 0 via LEDC; on panels without a backlight pin (GC9A01, SSD1309)
  the backlight stays unchanged (OLED is self-lit, GC9A01 is hardwired to
  VCC on this board) - on GC9A01 the content is instead reduced to
  clock+weather (see GC9A01 section above). Wakes automatically as soon as
  data arrives again, or via touch (ESP32-2432S028) or the boot button
  (GC9A01).

## Icons (Material Design Icons)

`main/display/font_mdi_icons_20.c` is an LVGL font generated with
[`lv_font_conv`](https://github.com/lvgl/lv_font_conv) from the npm package
`@mdi/font` (Material Design Icons, Pictogrammers, Apache-2.0 license) —
**only the 9 glyphs actually used** at 20px/4bpp, not the full icon set
(which would be several MB). `main/display/mdi_icons.h` declares the font
(`extern const lv_font_t mdi_icons_20;`) as well as a `#define MDI_<NAME>`
per icon (UTF-8-encoded codepoint as a C string), usable analogous to
LVGL's own `LV_SYMBOL_*` macros: `make_label(parent, MDI_COG, &mdi_icons_20,
color)`.

Used icons: `chip`, `thermometer`, `weather-sunny`, `weather-windy`,
`weather-rainy`, `water-percent`, `cog`, `arrow-left`, `wifi` (all
documented in `mdi_icons.h` with original name and Unicode codepoint).
`water-percent` (a drop with a percent sign) was added later for the
humidity display in the round GC9A01 UI (codepoint U+F058E, remapped to
U+E009) - see the regeneration command below, including this ninth entry in
the `-r` remapping.

**Important limitation**: an MDI glyph can only be rendered in a label whose
font is set to `&mdi_icons_20` — it can *not* be embedded in the middle of
text rendered with `&lv_font_montserrat_*` (unlike LVGL's `LV_SYMBOL_*`,
which are part of every Montserrat font size). That's why MDI icons in the
code are always their own, separate label objects next to the text, never
embedded in a shared string.

**Adding more icons**: look up the icon name in
`.../package/css/materialdesignicons.css` (codepoint after `content:
"\FXXXXX"`), then regenerate with a remapping to a free codepoint starting
from `0xE00A` upward (Basic Multilingual Plane, **do not** use the original
MDI codepoint directly!) - always include **all previous remaps**, otherwise
the existing icons shift:

```bash
npm pack @mdi/font@7.4.47 && tar xzf mdi-font-7.4.47.tgz
npx lv_font_conv --font package/fonts/materialdesignicons-webfont.ttf \
  -r '0xF004D=>0xE001,0xF0493=>0xE002,0xF050F=>0xE003,0xF0597=>0xE004,0xF0599=>0xE005,0xF059D=>0xE006,0xF05A9=>0xE007,0xF061A=>0xE008,0xF058E=>0xE009,<original>=>0xE00A' \
  --size 20 --bpp 4 --format lvgl --lv-font-name mdi_icons_20 --no-compress --no-prefilter \
  -o font_mdi_icons_20.c
```

Afterwards, replace the `#ifdef LV_LVGL_H_INCLUDE_SIMPLE` include block with
a plain `#include "lvgl.h"` (matching the rest of the project).

## Hardware data source: MQTT or USB/serial

Hardware data (CPU/GPU load, temperature, power) can be delivered either via
MQTT (default) or directly over USB/serial - switchable in the web portal
("Hardware data source" dropdown, no separate build needed). Only one source
is active at a time.

USB/serial mode reads from UART0 - the same line already exposed over USB by
the onboard USB-serial chip used for flashing/log output - at a fixed
**115200 baud**. No broker/WiFi is required for this path; the PC just needs
a serial connection to the board.

Both paths use the same JSON payload format - a JSON object with any of the
following fields (all optional - only included values get updated):

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

- **MQTT**: published as the payload on the configured topic (default:
  `pulsemqtt/hwinfo`).
- **USB/serial**: sent as one JSON object per line (`\n`-terminated) over the
  serial port at 115200 baud.

`tools/pc_bridge_example.py` shows an example Python script that sends values
either way:

```
python pc_bridge_example.py --host 192.168.1.50 --topic pulsemqtt/hwinfo   # MQTT
python pc_bridge_example.py --serial COM3                                  # USB/serial
```

## Structure

```
CMakeLists.txt, sdkconfig.defaults, partitions.csv
main/
  app_main.c           boot/wiring
  shared_state.[ch]    global state, ring buffers
  config_store.[ch]    NVS persistence
  hw_data.[ch]         shared JSON parsing for hardware data (used by MQTT + serial)
  board_profiles.[ch]  display type enum + pin/bus/UI profiles (core of multi-display support)
  web_portal.[ch]      WiFi (STA/open AP) + HTTP portal (incl. display/source selection) + OTA
  mqtt_handler.[ch]    esp-mqtt hardware data source
  serial_handler.[ch]  USB/UART0 hardware data source (alternative to MQTT)
  time_service.[ch]    SNTP + timezone
  weather_service.[ch] OpenWeatherMap fetch
  display_ui.[ch]      esp_lcd (multiple panel drivers) + LVGL UI (tile/mono/round UI)
  font_mdi_icons_20.c  generated Material Design Icons font
  mdi_icons.h          font declaration + MDI_* character macros
  touch_xpt2046.[ch]   XPT2046 SPI driver (ESP32-2432S028 profile only)
tools/
  pc_bridge_example.py example PC->device bridge (MQTT or USB/serial)
```
