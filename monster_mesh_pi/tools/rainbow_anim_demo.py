#!/usr/bin/env python3
"""Animated preview of the two best rainbow methods, so we can see the motion:
  - Vertical bands, SCROLLING (rainbow flows up the body)
  - Spectrum-by-luma, SHIMMER (whole spectrum rotates)
Both luma-preserving. Writes an animated GIF and opens it.
"""
import colorsys, importlib.util, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5; FRAMES = 24
SAMPLES = [(130, "Gyarados"), (149, "Dragonite")]


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b


def hue_luma(hue, sat, y):
    br, bg, bb = colorsys.hsv_to_rgb(hue % 1.0, sat, 1.0)
    br, bg, bb = br*255, bg*255, bb*255
    s = y / (luma(br, bg, bb) or 1.0)
    return (min(255, int(br*s)), min(255, int(bg*s)), min(255, int(bb*s)))


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3)|(r >> 2), (g << 2)|(g >> 4), (b << 3)|(b >> 2)


def reconstruct(indices, pal):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); px = img.load()
    for i, idx in enumerate(indices):
        if idx:
            px[i % SIZE, i // SIZE] = (*rgb565_to_rgb(pal[idx]), 255)
    return img


# Both scroll by adding a full-cycle offset to the hue, so the loop is seamless
# (off runs 0 -> 1 across the frames and wraps back to the start exactly).
def frame_vscroll(base, off):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a >= 128:
                op[x, y] = (*hue_luma(0.85*(y/(SIZE-1)) + off, 0.95, luma(r, g, b)), 255)
    return out


def frame_dscroll(base, off):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a >= 128:
                op[x, y] = (*hue_luma(0.85*((x + y)/(2*(SIZE-1))) + off, 0.95, luma(r, g, b)), 255)
    return out


def main():
    bases = []
    for dex, name in SAMPLES:
        img, shiny, label = b4m.fetch_pair(dex, "front")
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        idx, palN, palS = b4m.build_indexed(img, shiny)
        bases.append(reconstruct(idx, palN))
        print(f"  loaded {name} ({label})", file=sys.stderr)

    cell = SIZE*SCALE; pad = 8
    cols = len(SAMPLES)*2               # vscroll + shimmer per species
    W = pad + cols*(cell+pad); H = pad + cell + pad
    frames = []
    for f in range(FRAMES):
        off = f/FRAMES
        sheet = Image.new("RGB", (W, H), (248, 248, 248))
        ci = 0
        for base in bases:
            for fn in (frame_vscroll, frame_dscroll):
                cimg = fn(base, off)
                big = cimg.resize((cell, cell), Image.NEAREST)
                bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
                sheet.paste(bg, (pad + ci*(cell+pad), pad)); ci += 1
        frames.append(sheet)

    out = "/tmp/rainbow_anim.gif"
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=70, loop=0)
    print(f"\nwrote {out} — cols: Gyarados[vertical, diagonal]  Dragonite[vertical, diagonal]",
          file=sys.stderr)


if __name__ == "__main__":
    main()
