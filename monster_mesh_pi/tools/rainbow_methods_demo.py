#!/usr/bin/env python3
"""Render sample Gen-3 sprites under several RAINBOW recolor methods, side by
side, so we can eyeball which looks best. Writes a labelled contact sheet PNG.

All hue methods are LUMA-PRESERVING: each recolored pixel keeps the original
pixel's perceived brightness (0.299R+0.587G+0.114B), so the mon's shading/form
stays intact — only the colour changes. That's the key to it reading as the
same Pokemon, just rainbow.

Usage: ~/meshtastic-venv/bin/python3 tools/rainbow_methods_demo.py
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64
SCALE = 5
SAMPLES = [(6, "Charizard"), (130, "Gyarados"), (149, "Dragonite"), (196, "Espeon")]


def luma(r, g, b):
    return 0.299 * r + 0.587 * g + 0.114 * b


def hue_to_luma_rgb(hue, sat, y):
    """A colour of the given hue whose luma matches y (0..255). Preserves the
    original pixel brightness so shading survives the recolour."""
    br, bg, bb = colorsys.hsv_to_rgb(hue % 1.0, sat, 1.0)
    br, bg, bb = br * 255, bg * 255, bb * 255
    yb = luma(br, bg, bb) or 1.0
    s = y / yb
    return (min(255, int(br * s)), min(255, int(bg * s)), min(255, int(bb * s)))


# ---- methods: (name, fn(rgb, x, y) -> rgb) --------------------------------
def m_uniform(rgb, x, yy):        # single hue shift — NOT rainbow (reference)
    r, g, b = rgb
    return hue_to_luma_rgb(0.33, 0.9, luma(r, g, b))   # everything green


def m_spectrum_soft(rgb, x, yy):  # hue by brightness, moderate sat (holographic)
    r, g, b = rgb
    y = luma(r, g, b)
    return hue_to_luma_rgb(0.83 * (y / 255.0), 0.75, y)


def m_spectrum_vivid(rgb, x, yy): # hue by brightness, full sat (vivid)
    r, g, b = rgb
    y = luma(r, g, b)
    return hue_to_luma_rgb(0.83 * (y / 255.0), 1.0, y)


def m_vertical(rgb, x, yy):       # hue by row — literal rainbow bands, form kept
    r, g, b = rgb
    return hue_to_luma_rgb(0.85 * (yy / (SIZE - 1)), 0.95, luma(r, g, b))


def m_diagonal(rgb, x, yy):       # hue by x+y — diagonal rainbow sweep
    r, g, b = rgb
    return hue_to_luma_rgb(0.85 * ((x + yy) / (2 * (SIZE - 1))), 0.95, luma(r, g, b))


METHODS = [
    ("Normal", None),
    ("Shiny", "shiny"),
    ("Uniform hue\n(not rainbow)", m_uniform),
    ("Spectrum\nby-luma soft", m_spectrum_soft),
    ("Spectrum\nby-luma vivid", m_spectrum_vivid),
    ("Vertical\nbands", m_vertical),
    ("Diagonal\nsweep", m_diagonal),
]


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def reconstruct(indices, pal565):
    """Rebuild a 64x64 RGBA image from indices + an RGB565 palette (idx 0 = clear)."""
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = img.load()
    for i, idx in enumerate(indices):
        x, y = i % SIZE, i // SIZE
        if idx == 0:
            continue
        px[x, y] = (*rgb565_to_rgb(pal565[idx]), 255)
    return img


def apply_method(base_rgba, fn):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ip, op = base_rgba.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a < 128:
                continue
            op[x, y] = (*fn((r, g, b), x, y), 255)
    return out


def main():
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 15)
    except Exception:
        font = ImageFont.load_default()

    cell = SIZE * SCALE
    pad = 10
    hdr = 40
    cols, rows = len(METHODS), len(SAMPLES)
    W = pad + cols * (cell + pad)
    H = hdr + rows * (cell + pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 245))
    d = ImageDraw.Draw(sheet)

    for ci, (mname, fn) in enumerate(METHODS):
        d.multiline_text((pad + ci * (cell + pad) + 4, 4), mname, fill=(20, 20, 20), font=font)

    for ri, (dex, name) in enumerate(SAMPLES):
        # bake this sprite in-memory (front)
        img, shiny, label = b4m.fetch_pair(dex, "front")
        if img is None:
            continue
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        indices, palN, palS = b4m.build_indexed(img, shiny)
        base = reconstruct(indices, palN)
        base_shiny = reconstruct(indices, palS)

        for ci, (mname, fn) in enumerate(METHODS):
            if fn is None:
                cellimg = base
            elif fn == "shiny":
                cellimg = base_shiny
            else:
                cellimg = apply_method(base, fn)
            big = cellimg.resize((cell, cell), Image.NEAREST)
            bg = Image.new("RGB", (cell, cell), (255, 255, 255))
            bg.paste(big, (0, 0), big)
            sheet.paste(bg, (pad + ci * (cell + pad), hdr + ri * (cell + pad)))
        d.text((pad + 2, hdr + ri * (cell + pad) + 2), name, fill=(0, 0, 120), font=font)
        print(f"  rendered {name} ({label})", file=sys.stderr)

    out = "/tmp/rainbow_methods.png"
    sheet.save(out)
    print(f"\nwrote {out}  ({W}x{H})", file=sys.stderr)


if __name__ == "__main__":
    main()
