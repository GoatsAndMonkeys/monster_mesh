#pragma once
// ── Gen 2 sprite renderer for ncurses ────────────────────────────────────────
// Decodes a 4-color 56×56 front (or 48×48 back) sprite from the deflate-
// compressed bundle baked by tools/gen2_sprite_bake.py and paints it into a
// curses window using Unicode upper-half-block (▀, U+2580) characters.  Each
// character cell carries two pixels: the top half is the foreground colour,
// the bottom half is the background colour, so a single column of cells = 2
// vertical sprite pixels.  The 56×56 front therefore renders as 56 columns ×
// 28 rows of cells if drawn 1:1.  We support an integer down-scale factor
// so callers can shrink to fit (e.g. /2 → 28 cols × 14 rows).
//
// Palettes are per-species (4 RGB565 entries in kGen2ColorPalettes[dex]).
// Each draw call installs those four entries into a fixed colour-slot pool
// via init_color() + init_pair(), so the foe and player can show two
// different GBC palettes side-by-side without clashing.

#include <stdint.h>
#include <ncurses.h>

namespace SpriteRender {

// Initialise the colour-slot pools (must be called once after start_color()).
// Returns true if the terminal supports init_color() and enough colour pairs.
bool init();

// Paint a sprite at (y, x) inside `win`.
//   dex          1..151 (Gen 1 dex)
//   isBack       false → front sprite, true → back (player-side)
//   scaleDiv     1, 2, 4 — integer down-scale.  /1 = native, /2 fits 56×56 in
//                28 cols × 14 rows of cells, /4 in 14 × 7.
//   slot         which palette slot to use (0 = foe / palette pool A, 1 = you
//                / palette pool B).  Lets two species coexist on screen.
//   bgPair       colour pair to paint as the "transparent" background where
//                the sprite has no opaque pixels.  Use whatever surrounds
//                the sprite box so the sprite blends into the GB paper bg.
// Pixels are sampled with nearest-neighbour stride = scaleDiv.
void drawSprite(WINDOW *win, int y, int x, int dex, bool isBack,
                int scaleDiv, int slot, short bgPair);

// Returns dimensions, in character cells, that a sprite of (dex, isBack)
// will occupy at scaleDiv.  Useful for layout.
void cellSize(bool isBack, int scaleDiv, int *outW, int *outH);

} // namespace SpriteRender
