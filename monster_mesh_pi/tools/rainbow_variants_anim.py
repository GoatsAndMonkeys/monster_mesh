#!/usr/bin/env python3
"""Animated contact sheet for the three rainbow variants:
Rainbow | Dark-Rainbow | Blackout-Rainbow (aka Double-Dark Rainbow).

The diagonal rainbow band scrolls; loops seamlessly. Writes an animated GIF.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 3
FRAMES = 24
VARIANTS = ["Rainbow", "Dark-Rainbow", "Blackout-Rainbow"]
NV = len(VARIANTS)
SAMPLES = [(6, "Charizard"), (25, "Pikachu"), (94, "Gengar"), (130, "Gyarados")]
_GAMMA = [int((i/255.0)**2.1 * 255 + 0.5) for i in range(256)]
RIM = (60, 66, 82, 255)


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


def is_red(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255); hd = h*360
    return s >= 0.45 and (hd <= 10 or hd >= 342)


def _hue(x, yy, phase):
    return 0.85 * ((x + yy) / (2*(SIZE-1))) + phase


def rainbow(rgb, x, yy, phase):
    y = luma(*rgb); return hue_luma(_hue(x, yy, phase), sat_lite(y, 0.95), y)


def dark_rainbow(rgb, x, yy, phase):
    r, g, b = rgb
    if is_red(r, g, b):
        y = luma(r, g, b); return hue_luma(_hue(x, yy, phase), sat_lite(y, 0.95), y)
    y = _GAMMA[clamp(luma(r, g, b))]; return (y, y, y)


def blackout_rainbow(rgb, x, yy, phase):
    r, g, b = rgb
    if is_red(r, g, b):
        y = luma(r, g, b); pr, pg, pb = hue_luma(_hue(x, yy, phase), sat_lite(y, 0.95), y)
        return (clamp(pr*0.62), clamp(pg*0.62), clamp(pb*0.62))
    return (0, 0, 0)


FNS = [rainbow, dark_rainbow, blackout_rainbow]
NEEDS_RIM = [False, False, True]


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3)|(r >> 2), (g << 2)|(g >> 4), (b << 3)|(b >> 2)


def reconstruct(indices, pal):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); px = img.load()
    for i, idx in enumerate(indices):
        if idx: px[i % SIZE, i // SIZE] = (*rgb565_to_rgb(pal[idx]), 255)
    return img


def apply(base, fn, phase, rim):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a >= 128: op[x, y] = (*fn((r, g, b), x, y, phase), 255)
    if rim:
        for y in range(SIZE):
            for x in range(SIZE):
                if op[x, y][3] < 128: continue
                for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                    nx, ny = x+dx, y+dy
                    if not (0 <= nx < SIZE and 0 <= ny < SIZE) or ip[nx, ny][3] < 128:
                        op[x, y] = RIM; break
    return out


def main():
    try:
        fname = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
        fvar = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 12)
    except Exception:
        fname = fvar = ImageFont.load_default()

    bases = []
    for dex, name in SAMPLES:
        img, shiny, label = b4m.fetch_pair(dex, "front")
        if img is None: print(f"  {name}: MISSING", file=sys.stderr); continue
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        idx, palN, _ = b4m.build_indexed(img, shiny)
        bases.append((name, reconstruct(idx, palN)))
        print(f"  loaded {name} ({dex})", file=sys.stderr)

    cell = SIZE*SCALE; gap = 4
    block_w = NV*cell + (NV-1)*gap
    top_h = 18; name_h = 20
    cols = 2; rows = (len(bases) + cols - 1) // cols; bgap = 26
    W = 14 + cols*block_w + (cols-1)*bgap + 14
    row_h = top_h + name_h + cell + 10
    H = 8 + rows*row_h + 8

    frames = []
    for f in range(FRAMES):
        phase = f / FRAMES                       # 0..1 → seamless loop
        sheet = Image.new("RGB", (W, H), (232, 234, 240)); d = ImageDraw.Draw(sheet)
        for i, (name, base) in enumerate(bases):
            r, c = divmod(i, cols)
            x0 = 14 + c*(block_w + bgap); y0 = 8 + r*row_h
            for vi, vname in enumerate(VARIANTS):
                col = (150, 60, 30) if "Blackout" in vname else (120, 120, 128)
                d.text((x0 + vi*(cell+gap) + 2, y0), vname, fill=col, font=fvar)
            d.text((x0 + 2, y0 + top_h - 2), name, fill=(20, 30, 90), font=fname)
            for vi in range(NV):
                cimg = apply(base, FNS[vi], phase, NEEDS_RIM[vi])
                big = cimg.resize((cell, cell), Image.NEAREST)
                plate = (206, 212, 224) if vi == 2 else (255, 255, 255)
                bg = Image.new("RGB", (cell, cell), plate); bg.paste(big, (0, 0), big)
                sheet.paste(bg, (x0 + vi*(cell+gap), y0 + top_h + name_h))
        frames.append(sheet)
        print(f"  frame {f+1}/{FRAMES}", file=sys.stderr)

    out = os.path.join(HERE, "..", "..", "sprite-demos", "rainbow_variants_anim.gif")
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=70, loop=0,
                   optimize=True)
    print(f"\nwrote {out}  ({W}x{H}, {FRAMES} frames)", file=sys.stderr)


if __name__ == "__main__":
    main()
