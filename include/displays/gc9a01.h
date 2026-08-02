#pragma once

#include "display_common.h"

// -----------------------------------------------------------------------------
// GC9A01 (240x240 rundes Display) - alles in einem eigenen Namespace, damit
// dieser Header zusammen mit den anderen SPI-TFT-Headern in ein einziges
// Firmware-Image kompiliert werden kann (Laufzeit-Auswahl ueber
// display_factory.h). Kein #define fuer Name/Groesse/Pins - Makros wuerden
// beim gemeinsamen Einbinden mit den anderen Displays kollidieren.
// -----------------------------------------------------------------------------
namespace disp_gc9a01 {

inline constexpr const char *NAME   = "GC9A01";
inline constexpr bool HAS_TOUCH     = false;  // GC9A01-Module gibt es i.d.R. ohne Touch
inline constexpr bool HAS_SD        = false;
inline constexpr int16_t WIDTH      = 240;
inline constexpr int16_t HEIGHT     = 240;

// Reale Verkabelung des vorhandenen ESP32-C3+GC9A01-Aufbaus (siehe CLAUDE.md
// "Vorhandene Hardware") als EIN gemeinsamer Default fuer alle Targets -
// Strapping-Pins 2/8/9 bewusst frei gehalten. Passt der Default fuer ein
// anderes Target/eine andere Verkabelung nicht (z.B. GPIO0/1/10 dort belegt),
// laesst er sich pro Pin ueber das Webportal (Tab "Anzeige" -> "Pins")
// ueberschreiben, ohne Neu-Flash - siehe initDisplay() unten.
inline constexpr int8_t DEFAULT_MISO = -1; // GC9A01 hat gewoehnlich keinen MISO-Pin
inline constexpr int8_t DEFAULT_MOSI = 3;  // SDA / DIN
inline constexpr int8_t DEFAULT_SCLK = 4;  // SCL / SCK
inline constexpr int8_t DEFAULT_CS   = 1;
inline constexpr int8_t DEFAULT_DC   = 10;
inline constexpr int8_t DEFAULT_RST  = 0;
inline constexpr int8_t DEFAULT_BL   = -1; // -1: Backlight direkt an 3V3

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
  gfx = new Arduino_GC9A01(bus, pins.rst, 0 /* Rotation */, false /* IPS */);

  if (pins.bl >= 0) {
    pinMode(pins.bl, OUTPUT);
    digitalWrite(pins.bl, HIGH);
  }

  bool ok = gfx->begin();
  gfx->invertDisplay(true); // dieses GC9A01-Panel zeigt sonst Negativfarben
  return ok;
}

inline lv_display_t *initLvglDisplay() {
  return initLvglDisplaySPI(gfx, WIDTH, HEIGHT);
}

} // namespace disp_gc9a01
