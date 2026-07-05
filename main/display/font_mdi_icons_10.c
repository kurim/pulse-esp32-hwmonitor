/*******************************************************************************
 * Size: 10 px
 * Bpp: 4
 * Opts: --font package/fonts/materialdesignicons-webfont.ttf -r 0xF061A=>0xE001,0xF0241=>0xE002,0xF050F=>0xE003,0xF0599=>0xE004 --size 10 --bpp 4 --format lvgl --lv-font-name mdi_icons_10 --no-compress --no-prefilter -o font_mdi_icons_10.c
 ******************************************************************************/

#include "lvgl.h"

#ifndef MDI_ICONS_10
#define MDI_ICONS_10 1
#endif

#if MDI_ICONS_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+E001 "" */
    0x1, 0x11, 0x11, 0x0, 0x9e, 0xff, 0xff, 0xc6,
    0x2b, 0xff, 0xff, 0x61, 0xcf, 0xff, 0xff, 0xe7,
    0x1b, 0xff, 0xff, 0x61, 0xae, 0xfe, 0xee, 0xc6,
    0x6d, 0xf9, 0x99, 0x94, 0x5a, 0xdd, 0xdd, 0x83,

    /* U+E002 "" */
    0x1f, 0xff, 0xc0, 0x1f, 0xff, 0x50, 0x1f, 0xfd,
    0x0, 0x1f, 0xfd, 0x90, 0x9, 0xff, 0x60, 0x0,
    0xdd, 0x0, 0x0, 0xd3, 0x0, 0x0, 0x80, 0x0,
    0x0, 0x0, 0x0,

    /* U+E003 "" */
    0x9, 0xc2, 0x0, 0xd6, 0x70, 0xe, 0xc7, 0x0,
    0xff, 0x70, 0x2f, 0xf9, 0xb, 0xff, 0xf2, 0xcf,
    0xff, 0x34, 0xff, 0xa0, 0x0, 0x20, 0x0,

    /* U+E004 "" */
    0x0, 0x4, 0x40, 0x0, 0x0, 0x5, 0x50, 0x0,
    0x5a, 0x3c, 0xc3, 0xa5, 0x2, 0xd2, 0x2d, 0x20,
    0x1, 0xd0, 0xd, 0x10, 0x48, 0x6d, 0xd6, 0x84,
    0x13, 0x2, 0x20, 0x31, 0x0, 0x7, 0x70, 0x0,
    0x0, 0x0, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 160, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 32, .adv_w = 160, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 59, .adv_w = 160, .box_w = 5, .box_h = 9, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 82, .adv_w = 160, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = -1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 57345, .range_length = 4, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t mdi_icons_10 = {
#else
lv_font_t mdi_icons_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 9,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if MDI_ICONS_10*/

