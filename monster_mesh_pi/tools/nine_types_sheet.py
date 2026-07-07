#!/usr/bin/env python3
"""All NINE color types on one sheet, including the hidden Shiny Carrier.

Order: Regular, Shiny Carrier (looks Regular + subtle sparkle hint), Shiny,
Pink, Rainbow, Dark, Dark-Shiny, Dark-Pink, Dark-Rainbow.

The Shiny Carrier is genetically `Ss` — it renders as the plain Regular sprite
with a small sparkle glyph, the in-game "this one carries a shiny allele" hint.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 3
BLOCKS = 2                      # Pokemon per super-row (9 skins is wide)
VARIANTS = ["Regular", "Shiny Carrier", "Shiny", "Pink", "Rainbow",
            "Dark", "Dark Sh", "Dark Pk", "Dark Rbw"]
NV = len(VARIANTS)
SAMPLES = [
    (6, "Charizard"), (25, "Pikachu"), (94, "Gengar"),
    (131, "Lapras"), (150, "Mewtwo"), (196, "Espeon"),
]
_GAMMA = [int((i/255.0)**2.1 * 255 + 0.5) for i in range(256)]
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


def rainbow(rgb, x, yy):
    y = luma(*rgb); hue = 0.85 * ((x + yy) / (2*(SIZE-1)))
    return hue_luma(hue, sat_lite(y, 0.95), y)


def is_red(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255); hd = h*360
    return s >= 0.45 and (hd <= 10 or hd >= 342)


def goth(rgb, x, yy):
    r, g, b = rgb
    if is_red(r, g, b): return (r, g, b)
    y = _GAMMA[clamp(luma(r, g, b))]; return (y, y, y)


def pink(rgb, x, yy):
    y = luma(*rgb); return hue_luma(BH, sat_lite(y, 0.85), y)


def dark_pink(rgb, x, yy):
    r, g, b = rgb
    if is_red(r, g, b):
        y = luma(r, g, b); return hue_luma(BH, sat_lite(y, 0.85), y)
    y = _GAMMA[clamp(luma(r, g, b))]; return (y, y, y)


def dark_rainbow(rgb, x, yy):
    r, g, b = rgb
    if is_red(r, g, b):
        y = luma(r, g, b); hue = 0.85 * ((x + yy) / (2*(SIZE-1)))
        return hue_luma(hue, sat_lite(y, 0.95), y)
    y = _GAMMA[clamp(luma(r, g, b))]; return (y, y, y)


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


def sparkle(base):
    """Regular sprite + a small 4-point sparkle: the shiny-carrier hint."""
    out = base.copy(); d = ImageDraw.Draw(out)
    def star(cx, cy, r, col):
        d.line([(cx, cy-r), (cx, cy+r)], fill=col, width=1)
        d.line([(cx-r, cy), (cx+r, cy)], fill=col, width=1)
        d.point([(cx-1, cy-1), (cx+1, cy-1), (cx-1, cy+1), (cx+1, cy+1)], fill=col)
    star(SIZE-11, 11, 4, (245, 250, 255, 255))     # main sparkle, top-right
    star(SIZE-19, 20, 2, (210, 235, 255, 220))     # tiny second sparkle
    return out


def variants_for(dex):
    img, shiny, label = b4m.fetch_pair(dex, "front")
    if img is None: return None
    img = g3.scale_rgba(img, SIZE, SIZE)
    shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
    idx, palN, palS = b4m.build_indexed(img, shiny)
    base = reconstruct(idx, palN); shy = reconstruct(idx, palS)
    return [base, sparkle(base), shy, apply(base, pink), apply(base, rainbow),
            apply(base, goth), apply(shy, goth), apply(base, dark_pink),
            apply(base, dark_rainbow)]


def main():
    try:
        fname = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
        fvar = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 11)
    except Exception:
        fname = fvar = ImageFont.load_default()

    cell = SIZE*SCALE
    gap = 3
    block_w = NV*cell + (NV-1)*gap
    bgap = 26
    name_h = 20; top_h = 18
    superrows = (len(SAMPLES) + BLOCKS - 1) // BLOCKS
    W = 12 + BLOCKS*block_w + (BLOCKS-1)*bgap + 12
    row_h = top_h + name_h + cell + 10
    H = 6 + superrows*row_h + 6
    sheet = Image.new("RGB", (W, H), (245, 246, 248)); d = ImageDraw.Draw(sheet)

    for sr in range(superrows):
        y0 = 6 + sr*row_h
        for bi in range(BLOCKS):
            si = sr*BLOCKS + bi
            if si >= len(SAMPLES): break
            dex, name = SAMPLES[si]
            bx = 12 + bi*(block_w + bgap)
            v = variants_for(dex)
            if v is None:
                print(f"  {name}: MISSING", file=sys.stderr); continue
            for vi, vname in enumerate(VARIANTS):
                col = (70, 120, 200) if vname == "Shiny Carrier" else (120, 120, 128)
                d.text((bx + vi*(cell+gap) + 2, y0), vname, fill=col, font=fvar)
            d.text((bx + 2, y0 + top_h - 2), name, fill=(20, 30, 90), font=fname)
            for vi, cimg in enumerate(v):
                big = cimg.resize((cell, cell), Image.NEAREST)
                bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
                sheet.paste(bg, (bx + vi*(cell+gap), y0 + top_h + name_h))
            print(f"  {name} ({dex})", file=sys.stderr)

    out = os.path.join(HERE, "..", "..", "sprite-demos", "nine_types.png"); sheet.save(out)
    print(f"\nwrote {out}  ({W}x{H})", file=sys.stderr)


if __name__ == "__main__":
    main()
