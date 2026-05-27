# Pocket Pikachu 2 — T114 color TFT pet

The T114 colour pet. Same game-state model as the V3 `pikachu/` module
but rendered into a 240×135 ST7789 SPI TFT with a 4-colour palette per
animation. The state machine is duplicated (with a `2` suffix) so the
two boards can ship in the same firmware without name collisions.

## Hardware

| Component | Detail |
|-----------|--------|
| Board     | Heltec T114 (nRF52840) |
| Display   | 240×135 ST7789 SPI TFT |
| Button    | PRG (P0.10) |

## Rendering pipeline

The Meshtastic stock display driver is a 1-bit framebuffer wrapping the
ST7789Spi shim. Our patched shim adds `setRGB()` / `setBgRGB()` so the
1-bit content flushes as black-on-white pixels by default. For sprites
we bypass the framebuffer entirely and write RGB565 pixels straight to
the panel via SPI.

`Tft4ColorBlit.cpp` is the direct-SPI blitter:

```cpp
pikachu2::blit2bpp_rgb565(x, y, srcW, srcH, src2bpp, palette, zoom);
```

- `src2bpp` is a 2bpp packed buffer (4 px / byte, MSB-leftmost, row-major)
- `palette[4]` is RGB565 (`{ bg, light, dark, fg }` convention)
- `zoom` 1..3 — integer pixel duplication, no smoothing
- Output rect is `srcW * zoom × srcH * zoom` pixels

The blitter sets the ST7789's RAMWR window then streams pixels in one
SPI transaction. There's no per-pixel overhead — at zoom 1 we get full
frame rate for 40×40 / 56×56 sprites.

A consequence: after `blit2bpp_rgb565()` returns, the shim's `_RGB`
register is left at the LAST palette entry written. If the next thing
drawn via the 1-bit framebuffer expected `_RGB = 0x0000` (black), it'll
come out in whatever colour the sprite ended on. The Pentest module
hammers `setRGB(0x0000) / setBgRGB(0xFFFF)` every runOnce tick to keep
all native Meshtastic frames in B&W regardless of pikachu2's last blit.

## Animation system

`animations2.h` is auto-generated from the same `anims.json` as the V3
module but emits 2bpp PROGMEM blobs instead of 1-bit bitmaps. Each
animation uses a 4-colour palette tuned for the pet's mood (warm orange
when happy, cool blue during bath, etc.).

The game state machine in `game2.cpp` mirrors `pikachu/game.cpp` but
with `2`-suffixed identifiers (`gameState2`, `GS2_STAND`, etc.) so the
two coexist in one firmware image.

## Files

| File                              | Role |
|-----------------------------------|------|
| `Tft4ColorBlit.{h,cpp}`           | Direct-SPI 2bpp → RGB565 blitter |
| `animations2.h`                   | AUTO-GENERATED — 2bpp anim frames |
| `config2.h`                       | T114 game tuning constants |
| `game2.{h,cpp}`                   | T114 game state machine |
| `PocketPikachu2Module.{h,cpp}`    | MeshModule + drawFrame for the carousel |

## Board build

Use the `heltec-t114-pet` env. The `platformio.ini` excludes
`MonsterMeshEmulator.cpp` (no Game Boy ROM emulation on T114), the
heavyweight environmental sensors, MQTT, and other modules to leave room
for the 56×56 sprite atlas in flash.
