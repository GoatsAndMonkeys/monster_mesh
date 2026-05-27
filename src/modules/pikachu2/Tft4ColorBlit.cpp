#include "Tft4ColorBlit.h"

#if HAS_SCREEN && defined(USE_ST7789) && !defined(ARCH_ESP32)

#include <Arduino.h>
#include <SPI.h>
#include "variant.h"   // pulls in ST7789_NSS / ST7789_RS pin numbers

// The bundled meshtastic-st7789 library hardcodes these. We mirror them so
// our raw SPI burst doesn't fight the lib's transactions.
//   - SPI bus:  global SPI1 (declared in graphics/Screen.cpp on ESP_PLATFORM,
//                provided by the Arduino-nRF52 core elsewhere)
//   - Clock:    40 MHz, MSBFIRST, MODE0
//   - CASET/RASET window offset: the lib centres the visible 240x135 region
//                 inside the 320x240 controller frame (x+=40, y+=52).
//
// On the T114, MADCTL is normally re-flipped by Screen::handleSetup() to
// MV|MX which means "landscape with X-mirror". The offset math below mirrors
// what the lib's setAddrWindow() does — so coordinates passed in here are the
// SAME screen-space pixels you'd get with display->setPixel(x, y) elsewhere.
//
// SPI1 is declared extern in the Arduino-nRF52 core's SPI.h (line 108), so
// just including <SPI.h> exposes the symbol — no extra extern needed here.

namespace {

// ST77XX command opcodes (lifted from the lib so we don't depend on private
// headers).
constexpr uint8_t ST77XX_CASET = 0x2A;
constexpr uint8_t ST77XX_RASET = 0x2B;
constexpr uint8_t ST77XX_RAMWR = 0x2C;

// 320x240 → 240x135 centring offsets (same as ST7789Spi::setAddrWindow)
constexpr uint16_t COL_OFFSET = (320 - 240) / 2;  // 40
constexpr uint16_t ROW_OFFSET = (240 - 135) / 2;  // 52

// Match the lib's SPISettings exactly so back-to-back transactions stay sane.
const SPISettings kSpiSettings(40000000, MSBFIRST, SPI_MODE0);

inline void dcCmd()  { digitalWrite(ST7789_RS, LOW); }
inline void dcData() { digitalWrite(ST7789_RS, HIGH); }
inline void csLow()  { digitalWrite(ST7789_NSS, LOW); }
inline void csHigh() { digitalWrite(ST7789_NSS, HIGH); }

inline void writeCmd(uint8_t c) {
    dcCmd();
    SPI1.transfer(c);
    dcData();
}

inline void writeAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    x += COL_OFFSET;
    y += ROW_OFFSET;
    uint16_t x1 = x + w - 1;
    uint16_t y1 = y + h - 1;

    writeCmd(ST77XX_CASET);
    SPI1.transfer((uint8_t)(x  >> 8));
    SPI1.transfer((uint8_t)(x  & 0xFF));
    SPI1.transfer((uint8_t)(x1 >> 8));
    SPI1.transfer((uint8_t)(x1 & 0xFF));

    writeCmd(ST77XX_RASET);
    SPI1.transfer((uint8_t)(y  >> 8));
    SPI1.transfer((uint8_t)(y  & 0xFF));
    SPI1.transfer((uint8_t)(y1 >> 8));
    SPI1.transfer((uint8_t)(y1 & 0xFF));

    writeCmd(ST77XX_RAMWR);
}

inline uint8_t srcPixelIdx(const uint8_t *src, uint16_t index) {
    // 2bpp, 4 pixels per byte, MSB-first. Mirrors frame_pixel2_idx() in
    // animations2.h but inlined here so the blit doesn't pull in that header.
    uint8_t b = src[(index * 2) >> 3];
    uint8_t shift = 6 - ((index & 3) * 2);
    return (b >> shift) & 0x03;
}

// Swap a host-order RGB565 word to the big-endian byte order ST7789 expects
// on the wire.
inline uint16_t swap565(uint16_t c) {
    return (uint16_t)((c >> 8) | (c << 8));
}

} // anonymous namespace

namespace pikachu2 {

void blit2bpp_rgb565(int16_t x, int16_t y,
                     uint16_t srcW, uint16_t srcH,
                     const uint8_t *src2bpp,
                     const uint16_t palette[4],
                     uint8_t zoom)
{
    if (!src2bpp || !palette || zoom == 0 || srcW == 0 || srcH == 0) return;

    const uint16_t outW = (uint16_t)(srcW * zoom);
    const uint16_t outH = (uint16_t)(srcH * zoom);

    // Pre-byte-swap the palette once. Saves srcW*srcH per-pixel swaps.
    uint16_t paletteBE[4];
    for (int i = 0; i < 4; i++) paletteBE[i] = swap565(palette[i]);

    // One destination row buffer (zoom-expanded). 144 px × 2 B = 288 B for the
    // standard pikachu2 case. Fixed-size to keep VLA-free; covers the full
    // T114 screen width (240 px → 480 B).
    static constexpr uint16_t MAX_ROW_PX = 240;
    if (outW > MAX_ROW_PX) return;  // refuse to overflow the row buffer
    uint16_t rowBuf[MAX_ROW_PX];

    pinMode(ST7789_RS, OUTPUT);
    pinMode(ST7789_NSS, OUTPUT);

    csLow();
    SPI1.beginTransaction(kSpiSettings);
    writeAddrWindow((uint16_t)x, (uint16_t)y, outW, outH);

    for (uint16_t srcY = 0; srcY < srcH; srcY++) {
        // Build one zoomed row in rowBuf.
        const uint16_t srcRowBase = srcY * srcW;
        for (uint16_t srcX = 0; srcX < srcW; srcX++) {
            uint16_t be = paletteBE[srcPixelIdx(src2bpp, srcRowBase + srcX)];
            uint16_t *out = &rowBuf[srcX * zoom];
            for (uint8_t z = 0; z < zoom; z++) out[z] = be;
        }
        // Emit that row `zoom` times vertically.
        for (uint8_t z = 0; z < zoom; z++) {
            SPI1.transfer((void *)rowBuf, (void *)nullptr, (size_t)outW * 2);
        }
    }

    SPI1.endTransaction();
    csHigh();
}

} // namespace pikachu2

#endif // HAS_SCREEN && USE_ST7789 && !ARCH_ESP32
