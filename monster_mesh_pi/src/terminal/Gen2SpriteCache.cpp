#include "Gen2SpriteCache.h"
#include "Gen3ColorIcons.h"  // battle station now uses Gen 3 (Emerald/FRLG) art
#include "Gen3BackIcons.h"

extern "C" {
#include "puff.h"
}

#include <string.h>
#include <stdlib.h>

namespace Gen2SpriteCache {

// Two tables, one per direction. 387 slots (0..386) — slot 0 unused,
// to match the dex numbering used by the palette tables.
static SDL_Texture *s_front[387] = {};
static SDL_Texture *s_back [387] = {};

void init() {
    for (int i = 0; i < 387; i++) { s_front[i] = nullptr; s_back[i] = nullptr; }
}

void clear() {
    for (int i = 0; i < 387; i++) {
        if (s_front[i]) { SDL_DestroyTexture(s_front[i]); s_front[i] = nullptr; }
        if (s_back [i]) { SDL_DestroyTexture(s_back [i]); s_back [i] = nullptr; }
    }
}

void dims(bool isBack, int *outW, int *outH) {
    if (outW) *outW = isBack ? GEN3_BACK_W : GEN3_COLOR_W;
    if (outH) *outH = isBack ? GEN3_BACK_H : GEN3_COLOR_H;
}

// Decode the deflate blob for one species into a full-color RGBA (w*h*4) buffer.
static bool decodeSprite(int dex, bool isBack, uint8_t *out, size_t outCap) {
    if (dex < 1 || dex > 386) return false;
    const uint32_t *offsets = isBack ? kGen3BackDeflateOffsets
                                     : kGen3ColorDeflateOffsets;
    const uint8_t  *blob    = isBack ? kGen3BackDeflate
                                     : kGen3ColorDeflate;
    uint32_t start = offsets[dex];
    uint32_t end   = offsets[dex + 1];
    if (end <= start) return false;
    unsigned long destlen = outCap;
    unsigned long srclen  = end - start;
    int rc = puff(out, &destlen, blob + start, &srclen);
    return rc == 0;
}

static SDL_Texture *buildTexture(SDL_Renderer *renderer, int dex, bool isBack) {
    if (!renderer) return nullptr;
    if (dex < 1 || dex > 386) return nullptr;

    int w = isBack ? GEN3_BACK_W  : GEN3_COLOR_W;
    int h = isBack ? GEN3_BACK_H  : GEN3_COLOR_H;

    // Decode the full-color RGBA32 pixel buffer (w*h*4 bytes, R,G,B,A per pixel;
    // A=0 = transparent background). The decoded bytes are already in
    // SDL_PIXELFORMAT_RGBA32 order, so we hand them straight to the texture.
    static uint8_t buf[GEN3_COLOR_W * GEN3_COLOR_H * 4];  // 16384 B — 64x64 RGBA, covers front+back
    size_t  need = (size_t)(w * h * 4);
    if (!decodeSprite(dex, isBack, buf, need)) return nullptr;

    SDL_Texture *tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         w, h);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, nullptr, buf, w * 4);
    // Nearest-neighbour scaling so the upscaled (4x-8x) destination stays
    // crunchy GBC-pixel sharp instead of bilinear-blurred.  Also set globally
    // via SDL_HINT_RENDER_SCALE_QUALITY before renderer creation as a fallback
    // for SDL builds where SDL_SetTextureScaleMode is absent (< 2.0.12 — e.g.
    // Raspbian Buster ships 2.0.10).  The global hint is sufficient on its
    // own; this per-texture call is belt-and-suspenders for newer SDL.
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif
    return tex;
}

SDL_Texture *get(SDL_Renderer *renderer, int dex, bool isBack) {
    if (dex < 1 || dex > 386) return nullptr;
    SDL_Texture **slot = isBack ? &s_back[dex] : &s_front[dex];
    if (*slot) return *slot;
    *slot = buildTexture(renderer, dex, isBack);
    return *slot;
}

}  // namespace Gen2SpriteCache
