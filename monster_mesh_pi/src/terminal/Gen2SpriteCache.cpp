#include "Gen2SpriteCache.h"
#include "Gen3Front4bpp.h"   // 4bpp indexed Gen-3 front sprites + normal/shiny palettes
#include "Gen3Back4bpp.h"    // 4bpp indexed Gen-3 back sprites + normal/shiny palettes

extern "C" {
#include "puff.h"
}

#include <math.h>
#include <string.h>
#include <stdlib.h>

namespace Gen2SpriteCache {

// Static cache: [variant][dir][dex]. Rainbow (animated) is NOT cached here.
static SDL_Texture *s_cache[VAR_COUNT][2][387] = {};
// Dynamic rainbow texture, one per direction, regenerated per frame.
static SDL_Texture *s_dyn[2]   = {};
static int          s_dynDex[2] = { -1, -1 };

static uint8_t s_gammaLut[256];
static bool    s_gammaReady = false;
static void initGamma() {
    for (int i = 0; i < 256; ++i) {
        float v = powf(i / 255.0f, 2.1f) * 255.0f + 0.5f;
        s_gammaLut[i] = (uint8_t)(v > 255.0f ? 255 : (int)v);
    }
    s_gammaReady = true;
}

void init() {
    memset(s_cache, 0, sizeof(s_cache));
    s_dyn[0] = s_dyn[1] = nullptr; s_dynDex[0] = s_dynDex[1] = -1;
    if (!s_gammaReady) initGamma();
}

void clear() {
    for (int v = 0; v < VAR_COUNT; ++v)
        for (int d = 0; d < 2; ++d)
            for (int i = 0; i < 387; ++i)
                if (s_cache[v][d][i]) { SDL_DestroyTexture(s_cache[v][d][i]); s_cache[v][d][i] = nullptr; }
    for (int d = 0; d < 2; ++d) {
        if (s_dyn[d]) { SDL_DestroyTexture(s_dyn[d]); s_dyn[d] = nullptr; }
        s_dynDex[d] = -1;
    }
}

void dims(bool isBack, int *outW, int *outH) {
    if (outW) *outW = isBack ? GEN3_BACK_4BPP_W : GEN3_FRONT_4BPP_W;
    if (outH) *outH = isBack ? GEN3_BACK_4BPP_H : GEN3_FRONT_4BPP_H;
}

// ── luma-preserving recolour helpers (operate on 0..255 RGB) ─────────────────
static inline uint8_t luma(int r, int g, int b) { return (uint8_t)((77*r + 150*g + 29*b) >> 8); }

static void hueLuma(int deg, int sat /*0..255*/, int Y, uint8_t &or_, uint8_t &og, uint8_t &ob) {
    float h = (deg % 360) / 60.0f; int i = (int)h; float f = h - i; float s = sat / 255.0f;
    const float v = 255.0f;
    float p = v*(1-s), q = v*(1-s*f), t = v*(1-s*(1-f)); float r, g, b;
    switch (i % 6) {
        case 0: r=v; g=t; b=p; break;  case 1: r=q; g=v; b=p; break;  case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;  case 4: r=t; g=p; b=v; break;  default: r=v; g=p; b=q; break;
    }
    int yb = (77*(int)r + 150*(int)g + 29*(int)b) >> 8; if (yb < 1) yb = 1;
    float sc = (float)Y / yb;
    int R=(int)(r*sc), G=(int)(g*sc), B=(int)(b*sc);
    or_ = R>255?255:R; og = G>255?255:G; ob = B>255?255:B;
}

static inline int satLite(int Y, int baseSat) {
    if (Y <= 175) return baseSat;
    int pct = 90 * (Y - 175) / 80;               // 0..90
    int s = baseSat - baseSat * pct / 100;
    return s < 15 ? 15 : s;
}

// True red hue? (matches firmware Dark gate: sat>=0.45, hue in [-18deg,+10deg])
static bool isRed(int r, int g, int b) {
    int mx = r>g ? (r>b?r:b) : (g>b?g:b);
    int mn = r<g ? (r<b?r:b) : (g<b?g:b);
    int chroma = mx - mn;
    if (mx == 0 || mx != r || chroma*100 < 45*mx) return false;   // red-dominant, sat>=0.45
    int gb = g - b;
    return gb*6 <= chroma && (b-g)*10 <= 3*chroma;                // hue in [-18,+10]
}

// Apply a variant to one opaque pixel (r,g,b in/out). i is the pixel index.
static void applyVariant(int variant, int i, int w, int h, uint16_t phase,
                         uint8_t &r, uint8_t &g, uint8_t &b) {
    switch (variant) {
        case VAR_NORMAL: case VAR_SHINY: default:
            break;                                    // palette already chosen
        case VAR_RAINBOW: {
            int d = (i / w) + (i % w);
            int hue = ((d * 306) / (2 * (h - 1)) + phase * 20) % 360;
            uint8_t Y = luma(r, g, b);
            hueLuma(hue, satLite(Y, 242), Y, r, g, b);
            break;
        }
        case VAR_PINK: {
            uint8_t Y = luma(r, g, b);
            hueLuma(329, satLite(Y, 217), Y, r, g, b);
            break;
        }
        case VAR_DARK: case VAR_DARK_SHINY: {
            if (isRed(r, g, b)) break;                // keep the red
            uint8_t y = s_gammaLut[luma(r, g, b)];
            r = g = b = y;
            break;
        }
        case VAR_DARK_PINK: {
            if (isRed(r, g, b)) {                     // red -> pink, keep brightness
                uint8_t Y = luma(r, g, b);
                hueLuma(329, satLite(Y, 217), Y, r, g, b);
            } else {
                uint8_t y = s_gammaLut[luma(r, g, b)];
                r = g = b = y;
            }
            break;
        }
        case VAR_DARK_RAINBOW: {
            if (isRed(r, g, b)) {                     // red -> animated rainbow glow
                int d = (i / w) + (i % w);
                int hue = ((d * 306) / (2 * (h - 1)) + phase * 20) % 360;
                uint8_t Y = luma(r, g, b);
                hueLuma(hue, satLite(Y, 242), Y, r, g, b);
            } else {
                uint8_t y = s_gammaLut[luma(r, g, b)];
                r = g = b = y;
            }
            break;
        }
        // ── Blackout ("double dark", DD) — much darker than Dark. Everything
        // collapses to a near-black grayscale silhouette (~40% luma); the
        // Pink/Rainbow trait still tints the red pixels but heavily dimmed.
        case VAR_BLACKOUT: case VAR_BLACKOUT_SHINY: {
            uint8_t y = (uint8_t)(s_gammaLut[luma(r, g, b)] * 2 / 5);
            r = g = b = y;
            break;
        }
        case VAR_BLACKOUT_PINK: {
            if (isRed(r, g, b)) {
                uint8_t Y = (uint8_t)(luma(r, g, b) * 2 / 5);
                hueLuma(329, satLite(Y, 217), Y, r, g, b);
            } else {
                r = g = b = (uint8_t)(s_gammaLut[luma(r, g, b)] * 2 / 5);
            }
            break;
        }
        case VAR_BLACKOUT_RAINBOW: {
            if (isRed(r, g, b)) {
                int d = (i / w) + (i % w);
                int hue = ((d * 306) / (2 * (h - 1)) + phase * 20) % 360;
                uint8_t Y = (uint8_t)(luma(r, g, b) * 2 / 5);
                hueLuma(hue, satLite(Y, 242), Y, r, g, b);
            } else {
                r = g = b = (uint8_t)(s_gammaLut[luma(r, g, b)] * 2 / 5);
            }
            break;
        }
    }
}

// Decode one sprite into RGBA32 with the variant applied. `phase` animates Rainbow.
static bool decodeVariant(int dex, bool isBack, int variant, uint16_t phase,
                          uint8_t *out, int w, int h) {
    if (dex < 1 || dex > 386) return false;
    const uint32_t *offs = isBack ? kGen3Back4bppOffsets  : kGen3Front4bppOffsets;
    const uint8_t  *blob = isBack ? kGen3Back4bppDeflate  : kGen3Front4bppDeflate;
    bool useShiny = (variant == VAR_SHINY || variant == VAR_DARK_SHINY ||
                     variant == VAR_BLACKOUT_SHINY);
    const uint16_t *pal = isBack
        ? (useShiny ? kGen3BackPalShiny[dex]  : kGen3BackPalNormal[dex])
        : (useShiny ? kGen3FrontPalShiny[dex] : kGen3FrontPalNormal[dex]);
    uint32_t start = offs[dex], end = offs[dex + 1];
    if (end <= start) return false;
    static uint8_t idxbuf[GEN3_FRONT_4BPP_W * GEN3_FRONT_4BPP_H / 2];   // 2048 B
    unsigned long destlen = (unsigned long)(w * h / 2), srclen = end - start;
    if (puff(idxbuf, &destlen, blob + start, &srclen) != 0) return false;

    for (int i = 0; i < w * h; ++i) {
        uint8_t idx = (i & 1) ? (idxbuf[i >> 1] & 0x0F) : (idxbuf[i >> 1] >> 4);
        if (idx == 0) { out[i*4]=0; out[i*4+1]=0; out[i*4+2]=0; out[i*4+3]=0; continue; }
        uint16_t c = pal[idx];
        int r = ((c >> 11) & 0x1F) << 3;  r |= r >> 5;    // 5->8 bit expand
        int g = ((c >> 5)  & 0x3F) << 2;  g |= g >> 6;
        int b = (c & 0x1F) << 3;          b |= b >> 5;
        uint8_t rr = (uint8_t)r, gg = (uint8_t)g, bb = (uint8_t)b;
        applyVariant(variant, i, w, h, phase, rr, gg, bb);
        out[i*4] = rr; out[i*4+1] = gg; out[i*4+2] = bb; out[i*4+3] = 255;
    }
    return true;
}

static SDL_Texture *makeTex(SDL_Renderer *renderer, const uint8_t *buf, int w, int h) {
    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, nullptr, buf, w * 4);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif
    return tex;
}

SDL_Texture *get(SDL_Renderer *renderer, int dex, bool isBack, int variant, uint16_t phase) {
    if (dex < 1 || dex > 386) return nullptr;
    if (variant < 0 || variant >= VAR_COUNT) variant = VAR_NORMAL;
    if (!s_gammaReady) initGamma();
    int w = isBack ? GEN3_BACK_4BPP_W : GEN3_FRONT_4BPP_W;
    int h = isBack ? GEN3_BACK_4BPP_H : GEN3_FRONT_4BPP_H;
    static uint8_t buf[GEN3_FRONT_4BPP_W * GEN3_FRONT_4BPP_H * 4];      // 16384 B

    // Rainbow / Dark-Rainbow / Blackout-Rainbow scroll → regenerate each frame
    // into a persistent dynamic texture (one per direction). Decoded with the
    // actual variant so the Dark/Blackout darkening is applied to the glow.
    if (variant == VAR_RAINBOW || variant == VAR_DARK_RAINBOW ||
        variant == VAR_BLACKOUT_RAINBOW) {
        if (!renderer) return nullptr;
        int d = isBack ? 1 : 0;
        if (s_dyn[d] && s_dynDex[d] != dex) { SDL_DestroyTexture(s_dyn[d]); s_dyn[d] = nullptr; }
        if (!decodeVariant(dex, isBack, variant, phase, buf, w, h)) return nullptr;
        if (!s_dyn[d]) {
            s_dyn[d] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING, w, h);
            if (!s_dyn[d]) return nullptr;
            SDL_SetTextureBlendMode(s_dyn[d], SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_SetTextureScaleMode(s_dyn[d], SDL_ScaleModeNearest);
#endif
            s_dynDex[d] = dex;
        }
        SDL_UpdateTexture(s_dyn[d], nullptr, buf, w * 4);
        return s_dyn[d];
    }

    // Static variants → lazy per (variant, dir, dex) cache.
    SDL_Texture **slot = &s_cache[variant][isBack ? 1 : 0][dex];
    if (*slot) return *slot;
    if (!renderer) return nullptr;
    if (!decodeVariant(dex, isBack, variant, 0, buf, w, h)) return nullptr;
    *slot = makeTex(renderer, buf, w, h);
    return *slot;
}

}  // namespace Gen2SpriteCache
