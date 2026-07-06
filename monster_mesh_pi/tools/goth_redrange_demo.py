#!/usr/bin/env python3
"""Broaden the GOTH red gate so DARK reds (cheek red) and LIGHT reds/pinks
(salmon, pink) also survive the greyscale — not just vivid mid-reds. Compares
the current gate vs broader ones. Gamma-2.1 darkness curve throughout.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5
# pink + red + dark-red variety
SAMPLES = [(25, "Pikachu"), (36, "Clefable"), (39, "Jigglypuff"), (113, "Chansey"),
           (45, "Vileplume"), (80, "Slowbro"), (126, "Magmar"), (129, "Magikarp")]
_GAMMA = [int((i/255.0)**2.1 * 255 + 0.5) for i in range(256)]


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b
def clamp(v): return max(0, min(255, int(v)))


def make(orange_deg, crimson_deg, sat_min):
    def fn(rgb):
        r, g, b = rgb
        h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255)
        hd = h*360
        if s >= sat_min and (hd <= orange_deg or hd >= 360 - crimson_deg):
            return (r, g, b)
        y = _GAMMA[clamp(luma(r, g, b))]
        return (y, y, y)
    return fn


COLS = [
    ("Original", None),
    ("Current\nor10/cr18 s.45", make(10, 18, 0.45)),
    ("Broad reds\nor10/cr22 s.28", make(10, 22, 0.28)),
    ("Broad + pink\nor12/cr30 s.20", make(12, 30, 0.20)),
]


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3)|(r >> 2), (g << 2)|(g >> 4), (b << 3)|(b >> 2)


def reconstruct(indices, pal):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); px = img.load()
    for i, idx in enumerate(indices):
        if idx:
            px[i % SIZE, i // SIZE] = (*rgb565_to_rgb(pal[idx]), 255)
    return img


def apply(base, fn):
    if fn is None: return base
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a >= 128:
                op[x, y] = (*fn((r, g, b)), 255)
    return out


def main():
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 15)
    except Exception:
        font = ImageFont.load_default()
    cell = SIZE*SCALE; pad = 10; hdr = 42
    W = pad + len(COLS)*(cell+pad); H = hdr + len(SAMPLES)*(cell+pad)
    sheet = Image.new("RGB", (W, H), (232, 232, 236)); d = ImageDraw.Draw(sheet)
    for ci, (name, _) in enumerate(COLS):
        d.multiline_text((pad + ci*(cell+pad)+4, 4), name, fill=(20, 20, 20), font=font)

    for ri, (dex, name) in enumerate(SAMPLES):
        img, shiny, label = b4m.fetch_pair(dex, "front")
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        idx, palN, _ = b4m.build_indexed(img, shiny)
        base = reconstruct(idx, palN)
        for ci, (_, fn) in enumerate(COLS):
            big = apply(base, fn).resize((cell, cell), Image.NEAREST)
            bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
            sheet.paste(bg, (pad + ci*(cell+pad), hdr + ri*(cell+pad)))
        d.text((pad+2, hdr + ri*(cell+pad)+2), name, fill=(0, 0, 120), font=font)
        print(f"  rendered {name} ({label})", file=sys.stderr)

    out = "/tmp/goth_redrange.png"; sheet.save(out); print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
