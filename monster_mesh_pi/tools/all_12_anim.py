#!/usr/bin/env python3
"""Full contact sheet: all 12 color skins across, ~12 Pokemon down, as an
animated GIF. Only the three rainbow columns animate (band scrolls, seamless
loop); the other 9 are precomputed once for speed.

12 skins = 4 colors (Regular/Shiny/Pink/Rainbow) x 3 dark states (none/Dark/Blackout).
"""
import colorsys, importlib.util, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(g3)
b4 = importlib.util.spec_from_file_location("b4", os.path.join(HERE, "gen3_sprite_bake_4bpp.py"))
b4m = importlib.util.module_from_spec(b4); b4.loader.exec_module(b4m)

SIZE = 64; SCALE = 1
FRAMES = 16
LABELS = ["Reg", "Shiny", "Pink", "Rbw", "Dark", "Dk-Sh", "Dk-Pk", "Dk-Rbw",
          "Blk", "Bk-Sh", "Bk-Pk", "Bk-Rbw"]
NV = len(LABELS)
ANIM_COLS = {3, 7, 11}                 # Rainbow, Dark-Rainbow, Blackout-Rainbow
RIM_COLS = {8, 9, 10, 11}              # blackout tier gets the slate rim
SAMPLES = [(6, "Charizard"), (9, "Blastoise"), (3, "Venusaur"), (25, "Pikachu"),
           (94, "Gengar"), (130, "Gyarados"), (131, "Lapras"), (143, "Snorlax"),
           (149, "Dragonite"), (150, "Mewtwo"), (151, "Mew"), (248, "Tyranitar")]
_GAMMA = [int((i/255.0)**2.1 * 255 + 0.5) for i in range(256)]
BARBIE = (255, 46, 154); BH, _, _ = colorsys.rgb_to_hsv(BARBIE[0]/255, BARBIE[1]/255, BARBIE[2]/255)
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
    # "Smart" gate: pure red at any brightness, plus DARK warm accents only.
    # Catches Gyarados/Blastoise mouths + Gengar/Charizard eyes+flame, while
    # sparing bright orange bodies (Charizard/Scizor stay grey).
    h, s, v = colorsys.rgb_to_hsv(r/255, g/255, b/255); hd = h*360
    if s >= 0.45 and (hd <= 16 or hd >= 335): return True   # pure red, any brightness
    return s >= 0.42 and hd <= 30 and v <= 0.72             # dark warm accents only


def _hue(x, yy, phase): return 0.85 * ((x + yy) / (2*(SIZE-1))) + phase


# ── recolor fns: (rgb, x, y, phase) → rgb ─────────────────────────────────────
def f_ident(c, x, y, p):  return c
def f_pink(c, x, y, p):   yy = luma(*c); return hue_luma(BH, sat_lite(yy, 0.85), yy)
def f_rainbow(c, x, y, p): yy = luma(*c); return hue_luma(_hue(x, y, p), sat_lite(yy, 0.95), yy)
def f_dark(c, x, y, p):
    r, g, b = c
    if is_red(r, g, b): return (r, g, b)
    v = _GAMMA[clamp(luma(r, g, b))]; return (v, v, v)
def f_dpink(c, x, y, p):
    r, g, b = c
    if is_red(r, g, b): yy = luma(r, g, b); return hue_luma(BH, sat_lite(yy, 0.85), yy)
    v = _GAMMA[clamp(luma(r, g, b))]; return (v, v, v)
def f_drbw(c, x, y, p):
    r, g, b = c
    if is_red(r, g, b): yy = luma(r, g, b); return hue_luma(_hue(x, y, p), sat_lite(yy, 0.95), yy)
    v = _GAMMA[clamp(luma(r, g, b))]; return (v, v, v)
def f_blk(c, x, y, p):                     # Blackout: red→maroon, else black
    r, g, b = c
    if is_red(r, g, b): return (clamp(r*0.55), clamp(g*0.45), clamp(b*0.45))
    return (0, 0, 0)
def f_bpink(c, x, y, p):
    r, g, b = c
    if is_red(r, g, b):
        yy = luma(r, g, b); pr, pg, pb = hue_luma(BH, sat_lite(yy, 0.90), yy)
        return (clamp(pr*0.60), clamp(pg*0.60), clamp(pb*0.60))
    return (0, 0, 0)
def f_brbw(c, x, y, p):
    r, g, b = c
    if is_red(r, g, b):
        yy = luma(r, g, b); pr, pg, pb = hue_luma(_hue(x, y, p), sat_lite(yy, 0.95), yy)
        return (clamp(pr*0.62), clamp(pg*0.62), clamp(pb*0.62))
    return (0, 0, 0)


# column i → (source 'base'|'shiny', fn)
COLS = [("base", f_ident), ("shiny", f_ident), ("base", f_pink), ("base", f_rainbow),
        ("base", f_dark), ("shiny", f_dark), ("base", f_dpink), ("base", f_drbw),
        ("base", f_blk), ("shiny", f_blk), ("base", f_bpink), ("base", f_brbw)]


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


def tile(cimg, cell, plate):
    big = cimg.resize((cell, cell), Image.NEAREST)
    bg = Image.new("RGB", (cell, cell), plate); bg.paste(big, (0, 0), big)
    return bg


def main():
    try:
        fname = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 12)
        fvar = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 10)
    except Exception:
        fname = fvar = ImageFont.load_default()

    cell = SIZE*SCALE                       # 128
    gap = 3; left_w = 92; header_h = 20
    row_h = cell + 4
    grid_w = NV*cell + (NV-1)*gap
    W = 8 + left_w + grid_w + 8
    H = 6 + header_h + len(SAMPLES)*row_h + 8

    mons = []
    for dex, name in SAMPLES:
        img, shiny, label = b4m.fetch_pair(dex, "front")
        if img is None: print(f"  {name}: MISSING", file=sys.stderr); continue
        img = g3.scale_rgba(img, SIZE, SIZE)
        shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
        idx, palN, palS = b4m.build_indexed(img, shiny)
        srcs = {"base": reconstruct(idx, palN), "shiny": reconstruct(idx, palS)}
        # precompute the 9 static tiles once
        static = {}
        for i, (src, fn) in enumerate(COLS):
            if i in ANIM_COLS: continue
            plate = (206, 212, 224) if i in RIM_COLS else (255, 255, 255)
            static[i] = tile(apply(srcs[src], fn, 0.0, i in RIM_COLS), cell, plate)
        mons.append((name, srcs, static))
        print(f"  prepped {name} ({dex})", file=sys.stderr)

    frames = []
    for f in range(FRAMES):
        phase = f / FRAMES
        sheet = Image.new("RGB", (W, H), (236, 238, 243)); d = ImageDraw.Draw(sheet)
        gx = 8 + left_w
        for i, lab in enumerate(LABELS):
            col = (150, 60, 30) if i >= 8 else ((90, 90, 110) if i >= 4 else (120, 120, 128))
            d.text((gx + i*(cell+gap) + 3, 6), lab, fill=col, font=fvar)
        for ri, (name, srcs, static) in enumerate(mons):
            y0 = 6 + header_h + ri*row_h
            d.text((8, y0 + cell//2 - 8), name, fill=(20, 30, 90), font=fname)
            for i, (src, fn) in enumerate(COLS):
                x = gx + i*(cell+gap)
                if i in ANIM_COLS:
                    plate = (206, 212, 224) if i in RIM_COLS else (255, 255, 255)
                    t = tile(apply(srcs[src], fn, phase, i in RIM_COLS), cell, plate)
                else:
                    t = static[i]
                sheet.paste(t, (x, y0))
        frames.append(sheet)
        print(f"  frame {f+1}/{FRAMES}", file=sys.stderr)

    # One shared global palette across every frame (dither off) so static pixels
    # encode identically each frame — no requantization shimmer. Only the three
    # rainbow columns actually change RGB, so only they animate.
    montage = Image.new("RGB", (W, H*len(frames)))
    for i, fr in enumerate(frames):
        montage.paste(fr, (0, i*H))
    pal_img = montage.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE)
    pframes = [fr.quantize(palette=pal_img, dither=Image.NONE) for fr in frames]

    out = os.path.join(HERE, "..", "..", "sprite-demos", "all_12_anim.gif")
    pframes[0].save(out, save_all=True, append_images=pframes[1:], duration=80, loop=0,
                    disposal=1)
    print(f"\nwrote {out}  ({W}x{H}, {FRAMES} frames)", file=sys.stderr)


if __name__ == "__main__":
    main()
