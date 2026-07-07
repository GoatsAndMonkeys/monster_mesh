#!/usr/bin/env python3
"""Double-Dark 'Blackout' mockup: DD homozygotes render as a solid black
silhouette (Who's-That-Pokemon style), vs the single-D 'Dark' red-balloon look.

Columns: Regular | Dark (Dd) | Blackout (DD, pure) | Blackout+rim (readable variant)
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 3
VARIANTS = ["Regular", "Dark (Dd)", "Blackout (DD)", "Blackout + rim"]
NV = len(VARIANTS)
SAMPLES = [(6, "Charizard"), (25, "Pikachu"), (94, "Gengar"),
           (131, "Lapras"), (143, "Snorlax"), (150, "Mewtwo")]
_GAMMA = [int((i/255.0)**2.1 * 255 + 0.5) for i in range(256)]


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b
def clamp(v): return max(0, min(255, int(v)))


def is_red(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255); hd = h*360
    return s >= 0.45 and (hd <= 10 or hd >= 342)


def dark(rgb, x, yy):                 # single-D: red-balloon gamma dark
    r, g, b = rgb
    if is_red(r, g, b): return (r, g, b)
    y = _GAMMA[clamp(luma(r, g, b))]; return (y, y, y)


def blackout(rgb, x, yy):             # double-D: flat black silhouette
    return (0, 0, 0)


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


def rim_light(base):
    """Blackout with a faint edge rim so the silhouette stays readable on dark UI."""
    silo = apply(base, blackout); op = silo.load()
    for y in range(SIZE):
        for x in range(SIZE):
            if op[x, y][3] < 128: continue
            edge = False
            for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                nx, ny = x+dx, y+dy
                if not (0 <= nx < SIZE and 0 <= ny < SIZE) or base.load()[nx, ny][3] < 128:
                    edge = True; break
            if edge: op[x, y] = (60, 66, 82, 255)     # cool slate rim
    return silo


def variants_for(dex):
    img, shiny, label = b4m.fetch_pair(dex, "front")
    if img is None: return None
    img = g3.scale_rgba(img, SIZE, SIZE)
    shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
    idx, palN, palS = b4m.build_indexed(img, shiny)
    base = reconstruct(idx, palN)
    return [base, apply(base, dark), apply(base, blackout), rim_light(base)]


def main():
    try:
        fname = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
        fvar = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 12)
    except Exception:
        fname = fvar = ImageFont.load_default()

    cell = SIZE*SCALE; gap = 4
    block_w = NV*cell + (NV-1)*gap
    top_h = 18; name_h = 20
    row_h = top_h + name_h + cell + 12
    cols = 2
    rows = (len(SAMPLES) + cols - 1) // cols
    bgap = 30
    W = 14 + cols*block_w + (cols-1)*bgap + 14
    H = 8 + rows*row_h + 8
    # dark-ish backdrop so the blackout silhouette reads like the trivia card
    sheet = Image.new("RGB", (W, H), (232, 234, 240)); d = ImageDraw.Draw(sheet)

    for i, (dex, name) in enumerate(SAMPLES):
        r, c = divmod(i, cols)
        x0 = 14 + c*(block_w + bgap); y0 = 8 + r*row_h
        v = variants_for(dex)
        if v is None:
            print(f"  {name}: MISSING", file=sys.stderr); continue
        for vi, vname in enumerate(VARIANTS):
            col = (30, 30, 34) if "Blackout" in vname else (120, 120, 128)
            d.text((x0 + vi*(cell+gap) + 2, y0), vname, fill=col, font=fvar)
        d.text((x0 + 2, y0 + top_h - 2), name, fill=(20, 30, 90), font=fname)
        for vi, cimg in enumerate(v):
            big = cimg.resize((cell, cell), Image.NEAREST)
            # trivia-style: mid-grey plate for the two blackout columns
            plate = (206, 212, 224) if vi >= 2 else (255, 255, 255)
            bg = Image.new("RGB", (cell, cell), plate); bg.paste(big, (0, 0), big)
            sheet.paste(bg, (x0 + vi*(cell+gap), y0 + top_h + name_h))
        print(f"  {name} ({dex})", file=sys.stderr)

    out = os.path.join(HERE, "..", "..", "sprite-demos", "blackout.png"); sheet.save(out)
    print(f"\nwrote {out}  ({W}x{H})", file=sys.stderr)


if __name__ == "__main__":
    main()
