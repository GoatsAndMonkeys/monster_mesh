#include "SpriteRender.h"
#include "Gen2ColorIcons.h"
#include "Gen2BackIcons.h"

extern "C" {
#include "puff.h"
}

#include <string.h>
#include <stdlib.h>

namespace SpriteRender {

// ── Colour-slot allocation ───────────────────────────────────────────────────
// We carve a small pool out of the high colour indices.  Two palette pools
// (A and B) of 4 colours each, so foe and player can show different species.
// Above those, 16 colour pairs per pool covering every (top, bottom) palette-
// index combination.
//
// Slot layout (pool A = foe, pool B = player):
//   colours 32..35  : pool A palette entries 0..3
//   colours 36..39  : pool B palette entries 0..3
//   pairs   100..115: pool A pairs (top<<2 | bottom)
//   pairs   116..131: pool B pairs
// init() is idempotent; the host may call it more than once safely.

static constexpr short COLOR_POOL_A_BASE = 32;
static constexpr short COLOR_POOL_B_BASE = 36;
static constexpr short PAIR_POOL_A_BASE  = 100;
static constexpr short PAIR_POOL_B_BASE  = 116;

static bool s_initOk = false;

bool init() {
    if (s_initOk) return true;
    if (!has_colors()) return false;
    // Require at least 40 colour slots and 132 pairs (we use 100..131).
    if (COLORS < 40 || COLOR_PAIRS < 132) return false;
    if (!can_change_color()) return false;
    s_initOk = true;
    return true;
}

void cellSize(bool isBack, int scaleDiv, int *outW, int *outH) {
    int pxW = isBack ? GEN2_BACK_W : GEN2_COLOR_W;
    int pxH = isBack ? GEN2_BACK_H : GEN2_COLOR_H;
    if (scaleDiv < 1) scaleDiv = 1;
    int sampW = pxW / scaleDiv;
    int sampH = pxH / scaleDiv;
    // Two pixels per cell vertically (upper half-block); horizontally 1:1.
    if (outW) *outW = sampW;
    if (outH) *outH = (sampH + 1) / 2;
}

// Convert RGB565 to ncurses 0-1000 per-channel.
static void rgb565ToNc(uint16_t v, short *r, short *g, short *b) {
    int r5 = (v >> 11) & 0x1F;
    int g6 = (v >> 5)  & 0x3F;
    int b5 = (v)       & 0x1F;
    *r = (short)((r5 * 1000) / 31);
    *g = (short)((g6 * 1000) / 63);
    *b = (short)((b5 * 1000) / 31);
}

// Decode the deflate blob for one species into a 2bpp buffer.
// Caller supplies dest sized GEN2_COLOR_W*GEN2_COLOR_H/4 (front) or
// GEN2_BACK_W*GEN2_BACK_H/4 (back).  Returns true on success.
static bool decodeSprite(int dex, bool isBack, uint8_t *out, size_t outCap) {
    if (dex < 1 || dex > 151) return false;
    const uint32_t *offsets = isBack ? kGen2BackDeflateOffsets
                                     : kGen2ColorDeflateOffsets;
    const uint8_t  *blob    = isBack ? kGen2BackDeflate
                                     : kGen2ColorDeflate;
    uint32_t start = offsets[dex];
    uint32_t end   = offsets[dex + 1];
    if (end <= start) return false;
    unsigned long destlen = outCap;
    unsigned long srclen  = end - start;
    int rc = puff(out, &destlen, blob + start, &srclen);
    return rc == 0;
}

// Read the 2-bit palette index for pixel (row, col) out of a packed 2bpp
// buffer of width `w`.
static inline uint8_t pixel2bpp(const uint8_t *buf, int w, int row, int col) {
    int idx   = row * w + col;
    int byte  = idx >> 2;
    int shift = 6 - ((idx & 3) * 2);
    return (buf[byte] >> shift) & 0x03;
}

void drawSprite(WINDOW *win, int y, int x, int dex, bool isBack,
                int scaleDiv, int slot, short bgPair) {
    if (!s_initOk) return;
    if (dex < 1 || dex > 151) return;
    if (scaleDiv < 1) scaleDiv = 1;

    // Decode this species's 2bpp bitmap into a stack buffer.
    uint8_t buf[GEN2_COLOR_W * GEN2_COLOR_H / 4];  // 784 B (front) or smaller
    int pxW = isBack ? GEN2_BACK_W : GEN2_COLOR_W;
    int pxH = isBack ? GEN2_BACK_H : GEN2_COLOR_H;
    size_t need = (pxW * pxH + 3) / 4;
    if (!decodeSprite(dex, isBack, buf, need)) return;

    // Install this species's palette into the requested pool, then build
    // the 16 colour pairs covering every (top, bottom) palette-index combo.
    const uint16_t *pal = isBack ? kGen2BackPalettes[dex]
                                 : kGen2ColorPalettes[dex];
    short colBase  = (slot == 0) ? COLOR_POOL_A_BASE : COLOR_POOL_B_BASE;
    short pairBase = (slot == 0) ? PAIR_POOL_A_BASE  : PAIR_POOL_B_BASE;
    for (int i = 0; i < 4; i++) {
        short r, g, b;
        rgb565ToNc(pal[i], &r, &g, &b);
        init_color(colBase + i, r, g, b);
    }
    for (int top = 0; top < 4; top++) {
        for (int bot = 0; bot < 4; bot++) {
            init_pair(pairBase + (top << 2) + bot,
                      colBase + top, colBase + bot);
        }
    }

    // Paint cells.  Each row of cells covers 2 vertical pixels (sampled with
    // stride = scaleDiv).  Top pixel → foreground (pair top half) via the
    // ▀ glyph, which draws fg in its top half and bg in its bottom half.
    int sampW = pxW / scaleDiv;
    int sampH = pxH / scaleDiv;
    for (int cy = 0; cy * 2 < sampH; cy++) {
        for (int cx = 0; cx < sampW; cx++) {
            int srcCol = cx * scaleDiv;
            int srcRowTop = (cy * 2)     * scaleDiv;
            int srcRowBot = (cy * 2 + 1) * scaleDiv;
            uint8_t topIdx = pixel2bpp(buf, pxW, srcRowTop, srcCol);
            uint8_t botIdx = (srcRowBot < pxH)
                ? pixel2bpp(buf, pxW, srcRowBot, srcCol) : 0;
            // Palette index 0 is the "lightest"/background colour (matted
            // to white by the bake step).  Where both halves are bg, paint
            // the surrounding bgPair so the sprite reads as cut-out art on
            // the GB paper background instead of a hard rectangle.
            if (topIdx == 0 && botIdx == 0) {
                wattron(win, COLOR_PAIR(bgPair));
                mvwaddch(win, y + cy, x + cx, ' ');
                wattroff(win, COLOR_PAIR(bgPair));
                continue;
            }
            short p = pairBase + (topIdx << 2) + botIdx;
            wattron(win, COLOR_PAIR(p));
            // Upper-half-block: top half of cell = fg, bottom = bg.
            // Use a Unicode literal; ncurses' addch can render UTF-8 if the
            // locale was set (we do this in TerminalUI::startup()).
            mvwaddstr(win, y + cy, x + cx, "\xe2\x96\x80"); // U+2580
            wattroff(win, COLOR_PAIR(p));
        }
    }
}

} // namespace SpriteRender
