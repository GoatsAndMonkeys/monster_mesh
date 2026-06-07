#pragma once
// ── Gen2SpriteCache ──────────────────────────────────────────────────────────
// Lazy on-demand SDL texture cache for the deflate-compressed 4-color Gen 2
// (Crystal) sprite blobs.  First time a (dex, isBack) pair is requested:
//   1. puff()-decode the 2bpp packed buffer
//   2. expand to RGBA32, painting palette index 0 as fully transparent and
//      indexes 1..3 with the species's RGB565 -> RGB888 entries
//   3. upload to an SDL_Texture and return it
// Cache hits skip all of that and just return the cached texture.
//
// Front sprites are 56x56, back sprites are 48x48.  Caller scales via the
// destination rect in SDL_RenderCopy.

#include <SDL.h>
#include <stdint.h>

namespace Gen2SpriteCache {

// Initialise — clears the cache.  Safe to call multiple times.
void init();

// Throw away every cached texture (call before destroying the renderer).
void clear();

// Width/height of the texture this dex+isBack pair will produce.
// (56 front, 48 back.)
void dims(bool isBack, int *outW, int *outH);

// Fetch a texture for (dex, isBack) — decodes + uploads on first call,
// caches forever.  Returns nullptr on decode failure or invalid dex.
// `renderer` is required for first-time creation; cache hits ignore it.
SDL_Texture *get(SDL_Renderer *renderer, int dex, bool isBack);

}  // namespace Gen2SpriteCache
