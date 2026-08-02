#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_CHART   1
#define LV_USE_ARC     1
#define LV_USE_BAR     1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_UNSCII_8      1 // fuer das monochrome SSD1309-UI (Phase 2)
#define LV_FONT_UNSCII_16     1 // groesseres Boot-Logo auf breiten Displays (>=800px), siehe display_draw.cpp

// Ohne dieses Flag liefert lv_font_get_bitmap_fmt_txt() fuer jeden Glyph
// still NULL zurueck, sobald eine Font-Datei mit RLE-Kompression exportiert
// wurde (bitmap_format=1 im generierten .c, z.B. src/fonts/mdi_*.c) - Icons
// wuerden also gar nicht erst geladen statt nur falsch auszusehen. Default
// in lv_conf_internal.h ist 0 (aus).
#define LV_USE_FONT_COMPRESSED 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LOG 0

#define LV_MEM_SIZE (48 * 1024U)

#endif // LV_CONF_H
