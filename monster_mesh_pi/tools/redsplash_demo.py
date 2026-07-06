#!/usr/bin/env python3
"""'Red balloon' color-splash: desaturate the whole sprite to a DARK greyscale,
but keep pixels whose original hue is red (no added tint — the red simply
survives). A few red-range / darkness variations to pick from. Static.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5
# Species with prominent red so the splash is visible.
SAMPLES = [(6, "Charizard"), (212, "Scizor"), (126, "Magmar"), (100, "Voltorb")]


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b
def clamp(v): return max(0, min(255, int(v)))


# Asymmetric red gate: `orange_deg` is how far toward orange (+hue) we allow,
# `crimson_deg` how far toward crimson/magenta (-hue). Keep orange_deg small to
# reject orange while still catching deep/crimson reds.
def is_red(r, g, b, orange_deg, crimson_deg, sat_min):
    h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255)
    hd = h*360
    return s >= sat_min and (hd <= orange_deg or hd >= 360 - crimson_deg)


def make(orange_deg, crimson_deg, sat_min, dark):
    def fn(rgb):
        r, g, b = rgb
        if is_red(r, g, b, orange_deg, crimson_deg, sat_min):
            return (r, g, b)                     # keep the red, untouched
        y = luma(r, g, b) * dark
        return (clamp(y), clamp(y), clamp(y))
    return fn


def m_grey(rgb):
    y = luma(*rgb); return (clamp(y),)*3


COLS = [
    ("Dark grey\n(no splash)", lambda c: (lambda y: (clamp(y),)*3)(luma(*c)*0.45)),
    ("prev (±28)\ntoo orange", make(28, 28, 0.35, 0.45)),
    ("tight\nor10/cr18", make(10, 18, 0.45, 0.45)),
    ("tighter\nor6/cr15", make(6, 15, 0.50, 0.45)),
    ("red only\nor4/cr10", make(4, 10, 0.55, 0.45)),
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
    cell = SIZE*SCALE; pad = 10; hdr = 40
    W = pad + len(COLS)*(cell+pad); H = hdr + len(SAMPLES)*(cell+pad)
    sheet = Image.new("RGB", (W, H), (235, 235, 238)); d = ImageDraw.Draw(sheet)
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

    out = "/tmp/redsplash.png"; sheet.save(out); print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
