#!/usr/bin/env python3
"""Bake Gen 2 (Crystal) Pokémon sprites into two C++ headers for the
MonsterMesh-Pi terminal:

  - src/terminal/Gen2ColorIcons.h  (front sprites, 56x56)
  - src/terminal/Gen2BackIcons.h   (back sprites,  48x48)

Pipeline (per spec):
  1. Download PNG from PokeAPI (Crystal first, fallback to Gold/Silver, then
     Gen-I Yellow). Cache to tools/.cache/ to make re-runs idempotent.
  2. Pad/scale to canonical size (NEAREST). Treat alpha < 128 as transparent
     background; matte to white before quantization.
  3. Quantize to 4 colors via MEDIANCUT.
  4. Sort palette so index 0 = lightest (highest luminance), index 3 = darkest.
     Remap pixel indices accordingly. Transparent pixels collapse to index 0.
  5. Convert palette to RGB565.
  6. Pack pixels as 2 bpp, big-endian within byte (4 px/byte, MSB = leftmost).
  7. RAW-DEFLATE compress with wbits=-9 (512-byte window) level 9.
  8. Concatenate all 152 blobs (slot 0 is empty) and emit per-dex offsets.

Run from anywhere: outputs are written using absolute paths.

Usage:
    ~/meshtastic-venv/bin/python3 tools/gen2_sprite_bake.py
"""

from __future__ import annotations

import io
import os
import sys
import time
import urllib.request
import zlib
from pathlib import Path
from PIL import Image

# -------------------------------------------------------------------- paths --
ROOT       = Path("/Users/goatsandmonkeys/Documents/pokemesh/monster_mesh_pi")
CACHE_DIR  = ROOT / "tools" / ".cache"
OUT_FRONT  = ROOT / "src" / "terminal" / "Gen2ColorIcons.h"
OUT_BACK   = ROOT / "src" / "terminal" / "Gen2BackIcons.h"

# vendored puff
PENTEST_DIR = Path("/Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware/src/modules/pentest")
PUFF_C_SRC  = PENTEST_DIR / "puff.c"
PUFF_H_SRC  = PENTEST_DIR / "puff.h"
PUFF_C_DST  = ROOT / "src" / "terminal" / "puff.c"
PUFF_H_DST  = ROOT / "src" / "terminal" / "puff.h"

# ----------------------------------------------------------------- constants -
FRONT_W = FRONT_H = 56
BACK_W  = BACK_H  = 48
PAL_SIZE = 4

URL_VARIANTS = [
    # (label, front_url_template, back_url_template)
    ("crystal",
     "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-ii/crystal/{dex}.png",
     "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-ii/crystal/back/{dex}.png"),
    ("gold",
     "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-ii/gold/{dex}.png",
     "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-ii/gold/back/{dex}.png"),
    ("silver",
     "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-ii/silver/{dex}.png",
     "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-ii/silver/back/{dex}.png"),
    ("yellow",
     "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-i/yellow/transparent/{dex}.png",
     "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-i/yellow/back/transparent/{dex}.png"),
]

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

# --------------------------------------------------------------- download ---

def cache_path(label: str, dex: int, kind: str) -> Path:
    return CACHE_DIR / f"{label}_{kind}_{dex:03d}.png"

def _download(url: str, dst: Path) -> bool:
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            data = r.read()
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(data)
        return True
    except Exception:
        return False

def fetch_with_fallback(dex: int, kind: str):
    """Return (PIL.Image RGBA, label_used) or (None, None)."""
    for label, fr_tpl, bk_tpl in URL_VARIANTS:
        tpl = fr_tpl if kind == "front" else bk_tpl
        url = tpl.format(dex=dex)
        cp  = cache_path(label, dex, kind)
        if not cp.exists():
            if not _download(url, cp):
                continue
        # Loaded from cache (or freshly downloaded).
        try:
            img = Image.open(io.BytesIO(cp.read_bytes())).convert("RGBA")
            return img, label
        except Exception:
            try: cp.unlink()
            except Exception: pass
            continue
    return None, None

# --------------------------------------------------------------- pipeline ---

def matte_to_white(img: Image.Image) -> Image.Image:
    """Replace transparent pixels with pure white. Hard-alpha cut (<128 -> bg)."""
    w, h = img.size
    out = img.copy()
    px = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                px[x, y] = (255, 255, 255, 255)
            else:
                px[x, y] = (r, g, b, 255)
    return out

def pad_or_scale(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    """If smaller, NEAREST upscale to fit (preserving square aspect); if larger,
    downscale NEAREST. If smaller and non-square or doesn't fit exactly, center-
    pad with white the remainder."""
    w, h = img.size
    if (w, h) == (target_w, target_h):
        return img
    # All sprites should be square; scale to fit target.
    if w == h:
        return img.resize((target_w, target_h), Image.NEAREST)
    # Non-square fallback: scale longest side, center-pad with white.
    scale = min(target_w / w, target_h / h)
    nw = max(1, int(round(w * scale)))
    nh = max(1, int(round(h * scale)))
    scaled = img.resize((nw, nh), Image.NEAREST)
    canvas = Image.new("RGBA", (target_w, target_h), (255, 255, 255, 255))
    canvas.paste(scaled, ((target_w - nw) // 2, (target_h - nh) // 2), scaled)
    return canvas

def quantize_4(img: Image.Image):
    """Quantize RGB image to 4 colors using MEDIANCUT. Returns (P-mode image, palette list of 4 RGB tuples)."""
    p = img.convert("RGB").quantize(colors=PAL_SIZE, method=Image.Quantize.MEDIANCUT)
    raw = p.getpalette()
    if raw is None:
        raw = []
    # Pillow may return fewer than 4 entries when the image has <4 unique
    # colors. Pad with white so we always emit a 4-entry palette.
    pal = []
    for i in range(PAL_SIZE):
        if i * 3 + 2 < len(raw):
            pal.append((raw[i*3], raw[i*3+1], raw[i*3+2]))
        else:
            pal.append((255, 255, 255))
    return p, pal

def sort_palette_by_luminance(p_img: Image.Image, pal: list):
    """Reorder palette so index 0 = lightest, index 3 = darkest. Remap pixel data."""
    # Rec.601 luminance.
    def lum(rgb):
        r, g, b = rgb
        return 0.299 * r + 0.587 * g + 0.114 * b
    order = sorted(range(len(pal)), key=lambda i: -lum(pal[i]))  # descending
    new_pal = [pal[i] for i in order]
    # Build remap table: old_idx -> new_idx
    remap = [0] * len(pal)
    for new_i, old_i in enumerate(order):
        remap[old_i] = new_i
    w, h = p_img.size
    px = p_img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = remap[px[x, y]]
    return p_img, new_pal

def rgb_to_565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def pack_2bpp(indices: list[int], w: int, h: int) -> bytes:
    """Pack 2 bpp big-endian (MSB = leftmost pixel of the 4-pixel group)."""
    assert len(indices) == w * h
    out = bytearray()
    # rows are tightly packed; w is divisible by 4 for 56 and 48.
    bytes_per_row = (w + 3) // 4
    for y in range(h):
        for bx in range(bytes_per_row):
            v = 0
            for k in range(4):
                x = bx * 4 + k
                idx = indices[y * w + x] & 0x3 if x < w else 0
                v |= idx << (6 - 2 * k)
            out.append(v)
    return bytes(out)

def raw_deflate(data: bytes) -> bytes:
    """RAW deflate with 512-byte window (wbits=-9), level 9 — matches Gen1 baker."""
    co = zlib.compressobj(level=9, method=zlib.DEFLATED, wbits=-9)
    return co.compress(data) + co.flush()

# ------------------------------------------------------------ header writer -

def write_header(path: Path,
                 macro_w: str, macro_h: str,
                 pal_sym: str, deflate_sym: str, off_sym: str,
                 w: int, h: int,
                 palettes: list[tuple], deflates: list[bytes],
                 title: str):
    flat = bytearray()
    offsets = [0] * 153
    for dex in range(152):
        offsets[dex] = len(flat)
        flat.extend(deflates[dex])
    offsets[152] = len(flat)

    lines = []
    lines.append("// SPDX-License-Identifier: MIT")
    lines.append(f"// {title}")
    lines.append("// AUTO-GENERATED by tools/gen2_sprite_bake.py — DO NOT EDIT.")
    lines.append("//")
    lines.append("// Sprites compressed with RAW DEFLATE (zlib level 9, wbits=-9,")
    lines.append("// 512-byte window). Decode on-device via puff() (vendored as")
    lines.append("// puff.c/h in this directory).")
    lines.append("//")
    lines.append("// Palette layout: index 0 = lightest (transparent / background),")
    lines.append("// index 3 = darkest. 4 colors per species, RGB565.")
    lines.append("")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append(f"static constexpr uint16_t {macro_w} = {w};")
    lines.append(f"static constexpr uint16_t {macro_h} = {h};")
    lines.append("")
    lines.append(f"static const uint16_t {pal_sym}[152][4] = {{")
    for dex in range(152):
        p = palettes[dex]
        nm = NAMES[dex] if dex else "unused"
        lines.append(f"    {{ 0x{p[0]:04X}, 0x{p[1]:04X}, 0x{p[2]:04X}, 0x{p[3]:04X} }}, // {dex:3d} {nm}")
    lines.append("};")
    lines.append("")
    lines.append(f"// Total deflated sprite data: {len(flat)} B")
    lines.append(f"static const uint8_t {deflate_sym}[{len(flat)}] = {{")
    for i in range(0, len(flat), 16):
        chunk = flat[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append(f"static const uint32_t {off_sym}[153] = {{")
    for i in range(0, len(offsets), 8):
        chunk = offsets[i:i + 8]
        lines.append("    " + ", ".join(str(v) for v in chunk) + ",")
    lines.append("};")
    lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines))
    return len(flat)

# --------------------------------------------------------------- vendor puff -

def vendor_puff():
    PUFF_C_DST.parent.mkdir(parents=True, exist_ok=True)
    for src, dst in ((PUFF_C_SRC, PUFF_C_DST), (PUFF_H_SRC, PUFF_H_DST)):
        text = src.read_text()
        # Strip any Arduino include (none present at time of writing, but be safe).
        text = "\n".join(l for l in text.splitlines()
                         if "Arduino.h" not in l and "configuration.h" not in l)
        if not text.endswith("\n"):
            text += "\n"
        dst.write_text(text)
    print(f"[puff] vendored to {PUFF_C_DST}", file=sys.stderr)

# ------------------------------------------------------------------- main ---

def process_one(dex: int, kind: str, target_w: int, target_h: int):
    img, label = fetch_with_fallback(dex, kind)
    if img is None:
        return None, None, None
    img = pad_or_scale(img, target_w, target_h)
    img = matte_to_white(img)
    qimg, pal = quantize_4(img)
    qimg, pal = sort_palette_by_luminance(qimg, pal)
    indices = list(qimg.getdata())
    raw2bpp = pack_2bpp(indices, target_w, target_h)
    blob = raw_deflate(raw2bpp)
    palette_565 = tuple(rgb_to_565(c) for c in pal)
    return palette_565, blob, label

def main():
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    vendor_puff()

    blank_pal = (0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF)

    fallbacks_front = []   # list of (dex, name, label)
    fallbacks_back  = []
    failed_front    = []
    failed_back     = []

    front_palettes = [blank_pal] * 152
    front_blobs    = [b""] * 152
    back_palettes  = [blank_pal] * 152
    back_blobs     = [b""] * 152

    for dex in range(1, 152):
        # FRONT
        pal, blob, label = process_one(dex, "front", FRONT_W, FRONT_H)
        if blob is None:
            failed_front.append((dex, NAMES[dex]))
            print(f"[front] dex {dex:3d} {NAMES[dex]}: FAILED", file=sys.stderr)
        else:
            front_palettes[dex] = pal
            front_blobs[dex]    = blob
            if label != "crystal":
                fallbacks_front.append((dex, NAMES[dex], label))
            print(f"[front] dex {dex:3d} {NAMES[dex]:14s} src={label:8s} deflate={len(blob):4d} B",
                  file=sys.stderr)

        # BACK
        pal, blob, label = process_one(dex, "back", BACK_W, BACK_H)
        if blob is None:
            failed_back.append((dex, NAMES[dex]))
            print(f"[back ] dex {dex:3d} {NAMES[dex]}: FAILED", file=sys.stderr)
        else:
            back_palettes[dex] = pal
            back_blobs[dex]    = blob
            if label != "crystal":
                fallbacks_back.append((dex, NAMES[dex], label))
            print(f"[back ] dex {dex:3d} {NAMES[dex]:14s} src={label:8s} deflate={len(blob):4d} B",
                  file=sys.stderr)

    total_front = write_header(
        OUT_FRONT,
        "GEN2_COLOR_W", "GEN2_COLOR_H",
        "kGen2ColorPalettes", "kGen2ColorDeflate", "kGen2ColorDeflateOffsets",
        FRONT_W, FRONT_H,
        front_palettes, front_blobs,
        f"Gen2ColorIcons — {FRONT_W}x{FRONT_H} 4-color Gen-2 (Crystal) front sprites")

    total_back = write_header(
        OUT_BACK,
        "GEN2_BACK_W", "GEN2_BACK_H",
        "kGen2BackPalettes", "kGen2BackDeflate", "kGen2BackDeflateOffsets",
        BACK_W, BACK_H,
        back_palettes, back_blobs,
        f"Gen2BackIcons — {BACK_W}x{BACK_H} 4-color Gen-2 (Crystal) back sprites")

    print("", file=sys.stderr)
    print(f"=== Summary ===", file=sys.stderr)
    print(f"Front: {total_front} bytes total deflate -> {OUT_FRONT}", file=sys.stderr)
    print(f"Back:  {total_back} bytes total deflate -> {OUT_BACK}", file=sys.stderr)
    if fallbacks_front:
        print(f"Front fallbacks ({len(fallbacks_front)}):", file=sys.stderr)
        for dex, name, lab in fallbacks_front:
            print(f"  dex {dex:3d} {name}: {lab}", file=sys.stderr)
    if fallbacks_back:
        print(f"Back fallbacks ({len(fallbacks_back)}):", file=sys.stderr)
        for dex, name, lab in fallbacks_back:
            print(f"  dex {dex:3d} {name}: {lab}", file=sys.stderr)
    if failed_front:
        print(f"Front FAILURES ({len(failed_front)}):", file=sys.stderr)
        for dex, name in failed_front:
            print(f"  dex {dex:3d} {name}", file=sys.stderr)
    if failed_back:
        print(f"Back FAILURES ({len(failed_back)}):", file=sys.stderr)
        for dex, name in failed_back:
            print(f"  dex {dex:3d} {name}", file=sys.stderr)

if __name__ == "__main__":
    main()
