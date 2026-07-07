#!/usr/bin/env python3
"""Auto-accent: Dark/Blackout keep the mon's RED if it has red (Smart gate);
otherwise they keep the mon's most-vivid colour so red-less mons still get a pop
of colour instead of flat grey/black.

Columns: Regular | Dark | Blackout   (prints the chosen accent per mon)
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 2
VARIANTS = ["Regular", "Dark", "Blackout"]
NV = len(VARIANTS)
SAMPLES = [(6, "Charizard"), (9, "Blastoise"), (3, "Venusaur"), (25, "Pikachu"),
           (94, "Gengar"), (130, "Gyarados"), (131, "Lapras"), (143, "Snorlax"),
           (149, "Dragonite"), (150, "Mewtwo"), (151, "Mew"), (248, "Tyranitar")]
_GAMMA = [int((i/255.0)**2.1 * 255 + 0.5) for i in range(256)]
RIM = (60, 66, 82, 255)


def luma(r, g, b): return 0.299*r + 0.587*g + 0.114*b
def clamp(v): return max(0, min(255, int(v)))
def hsv(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255); return h*360, s, v


def is_red(r, g, b):
    hd, s, v = hsv(r, g, b)
    if s >= 0.45 and (hd <= 16 or hd >= 335): return True
    return s >= 0.42 and hd <= 30 and v <= 0.72


def pick_accent(base):
    """Return (predicate, label). Red if the mon has red; else its most-vivid
    MINORITY colour — the top-saturation hue that is NOT the dominant body hue,
    so we keep a small pop (eyes/gems/tips) rather than recolouring the body."""
    ip = base.load(); reds = 0; total = 0
    cover = {}; satsum = {}                         # per hue bucket
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a < 128: continue
            total += 1
            if is_red(r, g, b): reds += 1
            hd, s, v = hsv(r, g, b)
            if s >= 0.40 and 0.12 < v < 0.98:
                bkt = int(hd // 15)                 # 24 hue buckets
                cover[bkt] = cover.get(bkt, 0) + 1
                satsum[bkt] = satsum.get(bkt, 0.0) + s   # vividness x coverage
    if total and reds >= max(6, total * 0.012):
        return is_red, "red"
    if not cover:
        return is_red, "red(none)"
    body = max(cover, key=cover.get)                # biggest region = body → exclude
    cand = [b for b in cover if b != body] or [body]
    acc = max(cand, key=lambda b: satsum[b])        # most vivid*present non-body hue
    dh = acc * 15 + 7.5
    def gate(r, g, b, dh=dh):
        hd, s, v = hsv(r, g, b)
        d = abs(((hd - dh + 180) % 360) - 180)
        return s >= 0.35 and d <= 22
    return gate, f"hue~{int(dh)}"


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r << 3)|(r >> 2), (g << 2)|(g >> 4), (b << 3)|(b >> 2)


def reconstruct(indices, pal):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); px = img.load()
    for i, idx in enumerate(indices):
        if idx: px[i % SIZE, i // SIZE] = (*rgb565_to_rgb(pal[idx]), 255)
    return img


def make_dark(base, accent):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a < 128: continue
            if accent(r, g, b): op[x, y] = (r, g, b, 255)
            else:
                yv = _GAMMA[clamp(luma(r, g, b))]; op[x, y] = (yv, yv, yv, 255)
    return out


def make_blackout(base, accent):
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)); ip, op = base.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = ip[x, y]
            if a < 128: continue
            if accent(r, g, b):
                op[x, y] = (clamp(r*0.60), clamp(g*0.55), clamp(b*0.55), 255)  # darkened accent
            else:
                op[x, y] = (0, 0, 0, 255)
    for y in range(SIZE):
        for x in range(SIZE):
            if op[x, y][3] < 128: continue
            for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                nx, ny = x+dx, y+dy
                if not (0 <= nx < SIZE and 0 <= ny < SIZE) or ip[nx, ny][3] < 128:
                    op[x, y] = RIM; break
    return out


def variants_for(dex, name):
    img, shiny, label = b4m.fetch_pair(dex, "front")
    if img is None: return None
    img = g3.scale_rgba(img, SIZE, SIZE)
    shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
    idx, palN, _ = b4m.build_indexed(img, shiny)
    base = reconstruct(idx, palN)
    accent, lab = pick_accent(base)
    print(f"  {name:10} accent = {lab}", file=sys.stderr)
    return [base, make_dark(base, accent), make_blackout(base, accent)]


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
    cols = 3; rows = (len(SAMPLES) + cols - 1) // cols; bgap = 26
    W = 14 + cols*block_w + (cols-1)*bgap + 14
    H = 8 + rows*row_h + 8
    sheet = Image.new("RGB", (W, H), (236, 238, 243)); d = ImageDraw.Draw(sheet)

    for i, (dex, name) in enumerate(SAMPLES):
        r, c = divmod(i, cols)
        x0 = 14 + c*(block_w + bgap); y0 = 8 + r*row_h
        v = variants_for(dex, name)
        if v is None:
            print(f"  {name}: MISSING", file=sys.stderr); continue
        for vi, vname in enumerate(VARIANTS):
            col = (120, 120, 128) if vi == 0 else (150, 40, 40)
            d.text((x0 + vi*(cell+gap) + 2, y0), vname, fill=col, font=fvar)
        d.text((x0 + 2, y0 + top_h - 2), name, fill=(20, 30, 90), font=fname)
        for vi, cimg in enumerate(v):
            big = cimg.resize((cell, cell), Image.NEAREST)
            plate = (206, 212, 224) if vi == 2 else (255, 255, 255)
            bg = Image.new("RGB", (cell, cell), plate); bg.paste(big, (0, 0), big)
            sheet.paste(bg, (x0 + vi*(cell+gap), y0 + top_h + name_h))

    out = os.path.join(HERE, "..", "..", "sprite-demos", "accent_preview.png"); sheet.save(out)
    print(f"\nwrote {out}  ({W}x{H})", file=sys.stderr)


if __name__ == "__main__":
    main()
