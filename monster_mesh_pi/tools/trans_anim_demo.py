#!/usr/bin/env python3
"""Animated TRANS-FLAG band preview matching the on-device scroll (slow, smooth,
1 row/frame). Vertical + diagonal, seamless loop. Writes GIF and opens it.
"""
import colorsys, importlib.util, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5; FRAMES = 64
SAMPLES = [(130, "Gyarados"), (149, "Dragonite")]
TRANS = [(91, 206, 250), (245, 169, 184), (255, 255, 255), (245, 169, 184), (91, 206, 250)]


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b


def hue_luma(hue, sat, y):
    br, bg, bb = colorsys.hsv_to_rgb(hue % 1.0, sat, 1.0)
    br, bg, bb = br*255, bg*255, bb*255
    s = y / (luma(br, bg, bb) or 1.0)
    return (min(255, int(br*s)), min(255, int(bg*s)), min(255, int(bb*s)))


def tint_to(target, y):
    h, s, v = colorsys.rgb_to_hsv(target[0]/255, target[1]/255, target[2]/255)
    if s < 0.08:
        yy = int(max(0, min(255, y))); return (yy, yy, yy)
    return hue_luma(h, s, y)


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3)|(r >> 2), (g << 2)|(g >> 4), (b << 3)|(b >> 2)


def reconstruct(indices, pal):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); px = img.load()
    for i, idx in enumerate(indices):
        if idx:
            px[i % SIZE, i // SIZE] = (*rgb565_to_rgb(pal[idx]), 255)
    return img


def frame_tv(base, off):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        band = ((y + off) % SIZE) * 5 // SIZE
        if band > 4: band = 4
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a >= 128:
                op[x, y] = (*tint_to(TRANS[band], luma(r, g, b)), 255)
    return out


def frame_td(base, off):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a < 128: continue
            band = ((x + y + off) % (2*SIZE)) * 5 // (2*SIZE)
            if band > 4: band = 4
            op[x, y] = (*tint_to(TRANS[band], luma(r, g, b)), 255)
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
    cols = len(SAMPLES)*2
    W = pad + cols*(cell+pad); H = pad + cell + pad
    frames = []
    for f in range(FRAMES):
        sheet = Image.new("RGB", (W, H), (248, 248, 248)); ci = 0
        for base in bases:
            for fn, mul in ((frame_tv, 1), (frame_td, 2)):   # diag steps 2 -> seamless in 64
                cimg = fn(base, f*mul)
                big = cimg.resize((cell, cell), Image.NEAREST)
                bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
                sheet.paste(bg, (pad + ci*(cell+pad), pad)); ci += 1
        frames.append(sheet)

    out = "/tmp/trans_anim.gif"
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=90, loop=0)
    print(f"\nwrote {out} — cols: Gyarados[vertical, diagonal]  Dragonite[vertical, diagonal]",
          file=sys.stderr)


if __name__ == "__main__":
    main()
