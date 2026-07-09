#pragma once
// ── Gen2SpriteCache ──────────────────────────────────────────────────────────
// Lazy on-demand SDL texture cache for the 4bpp-indexed Gen 3 sprite blobs
// (Gen3Front4bpp.h / Gen3Back4bpp.h), with colour VARIANTS:
//   Regular, Shiny, Rainbow (animated), Dark, Dark-Shiny, Pink.
// Regular/Shiny/Dark/Dark-Shiny/Pink pick or recolour a palette and are cached
// per (dex, isBack, variant). Rainbow scrolls, so it is regenerated per frame
// into a persistent dynamic texture (front/back) rather than cached.
//
// Sprites are 64x64 native. Caller scales via the destination rect.

#include <SDL.h>
#include <stdint.h>

namespace Gen2SpriteCache {

// Colour variants — order matches the T-Deck firmware's SPR_* enum.
// Cycle order: Regular, Shiny, Pink, Rainbow, Dark, Dark-Shiny, Dark-Pink, Dark-Rainbow.
enum {
    VAR_NORMAL = 0, VAR_SHINY, VAR_PINK, VAR_RAINBOW, VAR_DARK, VAR_DARK_SHINY,
    VAR_DARK_PINK, VAR_DARK_RAINBOW,
    // Blackout = "double dark" (DD): darker than Dark. Order matches
    // breeding::Skin 8..11 so a genotype skin maps 1:1 to a VAR_.
    VAR_BLACKOUT, VAR_BLACKOUT_SHINY, VAR_BLACKOUT_PINK, VAR_BLACKOUT_RAINBOW,
    VAR_COUNT
};

void init();                 // clear the cache; safe to call repeatedly
void clear();                // destroy every cached/dynamic texture
void dims(bool isBack, int *outW, int *outH);   // 64x64

// Fetch a texture for (dex, isBack, variant). `phase` (0..359) animates the
// Rainbow variant; ignored otherwise. Returns nullptr on failure/invalid dex.
SDL_Texture *get(SDL_Renderer *renderer, int dex, bool isBack,
                 int variant = VAR_NORMAL, uint16_t phase = 0);

}  // namespace Gen2SpriteCache
