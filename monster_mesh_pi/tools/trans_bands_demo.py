#!/usr/bin/env python3
"""Mock up TRANS-FLAG band skins (light-blue / pink / white / pink / light-blue)
in vertical and diagonal orientations, next to normal + full-rainbow vertical for
reference. Luma-preserving, so each mon's shading/form still reads through the
bands. Writes a labelled contact sheet PNG and opens it.
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 5
SAMPLES = [(6, "Charizard"), (130, "Gyarados"), (149, "Dragonite"), (196, "Espeon")]

# Trans pride flag stripes, top→bottom.
TRANS = [(91, 206, 250), (245, 169, 184), (255, 255, 255), (245, 169, 184), (91, 206, 250)]


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b


def hue_luma(hue, sat, y):
    br, bg, bb = colorsys.hsv_to_rgb(hue % 1.0, sat, 1.0)
    br, bg, bb = br*255, bg*255, bb*255
    s = y / (luma(br, bg, bb) or 1.0)
    return (min(255, int(br*s)), min(255, int(bg*s)), min(255, int(bb*s)))


def tint_to(target, y):
    """Recolour a pixel of brightness y toward `target`'s hue/sat, keeping y.
    Near-white stripe (low sat) -> grayscale, so shading still reads."""
    h, s, v = colorsys.rgb_to_hsv(target[0]/255, target[1]/255, target[2]/255)
    if s < 0.08:
        yy = int(max(0, min(255, y)))
        return (yy, yy, yy)
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


def m_rainbow_vert(base):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a >= 128:
                op[x, y] = (*hue_luma(0.85*(y/(SIZE-1)), 0.95, luma(r, g, b)), 255)
    return out


def m_trans(base, diagonal):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    n = len(TRANS)
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a < 128:
                continue
            t = ((x + y) / (2*(SIZE-1))) if diagonal else (y / (SIZE-1))
            band = min(n-1, int(t * n))
            op[x, y] = (*tint_to(TRANS[band], luma(r, g, b)), 255)
    return out


COLS = [
    ("Normal", "normal"),
    ("Rainbow\nvertical", "rainbow"),
    ("Trans\nvertical", "tv"),
    ("Trans\ndiagonal", "td"),
]


def main():
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 15)
    except Exception:
        font = ImageFont.load_default()
    cell = SIZE*SCALE; pad = 10; hdr = 40
    W = pad + len(COLS)*(cell+pad); H = hdr + len(SAMPLES)*(cell+pad)
    sheet = Image.new("RGB", (W, H), (245, 245, 245)); d = ImageDraw.Draw(sheet)
    for ci, (name, _) in enumerate(COLS):
        d.multiline_text((pad + ci*(cell+pad) + 4, 4), name, fill=(20, 20, 20), font=font)

    for ri, (dex, name) in enumerate(SAMPLES):
        img, shiny, label = b4m.fetch_pair(dex, "front")
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        idx, palN, palS = b4m.build_indexed(img, shiny)
        base = reconstruct(idx, palN)
        for ci, (_, key) in enumerate(COLS):
            if key == "normal":   cimg = base
            elif key == "rainbow": cimg = m_rainbow_vert(base)
            elif key == "tv":     cimg = m_trans(base, False)
            else:                 cimg = m_trans(base, True)
            big = cimg.resize((cell, cell), Image.NEAREST)
            bg = Image.new("RGB", (cell, cell), (255, 255, 255)); bg.paste(big, (0, 0), big)
            sheet.paste(bg, (pad + ci*(cell+pad), hdr + ri*(cell+pad)))
        d.text((pad+2, hdr + ri*(cell+pad) + 2), name, fill=(0, 0, 120), font=font)
        print(f"  rendered {name} ({label})", file=sys.stderr)

    out = "/tmp/trans_bands.png"
    sheet.save(out); print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
