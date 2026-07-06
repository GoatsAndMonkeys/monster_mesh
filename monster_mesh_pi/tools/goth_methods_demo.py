#!/usr/bin/env python3
"""Mock up GOTH / vampire recolor methods (desaturated, darker, red/pink cast)
next to plain greyscale for reference. Static (not animated). Writes a labelled
contact sheet PNG and opens it.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5
SAMPLES = [(6, "Charizard"), (130, "Gyarados"), (149, "Dragonite"), (196, "Espeon")]


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b
def clamp(v): return max(0, min(255, int(v)))
def lerp(a, b, t): return tuple(clamp(a[i] + (b[i]-a[i])*t) for i in range(3))


# ---- methods ---------------------------------------------------------------
def m_grey(rgb):                         # reference: plain luma greyscale
    y = luma(*rgb); return (clamp(y),)*3


def m_grey_red(rgb):                      # greyscale, darkened, faint red/pink cast
    y = luma(*rgb) * 0.72
    return (clamp(y*1.18), clamp(y*0.90), clamp(y*0.98))


def m_duotone(rgb):                       # maroon shadows -> dusty pink highlights
    t = luma(*rgb)/255.0
    return lerp((26, 10, 16), (206, 162, 176), t)


def m_vampire(rgb):                       # darker, black -> deep crimson -> pale
    t = luma(*rgb)/255.0
    if t < 0.6:
        return lerp((12, 6, 8), (150, 24, 40), t/0.6)      # black -> blood red
    return lerp((150, 24, 40), (214, 180, 184), (t-0.6)/0.4)  # -> pale ash


def m_desat_dark(rgb):                     # keep faint hue, mostly grey, dark, pink lift
    h, s, v = colorsys.rgb_to_hsv(rgb[0]/255, rgb[1]/255, rgb[2]/255)
    r, g, b = colorsys.hsv_to_rgb(h, s*0.28, v*0.68)
    r, g, b = r*255, g*255, b*255
    # nudge toward pink
    return (clamp(r + 18), clamp(g + 2), clamp(b + 10))


def m_pastel_goth(rgb):                    # dark purple shadows -> soft pink highlights
    t = luma(*rgb)/255.0
    return lerp((28, 16, 32), (222, 176, 198), t)


COLS = [
    ("Greyscale", m_grey),
    ("Grey + red\ncast", m_grey_red),
    ("Duotone\nmaroon-pink", m_duotone),
    ("Vampire\n(crimson)", m_vampire),
    ("Desat dark\n+ pink", m_desat_dark),
    ("Pastel goth\n(purple-pink)", m_pastel_goth),
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
    sheet = Image.new("RGB", (W, H), (238, 238, 240)); d = ImageDraw.Draw(sheet)
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

    out = "/tmp/goth_methods.png"; sheet.save(out); print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
