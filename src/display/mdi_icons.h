#pragma once
// ------------------------------------------------------------------
// Material Design Icons (https://materialdesignicons.com/, Pictogrammers,
// Apache-2.0-Lizenz) als schlanke LVGL-Font-Teilmenge (die 9 hier
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
// Fix Schritt 1: alle 8 Codepoints per lv_font_conv-Remapping
// (-r 'quelle=>ziel') auf U+E001-U+E008 (Basic Multilingual Plane, Private
// Use Area) verschoben - selber 3-Byte-UTF-8-Bereich wie LVGLs eigene
// LV_SYMBOL_*-Makros. lv_font_conv wechselt dadurch automatisch von der
// "sparse tiny" auf die "format0 tiny" Cmap (zusammenhaengender Bereich).
// Per Diagnose-Log (lv_font_get_glyph_dsc) bestaetigt: der Codepoint-Lookup
// findet danach alle 8 Glyphen korrekt (richtige box_w/box_h/adv_w) - auf
// Hardware war trotzdem weiterhin KEIN Icon sichtbar. Die Cmap/Codepoints
// waren also gar nicht das eigentliche Problem.
//
// Fix Schritt 2: Font zusaetzlich mit --no-compress --no-prefilter neu
// generiert (bitmap_format 1 [RLE-komprimiert] -> 0 [unkomprimiert]).
// lv_font_get_glyph_dsc() liest nur Metadaten und fand die Glyphen bereits
// vorher korrekt - der eigentliche Fehler lag vermutlich beim Dekomprimieren
// der Bitmap-Daten selbst (separater Codepfad, wird von der Diagnose nicht
// abgedeckt).
//
// Verwendung: make_label(parent, MDI_CHIP, &mdi_icons_20, farbe);
// ------------------------------------------------------------------
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t mdi_icons_20;
// 28px-Variante (dieselben 9 Glyphen/Codepoints, siehe Kommentar oben) -
// wird nur vom Dashboard-UI fuer grosse Panels genutzt (display_ui_wide.c),
// wo Icons neben der 28px-Uhrzeit/Wetter-Schrift sonst unproportional klein
// wirken. Generiert mit demselben lv_font_conv-Aufruf wie mdi_icons_20,
// nur --size 28 --lv-font-name mdi_icons_28 -o font_mdi_icons_28.c.
extern const lv_font_t mdi_icons_28;

// Zeichen-Codes sind die remappten Font-internen Codepoints (U+E001-E009),
// nicht die MDI-Originalcodepoints - siehe Kommentar oben.
#define MDI_ARROW_LEFT     "\xEE\x80\x81"   // mdi-arrow-left       (Original U+F004D, remapped U+E001)
#define MDI_COG            "\xEE\x80\x82"   // mdi-cog              (Original U+F0493, remapped U+E002)
#define MDI_THERMOMETER    "\xEE\x80\x83"   // mdi-thermometer      (Original U+F050F, remapped U+E003)
#define MDI_RAIN           "\xEE\x80\x84"   // mdi-weather-rainy    (Original U+F0597, remapped U+E004)
#define MDI_SUN            "\xEE\x80\x85"   // mdi-weather-sunny    (Original U+F0599, remapped U+E005)
#define MDI_WIND           "\xEE\x80\x86"   // mdi-weather-windy    (Original U+F059D, remapped U+E006)
#define MDI_WIFI           "\xEE\x80\x87"   // mdi-wifi             (Original U+F05A9, remapped U+E007)
#define MDI_CHIP           "\xEE\x80\x88"   // mdi-chip             (Original U+F061A, remapped U+E008)
#define MDI_HUMIDITY       "\xEE\x80\x89"   // mdi-water-percent    (Original U+F058E, remapped U+E009)

#ifdef __cplusplus
}
#endif
