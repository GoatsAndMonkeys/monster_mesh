#!/usr/bin/env python3
"""Final keeper skins: Normal, Shiny, Rainbow-diagonal (light eyes, static frame),
Goth (tight red splash + gamma-2.1 darkness curve). Matches the on-device set.
Writes a contact sheet and opens it.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5
SAMPLES = [(6, "Charizard"), (130, "Gyarados"), (25, "Pikachu"), (149, "Dragonite"),
           (126, "Magmar"), (45, "Vileplume"), (196, "Espeon"), (129, "Magikarp")]

_GAMMA = [int((i/255.0)**2.1 * 255 + 0.5) for i in range(256)]
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
    if y <= 175: return base
    return max(0.06, base * (1.0 - (y - 175) / 80.0 * 0.90))


def rainbow_lite(rgb, x, yy):
    y = luma(*rgb)
    hue = 0.85 * ((x + yy) / (2*(SIZE-1)))
    return hue_luma(hue, sat_lite(y, 0.95), y)


def is_red(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255)
    hd = h*360
    return s >= 0.45 and (hd <= 10 or hd >= 342)


def goth(rgb, x, yy):
    r, g, b = rgb
    if is_red(r, g, b):
        return (r, g, b)
    y = _GAMMA[clamp(luma(r, g, b))]
    return (y, y, y)


def pink_lite(rgb, x, yy):
    y = luma(*rgb)
    return hue_luma(BH, sat_lite(y, 0.85), y)


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
                op[x, y] = (*fn((r, g, b), x, y), 255)
    return out


def main():
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 16)
    except Exception:
        font = ImageFont.load_default()
    cols = ["Normal", "Shiny", "Rainbow\n(light eyes)", "Goth\n(gamma 2.1)",
            "Goth Shiny", "Pink\n(light eyes)"]
    cell = SIZE*SCALE; pad = 10; hdr = 42
    W = pad + len(cols)*(cell+pad); H = hdr + len(SAMPLES)*(cell+pad)
    sheet = Image.new("RGB", (W, H), (240, 240, 244)); d = ImageDraw.Draw(sheet)
    for ci, name in enumerate(cols):
        d.multiline_text((pad + ci*(cell+pad)+4, 4), name, fill=(20, 20, 20), font=font)

    for ri, (dex, name) in enumerate(SAMPLES):
        img, shiny, label = b4m.fetch_pair(dex, "front")
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        idx, palN, palS = b4m.build_indexed(img, shiny)
        base = reconstruct(idx, palN)
        shinyimg = reconstruct(idx, palS)
        cells = [base, shinyimg, apply(base, rainbow_lite), apply(base, goth),
                 apply(shinyimg, goth), apply(base, pink_lite)]
        for ci, cimg in enumerate(cells):
            big = cimg.resize((cell, cell), Image.NEAREST)
            bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
            sheet.paste(bg, (pad + ci*(cell+pad), hdr + ri*(cell+pad)))
        d.text((pad+2, hdr + ri*(cell+pad)+2), name, fill=(0, 0, 120), font=font)
        print(f"  rendered {name} ({label})", file=sys.stderr)

    out = "/tmp/final_skins.png"; sheet.save(out); print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
