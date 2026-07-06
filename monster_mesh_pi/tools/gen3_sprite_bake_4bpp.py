#!/usr/bin/env python3
"""Bake Gen 3 sprites as 4bpp INDEXED + per-species NORMAL & SHINY palettes.

Motivation: the RGB565 bake (gen3_sprite_bake_tdeck.py) fuses colour into every
pixel, so shininess can't be a palette swap. GBA sprites are natively 16-colour
palette-indexed and shiny is literally the same tiles with a different palette.
This tool restores that: 4 bits/pixel index data + two 16-entry RGB565 palettes
per (dex, kind) — normal and shiny. Index 0 is transparent (GBA convention),
mapped to the 0xF81F sentinel the on-device renderer already knows.

Shiny palette alignment: PokeAPI hosts the shiny PNG at the same path with an
extra /shiny/ segment, rendered from the SAME game version, so the shiny image
is pixel-aligned with the normal one (a palette swap). We index the NORMAL
image, then for each index sample the SHINY image at the same pixels to build an
aligned shiny palette. If the shiny art is missing for that version, the shiny
palette falls back to the normal one (renders non-shiny — harmless).

Outputs into meshtastic-firmware/src/modules/monstermesh/:
  Gen3Front4bpp.h  (front, 64x64)   Gen3Back4bpp.h  (back, 64x64)

Usage:
  ~/meshtastic-venv/bin/python3 tools/gen3_sprite_bake_4bpp.py            # full 386
  DEX_SUBSET=6,25,130 ~/meshtastic-venv/bin/python3 tools/gen3_sprite_bake_4bpp.py  # validate
"""
import importlib.util, io, os, struct, sys
from collections import Counter
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUTDIR = os.path.join(REPO, "meshtastic-firmware", "src", "modules", "monstermesh")

# reuse the Pi bake's helpers (fetch_with_fallback, scale_rgba, raw_deflate, spname,
# cache_path, _download, URL_VARIANTS)
spec = importlib.util.spec_from_file_location("g3", os.path.join(HERE, "gen3_sprite_bake.py"))
g3 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g3)

SIZE = 64          # native Gen-3 art, bit-perfect into the fixed LVGL canvas
MAXDEX = 386
PAL = 16           # 16-colour palette; index 0 = transparent
SENTINEL = 0xF81F  # transparent marker (magenta) — mapped to bg on-device


def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def shiny_url(tpl, dex):
    """Insert a /shiny/ segment into a PokeAPI normal-sprite URL template.

    front: .../<ver>/{dex}.png       -> .../<ver>/shiny/{dex}.png
    back:  .../<ver>/back/{dex}.png   -> .../<ver>/back/shiny/{dex}.png
    """
    base = tpl.rsplit("/", 1)[0]   # strip "{dex}.png"
    return base + "/shiny/{dex}.png".format(dex="{dex}").format(dex=dex) \
        if False else base + "/shiny/" + tpl.rsplit("/", 1)[1].format(dex=dex)


def _load_png(cp):
    try:
        return Image.open(io.BytesIO(cp.read_bytes())).convert("RGBA")
    except Exception:
        try: cp.unlink()
        except Exception: pass
        return None


def fetch_shiny_label(dex, kind, label, tpl):
    """Fetch the shiny PNG for a SPECIFIC version `label`, so it is pixel-aligned
    with that version's normal art (a palette swap). Returns PIL RGBA or None."""
    cp = g3.cache_path(label + "_shiny", dex, kind)
    if not cp.exists():
        if not g3._download(shiny_url(tpl, dex), cp):
            return None
    return _load_png(cp)


def fetch_pair(dex, kind):
    """Return (normal RGBA, shiny RGBA or None, label). Prefer the first source
    version that has BOTH normal and shiny art (keeps the shiny palette aligned);
    else the first version with normal art (renders non-shiny)."""
    first_n, first_lab = None, None
    for label, fr_tpl, bk_tpl in g3.URL_VARIANTS:
        tpl = fr_tpl if kind == "front" else bk_tpl
        ncp = g3.cache_path(label, dex, kind)
        if not ncp.exists():
            g3._download(tpl.format(dex=dex), ncp)
        if not ncp.exists():
            continue
        nimg = _load_png(ncp)
        if nimg is None:
            continue
        if first_n is None:
            first_n, first_lab = nimg, label
        simg = fetch_shiny_label(dex, kind, label, tpl)
        if simg is not None:
            return nimg, simg, label          # both present — best alignment
    return first_n, None, first_lab


def build_indexed(normal_rgba, shiny_rgba):
    """Return (indices[SIZE*SIZE] 0..15, normalPal[16] rgb565, shinyPal[16] rgb565).

    Index 0 is transparent. Opaque pixels quantize to <=15 colours (indices
    1..15). The shiny palette is sampled from shiny_rgba at each index's pixels
    (aligned palette swap); falls back to the normal palette when shiny is
    absent."""
    data = list(normal_rgba.getdata())
    opaque = [(r, g, b) for (r, g, b, a) in data if a >= 128]
    n = SIZE * SIZE

    if not opaque:                       # fully transparent slot
        pal = [SENTINEL] * PAL
        return [0] * n, pal, pal

    # Quantize the opaque colours to <=15 entries.
    tmp = Image.new("RGB", (len(opaque), 1))
    tmp.putdata(opaque)
    q = tmp.quantize(colors=PAL - 1, method=Image.Quantize.MEDIANCUT)
    raw = q.getpalette() or []
    ncolors = min(PAL - 1, (len(raw)) // 3)
    pal_rgb = [(raw[i*3], raw[i*3+1], raw[i*3+2]) for i in range(ncolors)]
    if not pal_rgb:
        pal_rgb = [(255, 255, 255)]

    # Map the full (opaque) sprite onto those colours via a palette image.
    palimg = Image.new("P", (1, 1))
    flat = []
    for c in pal_rgb:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(pal_rgb))
    palimg.putpalette(flat)
    mapped = normal_rgba.convert("RGB").quantize(palette=palimg, dither=Image.Dither.NONE)
    mapped_idx = list(mapped.getdata())

    indices = [0] * n
    for i in range(n):
        _, _, _, a = data[i]
        indices[i] = 0 if a < 128 else (mapped_idx[i] % ncolors) + 1

    normalPal = [SENTINEL] + [rgb565(c) for c in pal_rgb]
    normalPal += [SENTINEL] * (PAL - len(normalPal))

    # Shiny palette: sample the aligned shiny image per index.
    if shiny_rgba is not None:
        sdata = list(shiny_rgba.getdata())
        buckets = {i: Counter() for i in range(1, PAL)}
        for i in range(n):
            idx = indices[i]
            if idx > 0 and i < len(sdata):
                r, g, b, a = sdata[i]
                if a >= 128:
                    buckets[idx][(r, g, b)] += 1
        shinyPal = list(normalPal)       # default = normal (any unfilled index)
        for idx in range(1, PAL):
            if buckets[idx]:
                shinyPal[idx] = rgb565(buckets[idx].most_common(1)[0][0])
    else:
        shinyPal = list(normalPal)

    return indices, normalPal, shinyPal


def pack_4bpp(indices):
    """2 pixels/byte, high nibble = left pixel."""
    out = bytearray()
    for i in range(0, len(indices), 2):
        hi = indices[i] & 0xF
        lo = indices[i + 1] & 0xF if i + 1 < len(indices) else 0
        out.append((hi << 4) | lo)
    return bytes(out)


def write_header(path, kind, sym, off_sym, pal_n_sym, pal_s_sym,
                 macro_w, macro_h, blobs, pals_n, pals_s, title):
    flat = bytearray()
    offsets = [0] * (MAXDEX + 2)
    for dex in range(MAXDEX + 1):
        offsets[dex] = len(flat)
        flat.extend(blobs[dex])
    offsets[MAXDEX + 1] = len(flat)

    L = []
    L.append("// SPDX-License-Identifier: MIT")
    L.append(f"// {title}")
    L.append("// AUTO-GENERATED by tools/gen3_sprite_bake_4bpp.py — DO NOT EDIT.")
    L.append("//")
    L.append("// 4bpp INDEXED Gen 3 sprites. Index 0 = transparent (0xF81F on-device).")
    L.append("// Pixels are 2/byte (high nibble = left pixel), RAW-DEFLATE compressed")
    L.append("// (wbits=-9); decode via puff(). Colour = <PalNormal|PalShiny>[dex][idx],")
    L.append("// a little-endian RGB565 word. Front and back carry their own palettes.")
    L.append("#pragma once")
    L.append("#include <stdint.h>")
    L.append("")
    L.append(f"static constexpr uint16_t {macro_w} = {SIZE};")
    L.append(f"static constexpr uint16_t {macro_h} = {SIZE};")
    L.append("")
    L.append(f"// Total deflated 4bpp index data: {len(flat)} B")
    L.append(f"static const uint8_t {sym}[{len(flat)}] = {{")
    for i in range(0, len(flat), 16):
        L.append("    " + ", ".join(f"0x{b:02X}" for b in flat[i:i+16]) + ",")
    L.append("};")
    L.append("")
    L.append(f"static const uint32_t {off_sym}[{MAXDEX + 2}] = {{")
    for i in range(0, len(offsets), 8):
        L.append("    " + ", ".join(str(v) for v in offsets[i:i+8]) + ",")
    L.append("};")
    L.append("")
    for palsym, pals in ((pal_n_sym, pals_n), (pal_s_sym, pals_s)):
        L.append(f"static const uint16_t {palsym}[{MAXDEX + 1}][{PAL}] = {{")
        for dex in range(MAXDEX + 1):
            p = pals[dex]
            L.append("    { " + ", ".join(f"0x{v:04X}" for v in p) + f" }},  // {dex}")
        L.append("};")
        L.append("")
    open(path, "w").write("\n".join(L))
    return len(flat)


def main():
    subset = os.environ.get("DEX_SUBSET")
    dexlist = [int(x) for x in subset.split(",")] if subset else list(range(1, MAXDEX + 1))

    front_blobs = [b""] * (MAXDEX + 1)
    back_blobs = [b""] * (MAXDEX + 1)
    front_pn = [[SENTINEL]*PAL for _ in range(MAXDEX + 1)]
    front_ps = [[SENTINEL]*PAL for _ in range(MAXDEX + 1)]
    back_pn = [[SENTINEL]*PAL for _ in range(MAXDEX + 1)]
    back_ps = [[SENTINEL]*PAL for _ in range(MAXDEX + 1)]

    for dex in dexlist:
        for kind, blobs, pn, ps in (("front", front_blobs, front_pn, front_ps),
                                    ("back", back_blobs, back_pn, back_ps)):
            img, shiny, label = fetch_pair(dex, kind)
            if img is None:
                print(f"  {dex:3d} {g3.spname(dex):14s} {kind}: MISSING", file=sys.stderr)
                continue
            img = g3.scale_rgba(img, SIZE, SIZE)
            shiny = g3.scale_rgba(shiny, SIZE, SIZE) if shiny is not None else None
            indices, palN, palS = build_indexed(img, shiny)
            blobs[dex] = g3.raw_deflate(pack_4bpp(indices))
            pn[dex] = palN
            ps[dex] = palS
            sflag = "shiny" if shiny is not None else "no-shiny"
            print(f"  {dex:3d} {g3.spname(dex):14s} {kind:5s} idx={len(blobs[dex]):4d}B "
                  f"({label}, {sflag})", file=sys.stderr)

    # When validating a subset, don't clobber the full headers.
    if subset:
        print(f"\n[validation subset {subset}] — headers NOT written", file=sys.stderr)
        for dex in dexlist:
            print(f"  dex {dex} front normalPal[1..4]="
                  f"{[hex(x) for x in front_pn[dex][1:5]]} "
                  f"shinyPal[1..4]={[hex(x) for x in front_ps[dex][1:5]]}", file=sys.stderr)
        return

    tf = write_header(os.path.join(OUTDIR, "Gen3Front4bpp.h"), "front",
                      "kGen3Front4bppDeflate", "kGen3Front4bppOffsets",
                      "kGen3FrontPalNormal", "kGen3FrontPalShiny",
                      "GEN3_FRONT_4BPP_W", "GEN3_FRONT_4BPP_H",
                      front_blobs, front_pn, front_ps,
                      f"Gen3Front4bpp — {SIZE}x{SIZE} 4bpp indexed Gen-3 front sprites")
    tb = write_header(os.path.join(OUTDIR, "Gen3Back4bpp.h"), "back",
                      "kGen3Back4bppDeflate", "kGen3Back4bppOffsets",
                      "kGen3BackPalNormal", "kGen3BackPalShiny",
                      "GEN3_BACK_4BPP_W", "GEN3_BACK_4BPP_H",
                      back_blobs, back_pn, back_ps,
                      f"Gen3Back4bpp — {SIZE}x{SIZE} 4bpp indexed Gen-3 back sprites")
    palbytes = (MAXDEX + 1) * PAL * 2 * 2 * 2   # normal+shiny, front+back
    print(f"\nfront idx {tf} B, back idx {tb} B, palettes ~{palbytes} B  -> {OUTDIR}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
