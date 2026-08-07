#pragma once

#include "display_common.h"

// ILI9341 (240x320 TFT) - siehe gc9a01.h fuer die Begruendung des Namespace-
// Musters (mehrere Display-Header sollen gemeinsam kompilierbar sein).
namespace disp_ili9341 {

inline constexpr const char *NAME   = "ILI9341";
inline constexpr bool HAS_TOUCH     = true;
inline constexpr bool HAS_SD        = false;
inline constexpr int16_t WIDTH      = 240;
inline constexpr int16_t HEIGHT     = 320;

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
inline constexpr int8_t DEFAULT_MISO = 12; // SDO
inline constexpr int8_t DEFAULT_MOSI = 13; // SDI / SDA
inline constexpr int8_t DEFAULT_SCLK = 14;
inline constexpr int8_t DEFAULT_CS   = 15;
inline constexpr int8_t DEFAULT_DC   = 2;
inline constexpr int8_t DEFAULT_RST  = -1; // -1 wenn an EN gekoppelt
inline constexpr int8_t DEFAULT_BL   = 21; // kein separates BL in dieser Config
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
  // Arduino_GFX-Default (ili9341_type1_init_operations) setzt VMCTR2 auf 0xB7
  // statt 0x86 -> ausgewaschene Farben (Schwarz wird dunkelgrau), siehe
  // https://github.com/moononournation/Arduino_GFX/issues/744. type3 ist
  // identisch bis auf den korrigierten VMCTR2-Wert, daher explizit erzwungen.
  gfx = new Arduino_ILI9341(bus, pins.rst, 0 /* Rotation */, false /* IPS */,
                             ILI9341_TFTWIDTH, ILI9341_TFTHEIGHT, 0, 0, 0, 0,
                             ili9341_type3_init_operations, sizeof(ili9341_type3_init_operations));

  if (pins.bl >= 0) {
    pinMode(pins.bl, OUTPUT);
    digitalWrite(pins.bl, HIGH);
  }

  return gfx->begin();
}

inline lv_display_t *initLvglDisplay() {
  return initLvglDisplaySPI(gfx, WIDTH, HEIGHT);
}

} // namespace disp_ili9341
