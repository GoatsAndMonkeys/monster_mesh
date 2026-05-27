#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// Tft4ColorBlit — direct 4-color blit to the ST7789 TFT, independent of the
// OLEDDisplay 1-bit shim used by the rest of Meshtastic.
//
// Why: the bundled meshtastic-st7789 lib wraps the panel as a 1-bit
// OLEDDisplay subclass with a single foreground color. That's fine for the
// sidebar text/lines (we want those to share the global TFT_MESH color), but
// the Pikachu pet sprite is authored with a 4-entry palette per animation and
// looks terrible as a monochrome silhouette. This helper streams 2bpp indexed
// pixels straight to the panel at full RGB565 resolution by driving the same
// SPI1 + CS/DC pins the lib uses.
//
// Constraints:
//   - T114 (nRF52840) ONLY. The ST7789 + SPI1 wiring used here matches the
//     ST7789Spi instance Meshtastic constructs in graphics/Screen.cpp on
//     USE_ST7789 builds. ESP32 T-Deck takes a different code path.
//   - MUST be called AFTER the OLEDDisplay framebuffer paint for that frame,
//     otherwise the next display->display() pass overwrites our pixels.
//
// Usage from drawFrame():
//   display->display();                            // flush sidebar/text first
//   pikachu2_blit2bpp_rgb565(x, y, srcW, srcH,
//                            sprite_2bpp_data,
//                            anim_palette, zoom);
// ──────────────────────────────────────────────────────────────────────────────

#include "configuration.h"

#if HAS_SCREEN && defined(USE_ST7789) && !defined(ARCH_ESP32)

#include <stdint.h>

namespace pikachu2 {

// Blit a 2-bits-per-pixel indexed bitmap to the ST7789 at screen coords (x,y).
//
// srcW × srcH is the SOURCE bitmap size (e.g. 36×30 for the Pikachu sprite).
// Source pixels are packed 4-per-byte, MSB-first, row-major — same layout as
// frame_pixel2_idx() in animations2.h.
//
// Each source pixel is inflated to a `zoom`×`zoom` block of RGB565 pixels on
// the panel. palette[0..3] maps the 2-bit index to RGB565 (host byte order;
// we swap to big-endian for the wire).
//
// On screen the painted rect is (x, y) → (x + srcW*zoom - 1, y + srcH*zoom - 1).
void blit2bpp_rgb565(int16_t x, int16_t y,
                     uint16_t srcW, uint16_t srcH,
                     const uint8_t *src2bpp,
                     const uint16_t palette[4],
                     uint8_t zoom = 1);

} // namespace pikachu2

#endif // HAS_SCREEN && USE_ST7789 && !ARCH_ESP32
