#pragma once

#include "display_common.h"

// -----------------------------------------------------------------------------
// ST7796S (320x480 TFT) - siehe gc9a01.h fuer die Begruendung des Namespace-
// Musters (mehrere Display-Header sollen gemeinsam kompilierbar sein).
// -----------------------------------------------------------------------------
namespace disp_st7796s {

inline constexpr const char *NAME   = "ST7796S";
inline constexpr bool HAS_TOUCH     = false;
inline constexpr bool HAS_SD        = false;
inline constexpr int16_t WIDTH      = 320;
inline constexpr int16_t HEIGHT     = 480;

// GPIO22/23 existieren auf ESP32-C3 nicht (nur GPIO0-21, siehe CLAUDE.md) -
// ein pinMode() darauf crasht den Chip sofort (siehe gc9a01.h fuer denselben
// Fallstrick). Restliche Pins liegen zufaellig bereits im C3-Bereich, nur
// MOSI/BL muessen pro Target unterschieden werden.
#if CONFIG_IDF_TARGET_ESP32C3
inline constexpr int8_t DEFAULT_MOSI = 6;
inline constexpr int8_t DEFAULT_MISO = 19;
inline constexpr int8_t DEFAULT_SCLK = 18;
inline constexpr int8_t DEFAULT_CS   = 5;
inline constexpr int8_t DEFAULT_DC   = 4;
inline constexpr int8_t DEFAULT_RST  = 16;
inline constexpr int8_t DEFAULT_BL   = 7;
#else
inline constexpr int8_t DEFAULT_MOSI = 23;
inline constexpr int8_t DEFAULT_MISO = 19;
inline constexpr int8_t DEFAULT_SCLK = 18;
inline constexpr int8_t DEFAULT_CS   = 5;
inline constexpr int8_t DEFAULT_DC   = 4;
inline constexpr int8_t DEFAULT_RST  = 16;
inline constexpr int8_t DEFAULT_BL   = 22;
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
  gfx = new Arduino_ST7796(bus, pins.rst, 0 /* Rotation */, false /* IPS */);

  if (pins.bl >= 0) {
    pinMode(pins.bl, OUTPUT);
    digitalWrite(pins.bl, HIGH);
  }

  return gfx->begin();
}

inline lv_display_t *initLvglDisplay() {
  return initLvglDisplaySPI(gfx, WIDTH, HEIGHT);
}

} // namespace disp_st7796s
