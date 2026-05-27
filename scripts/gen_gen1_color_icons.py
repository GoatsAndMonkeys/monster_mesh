#!/usr/bin/env python3
"""Generate 151 unique 40×40 4-color RLE-compressed Gen-1 sprites for T114.

Source: PokeAPI Gen-I Yellow front sprites (GBC palette). Fallback to Red/Blue.
Pipeline:
  1. Fetch PNG; flood-fill 4 corners through near-white -> background. Tight-
     crop silhouette, square-pad.
  2. Downsample to 40×40 via area-resample with alpha matting (transparent
     pixels collapse to a sentinel "transparent" color).
  3. Quantize to 4 colours (one reserved for transparent/background).
  4. RLE-encode the 1600 pixel indices: each byte = (run_len-1) << 2 | idx,
     where run_len in 1..64. Runs longer than 64 split across multiple bytes.
  5. Emit a C header with per-species palette (4 × RGB565) + RLE byte stream
     plus a directory table mapping dex -> offset/length.

Output: prints C definitions to stdout, suitable for #include into a header.

Usage: ~/meshtastic-venv/bin/python3 gen_gen1_color_icons.py > /tmp/color.txt
"""

import io
import sys
import urllib.request
from collections import deque
from PIL import Image

URL_PRIMARY = (
    "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/"
    "pokemon/versions/generation-ii/crystal/transparent/{dex}.png"
)
URL_FALLBACK = (
    "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/"
    "pokemon/versions/generation-i/yellow/transparent/{dex}.png"
)

OUT_W = OUT_H = 56
BG_NEAR_WHITE = 230
PAL_SIZE = 4  # 4 colours per sprite (one is transparent/background)
TRANSPARENT_IDX = 0  # always palette[0] is the BG / transparent colour


def fetch(dex: int) -> Image.Image:
    last = None
    for tpl in (URL_PRIMARY, URL_FALLBACK):
        try:
            with urllib.request.urlopen(tpl.format(dex=dex), timeout=30) as r:
                return Image.open(io.BytesIO(r.read())).convert("RGBA")
        except Exception as e:
            last = e
    raise RuntimeError(f"dex {dex}: {last}")


def background_mask(img):
    w, h = img.size
    px = img.load()
    bg = [[False] * w for _ in range(h)]

    def is_bg(x, y):
        r, g, b, a = px[x, y]
        if a == 0:
            return True
        return r >= BG_NEAR_WHITE and g >= BG_NEAR_WHITE and b >= BG_NEAR_WHITE

    q = deque()
    for cx, cy in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        if is_bg(cx, cy):
            bg[cy][cx] = True
            q.append((cx, cy))
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not bg[ny][nx] and is_bg(nx, ny):
                bg[ny][nx] = True
                q.append((nx, ny))
    return bg


def crop_square(img, bg):
    w, h = img.size
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if not bg[y][x]:
                xs.append(x); ys.append(y)
    if not xs:
        return img, bg
    x0, x1 = min(xs), max(xs) + 1
    y0, y1 = min(ys), max(ys) + 1
    cw, ch = x1 - x0, y1 - y0
    side = max(cw, ch)
    sx = max(0, min(w - side, x0 - (side - cw) // 2))
    sy = max(0, min(h - side, y0 - (side - ch) // 2))
    cropped = img.crop((sx, sy, sx + side, sy + side))
    new_bg = [row[sx:sx + side] for row in bg[sy:sy + side]]
    return cropped, new_bg


def matte_to_white(img, bg):
    """Replace background pixels with pure-white. HARD-ALPHA cut at edges
    (semi-transparent pixels go to white, not blended) so the 4-colour
    quantization doesn't introduce halo pixels at sprite outlines."""
    w, h = img.size
    out = img.copy()
    px = out.load()
    for y in range(h):
        for x in range(w):
            if bg[y][x]:
                px[x, y] = (255, 255, 255, 255)
            else:
                r, g, b, a = px[x, y]
                if a < 128:
                    # Mostly-transparent → treat as background.
                    px[x, y] = (255, 255, 255, 255)
                else:
                    # Mostly-opaque → keep original RGB (no alpha blending).
                    px[x, y] = (r, g, b, 255)
    return out


def quantize_to_4(img):
    """Quantize to 4 colours using Pillow median-cut. Force palette[0] = white
    (background) by relabeling the cluster nearest to white. Returns indexed
    image (mode 'P') with palette index 0 == background."""
    p = img.convert("RGB").quantize(colors=PAL_SIZE, method=Image.Quantize.MEDIANCUT)
    pal_raw = p.getpalette()[: PAL_SIZE * 3]
    pal = [(pal_raw[i * 3], pal_raw[i * 3 + 1], pal_raw[i * 3 + 2]) for i in range(PAL_SIZE)]
    # Find the index closest to pure white (background).
    def whiteness(rgb):
        r, g, b = rgb
        return -((255 - r) ** 2 + (255 - g) ** 2 + (255 - b) ** 2)
    bg_idx = max(range(PAL_SIZE), key=lambda i: whiteness(pal[i]))
    if bg_idx != TRANSPARENT_IDX:
        # Swap palette entries 0 and bg_idx, AND remap the image pixels.
        pal[TRANSPARENT_IDX], pal[bg_idx] = pal[bg_idx], pal[TRANSPARENT_IDX]
        w, h = p.size
        px = p.load()
        for y in range(h):
            for x in range(w):
                v = px[x, y]
                if v == TRANSPARENT_IDX:
                    px[x, y] = bg_idx
                elif v == bg_idx:
                    px[x, y] = TRANSPARENT_IDX
    return p, pal


def rgb_to_565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rle_encode(indices: list) -> list:
    """Compress a flat list of palette indices (0..3) using the per-byte
    format: (run_len-1) << 2 | idx, where run_len in 1..64."""
    out = []
    i = 0
    n = len(indices)
    while i < n:
        v = indices[i]
        run = 1
        while i + run < n and indices[i + run] == v and run < 64:
            run += 1
        out.append(((run - 1) << 2) | (v & 0x3))
        i += run
    return out


NAMES = [
    "", "Bulbasaur", "Ivysaur", "Venusaur", "Charmander", "Charmeleon", "Charizard",
    "Squirtle", "Wartortle", "Blastoise", "Caterpie", "Metapod", "Butterfree",
    "Weedle", "Kakuna", "Beedrill", "Pidgey", "Pidgeotto", "Pidgeot",
    "Rattata", "Raticate", "Spearow", "Fearow", "Ekans", "Arbok", "Pikachu",
    "Raichu", "Sandshrew", "Sandslash", "NidoranF", "Nidorina", "Nidoqueen",
    "NidoranM", "Nidorino", "Nidoking", "Clefairy", "Clefable", "Vulpix",
    "Ninetales", "Jigglypuff", "Wigglytuff", "Zubat", "Golbat", "Oddish",
    "Gloom", "Vileplume", "Paras", "Parasect", "Venonat", "Venomoth",
    "Diglett", "Dugtrio", "Meowth", "Persian", "Psyduck", "Golduck",
    "Mankey", "Primeape", "Growlithe", "Arcanine", "Poliwag", "Poliwhirl",
    "Poliwrath", "Abra", "Kadabra", "Alakazam", "Machop", "Machoke",
    "Machamp", "Bellsprout", "Weepinbell", "Victreebel", "Tentacool",
    "Tentacruel", "Geodude", "Graveler", "Golem", "Ponyta", "Rapidash",
    "Slowpoke", "Slowbro", "Magnemite", "Magneton", "Farfetchd", "Doduo",
    "Dodrio", "Seel", "Dewgong", "Grimer", "Muk", "Shellder", "Cloyster",
    "Gastly", "Haunter", "Gengar", "Onix", "Drowzee", "Hypno", "Krabby",
    "Kingler", "Voltorb", "Electrode", "Exeggcute", "Exeggutor", "Cubone",
    "Marowak", "Hitmonlee", "Hitmonchan", "Lickitung", "Koffing", "Weezing",
    "Rhyhorn", "Rhydon", "Chansey", "Tangela", "Kangaskhan", "Horsea",
    "Seadra", "Goldeen", "Seaking", "Staryu", "Starmie", "MrMime", "Scyther",
    "Jynx", "Electabuzz", "Magmar", "Pinsir", "Tauros", "Magikarp",
    "Gyarados", "Lapras", "Ditto", "Eevee", "Vaporeon", "Jolteon",
    "Flareon", "Porygon", "Omanyte", "Omastar", "Kabuto", "Kabutops",
    "Aerodactyl", "Snorlax", "Articuno", "Zapdos", "Moltres", "Dratini",
    "Dragonair", "Dragonite", "Mewtwo", "Mew",
]


def main():
    out = sys.stdout

    # Collect all sprite data first so we can build the directory + stream.
    palettes = [(0, 0, 0, 0)] * 152
    rle_streams = [b""] * 152
    blank_pal = (0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF)

    # Slot 0 = blank.
    palettes[0] = blank_pal
    rle_streams[0] = bytes(rle_encode([0] * (OUT_W * OUT_H)))

    for dex in range(1, 152):
        try:
            img = fetch(dex)
            bg = background_mask(img)
            img, bg = crop_square(img, bg)
            img = matte_to_white(img, bg)
            img = img.resize((OUT_W, OUT_H), Image.NEAREST)
            qimg, pal = quantize_to_4(img)
            indices = list(qimg.getdata())
            stream = bytes(rle_encode(indices))
            palettes[dex] = tuple(rgb_to_565(c) for c in pal)
            rle_streams[dex] = stream
            print(f"// dex {dex:3d} {NAMES[dex]}: rle={len(stream)} B",
                  file=sys.stderr)
        except Exception as e:
            print(f"// !!! dex {dex} {NAMES[dex]} FAILED: {e}", file=sys.stderr)
            palettes[dex] = blank_pal
            rle_streams[dex] = bytes(rle_encode([0] * (OUT_W * OUT_H)))

    total_data = sum(len(s) for s in rle_streams)
    print(f"// Total RLE data: {total_data} bytes ({total_data/1024:.1f} KB)",
          file=sys.stderr)

    # Build flat byte stream + per-dex offset directory.
    print("// AUTO-GENERATED by scripts/gen_gen1_color_icons.py — DO NOT EDIT.", file=out)
    print(f"// 152 sprites at {OUT_W}x{OUT_H} 2bpp + 4-colour palette per", file=out)
    print(f"// species, RLE-compressed. Total data: {total_data} bytes.", file=out)
    print(f"// Sprite slot 0 is unused (blank); slots 1..151 indexed by Pokedex#.", file=out)
    print("", file=out)
    print(f"static constexpr uint16_t GEN1_COLOR_W = {OUT_W};", file=out)
    print(f"static constexpr uint16_t GEN1_COLOR_H = {OUT_H};", file=out)
    print("", file=out)

    # Palettes table — 4 × uint16_t (RGB565) per species, indexed by dex.
    print("static const uint16_t kGen1ColorPalettes[152][4] = {", file=out)
    for dex in range(152):
        p = palettes[dex]
        nm = NAMES[dex] if dex else "unused"
        print(f"    {{ 0x{p[0]:04X}, 0x{p[1]:04X}, 0x{p[2]:04X}, 0x{p[3]:04X} }}, // {dex:3d} {nm}",
              file=out)
    print("};", file=out)
    print("", file=out)

    # Concatenated RLE stream + offset directory.
    offsets = [0] * 153   # offsets[dex] = start, offsets[dex+1] = end
    flat = bytearray()
    for dex in range(152):
        offsets[dex] = len(flat)
        flat.extend(rle_streams[dex])
    offsets[152] = len(flat)

    print(f"static const uint8_t kGen1ColorRle[{len(flat)}] = {{", file=out)
    for i in range(0, len(flat), 16):
        chunk = flat[i:i + 16]
        print("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",", file=out)
    print("};", file=out)
    print("", file=out)

    print("static const uint32_t kGen1ColorRleOffsets[153] = {", file=out)
    for i in range(0, len(offsets), 12):
        chunk = offsets[i:i + 12]
        print("    " + ", ".join(str(v) for v in chunk) + ",", file=out)
    print("};", file=out)


if __name__ == "__main__":
    main()
