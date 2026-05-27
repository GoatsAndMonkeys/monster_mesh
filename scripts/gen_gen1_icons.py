#!/usr/bin/env python3
"""Generate 151 unique 16x16 1-bit Gen-1 mini-icons for Gen1MiniIcons.h.

Pipeline (matches the comment block at the top of Gen1MiniIcons.h):

  1. Open PokeAPI Red/Blue front sprite PNG.
  2. Flood-fill from the 4 corners through near-white pixels -> background mask;
     everything else is the silhouette. Tight-crop to silhouette bbox.
  3. Silhouette mask S at 16x16: cell lit when foreground fill fraction >= 0.35.
  4. Detail mask D at 16x16: cell inked when ANY source pixel in the region is
     "very dark" (grayscale <= 32). Biases toward preserving thin outlines.
  5. Composite O = S AND NOT D, BUT only where the silhouette cell is INTERIOR
     (all 4-neighbors also lit in S). Keeps the body outline solid while
     revealing interior outlines as negative space.
  6. Pack to 32 bytes MSB-leftmost row-major.

Output: prints the 152-entry `kGen1IconBitmaps[152][32]` C array to stdout
(slot 0 unused, slots 1..151 indexed by Pokedex number).

Usage: ~/meshtastic-venv/bin/python3 gen_gen1_icons.py > /tmp/icons.txt
"""

import io
import sys
import urllib.request
from collections import deque
from PIL import Image

SPRITE_URL = (
    "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/"
    "pokemon/versions/generation-i/red-blue/transparent/{dex}.png"
)
# Fallback: non-transparent version (some sprites missing in transparent dir)
FALLBACK_URL = (
    "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/"
    "pokemon/versions/generation-i/red-blue/{dex}.png"
)

OUT_W = OUT_H = 16
FILL_THRESHOLD = 0.35   # cell-lit if >=35% foreground
DARK_THRESHOLD = 32     # grayscale value below this counts as inked detail
BG_NEAR_WHITE = 230     # pixels >= this in all channels = background candidate


def fetch_sprite(dex: int) -> Image.Image:
    """Download PNG for dex from PokeAPI. Returns RGBA Image."""
    for url in (SPRITE_URL.format(dex=dex), FALLBACK_URL.format(dex=dex)):
        try:
            with urllib.request.urlopen(url, timeout=30) as r:
                data = r.read()
            return Image.open(io.BytesIO(data)).convert("RGBA")
        except Exception as e:
            last = e
    raise RuntimeError(f"fetch failed for dex {dex}: {last}")


def background_mask(img: Image.Image) -> list:
    """4-corner flood fill through near-white pixels. Returns 2D bool array
    [y][x] where True = background. Treats alpha=0 as background too."""
    w, h = img.size
    px = img.load()
    bg = [[False] * w for _ in range(h)]

    def is_bg_pixel(x, y):
        r, g, b, a = px[x, y]
        if a == 0:
            return True
        return r >= BG_NEAR_WHITE and g >= BG_NEAR_WHITE and b >= BG_NEAR_WHITE

    q = deque()
    for cx, cy in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        if is_bg_pixel(cx, cy):
            bg[cy][cx] = True
            q.append((cx, cy))
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not bg[ny][nx] and is_bg_pixel(nx, ny):
                bg[ny][nx] = True
                q.append((nx, ny))
    return bg


def crop_to_silhouette(img: Image.Image, bg: list) -> tuple:
    """Tight-crop image+masks to silhouette bbox. Returns (cropped_img,
    cropped_bg) both centered & padded square."""
    w, h = img.size
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if not bg[y][x]:
                xs.append(x)
                ys.append(y)
    if not xs:
        # blank — return as-is
        return img, bg
    x0, x1 = min(xs), max(xs) + 1
    y0, y1 = min(ys), max(ys) + 1
    cw, ch = x1 - x0, y1 - y0
    # square-pad
    side = max(cw, ch)
    sx = x0 - (side - cw) // 2
    sy = y0 - (side - ch) // 2
    # clamp
    sx = max(0, min(w - side, sx))
    sy = max(0, min(h - side, sy))
    cropped = img.crop((sx, sy, sx + side, sy + side))
    new_bg = [row[sx:sx + side] for row in bg[sy:sy + side]]
    return cropped, new_bg


def build_masks(img: Image.Image, bg: list) -> tuple:
    """Return (silhouette[16][16] bool, detail[16][16] bool)."""
    w, h = img.size
    px = img.load()
    sil = [[False] * OUT_W for _ in range(OUT_H)]
    det = [[False] * OUT_W for _ in range(OUT_H)]
    for cy in range(OUT_H):
        y0 = int(cy * h / OUT_H)
        y1 = int((cy + 1) * h / OUT_H)
        if y1 <= y0:
            y1 = y0 + 1
        for cx in range(OUT_W):
            x0 = int(cx * w / OUT_W)
            x1 = int((cx + 1) * w / OUT_W)
            if x1 <= x0:
                x1 = x0 + 1
            fg = total = 0
            any_dark = False
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    total += 1
                    if not bg[yy][xx]:
                        fg += 1
                    r, g, b, _ = px[xx, yy]
                    gray = (r + g + b) // 3
                    if gray <= DARK_THRESHOLD:
                        any_dark = True
            if total and fg / total >= FILL_THRESHOLD:
                sil[cy][cx] = True
            if any_dark:
                det[cy][cx] = True
    return sil, det


def composite(sil: list, det: list) -> list:
    """O = sil AND NOT (det AND interior). Interior = sil & all 4-neighbors sil."""
    out = [[False] * OUT_W for _ in range(OUT_H)]
    for y in range(OUT_H):
        for x in range(OUT_W):
            if not sil[y][x]:
                continue
            interior = True
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < OUT_W and 0 <= ny < OUT_H and sil[ny][nx]):
                    interior = False
                    break
            if det[y][x] and interior:
                out[y][x] = False
            else:
                out[y][x] = True
    return out


def pack_bytes(grid: list) -> list:
    """Pack 16x16 bool grid to 32 bytes, 2 per row, MSB-leftmost."""
    bs = []
    for y in range(OUT_H):
        for byte_idx in range(2):
            b = 0
            for bit in range(8):
                x = byte_idx * 8 + bit
                if grid[y][x]:
                    b |= 1 << (7 - bit)
            bs.append(b)
    return bs


def grid_to_ascii(grid: list) -> str:
    return "\n".join("".join("#" if c else "." for c in row) for row in grid)


def emit_dex(dex: int, name: str, grid: list, out) -> None:
    bs = pack_bytes(grid)
    print(f"    // {dex:3d} {name}", file=out)
    print(f"    {{", file=out)
    for y in range(OUT_H):
        b0, b1 = bs[y * 2], bs[y * 2 + 1]
        ascii_row = "".join(
            "#" if grid[y][x] else "." for x in range(OUT_W)
        )
        print(f"        0b{b0:08b}, 0b{b1:08b}, // {ascii_row}", file=out)
    print(f"    }},", file=out)


# Names for dex 1..151 (canonical English Gen-1 names, abbreviated to fit comment column)
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
    print("// AUTO-GENERATED by scripts/gen_gen1_icons.py — DO NOT EDIT BY HAND.", file=out)
    print("// 152 × 32 = 4864 bytes of PROGMEM-friendly 1-bit 16×16 mini-icons,", file=out)
    print("// indexed directly by Pokédex number (slot 0 unused).", file=out)
    print("static const uint8_t kGen1IconBitmaps[152][32] = {", file=out)
    # slot 0 — blank
    blank = [[False] * OUT_W for _ in range(OUT_H)]
    emit_dex(0, "unused", blank, out)
    for dex in range(1, 152):
        try:
            img = fetch_sprite(dex)
            bg = background_mask(img)
            img, bg = crop_to_silhouette(img, bg)
            sil, det = build_masks(img, bg)
            grid = composite(sil, det)
            emit_dex(dex, NAMES[dex], grid, out)
        except Exception as e:
            print(f"// !!! dex {dex} failed: {e}", file=sys.stderr)
            emit_dex(dex, f"{NAMES[dex]} (FAILED — blank)", blank, out)
    print("};", file=out)


if __name__ == "__main__":
    main()
