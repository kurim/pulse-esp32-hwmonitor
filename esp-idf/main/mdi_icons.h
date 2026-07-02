#pragma once
// ------------------------------------------------------------------
// Material Design Icons (https://materialdesignicons.com/, Pictogrammers,
// Apache-2.0-Lizenz) als schlanke LVGL-Font-Teilmenge (die 8 hier
// tatsaechlich genutzten Glyphen, 20px, generiert mit lv_font_conv aus
// dem npm-Paket "@mdi/font"). Ersetzt die vorherigen handgezeichneten
// Vektor-Icons durch echte, wiedererkennbare Symbole.
//
// WICHTIG: Die MDI-Originalcodepoints liegen alle oberhalb U+FFFF (z.B.
// "chip" = U+F061A, im "Supplementary Private Use Area-A"), brauchen also
// 4-Byte-UTF-8. Zwei Anlaeufe mit den Original-Codepoints direkt (erst 12,
// dann auf 8 eng beieinanderliegende reduziert) zeigten auf echter Hardware
// UEBERHAUPT KEIN MDI-Icon, obwohl lv_font_conv anstandslos durchlief -
// vermutlich verwandt mit einem bekannten lv_font_conv-Bug bei der "sparse
// tiny" Cmap fuer hohe Codepoints (github.com/lvgl/lv_font_conv Issue #62).
// Fix: alle 8 Codepoints per lv_font_conv-Remapping (-r 'quelle=>ziel') auf
// U+E001-U+E008 (Basic Multilingual Plane, Private Use Area) verschoben -
// selber 3-Byte-UTF-8-Bereich wie LVGLs eigene LV_SYMBOL_*-Makros (die
// nachweislich funktionieren). Als angenehmer Nebeneffekt wechselt
// lv_font_conv dadurch automatisch von der fehleranfaelligen "sparse tiny"
// Cmap auf die simplere "format0 tiny" (zusammenhaengender Bereich, keine
// Lookup-Tabelle noetig).
//
// Verwendung: make_label(parent, MDI_CHIP, &mdi_icons_20, farbe);
// ------------------------------------------------------------------
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t mdi_icons_20;

// Zeichen-Codes sind die remappten Font-internen Codepoints (U+E001-E008),
// nicht die MDI-Originalcodepoints - siehe Kommentar oben.
#define MDI_ARROW_LEFT     "\xEE\x80\x81"   // mdi-arrow-left       (Original U+F004D, remapped U+E001)
#define MDI_COG            "\xEE\x80\x82"   // mdi-cog              (Original U+F0493, remapped U+E002)
#define MDI_THERMOMETER    "\xEE\x80\x83"   // mdi-thermometer      (Original U+F050F, remapped U+E003)
#define MDI_RAIN           "\xEE\x80\x84"   // mdi-weather-rainy    (Original U+F0597, remapped U+E004)
#define MDI_SUN            "\xEE\x80\x85"   // mdi-weather-sunny    (Original U+F0599, remapped U+E005)
#define MDI_WIND           "\xEE\x80\x86"   // mdi-weather-windy    (Original U+F059D, remapped U+E006)
#define MDI_WIFI           "\xEE\x80\x87"   // mdi-wifi             (Original U+F05A9, remapped U+E007)
#define MDI_CHIP           "\xEE\x80\x88"   // mdi-chip             (Original U+F061A, remapped U+E008)

#ifdef __cplusplus
}
#endif
