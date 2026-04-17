/*******************************************************************************
 * Size: 12 px
 * Bpp: 1
 * Opts: --no-compress --no-prefilter --bpp 1 --size 12 --font /Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware/.pio/libdeps/t-deck-tft/lvgl/scripts/built_in_font/unscii-8.ttf -r 0x20-0x7F --format lvgl -o /tmp/lv_font_unscii_12.c --force-fast-kern-format --lv-font-name lv_font_unscii_12
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

// Guard removed — always compile this font for MonsterMesh
#undef LV_FONT_UNSCII_12
#define LV_FONT_UNSCII_12 1

#if LV_FONT_UNSCII_12

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xff, 0xf8, 0x1c,

    /* U+0022 "\"" */
    0xe3, 0xf1, 0xf8, 0xfc, 0x70,

    /* U+0023 "#" */
    0x39, 0xc7, 0x38, 0xe7, 0x1c, 0xf, 0xfe, 0x73,
    0x8e, 0x7, 0xff, 0x39, 0xc7, 0x38, 0xe7, 0x0,

    /* U+0024 "$" */
    0x1c, 0xe, 0xf, 0xe0, 0xe, 0x1, 0xf8, 0xc,
    0x7, 0xff, 0x0, 0x7, 0x0,

    /* U+0025 "%" */
    0xe0, 0xfc, 0x3, 0x8e, 0x3, 0x80, 0x0, 0x38,
    0xe, 0x38, 0x0, 0xe0, 0xe0,

    /* U+0026 "&" */
    0x1f, 0x7, 0x30, 0x60, 0xf, 0x83, 0xef, 0xc0,
    0x39, 0xe7, 0xc, 0x20, 0x87, 0xdc,

    /* U+0027 "'" */
    0x39, 0xce, 0xe, 0x0,

    /* U+0028 "(" */
    0x1c, 0xe0, 0x38, 0xe3, 0x8e, 0xe, 0x38, 0x70,

    /* U+0029 ")" */
    0xe0, 0xe3, 0x87, 0x1c, 0x71, 0xce, 0x3, 0x80,

    /* U+002A "*" */
    0x38, 0xe1, 0xf8, 0x1f, 0x8f, 0xff, 0x1f, 0x80,
    0x0, 0x38, 0xe0,

    /* U+002B "+" */
    0x1c, 0xe, 0x7, 0x3, 0x8f, 0xf8, 0xe0, 0x70,
    0x38,

    /* U+002C "," */
    0x39, 0xce, 0xe, 0x0,

    /* U+002D "-" */
    0xff, 0x80,

    /* U+002E "." */
    0xff, 0x80,

    /* U+002F "/" */
    0x0, 0x70, 0xe, 0x0, 0x0, 0x38, 0x7, 0x0,
    0x0, 0x1c, 0x3, 0x80, 0x0, 0xe, 0x0,

    /* U+0030 "0" */
    0x3f, 0x71, 0xf8, 0xfd, 0xff, 0xbf, 0x1f, 0x8f,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+0031 "1" */
    0x1c, 0xe, 0x1f, 0x3, 0x81, 0xc0, 0xe0, 0x70,
    0x38, 0x1c, 0xe, 0x3f, 0xe0,

    /* U+0032 "2" */
    0x3f, 0x71, 0xc0, 0x0, 0xe1, 0xc0, 0x0, 0xe1,
    0xc0, 0xe0, 0x7f, 0xc0,

    /* U+0033 "3" */
    0x3f, 0x71, 0xc0, 0xe0, 0x71, 0xf0, 0x18, 0xf,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+0034 "4" */
    0x7, 0x80, 0xf0, 0x7e, 0x0, 0x3, 0xb9, 0xc7,
    0x38, 0x7, 0xff, 0x3, 0x80, 0x70, 0xe, 0x0,

    /* U+0035 "5" */
    0xff, 0xf0, 0x38, 0x1f, 0xe0, 0x38, 0x1c, 0xf,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+0036 "6" */
    0x1f, 0x1c, 0x38, 0x1c, 0xf, 0xf7, 0x1f, 0x8f,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+0037 "7" */
    0xff, 0x81, 0xc0, 0xe0, 0x70, 0x70, 0x0, 0x70,
    0x38, 0x1c, 0xe, 0x0,

    /* U+0038 "8" */
    0x3f, 0x71, 0xf8, 0xfc, 0x73, 0xf7, 0x1f, 0x8f,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+0039 "9" */
    0x7f, 0x71, 0xf8, 0xfc, 0x77, 0xf8, 0x1c, 0xe,
    0xe, 0x0, 0x3e, 0x0,

    /* U+003A ":" */
    0xff, 0x80, 0x3f, 0xe0,

    /* U+003B ";" */
    0x39, 0xce, 0x0, 0x0, 0xe7, 0x38, 0x38,

    /* U+003C "<" */
    0x7, 0x1c, 0x0, 0x38, 0xe0, 0x38, 0x3c, 0x1c,
    0x4, 0x7,

    /* U+003D "=" */
    0xff, 0x80, 0x0, 0x1f, 0xf0,

    /* U+003E ">" */
    0xe0, 0x38, 0x3c, 0x1c, 0x7, 0x0, 0x1c, 0x38,
    0x0, 0xe0,

    /* U+003F "?" */
    0x3f, 0x71, 0xc0, 0xe0, 0x70, 0x70, 0x0, 0x70,
    0x0, 0x0, 0xe, 0x0,

    /* U+0040 "@" */
    0x7f, 0x9c, 0x1f, 0x83, 0xf3, 0xfe, 0x7f, 0xcf,
    0xf9, 0xff, 0x0, 0x60, 0xf, 0xf0,

    /* U+0041 "A" */
    0x1c, 0xe, 0xf, 0xdc, 0x7e, 0x3f, 0x1f, 0x81,
    0xff, 0xe3, 0xf1, 0xf8, 0xe0,

    /* U+0042 "B" */
    0xff, 0x71, 0xf8, 0xfc, 0x7f, 0xf7, 0x1f, 0x8f,
    0xc7, 0xe0, 0x7f, 0x80,

    /* U+0043 "C" */
    0x3f, 0x71, 0xf8, 0x1c, 0xe, 0x7, 0x3, 0x81,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+0044 "D" */
    0xfc, 0x73, 0xb8, 0xdc, 0x7e, 0x3f, 0x1f, 0x8f,
    0xce, 0xe0, 0x7e, 0x0,

    /* U+0045 "E" */
    0xff, 0xf0, 0x38, 0x1c, 0xf, 0xf7, 0x3, 0x81,
    0xc0, 0xe0, 0x7f, 0xc0,

    /* U+0046 "F" */
    0xff, 0xf0, 0x38, 0x1c, 0xf, 0xf0, 0x3, 0x81,
    0xc0, 0xe0, 0x70, 0x0,

    /* U+0047 "G" */
    0x3f, 0x71, 0xf8, 0x1c, 0xe, 0xff, 0x1f, 0x8f,
    0xc7, 0x20, 0x1f, 0xc0,

    /* U+0048 "H" */
    0xe0, 0x71, 0xf8, 0xfc, 0x7e, 0x3f, 0xfc, 0x1,
    0xc7, 0xe3, 0xf1, 0xf8, 0xe0,

    /* U+0049 "I" */
    0xff, 0x8e, 0x7, 0x3, 0x81, 0xc0, 0xe0, 0x70,
    0x38, 0x1c, 0x7f, 0xc0,

    /* U+004A "J" */
    0x3, 0x81, 0xc0, 0xe0, 0x70, 0x38, 0x1c, 0xf,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+004B "K" */
    0xe0, 0x1c, 0x1f, 0x8e, 0x70, 0xe, 0x71, 0xf8,
    0x0, 0x7, 0x38, 0xe3, 0x9c, 0x73, 0x83, 0x80,

    /* U+004C "L" */
    0xe0, 0x70, 0x38, 0x1c, 0xe, 0x7, 0x3, 0x81,
    0xc0, 0xe0, 0x70, 0x3f, 0xe0,

    /* U+004D "M" */
    0xe0, 0xfc, 0x1f, 0xcf, 0xf8, 0xf, 0xff, 0xd9,
    0xf8, 0x3f, 0x7, 0xe0, 0xfc, 0x1f, 0x83, 0x80,

    /* U+004E "N" */
    0xe0, 0x1c, 0x1f, 0xe3, 0xfc, 0x7f, 0xcf, 0xcf,
    0xf8, 0xff, 0x1f, 0xe0, 0xfc, 0x1f, 0x83, 0x80,

    /* U+004F "O" */
    0x3f, 0x71, 0xf8, 0xfc, 0x7e, 0x3f, 0x1f, 0x8f,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+0050 "P" */
    0xff, 0x71, 0xf8, 0xfc, 0x7f, 0xf0, 0x3, 0x81,
    0xc0, 0xe0, 0x70, 0x0,

    /* U+0051 "Q" */
    0x3f, 0x71, 0xf8, 0xfc, 0x7e, 0x3f, 0x1f, 0x8f,
    0xce, 0x23, 0x1d, 0xc0,

    /* U+0052 "R" */
    0xff, 0x71, 0xf8, 0xfc, 0x7f, 0xf0, 0x3, 0x9d,
    0xc7, 0xe3, 0xf1, 0xc0,

    /* U+0053 "S" */
    0x3f, 0x71, 0xf8, 0x1c, 0x3, 0xf0, 0x18, 0xf,
    0xc7, 0x20, 0x1f, 0x80,

    /* U+0054 "T" */
    0xff, 0x8e, 0x7, 0x3, 0x81, 0xc0, 0xe0, 0x70,
    0x38, 0x1c, 0xe, 0x0,

    /* U+0055 "U" */
    0xe3, 0xf1, 0xf8, 0xfc, 0x7e, 0x3f, 0x1f, 0x8f,
    0xc7, 0xe3, 0x90, 0xf, 0xc0,

    /* U+0056 "V" */
    0xe3, 0xf1, 0xf8, 0xfc, 0x7e, 0x3f, 0x1f, 0x8e,
    0x7e, 0x0, 0xe, 0x0,

    /* U+0057 "W" */
    0xe0, 0x1c, 0x1f, 0x83, 0xf0, 0x7e, 0xf, 0xc9,
    0xf8, 0x7, 0xff, 0xfb, 0xe0, 0x3, 0x83, 0x80,

    /* U+0058 "X" */
    0xe0, 0x73, 0x8e, 0x38, 0x1, 0xf8, 0x7, 0x0,
    0x70, 0x1f, 0x83, 0x8e, 0x0, 0xe, 0x7,

    /* U+0059 "Y" */
    0xe0, 0x73, 0x8e, 0x38, 0x1, 0xf8, 0x7, 0x0,
    0x70, 0x7, 0x0, 0x70, 0x7, 0x0, 0x70,

    /* U+005A "Z" */
    0xff, 0x81, 0xc0, 0xe0, 0xe1, 0xc0, 0x0, 0xe1,
    0xc0, 0xe0, 0x7f, 0xc0,

    /* U+005B "[" */
    0xff, 0x8e, 0x38, 0xe3, 0x8e, 0x38, 0xe3, 0xf0,

    /* U+005C "\\" */
    0xe0, 0x3, 0x80, 0x1c, 0x1, 0xc0, 0x7, 0x0,
    0x38, 0x3, 0x80, 0xe, 0x0, 0x60, 0x7,

    /* U+005D "]" */
    0xfc, 0x71, 0xc7, 0x1c, 0x71, 0xc7, 0x1f, 0xf0,

    /* U+005E "^" */
    0x4, 0x0, 0x80, 0x7c, 0x1d, 0xc0, 0x1, 0xc1,
    0xc0,

    /* U+005F "_" */
    0xff, 0xf0,

    /* U+0060 "`" */
    0xe0, 0xe3, 0x87,

    /* U+0061 "a" */
    0x3f, 0x1, 0xc0, 0xe7, 0xfe, 0x39, 0x0, 0xfe,

    /* U+0062 "b" */
    0xe0, 0x70, 0x38, 0x1c, 0xf, 0xf7, 0x1f, 0x8f,
    0xc7, 0xe3, 0xf0, 0x3f, 0xc0,

    /* U+0063 "c" */
    0x3f, 0xe0, 0xe0, 0xe0, 0xe0, 0x20, 0x3f,

    /* U+0064 "d" */
    0x3, 0x81, 0xc0, 0xe0, 0x77, 0xff, 0x1f, 0x8f,
    0xc7, 0xe3, 0xb0, 0x1f, 0xe0,

    /* U+0065 "e" */
    0x3f, 0x71, 0xf8, 0x1f, 0xfe, 0x1, 0x0, 0xfc,

    /* U+0066 "f" */
    0x1f, 0x38, 0x38, 0xff, 0x38, 0x38, 0x38, 0x38,
    0x38, 0x38,

    /* U+0067 "g" */
    0x7f, 0xf1, 0xf8, 0xfc, 0x76, 0x3, 0xfc, 0xe,
    0x7, 0xff, 0x0,

    /* U+0068 "h" */
    0xe0, 0x70, 0x38, 0x1c, 0xf, 0xf7, 0x1f, 0x8f,
    0xc7, 0xe3, 0xf1, 0xf8, 0xe0,

    /* U+0069 "i" */
    0x38, 0x0, 0x0, 0xf8, 0x38, 0x38, 0x38, 0x38,
    0x38, 0x3f,

    /* U+006A "j" */
    0x7, 0x0, 0x0, 0x7, 0x7, 0x7, 0x7, 0x7,
    0x7, 0x7, 0x0, 0xfc,

    /* U+006B "k" */
    0xe0, 0x70, 0x38, 0x1c, 0xe, 0x3f, 0x3b, 0x81,
    0xf8, 0xe7, 0x71, 0xb8, 0xe0,

    /* U+006C "l" */
    0xf8, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38,
    0x38, 0x3f,

    /* U+006D "m" */
    0xe0, 0x1c, 0x73, 0xff, 0xf6, 0xe, 0xcf, 0xd9,
    0xf8, 0x3f, 0x7,

    /* U+006E "n" */
    0xff, 0x71, 0xf8, 0xfc, 0x7e, 0x3f, 0x1f, 0x8e,

    /* U+006F "o" */
    0x3f, 0x71, 0xf8, 0xfc, 0x7e, 0x39, 0x0, 0xfc,

    /* U+0070 "p" */
    0xff, 0x71, 0xf8, 0xfc, 0x7e, 0x7, 0xfb, 0x81,
    0xc0, 0xe0, 0x0,

    /* U+0071 "q" */
    0x7f, 0xf1, 0xf8, 0xfc, 0x76, 0x3, 0xfc, 0xe,
    0x7, 0x3, 0x80,

    /* U+0072 "r" */
    0xff, 0x71, 0xf8, 0x1c, 0xe, 0x7, 0x3, 0x80,

    /* U+0073 "s" */
    0x3f, 0xf0, 0x8, 0x7, 0xe0, 0x38, 0x3, 0xfc,

    /* U+0074 "t" */
    0x38, 0x1c, 0xe, 0x7, 0xf, 0xf9, 0xc0, 0xe0,
    0x70, 0x38, 0xc, 0x7, 0xe0,

    /* U+0075 "u" */
    0xe3, 0xf1, 0xf8, 0xfc, 0x7e, 0x3f, 0x1d, 0x80,
    0xff,

    /* U+0076 "v" */
    0xe3, 0xf1, 0xf8, 0xfc, 0x73, 0xf0, 0x0, 0x70,

    /* U+0077 "w" */
    0x0, 0xfc, 0x1f, 0x83, 0xf0, 0xe, 0x4e, 0x7f,
    0x0, 0x1, 0xdc,

    /* U+0078 "x" */
    0xe0, 0xe7, 0x70, 0xe0, 0xf, 0x83, 0xb8, 0x0,
    0x38, 0x38,

    /* U+0079 "y" */
    0xe3, 0xf1, 0xf8, 0xfc, 0x72, 0x1, 0xfc, 0xe,
    0x7, 0x3f, 0x0,

    /* U+007A "z" */
    0xff, 0x83, 0x80, 0x3, 0x83, 0x81, 0xc3, 0xfe,

    /* U+007B "{" */
    0x7, 0x8e, 0x7, 0x3, 0x8f, 0x80, 0xe0, 0x70,
    0x38, 0x1c, 0x3, 0xc0,

    /* U+007C "|" */
    0xff, 0xff, 0xff, 0xfc,

    /* U+007D "}" */
    0xf8, 0xe, 0x7, 0x3, 0x80, 0x78, 0xe0, 0x70,
    0x38, 0x0, 0x7c, 0x0,

    /* U+007E "~" */
    0x7c, 0xe0, 0x3, 0xbe, 0x0,

    /* U+007F "" */
    0xe0, 0x1b, 0x3, 0x60, 0x6d, 0xfd, 0x99, 0x83,
    0x38, 0x60, 0xc, 0x1, 0x80, 0x30
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 192, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 12},
    {.bitmap_index = 1, .adv_w = 192, .box_w = 3, .box_h = 10, .ofs_x = 4, .ofs_y = 2},
    {.bitmap_index = 5, .adv_w = 192, .box_w = 9, .box_h = 4, .ofs_x = 1, .ofs_y = 8},
    {.bitmap_index = 10, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 26, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 39, .adv_w = 192, .box_w = 11, .box_h = 9, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 52, .adv_w = 192, .box_w = 11, .box_h = 10, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 66, .adv_w = 192, .box_w = 5, .box_h = 5, .ofs_x = 3, .ofs_y = 8},
    {.bitmap_index = 70, .adv_w = 192, .box_w = 6, .box_h = 10, .ofs_x = 3, .ofs_y = 2},
    {.bitmap_index = 78, .adv_w = 192, .box_w = 6, .box_h = 10, .ofs_x = 3, .ofs_y = 2},
    {.bitmap_index = 86, .adv_w = 192, .box_w = 12, .box_h = 7, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 97, .adv_w = 192, .box_w = 9, .box_h = 8, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 106, .adv_w = 192, .box_w = 5, .box_h = 5, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 192, .box_w = 9, .box_h = 1, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 112, .adv_w = 192, .box_w = 3, .box_h = 3, .ofs_x = 4, .ofs_y = 2},
    {.bitmap_index = 114, .adv_w = 192, .box_w = 12, .box_h = 10, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 129, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 141, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 154, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 166, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 178, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 194, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 206, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 218, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 230, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 242, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 254, .adv_w = 192, .box_w = 3, .box_h = 9, .ofs_x = 4, .ofs_y = 2},
    {.bitmap_index = 258, .adv_w = 192, .box_w = 5, .box_h = 11, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 265, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 275, .adv_w = 192, .box_w = 9, .box_h = 4, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 280, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 290, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 302, .adv_w = 192, .box_w = 11, .box_h = 10, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 316, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 329, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 341, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 353, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 365, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 377, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 389, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 401, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 414, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 426, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 438, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 454, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 467, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 483, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 499, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 511, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 523, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 535, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 547, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 559, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 571, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 584, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 596, .adv_w = 192, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 612, .adv_w = 192, .box_w = 12, .box_h = 10, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 627, .adv_w = 192, .box_w = 12, .box_h = 10, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 642, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 654, .adv_w = 192, .box_w = 6, .box_h = 10, .ofs_x = 3, .ofs_y = 2},
    {.bitmap_index = 662, .adv_w = 192, .box_w = 12, .box_h = 10, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 677, .adv_w = 192, .box_w = 6, .box_h = 10, .ofs_x = 3, .ofs_y = 2},
    {.bitmap_index = 685, .adv_w = 192, .box_w = 11, .box_h = 6, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 694, .adv_w = 192, .box_w = 12, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 696, .adv_w = 192, .box_w = 6, .box_h = 4, .ofs_x = 4, .ofs_y = 8},
    {.bitmap_index = 699, .adv_w = 192, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 707, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 720, .adv_w = 192, .box_w = 8, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 727, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 740, .adv_w = 192, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 748, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 758, .adv_w = 192, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 769, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 782, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 792, .adv_w = 192, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 804, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 817, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 827, .adv_w = 192, .box_w = 11, .box_h = 8, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 838, .adv_w = 192, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 846, .adv_w = 192, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 854, .adv_w = 192, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 865, .adv_w = 192, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 876, .adv_w = 192, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 884, .adv_w = 192, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 892, .adv_w = 192, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 905, .adv_w = 192, .box_w = 9, .box_h = 8, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 914, .adv_w = 192, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 922, .adv_w = 192, .box_w = 11, .box_h = 8, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 933, .adv_w = 192, .box_w = 11, .box_h = 7, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 943, .adv_w = 192, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 954, .adv_w = 192, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 962, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 974, .adv_w = 192, .box_w = 3, .box_h = 10, .ofs_x = 4, .ofs_y = 2},
    {.bitmap_index = 978, .adv_w = 192, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 990, .adv_w = 192, .box_w = 11, .box_h = 3, .ofs_x = 0, .ofs_y = 9},
    {.bitmap_index = 995, .adv_w = 192, .box_w = 11, .box_h = 10, .ofs_x = 0, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 96, .glyph_id_start = 1,
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
const lv_font_t lv_font_unscii_12 = {
#else
lv_font_t lv_font_unscii_12 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 13,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
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



#endif /*#if LV_FONT_UNSCII_12*/

