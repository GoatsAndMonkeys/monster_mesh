#!/usr/bin/env python3
"""Generate DaycareData.h for MonsterMesh from the offline Gen 1-3 pokedex dump.

Parses /tmp/pokedex/pokedex/{pokemon,moves}/NNN-name.txt (extracted from the
`pokedex` skill tarball) and emits the auto-generated DaycareData.h consumed by
the firmware and the Pi daemon. Covers all 386 Gen 1-3 species and 354 moves.

Python stdlib only. Run:  python3 gen_daycare_data.py
"""
import os
import re
import sys
import unicodedata

POKEDEX_ROOT = "/tmp/pokedex/pokedex"
POKEMON_DIR = os.path.join(POKEDEX_ROOT, "pokemon")
MOVES_DIR = os.path.join(POKEDEX_ROOT, "moves")

OUT_FIRMWARE = "/Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware/src/modules/monstermesh/DaycareData.h"
OUT_PI = "/Users/goatsandmonkeys/Documents/pokemesh/monster_mesh_pi/src/shared/DaycareData.h"

NUM_SPECIES = 386
NUM_MOVES = 354

# ── Type mapping (0-14 unchanged; Dark=15, Steel=16 appended) ──────────────
TYPE_ENUM = [
    ("TYPE_NORMAL", "Normal"), ("TYPE_FIGHTING", "Fighting"), ("TYPE_FLYING", "Flying"),
    ("TYPE_POISON", "Poison"), ("TYPE_GROUND", "Ground"), ("TYPE_ROCK", "Rock"),
    ("TYPE_BUG", "Bug"), ("TYPE_GHOST", "Ghost"), ("TYPE_FIRE", "Fire"),
    ("TYPE_WATER", "Water"), ("TYPE_GRASS", "Grass"), ("TYPE_ELECTRIC", "Electric"),
    ("TYPE_PSYCHIC", "Psychic"), ("TYPE_ICE", "Ice"), ("TYPE_DRAGON", "Dragon"),
    ("TYPE_DARK", "Dark"), ("TYPE_STEEL", "Steel"),
]
NAME2TYPE = {n: i for i, (_, n) in enumerate(TYPE_ENUM)}
T_NORMAL = 0
T_FIGHTING = 1
T_FLYING = 2
T_ROCK = 5
T_GROUND = 4
T_BUG = 6
T_GRASS = 10
T_WATER = 9
T_ICE = 13
T_GHOST = 7
T_PSYCHIC = 12

# Habitat / bias / personality enums (must match DaycareData.h values)
HAB_CAVE, HAB_FOREST, HAB_GRASSLAND, HAB_MOUNTAIN, HAB_RARE, \
    HAB_ROUGH_TERRAIN, HAB_SEA, HAB_URBAN, HAB_WATERS_EDGE = range(9)

BIAS_HP, BIAS_ATK, BIAS_DEF, BIAS_SPA, BIAS_SPD = range(5)

# Legendaries (Gen 1-3) -> RARE habitat
LEGENDARY = {144, 145, 146, 150, 151,
             243, 244, 245, 249, 250, 251,
             377, 378, 379, 380, 381, 382, 383, 384, 385, 386}

# Species display-name overrides (filename title-casing can't reproduce these)
SPECIES_NAME_OVERRIDE = {
    83: "Farfetch'd",
    122: "Mr. Mime",
    250: "Ho-Oh",
    386: "Deoxys",
}

# Preferred learnset game order for the level-up move pool
LEARNSET_PREF = ["E", "FRLG", "RS", "C", "GS", "Y", "RB"]


def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def title_from_filename(fn):
    base = re.sub(r"^\d+-", "", fn[:-4])
    return " ".join(w.capitalize() for w in base.split("-"))


# ── Build move id <-> name tables ──────────────────────────────────────────
def build_moves():
    names = ["???"] * (NUM_MOVES + 1)
    name2id = {}
    for fn in os.listdir(MOVES_DIR):
        m = re.match(r"^(\d+)-", fn)
        if not m:
            continue
        mid = int(m.group(1))
        if mid < 1 or mid > NUM_MOVES:
            continue
        disp = title_from_filename(fn)
        names[mid] = disp
        name2id[norm(disp)] = mid
        # also index by the raw filename slug for robust learnset matching
        name2id[norm(re.sub(r"^\d+-", "", fn[:-4]))] = mid
    return names, name2id


# ── Species file parsing ───────────────────────────────────────────────────
def read_species(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def parse_types(text):
    m = re.search(r"^Type:\s*(.+)$", text, re.M)
    raw = m.group(1).strip()
    parts = [p.strip() for p in raw.split("/")]
    mono = len(parts) == 1

    def tok(p):
        if p == "Fairy":
            return None  # doesn't exist in Gen 1-3
        return NAME2TYPE[p]

    t1 = tok(parts[0])
    t2 = tok(parts[1]) if not mono else t1
    # Resolve Fairy fallbacks
    if t1 is None and (t2 is None):
        t1 = t2 = T_NORMAL
    elif t1 is None:
        t1 = t2  # use the other (non-Fairy) type
    elif t2 is None:
        t2 = t1  # drop Fairy -> mono
    return t1, t2


def parse_stats(text):
    m = re.search(r"^Base stats:\s*HP (\d+), Atk (\d+), Def (\d+), "
                  r"SpA (\d+), SpD (\d+), Spe (\d+)", text, re.M)
    hp, atk, dfn, spa, spd, spe = (int(x) for x in m.groups())
    return hp, atk, dfn, spa, spd, spe


def stat_bias(stats):
    hp, atk, dfn, spa, spd, spe = stats
    vals = {BIAS_HP: hp, BIAS_ATK: atk, BIAS_DEF: dfn, BIAS_SPA: spa, BIAS_SPD: spd}
    mx = max(vals.values())
    for b in (BIAS_ATK, BIAS_SPA, BIAS_DEF, BIAS_SPD, BIAS_HP):  # tie order
        if vals[b] == mx:
            return b
    return BIAS_HP


def article_intro(text):
    """Text from article start up to the first '== ' section header."""
    idx = text.find("BULBAPEDIA ARTICLE")
    if idx < 0:
        return ""
    body = text[idx:]
    m = re.search(r"\n== ", body)
    if m:
        body = body[:m.start()]
    return body.lower()


def biology_section(text):
    m = re.search(r"== Biology ==\s*(.*?)(?:\n== |\Z)", text, re.S)
    return (m.group(1) if m else "").lower()


def evo_stage(text):
    intro = article_intro(text)
    has_from = ("evolves from" in intro) or ("evolved form of" in intro)
    has_into = "evolves into" in intro
    if has_from and has_into:
        return 1
    if has_from:
        return 2
    return 0


PERS_PROUD, PERS_CLUMSY, PERS_AGGRESSIVE, PERS_GENTLE, PERS_SNEAKY, \
    PERS_LAZY, PERS_ANXIOUS = range(7)

# (personality, keyword list) checked in this priority order
PERS_KEYWORDS = [
    (PERS_AGGRESSIVE, ["aggressive", "fierce", "violent", "ferocious", "furious"]),
    (PERS_GENTLE, ["gentle", "kind", "timid", "shy"]),
    (PERS_PROUD, ["proud", "majestic", "noble"]),
    (PERS_LAZY, ["lazy", "sleeps", "sluggish"]),
    (PERS_CLUMSY, ["clumsy", "awkward"]),
    (PERS_SNEAKY, ["sneaky", "steals", "tricks", "mischief"]),
    (PERS_ANXIOUS, ["nervous", "anxious", "fearful"]),
]


def personality(text, stats, bias):
    bio = biology_section(text) or article_intro(text)
    for pers, kws in PERS_KEYWORDS:
        if any(k in bio for k in kws):
            return pers
    # Fallback by stat profile (deterministic)
    hp, atk, dfn, spa, spd, spe = stats
    total = sum(stats)
    if total < 300:
        return PERS_CLUMSY
    top = max(atk, dfn, spe)
    if atk == top:
        return PERS_AGGRESSIVE
    if dfn == top:
        return PERS_PROUD
    if spe == top:
        return PERS_SNEAKY
    return PERS_GENTLE


def habitat(t1, t2, dexnum, stage):
    if dexnum in LEGENDARY:
        return HAB_RARE
    types = {t1, t2}
    if T_WATER in types:
        return HAB_SEA
    if T_ROCK in types or T_GROUND in types:
        return HAB_MOUNTAIN if stage == 2 else HAB_CAVE
    if T_BUG in types or T_GRASS in types:
        return HAB_FOREST
    if T_ICE in types:
        return HAB_MOUNTAIN
    if T_GHOST in types:
        return HAB_CAVE
    if T_FLYING in types or T_NORMAL in types:
        return HAB_GRASSLAND
    return HAB_GRASSLAND


def parse_levelup_moves(text, name2id):
    for tag in LEARNSET_PREF:
        # find the block header line "[E]" etc.
        m = re.search(r"^\[" + re.escape(tag) + r"\]\s*$", text, re.M)
        if not m:
            continue
        block = text[m.end():]
        nxt = re.search(r"^\[[A-Z]+\]\s*$", block, re.M)
        if nxt:
            block = block[:nxt.start()]
        lm = re.search(r"Level-up:\s*(.+)", block)
        if not lm:
            continue
        line = lm.group(1)
        raw = re.findall(r"([\w .'’\-]+?)\s*\(L\d+\)", line)
        seen = set()
        out = []
        for nm in raw:
            mid = name2id.get(norm(nm))
            if mid is None:
                continue
            if mid in seen:
                continue
            seen.add(mid)
            out.append(mid)
            if len(out) >= 15:
                break
        if out:
            return out
    return []


STOPWORDS = set("""a an the it its it's is was were on at of to in and with this that for by
from as are be been being has have had can could will would may might should must into up
out down over under when where while who what which they their them he she his her him if or
but so than then there these those other others one two three more most very just also such
kind kinds type using use used uses about around through near far away back off not no nor
each any all some many much few no yes do does did done get got make makes made like likes
""".split())


def flavor_keywords(text, species_name):
    m = re.search(r"^\[[A-Za-z]+\]\s*(.+)$", text[text.find("POKEDEX ENTRIES"):], re.M)
    entry = m.group(1) if m else ""
    # Rejoin words broken across a line by a soft hyphen (+optional space): "si­ lently" -> "silently"
    entry = re.sub("­\\s*", "", entry)
    # Fold accents (POKéMON -> POKEMON)
    entry = unicodedata.normalize("NFKD", entry).encode("ascii", "ignore").decode()
    words = re.findall(r"[A-Za-z]+", entry.lower())
    own = set(re.findall(r"[a-z]+", species_name.lower()))
    kws = []
    for w in words:
        if len(w) < 3:
            continue
        if w in STOPWORDS or w in own:
            continue
        if w in ("pokemon", "pokmon"):
            continue
        if w in kws:
            continue
        kws.append(w)
        if len(kws) >= 5:
            break
    while len(kws) < 5:
        kws.append("mystery")
    return "|".join(kws[:5])


# ── Type behavior weights (0-14 kept verbatim; Dark/Steel appended) ─────────
TYPE_BEHAVIORS = [
    (200, 80, 120, 180, 60, "normal"),
    (120, 255, 80, 60, 40, "fighting"),
    (140, 100, 200, 100, 80, "flying"),
    (60, 100, 100, 120, 200, "poison"),
    (80, 120, 180, 160, 40, "ground"),
    (60, 140, 120, 200, 20, "rock"),
    (100, 100, 180, 120, 80, "bug"),
    (40, 100, 120, 80, 255, "ghost"),
    (100, 200, 120, 140, 100, "fire"),
    (160, 100, 180, 120, 80, "water"),
    (160, 60, 140, 200, 40, "grass"),
    (120, 140, 160, 60, 200, "electric"),
    (80, 100, 100, 160, 120, "psychic"),
    (100, 80, 120, 180, 60, "ice"),
    (60, 200, 140, 100, 60, "dragon"),
    (70, 200, 150, 80, 220, "dark"),    # sneaky/aggressive, high mischief
    (80, 160, 100, 220, 20, "steel"),   # stoic, defensive, low mischief
]


def build():
    move_names, name2id = build_moves()

    species = []  # dicts
    species_names = []
    flavor = []
    for dex in range(1, NUM_SPECIES + 1):
        files = [f for f in os.listdir(POKEMON_DIR) if f.startswith("%03d-" % dex)]
        if not files:
            sys.exit("missing species %d" % dex)
        path = os.path.join(POKEMON_DIR, files[0])
        text = read_species(path)

        name = SPECIES_NAME_OVERRIDE.get(dex, title_from_filename(files[0]))
        species_names.append(name)

        t1, t2 = parse_types(text)
        stats = parse_stats(text)
        bias = stat_bias(stats)
        stage = evo_stage(text)
        pers = personality(text, stats, bias)
        hab = habitat(t1, t2, dex, stage)
        moves = parse_levelup_moves(text, name2id)
        flavor.append(flavor_keywords(text, name))

        species.append({
            "dex": dex, "t1": t1, "t2": t2, "hab": hab, "bias": bias,
            "pers": pers, "stage": stage, "moves": moves, "name": name,
        })

    return move_names, species, species_names, flavor


PERS_LABEL = ["PERS_PROUD", "PERS_CLUMSY", "PERS_AGGRESSIVE", "PERS_GENTLE",
              "PERS_SNEAKY", "PERS_LAZY", "PERS_ANXIOUS"]


def emit(move_names, species, species_names, flavor, include_line):
    L = []
    w = L.append
    w("#pragma once")
    w("// AUTO-GENERATED by scripts/gen_daycare_data.py — DO NOT EDIT")
    w("// Source: offline Gen 1-3 pokedex dump (Bulbapedia / PokeAPI)")
    w("")
    w(include_line)
    w("")
    w("// ── Pokemon types ───────────────────────────────────────")
    w("enum PkmnType : uint8_t {")
    for i, (label, _) in enumerate(TYPE_ENUM):
        w("    %s = %d," % (label, i))
    w("    TYPE_COUNT = %d" % len(TYPE_ENUM))
    w("};")
    w("")
    w("enum PkmnHabitat : uint8_t {")
    w("    HABITAT_CAVE = 0,")
    w("    HABITAT_FOREST = 1,")
    w("    HABITAT_GRASSLAND = 2,")
    w("    HABITAT_MOUNTAIN = 3,")
    w("    HABITAT_RARE = 4,")
    w("    HABITAT_ROUGH_TERRAIN = 5,")
    w("    HABITAT_SEA = 6,")
    w("    HABITAT_URBAN = 7,")
    w("    HABITAT_WATERS_EDGE = 8,")
    w("    HABITAT_COUNT = 9")
    w("};")
    w("")
    w("enum PkmnPersonality : uint8_t {")
    w("    PERS_PROUD = 0,")
    w("    PERS_CLUMSY = 1,")
    w("    PERS_AGGRESSIVE = 2,")
    w("    PERS_GENTLE = 3,")
    w("    PERS_SNEAKY = 4,")
    w("    PERS_LAZY = 5,")
    w("    PERS_ANXIOUS = 6,")
    w("    PERS_COUNT = 7")
    w("};")
    w("")
    w("enum StatBias : uint8_t {")
    w("    BIAS_HP = 0, BIAS_ATK = 1, BIAS_DEF = 2, BIAS_SPA = 3, BIAS_SPD = 4")
    w("};")
    w("")
    w("// ── Species profile ─────────────────────────────────────")
    w("struct DaycareSpecies {")
    w("    uint16_t dexNum;")
    w("    uint8_t type1;")
    w("    uint8_t type2;")
    w("    uint8_t habitat;")
    w("    uint8_t statBias;")
    w("    uint8_t personality;")
    w("    uint8_t evoStage;")
    w("    uint8_t moveCount;")
    w("    uint16_t moves[15];")
    w("    uint16_t flavorIdx;     // index into daycareFlavorFragments[]")
    w("};")
    w("")
    # Move names
    w("// ── Move names (Gen 1-3, %d moves) ────────────────────" % NUM_MOVES)
    w("static const char *const daycareMoveNames[] = {")
    w('    "???",  // 0 (unused)')
    for mid in range(1, NUM_MOVES + 1):
        w('    "%s",  // %d' % (move_names[mid].replace('"', '\\"'), mid))
    w("};")
    w("static constexpr uint16_t DAYCARE_MOVE_COUNT = %d;" % NUM_MOVES)
    w("")
    # Species names
    w("// ── Species display names (Pokedex order 1-%d) ─────────" % NUM_SPECIES)
    w("static const char *const daycareSpeciesNames[] = {")
    w('    "???",  // 0 (unused)')
    for i, nm in enumerate(species_names, 1):
        w('    "%s",  // %d' % (nm.replace('"', '\\"'), i))
    w("};")
    w("")
    # Flavor fragments
    w("// ── Flavor fragments (keywords from Pokedex entries) ────────")
    w("static const char *const daycareFlavorFragments[] = {")
    for i, fr in enumerate(flavor):
        w('    "%s",  // %d: %s' % (fr, i, species_names[i]))
    w("};")
    w("")
    # Species profiles
    w("// ── Species profiles (Pokedex order 1-%d) ─────────────" % NUM_SPECIES)
    w("static const DaycareSpecies daycareSpecies[] = {")
    for sp in species:
        moves = list(sp["moves"])[:15]
        cnt = len(moves)
        moves += [0] * (15 - cnt)
        marr = "{" + ", ".join(str(m) for m in moves) + "}"
        w("    // [%3d] %s" % (sp["dex"], sp["name"]))
        w("    {%d, %d, %d, %d, %d, %s, %d, %d, %s, %d}," % (
            sp["dex"], sp["t1"], sp["t2"], sp["hab"], sp["bias"],
            PERS_LABEL[sp["pers"]], sp["stage"], cnt, marr, sp["dex"] - 1))
    w("};")
    w("static constexpr uint16_t DAYCARE_SPECIES_COUNT = %d;" % NUM_SPECIES)
    w("")
    # Type behaviors
    w("// ── Type behavior weights ───────────────────────────────")
    w("// { social, combat, explore, rest, mischief }")
    w("struct TypeBehavior {")
    w("    uint8_t social, combat, explore, rest, mischief;")
    w("};")
    w("")
    w("static const TypeBehavior daycareTypeBehaviors[TYPE_COUNT] = {")
    for soc, com, exp, rst, mis, label in TYPE_BEHAVIORS:
        w("    {%3d, %3d, %3d, %3d, %3d},  // %s" % (soc, com, exp, rst, mis, label))
    w("};")
    w("")
    # Accessor
    w("// ── Dex number -> species table entry (1-based, nullptr = invalid) ──")
    w("inline const DaycareSpecies *daycareGetSpecies(uint16_t dexNum) {")
    w("    if (dexNum < 1 || dexNum > DAYCARE_SPECIES_COUNT) return nullptr;")
    w("    return &daycareSpecies[dexNum - 1];")
    w("}")
    w("")
    return "\n".join(L)


def main():
    move_names, species, species_names, flavor = build()
    body_fw = emit(move_names, species, species_names, flavor, "#include <Arduino.h>")
    body_pi = emit(move_names, species, species_names, flavor, '#include "platform.h"')
    with open(OUT_FIRMWARE, "w", encoding="utf-8") as f:
        f.write(body_fw)
    with open(OUT_PI, "w", encoding="utf-8") as f:
        f.write(body_pi)
    # Report
    print("species rows: %d" % len(species))
    print("move names (incl 0): %d" % (NUM_MOVES + 1))
    print("firmware: %s (%d bytes)" % (OUT_FIRMWARE, os.path.getsize(OUT_FIRMWARE)))
    print("pi:       %s (%d bytes)" % (OUT_PI, os.path.getsize(OUT_PI)))
    for dex in (1, 248, 376):
        sp = species[dex - 1]
        print("  #%d %s type=%d/%d hab=%d bias=%d pers=%s stage=%d moves=%d %s" % (
            sp["dex"], sp["name"], sp["t1"], sp["t2"], sp["hab"], sp["bias"],
            PERS_LABEL[sp["pers"]], sp["stage"], len(sp["moves"]), sp["moves"][:6]))


if __name__ == "__main__":
    main()
