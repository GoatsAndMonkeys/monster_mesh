#!/usr/bin/env python3
"""Apply the 'light eyes' trick (fade saturation on bright pixels so whites/eyes
stay pale) to the DIAGONAL RAINBOW and the PINK. Compares uniform vs light-eyes.
Rainbow shown as a static frame. Writes a sheet and opens it.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5
SAMPLES = [(6, "Charizard"), (130, "Gyarados"), (25, "Pikachu"), (196, "Espeon"), (149, "Dragonite")]
BARBIE = (255, 46, 154)
BH, _, _ = colorsys.rgb_to_hsv(BARBIE[0]/255, BARBIE[1]/255, BARBIE[2]/255)


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b
def clamp(v): return max(0, min(255, int(v)))


def hue_luma(hue, sat, y):
    br, bg, bb = colorsys.hsv_to_rgb(hue % 1.0, sat, 1.0)
    br, bg, bb = br*255, bg*255, bb*255
    s = y / (luma(br, bg, bb) or 1.0)
    return (clamp(br*s), clamp(bg*s), clamp(bb*s))


def sat_lite(y, base):
    """Fade saturation on bright pixels so whites/eyes stay pale."""
    if y <= 175:
        return base
    return max(0.06, base * (1.0 - (y - 175) / 80.0 * 0.90))


def rainbow_uniform(rgb, x, yy):
    hue = 0.85 * ((x + yy) / (2*(SIZE-1)))
    return hue_luma(hue, 0.95, luma(*rgb))


def rainbow_lite(rgb, x, yy):
    y = luma(*rgb)
    hue = 0.85 * ((x + yy) / (2*(SIZE-1)))
    return hue_luma(hue, sat_lite(y, 0.95), y)


def pink_lite(rgb, x, yy):
    y = luma(*rgb)
    return hue_luma(BH, sat_lite(y, 0.85), y)


COLS = [
    ("Normal", None),
    ("Rainbow\nuniform", rainbow_uniform),
    ("Rainbow +\nlight eyes", rainbow_lite),
    ("Pink +\nlight eyes", pink_lite),
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
                op[x, y] = (*fn((r, g, b), x, y), 255)
    return out


def main():
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 15)
    except Exception:
        font = ImageFont.load_default()
    cell = SIZE*SCALE; pad = 10; hdr = 40
    W = pad + len(COLS)*(cell+pad); H = hdr + len(SAMPLES)*(cell+pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 248)); d = ImageDraw.Draw(sheet)
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

    out = "/tmp/lighteyes.png"; sheet.save(out); print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
