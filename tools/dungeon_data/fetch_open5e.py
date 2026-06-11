#!/usr/bin/env python3
"""
fetch_open5e.py — Download and filter Open5e content for Dungeons and MonstersMesh.

Pulls spells, conditions, backgrounds, feats, and classes from api.open5e.com
and writes binary content tables to data/ for SD card loading.

Usage:
    python fetch_open5e.py [--output data/]

Run once to build the content tables. Tables go on the SD card;
firmware loads them at runtime via DungeonContent::loadFromSD().
"""

import argparse
import json
import struct
import urllib.request
import os
from pathlib import Path

BASE_URL = "https://api.open5e.com/v1"

# Pokemon types assigned to D&D spell schools/effects for matchup purposes.
# Key = spell name keyword or school; Value = PokeType index
SPELL_TYPE_MAP = {
    "fire":         2,  # Fire
    "lightning":    3,  # Electric
    "thunder":      3,  # Electric
    "ice":          5,  # Ice
    "frost":        5,  # Ice
    "cold":         5,  # Ice
    "poison":       7,  # Poison
    "acid":         7,  # Poison
    "necrotic":     13, # Ghost
    "psychic":      10, # Psychic
    "radiant":      17, # Fairy
    "cure":         17, # Fairy
    "heal":         17, # Fairy
    "charm":        17, # Fairy
    "force":        10, # Psychic (magic missile etc.)
    "water":        2,  # Water
    "earth":        8,  # Ground
    "stone":        12, # Rock
    "wind":         9,  # Flying
    "air":          9,  # Flying
    "shadow":       13, # Ghost
    "dark":         15, # Dark
    "light":        0,  # Normal
    "sonic":        0,  # Normal
}

# Base power rough equivalents for spell levels (Pokemon scale, single target)
SPELL_LEVEL_POWER = {
    0: 40,   # cantrip
    1: 50,
    2: 65,
    3: 80,
    4: 90,
    5: 100,
    6: 110,
    7: 120,
    8: 130,
    9: 140,
}


def fetch_paginated(endpoint: str) -> list:
    results = []
    url = f"{BASE_URL}/{endpoint}/?limit=100"
    while url:
        print(f"  GET {url}")
        with urllib.request.urlopen(url, timeout=15) as r:
            data = json.load(r)
        results.extend(data.get("results", []))
        url = data.get("next")
    return results


def poke_type_for_spell(spell: dict) -> int:
    """Assign a Pokemon type index based on spell name + school."""
    text = (spell.get("name", "") + " " + spell.get("school", "")).lower()
    for keyword, ptype in SPELL_TYPE_MAP.items():
        if keyword in text:
            return ptype
    return 0  # Normal as fallback


def base_power_for_spell(spell: dict) -> int:
    level = spell.get("level_int", 0)
    return SPELL_LEVEL_POWER.get(level, 40)


def write_spells(spells: list, outpath: Path):
    """
    Binary format per spell record (38 bytes):
      uint16 id
      char[24] name (null-padded)
      uint8  level
      uint8  poke_type
      uint16 poke_base_power
      uint8  effect_id
      uint8  flags (bit0=healing, bit1=buff)
    """
    outpath.parent.mkdir(parents=True, exist_ok=True)
    with open(outpath, "wb") as f:
        for i, spell in enumerate(spells):
            if spell.get("level_int", 0) == 0:
                continue  # skip cantrips for now — add later
            name = spell.get("name", "")[:23].encode().ljust(24, b"\x00")
            level = min(spell.get("level_int", 1), 9)
            ptype = poke_type_for_spell(spell)
            power = base_power_for_spell(spell)
            flags = 0
            desc = (spell.get("desc", "") + spell.get("higher_level", "")).lower()
            if any(k in desc for k in ["heal", "restore", "cure", "regain"]):
                flags |= 1
            if any(k in desc for k in ["bonus", "advantage", "increase your"]):
                flags |= 2
            f.write(struct.pack("<HH24sBBHBB", i, i, name, level, ptype, power, 0, flags))
    print(f"  Wrote {outpath} ({os.path.getsize(outpath)} bytes)")


def write_conditions(conditions: list, outpath: Path):
    """
    Binary format per condition (80 bytes):
      uint8  id
      char[24] name
      char[55] description (truncated)
    """
    outpath.parent.mkdir(parents=True, exist_ok=True)
    with open(outpath, "wb") as f:
        for i, cond in enumerate(conditions):
            name = cond.get("name", "")[:23].encode().ljust(24, b"\x00")
            desc = cond.get("desc", "")[:54].encode("ascii", errors="replace").ljust(55, b"\x00")
            f.write(struct.pack("<B24s55s", i, name, desc))
    print(f"  Wrote {outpath} ({os.path.getsize(outpath)} bytes)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="data")
    args = parser.parse_args()
    outdir = Path(args.output)

    print("Fetching spells...")
    spells = fetch_paginated("spells")
    write_spells(spells, outdir / "spells.bin")

    print("Fetching conditions...")
    conditions = fetch_paginated("conditions")
    write_conditions(conditions, outdir / "conditions.bin")

    # TODO: backgrounds, feats, classes (Phase 6)
    print("Done. Copy data/ to SD card /dungeon/ directory.")


if __name__ == "__main__":
    main()
