#pragma once
// ------------------------------------------------------------------
// Material Design Icons (https://materialdesignicons.com/, Pictogrammers,
// Apache-2.0-Lizenz) als schlanke LVGL-Font-Teilmenge (die 8 hier
// tatsaechlich genutzten Glyphen, 20px, generiert mit lv_font_conv aus
// dem npm-Paket "@mdi/font"). Ersetzt die vorherigen handgezeichneten
// Vektor-Icons durch echte, wiedererkennbare Symbole.
//
// Bewusst NUR diese 8 eng beieinanderliegenden Codepoints (0xF004D-
// 0xF061A) - ein frueherer Versuch mit zusaetzlichen, weit entfernten
// Codepoints (unter anderem 0xF140B "lightning-bolt") loeste einen
// bekannten lv_font_conv-Bug aus (github.com/lvgl/lv_font_conv Issue #62:
// "Codepoint delta out of range" / kaputte sparse-cmap bei grossen Luecken
// zwischen den Codepoints), wodurch auf dem Geraet KEIN einziges MDI-Icon
// sichtbar war (leerer Platz statt Glyph).
//
// Verwendung: make_label(parent, MDI_CHIP, &mdi_icons_20, farbe);
// ------------------------------------------------------------------
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t mdi_icons_20;

#define MDI_CHIP           "\xF3\xB0\x98\x9A"   // mdi-chip             U+F061A
#define MDI_THERMOMETER    "\xF3\xB0\x94\x8F"   // mdi-thermometer      U+F050F
#define MDI_SUN             "\xF3\xB0\x96\x99"   // mdi-weather-sunny    U+F0599
#define MDI_WIND            "\xF3\xB0\x96\x9D"   // mdi-weather-windy    U+F059D
#define MDI_RAIN            "\xF3\xB0\x96\x97"   // mdi-weather-rainy    U+F0597
#define MDI_COG             "\xF3\xB0\x92\x93"   // mdi-cog              U+F0493
#define MDI_ARROW_LEFT      "\xF3\xB0\x81\x8D"   // mdi-arrow-left       U+F004D
#define MDI_WIFI            "\xF3\xB0\x96\xA9"   // mdi-wifi             U+F05A9

#ifdef __cplusplus
}
#endif
