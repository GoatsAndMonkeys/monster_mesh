#pragma once
// ── BitmapFont ───────────────────────────────────────────────────────────────
// Hand-rolled 5x7 ASCII bitmap font for the SDL2 battle window.  Covers
// printable ASCII 0x20..0x7E.  Glyphs are stored as 7 bytes per character,
// each byte holding 5 pixels in bits 4..0 (bit 4 = leftmost column).
//
// Rendering pumps the glyph through a single integer scale factor — 2x is
// the recommended default (effective 10x14 px at 640x480), 3x for headings.
// Background is fully transparent; only "on" pixels are drawn.
//
// No external font file, no TTF dependency — keeps the cross-compile to the
// Pi clean.  License: MIT, hand-authored for this project.

#include <SDL.h>
#include <stdint.h>

namespace BitmapFont {

// 7 rows per glyph, leftmost column = bit 4 (0x10), rightmost = bit 0 (0x01).
// Index by (ascii - 0x20).
extern const uint8_t kGlyphs[96][7];

static constexpr int GLYPH_W = 5;
static constexpr int GLYPH_H = 7;
static constexpr int GLYPH_ADVANCE = 6;   // 5 px glyph + 1 px gap

// Draw a single character at (x, y) with the given color and integer scale.
// Pixels are emitted as filled SDL rectangles.  Returns the post-glyph
// advance (in destination pixels) — i.e., where to draw the next char.
int drawChar(SDL_Renderer *r, int x, int y, char ch, SDL_Color fg, int scale);

// Draw a NUL-terminated string left-to-right.  Stops at NUL.  Returns the
// total destination width consumed.
int drawString(SDL_Renderer *r, int x, int y, const char *s, SDL_Color fg, int scale);

// Width in destination pixels of `s` rendered at `scale` — caller can use
// this for centering / right-alignment.
int stringWidth(const char *s, int scale);

}  // namespace BitmapFont
