#!/usr/bin/env python3
"""Compare Pink (on the normal palette) vs Pink-Shiny (pink on the SHINY palette).
Pink is luma-driven, so it differs wherever the shiny changes brightness.
Columns: Normal, Shiny, Pink (reg), Pink Shiny.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5
SAMPLES = [(6, "Charizard"), (38, "Ninetales"), (94, "Gengar"), (130, "Gyarados"),
           (149, "Dragonite"), (196, "Espeon"), (197, "Umbreon"), (129, "Magikarp")]
BARBIE = (255, 46, 154); BH, _, _ = colorsys.rgb_to_hsv(BARBIE[0]/255, BARBIE[1]/255, BARBIE[2]/255)


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


def pink(rgb, x, yy):
    y = luma(*rgb); return hue_luma(BH, sat_lite(y, 0.85), y)


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3)|(r >> 2), (g << 2)|(g >> 4), (b << 3)|(b >> 2)


def reconstruct(indices, pal):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); px = img.load()
    for i, idx in enumerate(indices):
        if idx: px[i % SIZE, i // SIZE] = (*rgb565_to_rgb(pal[idx]), 255)
    return img


def apply(base, fn):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a >= 128: op[x, y] = (*fn((r, g, b), x, y), 255)
    return out


def main():
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 15)
    except Exception:
        font = ImageFont.load_default()
    cols = ["Normal", "Shiny", "Pink\n(reg)", "Pink Shiny"]
    cell = SIZE*SCALE; pad = 10; hdr = 42
    W = pad + len(cols)*(cell+pad); H = hdr + len(SAMPLES)*(cell+pad)
    sheet = Image.new("RGB", (W, H), (255, 240, 248)); d = ImageDraw.Draw(sheet)
    for ci, name in enumerate(cols):
        d.multiline_text((pad + ci*(cell+pad)+4, 4), name, fill=(150, 20, 90), font=font)

    for ri, (dex, name) in enumerate(SAMPLES):
        img, shiny, label = b4m.fetch_pair(dex, "front")
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        idx, palN, palS = b4m.build_indexed(img, shiny)
        base = reconstruct(idx, palN); shy = reconstruct(idx, palS)
        cells = [base, shy, apply(base, pink), apply(shy, pink)]
        for ci, cimg in enumerate(cells):
            big = cimg.resize((cell, cell), Image.NEAREST)
            bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
            sheet.paste(bg, (pad + ci*(cell+pad), hdr + ri*(cell+pad)))
        d.text((pad+2, hdr + ri*(cell+pad)+2), name, fill=(150, 20, 90), font=font)
        print(f"  rendered {name} ({label})", file=sys.stderr)

    out = "/tmp/pink_shiny.png"; sheet.save(out); print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
