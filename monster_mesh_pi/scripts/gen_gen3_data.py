#!/usr/bin/env python3
"""
Generate Gen 2/3-mechanics battle data headers from the vendored Pokemon
Showdown data cache, for the MonsterMesh Gen 3 battle prototype.

Emits (into src/battle/):
  showdown_gen3_basestats.h   -- 151 species, SPLIT Special (spa/spd) + modern types
  showdown_gen3_typechart.h   -- 18-type chart (adds Dark/Steel; Gen-2 fixes) + phys/spec-by-type
  showdown_gen3_moves.h       -- the 165 Gen-1 move ids, re-typed & re-powered to modern values,
                                 keeping the engine's EFF_* effect + PP so behaviour still works.

Why "Gen 2/3": the balance wins the project wants (Special split, fixed crits,
Dark/Steel, Ghost->Psychic, re-typed moves like Bite->Dark) all landed in Gen 2
and carry through Gen 3. Physical/special is still decided by TYPE (Gen 4 made it
per-move), so this stays a small, self-consistent data swap. Stats still use the
Gen-1 DV/stat-exp formula in the engine -- we only split the Special base stat.

Source: Pokemon Showdown data (MIT). Run from anywhere; paths auto-detected.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

# ── Locate the Showdown data cache ────────────────────────────────────────────
CACHE_CANDIDATES = [
    os.path.join(REPO, "meshtastic-firmware", "scripts", ".showdown_cache", "data"),
    os.path.join(REPO, "scripts", ".showdown_cache", "data"),
    os.path.join(HERE, ".showdown_cache", "data"),
]
DATA = next((p for p in CACHE_CANDIDATES if os.path.isdir(p)), None)
if not DATA:
    sys.exit("ERROR: could not find .showdown_cache/data (looked in:\n  " +
             "\n  ".join(CACHE_CANDIDATES) + ")")

OUT = os.path.join(REPO, "monster_mesh_pi", "src", "battle")
GEN1_MOVES_H = os.path.join(OUT, "showdown_gen1_moves.h")

# ── Type numbering ────────────────────────────────────────────────────────────
# Indices 0-15 are IDENTICAL to the Gen-1 headers (so shared code / debugging
# lines up); 16=Dark and 17=Steel are appended. "BIRD" (6) is a dead Gen-1 slot
# kept only for index parity -- nothing maps to it.
TYPE_ORDER = ["Normal", "Fighting", "Flying", "Poison", "Ground", "Rock",
              "Bird", "Bug", "Ghost", "Fire", "Water", "Grass", "Electric",
              "Psychic", "Ice", "Dragon", "Dark", "Steel"]
TYPE_IDX = {t: i for i, t in enumerate(TYPE_ORDER)}
NTYPES = len(TYPE_ORDER)

# Gen 2/3 physical/special split is by attack TYPE (Gen 4 made it per-move).
SPECIAL_TYPES = {"Fire", "Water", "Grass", "Electric", "Psychic", "Ice",
                 "Dragon", "Dark"}


def read(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as f:
        return f.read()


def norm(name):
    """Move/'species' name -> Showdown id (lowercase alnum)."""
    return re.sub(r"[^a-z0-9]", "", name.lower())


# ── Parse pokedex.ts: dex -> (baseStats, types), base forms only ──────────────
def parse_pokedex():
    text = read("pokedex.ts")
    by_dex = {}
    # top-level entries: "\tkey: {  ... \n\t},"
    for m in re.finditer(r"\n\t(\w+): \{\n(.*?)\n\t\},", text, re.S):
        body = m.group(2)
        # skip alternate forms (megas, gmax, regionals) -- they carry baseSpecies
        if "baseSpecies:" in body:
            continue
        nm = re.search(r"\bnum:\s*(-?\d+)", body)
        if not nm:
            continue
        num = int(nm.group(1))
        if not (1 <= num <= 151):
            continue
        tm = re.search(r"types:\s*\[([^\]]*)\]", body)
        bm = re.search(r"baseStats:\s*\{([^}]*)\}", body)
        if not tm or not bm:
            continue
        types = re.findall(r'"([^"]+)"', tm.group(1))
        # Fairy didn't exist until Gen 6. Among the Gen-1 dex the modern
        # Fairy-types (Clefairy/Jigglypuff lines -> Normal, Mr. Mime -> Psychic)
        # were the OTHER type (or Normal) in Gen 3, so drop Fairy to recover the
        # Gen-3 typing. Steel/Dark gains (e.g. Magnemite -> Electric/Steel) are
        # real Gen-2 retcons and are kept.
        types = [t for t in types if t != "Fairy"] or ["Normal"]
        stats = dict(re.findall(r"(\w+):\s*(\d+)", bm.group(1)))
        # Prefer the lowest-num duplicate is irrelevant since we key by num and
        # base forms are unique per num after the baseSpecies filter.
        by_dex[num] = {
            "hp": int(stats["hp"]), "atk": int(stats["atk"]),
            "def": int(stats["def"]), "spa": int(stats["spa"]),
            "spd": int(stats["spd"]), "spe": int(stats["spe"]),
            "t1": TYPE_IDX[types[0]],
            "t2": TYPE_IDX[types[1]] if len(types) > 1 else TYPE_IDX[types[0]],
        }
    missing = [d for d in range(1, 152) if d not in by_dex]
    if missing:
        sys.exit(f"ERROR: pokedex missing dex numbers: {missing}")
    return by_dex


# ── Parse typechart.ts: build [NTYPES][NTYPES] effectiveness ──────────────────
# Engine encoding: 0=immune, 1=resist(0.5x), 2=neutral(1x), 4=super(2x).
# Showdown damageTaken code: 0=neutral, 1=super(weak to), 2=resist, 3=immune.
SD_TO_ENG = {0: 2, 1: 4, 2: 1, 3: 0}


def parse_typechart():
    text = read("typechart.ts")
    # default neutral everywhere (covers the dead BIRD slot and any gaps)
    chart = [[2] * NTYPES for _ in range(NTYPES)]
    for m in re.finditer(r"\n\t(\w+): \{\n\t\tdamageTaken: \{(.*?)\n\t\t\},", text, re.S):
        dname = m.group(1).capitalize()
        if dname not in TYPE_IDX:
            continue  # Fairy, Stellar, ??? -- not used in Gen 3 / by us
        d = TYPE_IDX[dname]
        for a_name, code in re.findall(r"(\w+):\s*(\d)", m.group(2)):
            if a_name not in TYPE_IDX:
                continue
            a = TYPE_IDX[a_name]
            chart[a][d] = SD_TO_ENG[int(code)]
    return chart


# ── Parse moves.ts: id -> (type, power, accuracy) modern values ───────────────
def parse_moves_modern():
    text = read("moves.ts")
    out = {}
    for m in re.finditer(r"\n\t(\w+): \{\n(.*?)\n\t\},", text, re.S):
        mid, body = m.group(1), m.group(2)
        tm = re.search(r'\btype:\s*"([^"]+)"', body)
        pm = re.search(r"\bbasePower:\s*(\d+)", body)
        am = re.search(r"\baccuracy:\s*(true|\d+)", body)
        if not tm:
            continue
        out[mid] = {
            "type": tm.group(1),
            "power": int(pm.group(1)) if pm else 0,
            # accuracy:true means never-miss -> engine uses 0 for that
            "acc": 0 if (am and am.group(1) == "true") else (int(am.group(1)) if am else 0),
        }
    return out


# ── Parse our existing gen1 moves header: keep id/name/pp/priority/EFF/chance ──
def parse_gen1_moves():
    """Returns list of dicts in id order, plus the trailing showdown id comment."""
    rows = []
    line_re = re.compile(
        r"\{\s*(\d+),\s*\"([^\"]*)\",\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),"
        r"\s*(-?\d+),\s*(EFF_\w+),\s*(\d+)\s*\}[^/]*//\s*(\w+)")
    with open(GEN1_MOVES_H, encoding="utf-8") as f:
        for line in f:
            mo = line_re.search(line)
            if not mo:
                continue
            rows.append({
                "num": int(mo.group(1)), "name": mo.group(2),
                "pp": int(mo.group(6)), "priority": int(mo.group(7)),
                "eff": mo.group(8), "effchance": int(mo.group(9)),
                "sid": mo.group(10),
            })
    if not rows:
        sys.exit("ERROR: parsed 0 moves from " + GEN1_MOVES_H)
    return rows


HEADER = """// SPDX-License-Identifier: MIT
//
// Auto-generated by scripts/gen_gen3_data.py -- DO NOT EDIT BY HAND.
//
// Gen 2/3-mechanics battle data (Special split, Dark/Steel, modern type chart
// and move typings). Physical/special is still decided by TYPE (Gen 4 made it
// per-move). Source: Pokemon Showdown data (MIT), Smogon University.
// Pokemon and all related marks are trademarks of Nintendo / Game Freak /
// Creatures -- this file contains only mechanical/statistical data.
//
#pragma once
#include <stdint.h>
"""


def gen_basestats(dex):
    lines = [HEADER, "",
             "// Gen 3 base stats: Special is split into spa (Sp.Atk) and spd (Sp.Def).",
             "// spe = Speed. Types use the numbering in showdown_gen3_typechart.h.",
             "struct Gen3BaseStats {",
             "    uint8_t hp, atk, def, spa, spd, spe;",
             "    uint8_t type1, type2;",
             "};", "",
             "static constexpr Gen3BaseStats GEN3_BASE_STATS[152] = {",
             "    { 0, 0, 0, 0, 0, 0, 0, 0 },  // 0 placeholder"]
    for d in range(1, 152):
        e = dex[d]
        lines.append("    {{ {hp:3d}, {atk:3d}, {df:3d}, {spa:3d}, {spd:3d}, {spe:3d}, "
                     "{t1:2d}, {t2:2d} }},  // {d}".format(
                         hp=e["hp"], atk=e["atk"], df=e["def"], spa=e["spa"],
                         spd=e["spd"], spe=e["spe"], t1=e["t1"], t2=e["t2"], d=d))
    lines.append("};")
    return "\n".join(lines) + "\n"


def gen_typechart(chart):
    lines = [HEADER, "",
             "static constexpr const char *GEN3_TYPE_NAMES[] = {"]
    for t in TYPE_ORDER:
        lines.append(f'    "{t.upper()}",')
    lines.append("};")
    lines.append(f"static constexpr uint8_t GEN3_TYPE_COUNT = {NTYPES};")
    lines.append("")
    lines.append("// Physical/special is by ATTACK TYPE in Gen 2/3 (true = special).")
    flags = ", ".join("1" if TYPE_ORDER[i] in SPECIAL_TYPES else "0"
                      for i in range(NTYPES))
    lines.append(f"static constexpr uint8_t GEN3_TYPE_IS_SPECIAL[{NTYPES}] = {{ {flags} }};")
    lines.append("")
    lines.append("// GEN3_TYPECHART[attackerType][defenderType]:")
    lines.append("// 0 = immune, 1 = resist (0.5x), 2 = neutral (1x), 4 = super (2x).")
    lines.append(f"static constexpr uint8_t GEN3_TYPECHART[{NTYPES}][{NTYPES}] = {{")
    for a in range(NTYPES):
        row = ", ".join(str(chart[a][d]) for d in range(NTYPES))
        lines.append(f"    {{ {row} }},  // {TYPE_ORDER[a].lower()}")
    lines.append("};")
    return "\n".join(lines) + "\n"


def gen_moves(g1rows, modern):
    lines = [HEADER, "",
             '#include "showdown_gen1_moves.h"  // reuse Gen1MoveData + EFF_* enum',
             "",
             "// Same 165 move ids the Gen-1 saves store, but each move's TYPE, power",
             "// and accuracy are updated to modern (Gen 2/3+) values (e.g. Bite->Dark,",
             "// Karate Chop->Fighting). PP + the engine EFF_* effect are kept from Gen 1",
             "// so move behaviour still runs. Category is derived from the (new) type.",
             "static constexpr Gen1MoveData GEN3_MOVES[] = {"]
    retyped = 0
    for r in g1rows:
        mod = modern.get(r["sid"])
        if mod and mod["type"] in TYPE_IDX:
            tp = TYPE_IDX[mod["type"]]
            pw = mod["power"]
            ac = mod["acc"]
        else:
            # move not in modern dex (shouldn't happen for gen1 moves) -> keep gen1-ish
            tp, pw, ac = 0, 0, 0
        lines.append('    {{ {num:3d}, "{name}", {tp:2d}, {pw:3d}, {ac:3d}, {pp:2d}, '
                     '{pr:2d}, {eff}, {ec:3d} }},  // {sid}'.format(
                         num=r["num"], name=r["name"], tp=tp, pw=pw, ac=ac,
                         pp=r["pp"], pr=r["priority"], eff=r["eff"],
                         ec=r["effchance"], sid=r["sid"]))
    lines.append("};")
    lines.append(f"static constexpr uint8_t GEN3_MOVE_COUNT = {len(g1rows)};")
    lines.append("")
    lines.append("inline const Gen1MoveData *gen3Move(uint8_t num) {")
    lines.append("    for (uint8_t i = 0; i < GEN3_MOVE_COUNT; ++i)")
    lines.append("        if (GEN3_MOVES[i].num == num) return &GEN3_MOVES[i];")
    lines.append("    return nullptr;")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    dex = parse_pokedex()
    chart = parse_typechart()
    modern = parse_moves_modern()
    g1rows = parse_gen1_moves()

    outputs = {
        "showdown_gen3_basestats.h": gen_basestats(dex),
        "showdown_gen3_typechart.h": gen_typechart(chart),
        "showdown_gen3_moves.h": gen_moves(g1rows, modern),
    }
    for fname, content in outputs.items():
        path = os.path.join(OUT, fname)
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"wrote {path} ({content.count(chr(10))} lines)")

    # quick sanity spot-checks
    print("\n-- sanity --")
    print(f"  species parsed: {len(dex)} (want 151)")
    # Gengar should be Ghost/Poison, high spa
    g = dex[94]
    print(f"  #94 Gengar spa={g['spa']} spd={g['spd']} "
          f"types=({TYPE_ORDER[g['t1']]},{TYPE_ORDER[g['t2']]})")
    # Magnemite should have gained Steel in Gen 2+
    mag = dex[81]
    print(f"  #81 Magnemite types=({TYPE_ORDER[mag['t1']]},{TYPE_ORDER[mag['t2']]}) "
          "(expect Electric,Steel)")
    # Ghost should now hit Psychic super-effectively
    print(f"  Ghost->Psychic eff code = {chart[TYPE_IDX['Ghost']][TYPE_IDX['Psychic']]} "
          "(expect 4 = super effective)")
    print(f"  Normal->Ghost eff code  = {chart[TYPE_IDX['Normal']][TYPE_IDX['Ghost']]} "
          "(expect 0 = immune)")
    bite = next((r for r in g1rows if r["sid"] == "bite"), None)
    if bite:
        bt = modern.get("bite", {})
        print(f"  Bite modern type = {bt.get('type')} (expect Dark)")


if __name__ == "__main__":
    main()
