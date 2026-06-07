// test_gen2_sprite.cpp
//
// Round-trip verifier for the Gen-2 sprite headers produced by
// tools/gen2_sprite_bake.py. Decodes Pikachu (dex 25) front + back via puff()
// and writes PPM (P6) snapshots so we can eyeball them.
//
// Build:
//   g++ -std=c++17 tools/test_gen2_sprite.cpp \
//       /Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware/src/modules/pentest/puff.c \
//       -o /tmp/test_gen2_sprite
// Run:
//   /tmp/test_gen2_sprite
//
// On success, two files appear:
//   /tmp/gen2_pikachu_front.ppm  (56x56)
//   /tmp/gen2_pikachu_back.ppm   (48x48)

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include "../src/terminal/puff.h"
}

#include "../src/terminal/Gen2ColorIcons.h"
#include "../src/terminal/Gen2BackIcons.h"

static void rgb565_to_rgb888(uint16_t c, uint8_t out[3])
{
    uint8_t r5 = (c >> 11) & 0x1F;
    uint8_t g6 = (c >> 5)  & 0x3F;
    uint8_t b5 =  c        & 0x1F;
    out[0] = (r5 << 3) | (r5 >> 2);
    out[1] = (g6 << 2) | (g6 >> 4);
    out[2] = (b5 << 3) | (b5 >> 2);
}

static bool decode_front(uint8_t dex, std::vector<uint8_t>& out2bpp)
{
    uint32_t start = kGen2ColorDeflateOffsets[dex];
    uint32_t end   = kGen2ColorDeflateOffsets[dex + 1];
    if (end <= start) {
        fprintf(stderr, "front dex %u: empty\n", dex);
        return false;
    }
    out2bpp.assign((size_t)GEN2_COLOR_W * GEN2_COLOR_H / 4, 0);
    unsigned long dlen = out2bpp.size();
    unsigned long slen = end - start;
    int rc = puff(out2bpp.data(), &dlen, &kGen2ColorDeflate[start], &slen);
    if (rc != 0) { fprintf(stderr, "front dex %u: puff rc=%d\n", dex, rc); return false; }
    if (dlen != out2bpp.size()) {
        fprintf(stderr, "front dex %u: decoded %lu, want %zu\n", dex, dlen, out2bpp.size());
        return false;
    }
    return true;
}

static bool decode_back(uint8_t dex, std::vector<uint8_t>& out2bpp)
{
    uint32_t start = kGen2BackDeflateOffsets[dex];
    uint32_t end   = kGen2BackDeflateOffsets[dex + 1];
    if (end <= start) {
        fprintf(stderr, "back dex %u: empty\n", dex);
        return false;
    }
    out2bpp.assign((size_t)GEN2_BACK_W * GEN2_BACK_H / 4, 0);
    unsigned long dlen = out2bpp.size();
    unsigned long slen = end - start;
    int rc = puff(out2bpp.data(), &dlen, &kGen2BackDeflate[start], &slen);
    if (rc != 0) { fprintf(stderr, "back dex %u: puff rc=%d\n", dex, rc); return false; }
    if (dlen != out2bpp.size()) {
        fprintf(stderr, "back dex %u: decoded %lu, want %zu\n", dex, dlen, out2bpp.size());
        return false;
    }
    return true;
}

static bool write_ppm(const char* path,
                      const std::vector<uint8_t>& bits,
                      const uint16_t pal[4],
                      uint16_t w, uint16_t h)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) { perror(path); return false; }
    fprintf(fp, "P6\n%u %u\n255\n", w, h);
    for (uint16_t y = 0; y < h; ++y) {
        for (uint16_t x = 0; x < w; ++x) {
            size_t bit  = (size_t)y * w + x;
            size_t byte = bit / 4;
            uint8_t sh  = (uint8_t)(6 - 2 * (bit % 4));
            uint8_t idx = (bits[byte] >> sh) & 0x3;
            uint8_t rgb[3];
            rgb565_to_rgb888(pal[idx], rgb);
            fwrite(rgb, 1, 3, fp);
        }
    }
    fclose(fp);
    return true;
}

int main()
{
    constexpr uint8_t DEX = 25;  // Pikachu

    std::vector<uint8_t> front, back;
    if (!decode_front(DEX, front)) return 1;
    if (!decode_back(DEX, back))   return 2;

    const char* fp = "/tmp/gen2_pikachu_front.ppm";
    const char* bp = "/tmp/gen2_pikachu_back.ppm";
    if (!write_ppm(fp, front, kGen2ColorPalettes[DEX], GEN2_COLOR_W, GEN2_COLOR_H)) return 3;
    if (!write_ppm(bp, back,  kGen2BackPalettes[DEX],  GEN2_BACK_W,  GEN2_BACK_H))  return 4;

    printf("OK\n");
    printf("front: %s  (%ux%u)\n", fp, GEN2_COLOR_W, GEN2_COLOR_H);
    printf("back : %s  (%ux%u)\n", bp, GEN2_BACK_W, GEN2_BACK_H);

    // Print palette for sanity.
    printf("front pal:");
    for (int i = 0; i < 4; ++i) printf(" %04X", kGen2ColorPalettes[DEX][i]);
    printf("\n back pal:");
    for (int i = 0; i < 4; ++i) printf(" %04X", kGen2BackPalettes[DEX][i]);
    printf("\n");
    return 0;
}
