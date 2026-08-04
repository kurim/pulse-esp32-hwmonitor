#pragma once
#include <stdint.h>

// -1 ist ein gueltiger Pin-Wert ("nicht verdrahtet", z.B. MISO beim GC9A01).
// PIN_UNSET markiert stattdessen "kein Override in der NVS hinterlegt".
// Siehe CLAUDE.md ("PIN_UNSET (nicht -1) markiert kein Override").
static constexpr int8_t PIN_UNSET = -128;

// Einheitliches Layout fuer alle SPI-TFT-Displays aus include/displays/,
// auch wenn ein konkreter Treiber nicht jedes Feld nutzt (z.B. kein
// separates BL) - so bleibt die NVS-Blobgroesse unabhaengig vom gewaehlten
// Panel gleich, statt bei einem Wechsel des Displaytyps zu mismatchen.
typedef struct {
  int8_t mosi = PIN_UNSET;
  int8_t miso = PIN_UNSET;
  int8_t sclk = PIN_UNSET;
  int8_t cs   = PIN_UNSET;
  int8_t dc   = PIN_UNSET;
  int8_t rst  = PIN_UNSET;
  int8_t bl   = PIN_UNSET;
} pin_override_t;

inline void pin_override_set_defaults(pin_override_t *p) {
  *p = pin_override_t{};
}

// I2C-Pendant zu pin_override_t, fuer die monochromen I2C-OLEDs
// (include/displays/ssd1309.h: DISPLAY_GENERIC_SSD1309 und
// DISPLAY_GENERIC_SH1106 teilen sich dieselbe Pin-Form/dasselbe Feld
// app_config.mono_i2c_pins, da immer nur einer der beiden Typen gleichzeitig
// gewaehlt sein kann - dieselbe Konvention wie GC9A01/ILI9488/ST7796S, die
// sich ebenfalls ein gemeinsames pin_overrides teilen). Andere Pin-Form
// (SDA/SCL statt MOSI/MISO/SCLK/CS/DC), deshalb eigenes Layout statt
// pin_override_t mit ungenutzten Feldern zu ueberladen. i2c_addr 0 = kein
// Override (0x00 ist keine gueltige I2C-Adresse), analog zu PIN_UNSET bei
// den Pin-Feldern.
typedef struct {
  int8_t  sda      = PIN_UNSET;
  int8_t  scl      = PIN_UNSET;
  int8_t  rst      = PIN_UNSET;
  uint8_t i2c_addr = 0;
} pin_override_i2c_t;

inline void pin_override_i2c_set_defaults(pin_override_i2c_t *p) {
  *p = pin_override_i2c_t{};
}
