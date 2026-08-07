#pragma once

#include "display_common.h"

// -----------------------------------------------------------------------------
// ILI9488 (320x480 TFT) - siehe gc9a01.h fuer die Begruendung des Namespace-
// Musters (mehrere Display-Header sollen gemeinsam kompilierbar sein).
// -----------------------------------------------------------------------------
namespace disp_ili9488 {

inline constexpr const char *NAME   = "ILI9488";
inline constexpr bool HAS_TOUCH     = false;
inline constexpr bool HAS_SD        = false;
inline constexpr int16_t WIDTH      = 320;
inline constexpr int16_t HEIGHT     = 480;

// GPIO23 existiert auf ESP32-C3 nicht (nur GPIO0-21, siehe CLAUDE.md) - ein
// pinMode() darauf crasht den Chip sofort (siehe gc9a01.h fuer denselben
// Fallstrick). Restliche Pins liegen zufaellig bereits im C3-Bereich, nur
// MOSI muss pro Target unterschieden werden.
#if CONFIG_IDF_TARGET_ESP32C3
inline constexpr int8_t DEFAULT_MISO = 19; // SDO
inline constexpr int8_t DEFAULT_MOSI = 6;  // SDI / SDA
inline constexpr int8_t DEFAULT_SCLK = 18;
inline constexpr int8_t DEFAULT_CS   = 5;
inline constexpr int8_t DEFAULT_DC   = 4;
inline constexpr int8_t DEFAULT_RST  = 16; // -1 wenn an EN gekoppelt
inline constexpr int8_t DEFAULT_BL   = -1; // kein separates BL in dieser Config
#else
inline constexpr int8_t DEFAULT_MISO = 19; // SDO
inline constexpr int8_t DEFAULT_MOSI = 23; // SDI / SDA
inline constexpr int8_t DEFAULT_SCLK = 18;
inline constexpr int8_t DEFAULT_CS   = 5;
inline constexpr int8_t DEFAULT_DC   = 4;
inline constexpr int8_t DEFAULT_RST  = 16; // -1 wenn an EN gekoppelt
inline constexpr int8_t DEFAULT_BL   = -1; // kein separates BL in dieser Config
#endif

inline Arduino_DataBus *bus = nullptr;
inline Arduino_GFX     *gfx = nullptr;

inline bool initDisplay(const pin_override_t *override = nullptr) {
  pin_override_t pins;
  pins.mosi = DEFAULT_MOSI;
  pins.miso = DEFAULT_MISO;
  pins.sclk = DEFAULT_SCLK;
  pins.cs   = DEFAULT_CS;
  pins.dc   = DEFAULT_DC;
  pins.rst  = DEFAULT_RST;
  pins.bl   = DEFAULT_BL;
  applyPinOverrides(override, pins);

  bus = new Arduino_ESP32SPI(pins.dc, pins.cs, pins.sclk, pins.mosi, pins.miso, kGenericSpiHost);
  // Rotation kommt aus app_config.rotation statt fest 0 - siehe gc9a01.h.
  gfx = new Arduino_ILI9488(bus, pins.rst, app_config.rotation % 4, false /* IPS */);

  if (pins.bl >= 0) {
    pinMode(pins.bl, OUTPUT);
    digitalWrite(pins.bl, HIGH);
  }

  return gfx->begin();
}

inline lv_display_t *initLvglDisplay() {
  // gfx->width()/height() statt WIDTH/HEIGHT - siehe gc9a01.h.
  return initLvglDisplaySPI(gfx, gfx->width(), gfx->height());
}

} // namespace disp_ili9488
