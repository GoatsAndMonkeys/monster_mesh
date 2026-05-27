// SPDX-License-Identifier: MIT
//
// Gen1MinimalStats — self-contained Gen 1 stat-computation helper for the
// gym build, so we can drop the full Gen1BattleEngine.cpp (~30 KB) and the
// LORD / daycare .cpp files (~80 KB) on heltec gym + T114 builds.
//
// The gym's only Gen-1 needs are:
//   1. Compute HP/Atk/Def/Spd/Spc from base stats + level (so it can build
//      a Gen1Party and ship it to the T-Deck via TEXT_BATTLE_PARTY chunks)
//   2. Convert dex number → SAV's internal hex code (Gen1Pokemon.species)
//
// We pull both lookup tables from existing HEADERS only (no .cpp deps):
//   - showdown_gen1_basestats.h  → GEN1_BASE_STATS[152]
//   - DaycareSavPatcher.h        → dexToInternal[152]
//
// The T-Deck still uses the full Gen1BattleEngine for actual battles.

#pragma once
#include <stdint.h>
#include "../showdown_gen1_basestats.h"   // GEN1_BASE_STATS (dex-indexed)
#include "../DaycareSavPatcher.h"         // dexToInternal[] (header-only data)

// ── Gen 1 stat formula (IV=8, StatExp=0) ───────────────────────────────────
//   stat = floor(((Base + 8) * 2) * Level / 100) + 5
//   HP   = floor(((Base + 8) * 2) * Level / 100) + Level + 10
// Matches Gen1BattleEngine::initBattlePokeFromBase exactly so parties built
// on the gym get the same stats they would on the T-Deck.

static inline uint16_t gen1ComputeStat(uint8_t base, uint8_t level, bool isHp)
{
    uint32_t v = ((uint32_t)(base + 8) * 2) * level / 100;
    return (uint16_t)(isHp ? v + level + 10 : v + 5);
}

struct Gen1MinimalStats {
    uint16_t hp, atk, def, spd, spc;
    uint8_t  type1, type2;
};

static inline Gen1MinimalStats gen1MinimalStats(uint8_t dex, uint8_t level)
{
    Gen1MinimalStats out{};
    if (dex == 0 || dex > 151) return out;
    const Gen1BaseStats &b = GEN1_BASE_STATS[dex];
    out.hp    = gen1ComputeStat(b.hp,  level, true);
    out.atk   = gen1ComputeStat(b.atk, level, false);
    out.def   = gen1ComputeStat(b.def, level, false);
    out.spd   = gen1ComputeStat(b.spd, level, false);
    out.spc   = gen1ComputeStat(b.spc, level, false);
    out.type1 = b.type1;
    out.type2 = b.type2;
    return out;
}

// Convenience wrapper around DaycareSavPatcher's dexToInternal[] so callers
// don't have to know about the daycare module to look up the SAV-format
// species byte.
static inline uint8_t gen1DexToInternal(uint8_t dex)
{
    return (dex < 152) ? dexToInternal[dex] : 0;
}
