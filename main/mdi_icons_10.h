#pragma once
// ------------------------------------------------------------------
// Material Design Icons (https://materialdesignicons.com/, Pictogrammers,
// Apache-2.0-Lizenz), 10px-Variante fuer das SSD1309-Minimal-UI (128x64,
// monochrom, siehe build_mono_ui() in display_ui.c). Eigene, kleinere
// Font-Teilmenge statt mdi_icons.h wiederzuverwenden - LVGL-Fonts lassen
// sich nicht verlustfrei zur Laufzeit skalieren, jede benoetigte Groesse
// braucht ihre eigene generierte Bitmap-Font.
//
// Generiert mit lv_font_conv aus demselben @mdi/font-npm-Paket wie
// mdi_icons.h, dieselbe Remapping-Technik (Codepoints oberhalb U+FFFF auf
// die BMP-Private-Use-Area E001-E004 verschoben) - Hintergrund zum sonst
// stummen lv_font_conv-Cmap-/Kompressions-Bug siehe Kommentar in
// mdi_icons.h.
//
// Erzeugt mit:
//   npx lv_font_conv --font materialdesignicons-webfont.ttf
//     -r '0xF061A=>0xE001,0xF0241=>0xE002,0xF050F=>0xE003,0xF0599=>0xE004'
//     --size 10 --bpp 4 --format lvgl --lv-font-name mdi_icons_10
//     --no-compress --no-prefilter -o font_mdi_icons_10.c
//
// Verwendung: make_label(parent, MDI10_CHIP, &mdi_icons_10, farbe);
// ------------------------------------------------------------------
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t mdi_icons_10;

// Zeichen-Codes sind die remappten Font-internen Codepoints (U+E001-E004),
// nicht die MDI-Originalcodepoints - siehe Kommentar oben.
#define MDI10_CHIP         "\xEE\x80\x81"   // mdi-chip          (Original U+F061A, remapped U+E001)
#define MDI10_FLASH        "\xEE\x80\x82"   // mdi-flash         (Original U+F0241, remapped U+E002)
#define MDI10_THERMOMETER  "\xEE\x80\x83"   // mdi-thermometer   (Original U+F050F, remapped U+E003)
#define MDI10_SUN          "\xEE\x80\x84"   // mdi-weather-sunny (Original U+F0599, remapped U+E004)

#ifdef __cplusplus
}
#endif
