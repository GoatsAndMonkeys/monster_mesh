#!/usr/bin/env python3
"""Bake hand-authored Gritty (NHL Flyers mascot) sprites into the MonsterMesh-Pi
sprite format used by Gen2SpriteCache.

Gritty is drawn procedurally as a 4-colour, 2bpp indexed bitmap matching the
exact dimensions of the Gen-2 sprites (front 56x56, back 48x48) so the SDL
BattleWindow can render him through the same path with no size special-casing.

Palette (index 0 = transparent, 1..3 = RGB565):
    0 transparent
    1 white   (googly eyes / jersey "00")
    2 orange  (shaggy fur)
    3 black   (outline / pupils / grin)

Outputs:
    src/terminal/GrittySprite.h      (committed asset)
    <preview path>/gritty_front.png  (preview only, when --preview)
    <preview path>/gritty_back.png

Run:
    ~/meshtastic-venv/bin/python3 tools/gritty_sprite_bake.py [--preview DIR]
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

ROOT  = Path("/Users/goatsandmonkeys/Documents/pokemesh/monster_mesh_pi")
OUT_H = ROOT / "src" / "terminal" / "GrittySprite.h"

FRONT_W = FRONT_H = 56
BACK_W  = BACK_H  = 48

# RGB888 used for drawing + preview; converted to RGB565 for the header.
TRANSPARENT = (255, 255, 255)   # index 0 (never drawn opaque)
WHITE       = (255, 255, 255)   # index 1
ORANGE      = (243, 112, 33)    # index 2  (Gritty orange)
BLACK       = (24, 24, 24)      # index 3

PAL_RGB = [TRANSPARENT, WHITE, ORANGE, BLACK]


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# ---------------------------------------------------------------- raster -----
class Grid:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [0] * (w * h)          # index buffer, default 0 (transparent)

    def set(self, x, y, idx):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = idx

    def get(self, x, y):
        return self.px[y * self.w + x]

    def disc(self, cx, cy, r, idx):
        for y in range(self.h):
            for x in range(self.w):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.set(x, y, idx)

    def ring(self, cx, cy, r_out, r_in, idx):
        for y in range(self.h):
            for x in range(self.w):
                d2 = (x - cx) ** 2 + (y - cy) ** 2
                if r_in * r_in <= d2 <= r_out * r_out:
                    self.set(x, y, idx)

    def ellipse_solid(self, cx, cy, rx, ry, idx):
        for y in range(self.h):
            for x in range(self.w):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.set(x, y, idx)

    def tri(self, p0, p1, p2, idx):
        xs = [p0[0], p1[0], p2[0]]
        ys = [p0[1], p1[1], p2[1]]
        def sign(a, b, c):
            return (a[0] - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (a[1] - c[1])
        for y in range(min(ys), max(ys) + 1):
            for x in range(min(xs), max(xs) + 1):
                p = (x, y)
                d1 = sign(p, p0, p1)
                d2 = sign(p, p1, p2)
                d3 = sign(p, p2, p0)
                neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
                pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
                if not (neg and pos):
                    self.set(x, y, idx)


def shaggy_body(g, cx, cy, rx, ry, spikes=20, amp=0.13):
    """Fill a furry orange blob with a jagged black rim, centred at (cx,cy)."""
    for y in range(g.h):
        for x in range(g.w):
            dx, dy = x - cx, y - cy
            nd = math.sqrt((dx / rx) ** 2 + (dy / ry) ** 2)
            ang = math.atan2(dy, dx)
            t = (ang * spikes / (2 * math.pi)) % 1.0
            tooth = abs(2 * t - 1.0)            # 0..1 triangle wave
            edge = 1.0 + amp * tooth            # spike outward
            if nd <= edge:
                # 2px-ish black rim near the spiky boundary, orange inside
                if nd >= edge - 0.13:
                    g.set(x, y, 3)
                else:
                    g.set(x, y, 2)


def googly_eye(g, cx, cy, r, px, py, pr):
    """White eyeball with black rim + black pupil at (px,py)."""
    g.disc(cx, cy, r, 3)            # black rim base
    g.disc(cx, cy, r - 2, 1)       # white sclera
    g.disc(px, py, pr, 3)          # pupil


# ----------------------------------------------------------------- front -----
def build_front():
    g = Grid(FRONT_W, FRONT_H)
    cx = 28
    # Shaggy orange body/head, slightly taller than wide.
    shaggy_body(g, cx, 31, 23, 24, spikes=22, amp=0.14)

    # Big googly eyes near the top, overlapping for the manic Gritty stare.
    googly_eye(g, 19, 23, 10, 21, 25, 4)
    googly_eye(g, 37, 23, 10, 35, 25, 4)

    # Orange cone nose between/below the eyes with a black outline.
    g.tri((28, 27), (22, 39), (34, 39), 3)     # black outline triangle
    g.tri((28, 29), (24, 38), (32, 38), 2)     # orange fill inset

    # Wide open grin below the nose.
    g.ellipse_solid(cx, 45, 13, 6, 3)          # black mouth
    return g


# ------------------------------------------------------------------ back -----
def build_back():
    g = Grid(BACK_W, BACK_H)
    cx, cy = 24, 25
    # Shaggy orange back of the blob.
    shaggy_body(g, cx, cy, 20, 22, spikes=20, amp=0.15)

    # White "00" Flyers jersey number across the back.
    g.ring(17, 25, 6, 3, 1)
    g.ring(31, 25, 6, 3, 1)
    return g


# ----------------------------------------------------------------- pack ------
def pack_2bpp(g):
    """4 px/byte, MSB = leftmost, matching Gen2SpriteCache::pixel2bpp."""
    out = bytearray((g.w * g.h + 3) // 4)
    for i in range(g.w * g.h):
        val = g.px[i] & 3
        byte = i >> 2
        shift = 6 - ((i & 3) * 2)
        out[byte] |= val << shift
    return bytes(out)


def emit_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for r in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in data[r:r + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def write_header(front, back):
    fpk = pack_2bpp(front)
    bpk = pack_2bpp(back)
    pal = ", ".join(f"0x{rgb565(c):04X}" for c in PAL_RGB)
    body = f"""#pragma once
// GrittySprite — hand-authored 4-colour Gritty (Philadelphia Flyers mascot)
// sprites for the Pentest Pikachu boss showdown.
// AUTO-GENERATED by tools/gritty_sprite_bake.py — DO NOT EDIT.
//
// Format matches Gen2SpriteCache: 2bpp packed (4 px/byte, MSB = leftmost),
// palette index 0 = transparent, 1..3 = RGB565.  Front 56x56, back 48x48 —
// identical dimensions to the Gen-2 sprites so no size special-casing is
// needed downstream.

#include <stdint.h>

// Out-of-dex sentinel species used only for SDL sprite rendering (the battle
// engine never sees this value).
static constexpr int GRITTY_DEX = 200;

static constexpr uint16_t GRITTY_FRONT_W = {FRONT_W};
static constexpr uint16_t GRITTY_FRONT_H = {FRONT_H};
static constexpr uint16_t GRITTY_BACK_W  = {BACK_W};
static constexpr uint16_t GRITTY_BACK_H  = {BACK_H};

// index 0 transparent, 1 white, 2 orange, 3 black
static const uint16_t kGrittyPalette[4] = {{ {pal} }};

{emit_array('kGrittyFront2bpp', fpk)}

{emit_array('kGrittyBack2bpp', bpk)}
"""
    OUT_H.write_text(body)
    print(f"wrote {OUT_H}  (front {len(fpk)}B, back {len(bpk)}B)")


def save_preview(g, path, scale=8):
    from PIL import Image
    img = Image.new("RGB", (g.w * scale, g.h * scale), (210, 210, 210))
    px = img.load()
    for y in range(g.h):
        for x in range(g.w):
            idx = g.get(x, y)
            if idx == 0:
                continue
            col = PAL_RGB[idx]
            for yy in range(scale):
                for xx in range(scale):
                    px[x * scale + xx, y * scale + yy] = col
    img.save(path)
    print(f"preview -> {path}")


def main():
    front = build_front()
    back = build_back()

    if "--preview" in sys.argv:
        d = Path(sys.argv[sys.argv.index("--preview") + 1])
        save_preview(front, d / "gritty_front.png")
        save_preview(back, d / "gritty_back.png")
    else:
        write_header(front, back)


if __name__ == "__main__":
    main()
