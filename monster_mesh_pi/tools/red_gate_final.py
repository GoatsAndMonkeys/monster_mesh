#!/usr/bin/env python3
"""Resolve the mouth-vs-body conflict with a brightness condition.

Blastoise mouth is DARK (val<=0.68); Charizard/Scizor orange bodies are BRIGHT
(val>=0.81). So: keep pure red at any brightness, but only grab warm 16-30 hues
when they're DARK. That catches mouths, spares bright orange bodies.

Columns: Regular | Current | Wide (hue28/sat.42) | Smart (dark-warm)
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 2
VARIANTS = ["Regular", "Current", "Wide", "Smart"]
NV = len(VARIANTS)
SAMPLES = [(9, "Blastoise"), (130, "Gyarados"), (94, "Gengar"),
           (6, "Charizard"), (212, "Scizor"), (257, "Blaziken")]
_GAMMA = [int((i/255.0)**2.1 * 255 + 0.5) for i in range(256)]


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b
def clamp(v): return max(0, min(255, int(v)))
def hsv(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255); return h*360, s, v


def g_current(r, g, b):
    hd, s, v = hsv(r, g, b); return s >= 0.45 and (hd <= 10 or hd >= 342)


def g_wide(r, g, b):
    hd, s, v = hsv(r, g, b); return s >= 0.42 and (hd <= 28 or hd >= 335)


def g_smart(r, g, b):
    hd, s, v = hsv(r, g, b)
    if s >= 0.45 and (hd <= 16 or hd >= 335): return True     # pure red, any brightness
    return s >= 0.42 and hd <= 30 and v <= 0.72               # dark warm accents only


GATES = [g_current, g_wide, g_smart]


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3)|(r >> 2), (g << 2)|(g >> 4), (b << 3)|(b >> 2)


def reconstruct(indices, pal):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); px = img.load()
    for i, idx in enumerate(indices):
        if idx: px[i % SIZE, i // SIZE] = (*rgb565_to_rgb(pal[idx]), 255)
    return img


def apply(base, gate):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a < 128: continue
            if gate(r, g, b): op[x, y] = (r, g, b, 255)
            else:
                yv = _GAMMA[clamp(luma(r, g, b))]; op[x, y] = (yv, yv, yv, 255)
    return out


def variants_for(dex):
    img, shiny, label = b4m.fetch_pair(dex, "front")
    if img is None: return None
    img = g3.scale_rgba(img, SIZE, SIZE)
    shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
    idx, palN, _ = b4m.build_indexed(img, shiny)
    base = reconstruct(idx, palN)
    return [base] + [apply(base, g) for g in GATES]


def main():
    try:
        fname = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 14)
        fvar = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 12)
    except Exception:
        fname = fvar = ImageFont.load_default()

    cell = SIZE*SCALE; gap = 4
    block_w = NV*cell + (NV-1)*gap
    top_h = 18; name_h = 20
    row_h = top_h + name_h + cell + 12
    cols = 2; rows = (len(SAMPLES) + cols - 1) // cols; bgap = 30
    W = 14 + cols*block_w + (cols-1)*bgap + 14
    H = 8 + rows*row_h + 8
    sheet = Image.new("RGB", (W, H), (236, 238, 243)); d = ImageDraw.Draw(sheet)

    for i, (dex, name) in enumerate(SAMPLES):
        r, c = divmod(i, cols)
        x0 = 14 + c*(block_w + bgap); y0 = 8 + r*row_h
        v = variants_for(dex)
        if v is None:
            print(f"  {name}: MISSING", file=sys.stderr); continue
        for vi, vname in enumerate(VARIANTS):
            col = (120, 120, 128) if vi == 0 else (150, 40, 40)
            d.text((x0 + vi*(cell+gap) + 2, y0), vname, fill=col, font=fvar)
        d.text((x0 + 2, y0 + top_h - 2), name, fill=(20, 30, 90), font=fname)
        for vi, cimg in enumerate(v):
            big = cimg.resize((cell, cell), Image.NEAREST)
            bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
            sheet.paste(bg, (x0 + vi*(cell+gap), y0 + top_h + name_h))
        print(f"  {name} ({dex})", file=sys.stderr)

    out = os.path.join(HERE, "..", "..", "sprite-demos", "red_gate_final.png"); sheet.save(out)
    print(f"\nwrote {out}  ({W}x{H})", file=sys.stderr)


if __name__ == "__main__":
    main()
