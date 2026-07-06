#!/usr/bin/env python3
"""Looped GIF of the diagonal rainbow WITH light eyes (saturation fades on bright
pixels so whites/eyes stay pale). Seamless loop. Writes GIF and opens it.
"""
import colorsys, importlib.util, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5; FRAMES = 24
SAMPLES = [(6, "Charizard"), (130, "Gyarados"), (25, "Pikachu"), (149, "Dragonite")]


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


def frame(base, off):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a >= 128:
                yv = luma(r, g, b)
                hue = 0.85*((x + y)/(2*(SIZE-1))) + off
                op[x, y] = (*hue_luma(hue, sat_lite(yv, 0.95), yv), 255)
    return out


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3)|(r >> 2), (g << 2)|(g >> 4), (b << 3)|(b >> 2)


def reconstruct(indices, pal):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); px = img.load()
    for i, idx in enumerate(indices):
        if idx:
            px[i % SIZE, i // SIZE] = (*rgb565_to_rgb(pal[idx]), 255)
    return img


def main():
    bases = []
    for dex, name in SAMPLES:
        img, shiny, label = b4m.fetch_pair(dex, "front")
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        idx, palN, _ = b4m.build_indexed(img, shiny)
        bases.append(reconstruct(idx, palN))
        print(f"  loaded {name} ({label})", file=sys.stderr)

    cell = SIZE*SCALE; pad = 8
    W = pad + len(bases)*(cell+pad); H = pad + cell + pad
    frames = []
    for f in range(FRAMES):
        off = f/FRAMES
        sheet = Image.new("RGB", (W, H), (248, 248, 248))
        for ci, base in enumerate(bases):
            big = frame(base, off).resize((cell, cell), Image.NEAREST)
            bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
            sheet.paste(bg, (pad + ci*(cell+pad), pad))
        frames.append(sheet)

    out = "/tmp/rainbow_lite_anim.gif"
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=70, loop=0)
    print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
