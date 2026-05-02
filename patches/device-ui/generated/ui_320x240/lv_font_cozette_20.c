/*******************************************************************************
 * Size: 20 px
 * Bpp: 1
 * Opts: --font /tmp/CozetteVector.ttf --size 20 --bpp 1 --format lvgl -o /tmp/lv_font_cozette_20.c --range 0x20-0x7E
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_FONT_COZETTE_20
#define LV_FONT_COZETTE_20 1
#endif

#if LV_FONT_COZETTE_20

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xff, 0xc3,

    /* U+0022 "\"" */
    0xde, 0xf7, 0xb0,

    /* U+0023 "#" */
    0x6c, 0x6c, 0x6c, 0xff, 0x6c, 0x6c, 0x6c, 0x6c,
    0xff, 0x6c, 0x6c, 0x6c,

    /* U+0024 "$" */
    0x18, 0x18, 0x3c, 0xdb, 0xd8, 0xd8, 0x3c, 0x18,
    0x1b, 0x1b, 0x1b, 0xdb, 0x18, 0x3c, 0x18,

    /* U+0025 "%" */
    0x20, 0x0, 0xd8, 0x0, 0x23, 0x4, 0x8, 0x18,
    0x20, 0x40, 0xc4, 0x1b, 0x0, 0x4,

    /* U+0026 "&" */
    0x18, 0x20, 0x24, 0x24, 0x24, 0x18, 0x18, 0x3b,
    0xc4, 0xc4, 0xc4, 0xc4, 0x0, 0x3b,

    /* U+0027 "'" */
    0xff,

    /* U+0028 "(" */
    0xc, 0x42, 0x8, 0x23, 0xc, 0x30, 0xc3, 0xc,
    0x30, 0x20, 0x82, 0x7,

    /* U+0029 ")" */
    0xc1, 0x2, 0x8, 0x20, 0x70, 0xc3, 0xc, 0x30,
    0xc3, 0x20, 0x82, 0x30,

    /* U+002A "*" */
    0x64, 0x18, 0x18, 0xff, 0x18, 0x8, 0x64,

    /* U+002B "+" */
    0x18, 0x18, 0x18, 0xff, 0x18, 0x18, 0x18,

    /* U+002C "," */
    0xff, 0x93, 0x80,

    /* U+002D "-" */
    0xff,

    /* U+002E "." */
    0xff, 0x80,

    /* U+002F "/" */
    0x3, 0x3, 0x3, 0x4, 0x4, 0x4, 0x18, 0x18,
    0x18, 0x20, 0x20, 0x20, 0xc0, 0xc0, 0xc0,

    /* U+0030 "0" */
    0x3c, 0xc3, 0xc3, 0xc3, 0xdb, 0xdb, 0xdb, 0xc3,
    0xc3, 0xc3, 0x40, 0x3c,

    /* U+0031 "1" */
    0x18, 0x18, 0x38, 0xd8, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0xff,

    /* U+0032 "2" */
    0x3c, 0xc3, 0x3, 0x3, 0x0, 0x4, 0x18, 0x0,
    0x20, 0xc0, 0xc0, 0xff,

    /* U+0033 "3" */
    0x3c, 0xc3, 0x3, 0x3, 0x4, 0x1c, 0x3, 0x3,
    0x3, 0xc3, 0x40, 0x3c,

    /* U+0034 "4" */
    0x3, 0x1, 0x81, 0xc3, 0x62, 0x31, 0x1b, 0xd,
    0x86, 0xff, 0x81, 0x80, 0xc0, 0x60,

    /* U+0035 "5" */
    0xff, 0xc0, 0xc0, 0xc0, 0xc0, 0xfc, 0x3, 0x3,
    0x3, 0xc3, 0x40, 0x3c,

    /* U+0036 "6" */
    0x1c, 0x20, 0x20, 0xc0, 0xc0, 0xfc, 0xc3, 0xc3,
    0xc3, 0xc3, 0x40, 0x3c,

    /* U+0037 "7" */
    0xff, 0x1, 0x1, 0x6, 0x6, 0x6, 0x8, 0x8,
    0x8, 0x30, 0x30, 0x30,

    /* U+0038 "8" */
    0x3c, 0xc3, 0xc3, 0xc3, 0x40, 0x3c, 0xc3, 0xc3,
    0xc3, 0xc3, 0x40, 0x3c,

    /* U+0039 "9" */
    0x3c, 0x40, 0xc3, 0xc3, 0xc3, 0xc3, 0x3f, 0x3,
    0x3, 0x4, 0x8, 0x38,

    /* U+003A ":" */
    0xff, 0x80, 0x3f, 0xe0,

    /* U+003B ";" */
    0xff, 0x80, 0x3f, 0xe4, 0xe0,

    /* U+003C "<" */
    0x4, 0x62, 0x8, 0xc1, 0x2, 0x6, 0x8, 0x10,

    /* U+003D "=" */
    0xff, 0x0, 0x0, 0xff,

    /* U+003E ">" */
    0xc0, 0x82, 0x6, 0x4, 0x1, 0x88, 0x43, 0x0,

    /* U+003F "?" */
    0x3c, 0xc3, 0x3, 0x3, 0x4, 0x4, 0x18, 0x18,
    0x18, 0x0, 0x0, 0x18,

    /* U+0040 "@" */
    0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xdf, 0xdb, 0xdb,
    0xdf, 0xc0, 0x40, 0x3f,

    /* U+0041 "A" */
    0x3c, 0x40, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3,

    /* U+0042 "B" */
    0xfc, 0xc3, 0xc3, 0xc3, 0xc0, 0xfc, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc0, 0xfc,

    /* U+0043 "C" */
    0x3c, 0x44, 0xc3, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc3, 0x44, 0x3c,

    /* U+0044 "D" */
    0xf0, 0xd0, 0xcc, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xcc, 0xd0, 0xf0,

    /* U+0045 "E" */
    0xff, 0xc0, 0xc0, 0xc0, 0xc0, 0xfc, 0xc0, 0xc0,
    0xc0, 0xc0, 0xc0, 0xff,

    /* U+0046 "F" */
    0xff, 0xc0, 0xc0, 0xc0, 0xc0, 0xfc, 0xc0, 0xc0,
    0xc0, 0xc0, 0xc0, 0xc0,

    /* U+0047 "G" */
    0x3c, 0xc3, 0xc0, 0xc0, 0xc0, 0xc0, 0xc7, 0xc3,
    0xc3, 0xc3, 0x40, 0x3c,

    /* U+0048 "H" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3,

    /* U+0049 "I" */
    0xfb, 0x18, 0xc6, 0x31, 0x8c, 0x63, 0x19, 0xf0,

    /* U+004A "J" */
    0x1f, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0xc3,
    0xc3, 0xc3, 0x40, 0x3c,

    /* U+004B "K" */
    0xc3, 0xc4, 0xc4, 0xd8, 0xd8, 0xf8, 0xc4, 0xc4,
    0xc4, 0xc3, 0xc3, 0xc3,

    /* U+004C "L" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xc0, 0xff,

    /* U+004D "M" */
    0xc3, 0xc3, 0xe7, 0xdb, 0xdb, 0xdb, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3,

    /* U+004E "N" */
    0xc3, 0xe3, 0xe3, 0xe3, 0xdb, 0xdb, 0xdb, 0xc7,
    0xc7, 0xc7, 0xc3, 0xc3,

    /* U+004F "O" */
    0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0x44, 0x3c,

    /* U+0050 "P" */
    0xfc, 0xc4, 0xc3, 0xc3, 0xc3, 0xc3, 0xfc, 0xc0,
    0xc0, 0xc0, 0xc0, 0xc0,

    /* U+0051 "Q" */
    0x3c, 0x44, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc4, 0x4c, 0x3b, 0x3, 0x3,

    /* U+0052 "R" */
    0xfc, 0xc3, 0xc3, 0xc3, 0xc0, 0xfc, 0xcc, 0xcc,
    0xc3, 0xc3, 0xc3, 0xc3,

    /* U+0053 "S" */
    0x3c, 0xc3, 0xc0, 0xc0, 0x40, 0x3c, 0x3, 0x3,
    0x3, 0xc3, 0x40, 0x3c,

    /* U+0054 "T" */
    0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18,

    /* U+0055 "U" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0x40, 0x3c,

    /* U+0056 "V" */
    0xc3, 0xc3, 0xc3, 0xc3, 0x44, 0x24, 0x24, 0x24,
    0x24, 0x18, 0x18, 0x18,

    /* U+0057 "W" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xdb, 0xdb, 0x5c,
    0x3c, 0x24, 0x24, 0x24,

    /* U+0058 "X" */
    0xc3, 0xc3, 0xc3, 0x24, 0x18, 0x18, 0x18, 0x0,
    0x24, 0xc3, 0xc3, 0xc3,

    /* U+0059 "Y" */
    0xc3, 0xc3, 0xc3, 0xc3, 0x44, 0x24, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18,

    /* U+005A "Z" */
    0xff, 0x4, 0x4, 0x18, 0x18, 0x18, 0x20, 0x20,
    0x20, 0xc0, 0xc0, 0xff,

    /* U+005B "[" */
    0xfe, 0x31, 0x8c, 0x63, 0x18, 0xc6, 0x31, 0x8c,
    0x63, 0x1f,

    /* U+005C "\\" */
    0xc0, 0xc0, 0xc0, 0x20, 0x20, 0x20, 0x18, 0x18,
    0x18, 0x4, 0x4, 0x4, 0x3, 0x3, 0x3,

    /* U+005D "]" */
    0xf8, 0xc6, 0x31, 0x8c, 0x63, 0x18, 0xc6, 0x31,
    0x8c, 0x7f,

    /* U+005E "^" */
    0x18, 0x24, 0x44, 0xc3,

    /* U+005F "_" */
    0xff,

    /* U+0060 "`" */
    0xc0, 0x80,

    /* U+0061 "a" */
    0x3f, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc7, 0x43,
    0x3b,

    /* U+0062 "b" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xfc, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3, 0xc0, 0xfc,

    /* U+0063 "c" */
    0x3c, 0x44, 0xc3, 0xc0, 0xc0, 0xc0, 0xc3, 0x44,
    0x3c,

    /* U+0064 "d" */
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3f, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3, 0x3, 0x3f,

    /* U+0065 "e" */
    0x3c, 0xc3, 0xc3, 0xff, 0xc0, 0xc0, 0xc3, 0x44,
    0x3c,

    /* U+0066 "f" */
    0x1f, 0x20, 0x60, 0x60, 0x60, 0xfc, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60,

    /* U+0067 "g" */
    0x3f, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x3,
    0x3f, 0x3, 0x3, 0x3, 0x3c,

    /* U+0068 "h" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xfc, 0xc4, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,

    /* U+0069 "i" */
    0x30, 0x0, 0x7, 0x83, 0x6, 0xc, 0x18, 0x30,
    0x60, 0xc0, 0x70,

    /* U+006A "j" */
    0xc, 0x0, 0xf, 0xc, 0x30, 0xc3, 0xc, 0x30,
    0xc3, 0xcc, 0x80,

    /* U+006B "k" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc3, 0xc4, 0xc4,
    0xd8, 0xd8, 0xf8, 0xc4, 0xc4, 0xc3,

    /* U+006C "l" */
    0xf1, 0x8c, 0x63, 0x18, 0xc6, 0x31, 0x8c, 0x63,
    0x1c,

    /* U+006D "m" */
    0xe4, 0xec, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
    0xdb,

    /* U+006E "n" */
    0xfc, 0xc4, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3,

    /* U+006F "o" */
    0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x44,
    0x3c,

    /* U+0070 "p" */
    0xfc, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc0,
    0xfc, 0xc0, 0xc0, 0xc0, 0xc0,

    /* U+0071 "q" */
    0x3f, 0x61, 0xb0, 0xd8, 0x6c, 0x36, 0x1b, 0xc,
    0x6, 0x3f, 0x1, 0x80, 0xc0, 0x60, 0x38,

    /* U+0072 "r" */
    0xfc, 0xc4, 0xc3, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0,

    /* U+0073 "s" */
    0x3f, 0x40, 0xc0, 0x3c, 0x3, 0x3, 0x3, 0x0,
    0xfc,

    /* U+0074 "t" */
    0x60, 0x60, 0x60, 0xfc, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x20, 0x1f,

    /* U+0075 "u" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x3,
    0x3f,

    /* U+0076 "v" */
    0xc3, 0xc3, 0xc3, 0x24, 0x24, 0x24, 0x18, 0x18,
    0x18,

    /* U+0077 "w" */
    0xc3, 0xc3, 0xc3, 0xdb, 0xdb, 0xdb, 0x24, 0x24,
    0x24,

    /* U+0078 "x" */
    0xc3, 0x0, 0x24, 0x18, 0x18, 0x18, 0x24, 0x0,
    0xc3,

    /* U+0079 "y" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x43,
    0x3f, 0x3, 0x3, 0x3, 0x3c,

    /* U+007A "z" */
    0xff, 0x4, 0x4, 0x18, 0x0, 0x20, 0xc0, 0xc0,
    0xff,

    /* U+007B "{" */
    0x7, 0x8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0xe0, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x8,
    0x7,

    /* U+007C "|" */
    0xff, 0xff, 0xff, 0xff, 0xc0,

    /* U+007D "}" */
    0xe0, 0x20, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x7, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x20,
    0xe0,

    /* U+007E "~" */
    0x23, 0xdb, 0xc8, 0xc4
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 148, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 148, .box_w = 2, .box_h = 12, .ofs_x = 5, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 148, .box_w = 5, .box_h = 4, .ofs_x = 3, .ofs_y = 10},
    {.bitmap_index = 7, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 19, .adv_w = 148, .box_w = 8, .box_h = 15, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 34, .adv_w = 148, .box_w = 8, .box_h = 14, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 48, .adv_w = 148, .box_w = 8, .box_h = 14, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 62, .adv_w = 148, .box_w = 2, .box_h = 4, .ofs_x = 5, .ofs_y = 10},
    {.bitmap_index = 63, .adv_w = 148, .box_w = 6, .box_h = 16, .ofs_x = 3, .ofs_y = -2},
    {.bitmap_index = 75, .adv_w = 148, .box_w = 6, .box_h = 16, .ofs_x = 3, .ofs_y = -2},
    {.bitmap_index = 87, .adv_w = 148, .box_w = 8, .box_h = 7, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 94, .adv_w = 148, .box_w = 8, .box_h = 7, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 101, .adv_w = 148, .box_w = 3, .box_h = 6, .ofs_x = 3, .ofs_y = -3},
    {.bitmap_index = 104, .adv_w = 148, .box_w = 8, .box_h = 1, .ofs_x = 2, .ofs_y = 5},
    {.bitmap_index = 105, .adv_w = 148, .box_w = 3, .box_h = 3, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 107, .adv_w = 148, .box_w = 8, .box_h = 15, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 122, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 134, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 146, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 170, .adv_w = 148, .box_w = 9, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 184, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 196, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 220, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 232, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 148, .box_w = 3, .box_h = 9, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 248, .adv_w = 148, .box_w = 3, .box_h = 12, .ofs_x = 3, .ofs_y = -3},
    {.bitmap_index = 253, .adv_w = 148, .box_w = 6, .box_h = 10, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 261, .adv_w = 148, .box_w = 8, .box_h = 4, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 265, .adv_w = 148, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 273, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 285, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 297, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 309, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 321, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 333, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 345, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 357, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 369, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 381, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 393, .adv_w = 148, .box_w = 5, .box_h = 12, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 401, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 413, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 425, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 437, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 449, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 461, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 473, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 485, .adv_w = 148, .box_w = 8, .box_h = 14, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 499, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 511, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 523, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 535, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 547, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 559, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 571, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 583, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 595, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 607, .adv_w = 148, .box_w = 5, .box_h = 16, .ofs_x = 3, .ofs_y = -2},
    {.bitmap_index = 617, .adv_w = 148, .box_w = 8, .box_h = 15, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 632, .adv_w = 148, .box_w = 5, .box_h = 16, .ofs_x = 3, .ofs_y = -2},
    {.bitmap_index = 642, .adv_w = 148, .box_w = 8, .box_h = 4, .ofs_x = 2, .ofs_y = 11},
    {.bitmap_index = 646, .adv_w = 148, .box_w = 8, .box_h = 1, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 647, .adv_w = 148, .box_w = 3, .box_h = 3, .ofs_x = 3, .ofs_y = 11},
    {.bitmap_index = 649, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 658, .adv_w = 148, .box_w = 8, .box_h = 14, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 672, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 681, .adv_w = 148, .box_w = 8, .box_h = 14, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 695, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 704, .adv_w = 148, .box_w = 8, .box_h = 14, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 718, .adv_w = 148, .box_w = 8, .box_h = 13, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 731, .adv_w = 148, .box_w = 8, .box_h = 14, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 745, .adv_w = 148, .box_w = 7, .box_h = 12, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 756, .adv_w = 148, .box_w = 6, .box_h = 14, .ofs_x = 3, .ofs_y = -2},
    {.bitmap_index = 767, .adv_w = 148, .box_w = 8, .box_h = 14, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 781, .adv_w = 148, .box_w = 5, .box_h = 14, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 790, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 799, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 808, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 817, .adv_w = 148, .box_w = 8, .box_h = 13, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 830, .adv_w = 148, .box_w = 9, .box_h = 13, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 845, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 854, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 863, .adv_w = 148, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 875, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 884, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 893, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 902, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 911, .adv_w = 148, .box_w = 8, .box_h = 13, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 924, .adv_w = 148, .box_w = 8, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 933, .adv_w = 148, .box_w = 8, .box_h = 17, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 950, .adv_w = 148, .box_w = 2, .box_h = 17, .ofs_x = 5, .ofs_y = -3},
    {.bitmap_index = 955, .adv_w = 148, .box_w = 8, .box_h = 17, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 972, .adv_w = 148, .box_w = 8, .box_h = 4, .ofs_x = 2, .ofs_y = 3}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
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
    .bpp = 1,
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
const lv_font_t lv_font_cozette_20 = {
#else
lv_font_t lv_font_cozette_20 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 19,          /*The maximum line height required by the font*/
    .base_line = 4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 2,
#endif
    .static_bitmap = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_COZETTE_20*/

