#include "Gen2SpriteCache.h"
#include "Gen2ColorIcons.h"
#include "Gen2BackIcons.h"

extern "C" {
#include "puff.h"
}

#include <string.h>
#include <stdlib.h>

namespace Gen2SpriteCache {

// Two tables, one for each direction.  152 slots (0..151) — slot 0 unused
// to match the dex numbering used by the palette tables.
static SDL_Texture *s_front[152] = {};
static SDL_Texture *s_back [152] = {};

void init() {
    for (int i = 0; i < 152; i++) { s_front[i] = nullptr; s_back[i] = nullptr; }
}

void clear() {
    for (int i = 0; i < 152; i++) {
        if (s_front[i]) { SDL_DestroyTexture(s_front[i]); s_front[i] = nullptr; }
        if (s_back [i]) { SDL_DestroyTexture(s_back [i]); s_back [i] = nullptr; }
    }
}

void dims(bool isBack, int *outW, int *outH) {
    if (outW) *outW = isBack ? GEN2_BACK_W : GEN2_COLOR_W;
    if (outH) *outH = isBack ? GEN2_BACK_H : GEN2_COLOR_H;
}

// Decode the deflate blob for one species into a 2bpp buffer.
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

// 2-bit pixel access at (row, col) inside a packed 2bpp buffer of width w.
static inline uint8_t pixel2bpp(const uint8_t *buf, int w, int row, int col) {
    int idx   = row * w + col;
    int byte  = idx >> 2;
    int shift = 6 - ((idx & 3) * 2);
    return (buf[byte] >> shift) & 0x03;
}

// RGB565 -> RGBA8888 (alpha = 0xFF).
static inline uint32_t rgb565to8888(uint16_t v) {
    uint8_t r5 = (v >> 11) & 0x1F;
    uint8_t g6 = (v >> 5)  & 0x3F;
    uint8_t b5 = (v)       & 0x1F;
    uint8_t r = (uint8_t)((r5 * 255 + 15) / 31);
    uint8_t g = (uint8_t)((g6 * 255 + 31) / 63);
    uint8_t b = (uint8_t)((b5 * 255 + 15) / 31);
    // SDL_PIXELFORMAT_RGBA32 is byte-order R,G,B,A regardless of endian.
    return ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)0xFF << 24);
}

static SDL_Texture *buildTexture(SDL_Renderer *renderer, int dex, bool isBack) {
    if (!renderer) return nullptr;
    if (dex < 1 || dex > 151) return nullptr;

    int w = isBack ? GEN2_BACK_W  : GEN2_COLOR_W;
    int h = isBack ? GEN2_BACK_H  : GEN2_COLOR_H;

    // Decode 2bpp packed buffer.
    uint8_t buf[GEN2_COLOR_W * GEN2_COLOR_H / 4];  // 784 B — covers both sizes
    size_t  need = (size_t)(w * h + 3) / 4;
    if (!decodeSprite(dex, isBack, buf, need)) return nullptr;

    // Per-species palette: index 0 -> transparent, 1..3 -> RGB565 entries.
    const uint16_t *pal = isBack ? kGen2BackPalettes[dex]
                                 : kGen2ColorPalettes[dex];
    uint32_t rgba[4];
    rgba[0] = 0;                              // transparent
    rgba[1] = rgb565to8888(pal[1]);
    rgba[2] = rgb565to8888(pal[2]);
    rgba[3] = rgb565to8888(pal[3]);

    // Expand 2bpp -> RGBA32.
    uint32_t *pixels = (uint32_t *)malloc(sizeof(uint32_t) * w * h);
    if (!pixels) return nullptr;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t idx = pixel2bpp(buf, w, y, x);
            pixels[y * w + x] = rgba[idx];
        }
    }

    SDL_Texture *tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         w, h);
    if (!tex) {
        free(pixels);
        return nullptr;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, nullptr, pixels, w * 4);
    free(pixels);
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
    if (dex < 1 || dex > 151) return nullptr;
    SDL_Texture **slot = isBack ? &s_back[dex] : &s_front[dex];
    if (*slot) return *slot;
    *slot = buildTexture(renderer, dex, isBack);
    return *slot;
}

}  // namespace Gen2SpriteCache
