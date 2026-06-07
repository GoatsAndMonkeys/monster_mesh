// SPDX-License-Identifier: MIT
// See ../shared/LordLogic.h.
//
// Ported verbatim from the T-Deck MonsterMesh firmware (LordLogic.cpp).

#include "../shared/LordLogic.h"
#include "showdown_gen1_basestats.h"   // GEN1_BASE_STATS[]

// ── NG+ tier scaling ────────────────────────────────────────────────────────

uint8_t lordScaleLevel(uint8_t baseLevel, uint8_t ngPlusTier, bool isE4)
{
    if (ngPlusTier == 0) return baseLevel;
    uint8_t gymLvl;
    switch (ngPlusTier) {
        case 1:  gymLvl = 60;  break;
        case 2:  gymLvl = 70;  break;
        case 3:  gymLvl = 80;  break;
        case 4:  gymLvl = 90;  break;
        default: gymLvl = 100; break;
    }
    int target = isE4 ? (int)gymLvl + 10 : (int)gymLvl;
    if (target > 100) target = 100;
    return (baseLevel > target) ? baseLevel : (uint8_t)target;
}

static uint8_t s_ngPlusTier = 0;

void lordSetCurrentNgPlusTier(uint8_t tier)
{
    s_ngPlusTier = tier > 5 ? 5 : tier;
}

uint8_t lordCurrentNgPlusTier()
{
    return s_ngPlusTier;
}

// Per-type "go-to" coverage moves. Index = Gen 1 type id from the
// typechart (NORMAL..DRAGON). 0 = no move available for that type.
static const uint8_t kCoverageByType[16] = {
    36,    // 0  NORMAL    Take Down
    70,    // 1  FIGHTING  Strength
    19,    // 2  FLYING    Fly
    188,   // 3  POISON    Sludge
    89,    // 4  GROUND    Earthquake
    88,    // 5  ROCK      Rock Throw
    19,    // 6  BIRD      Fly
    63,    // 7  BUG       Pin Missile
    122,   // 8  GHOST     Lick
    53,    // 9  FIRE      Flamethrower
    56,    // 10 WATER     Hydro Pump
    76,    // 11 GRASS     Solar Beam
    87,    // 12 ELECTRIC  Thunderbolt
    94,    // 13 PSYCHIC   Psychic
    58,    // 14 ICE       Ice Beam
    82,    // 15 DRAGON    Dragon Rage
};

void lordApplyNgPlusMoves(uint8_t dex, uint8_t tier, uint8_t moves[4])
{
    if (tier == 0) return;
    if (dex >= 152) return;
    const Gen1BaseStats &b = GEN1_BASE_STATS[dex];
    uint8_t cov1 = (b.type1 < 16) ? kCoverageByType[b.type1] : 0;
    uint8_t cov2 = (b.type2 < 16 && b.type2 != b.type1)
                     ? kCoverageByType[b.type2] : 0;

    auto has = [&](uint8_t mv) -> bool {
        if (mv == 0) return true;
        for (uint8_t i = 0; i < 4; ++i) if (moves[i] == mv) return true;
        return false;
    };

    // Tier 1+: guarantee at least one NEW coverage move (finisher slot 3).
    uint8_t firstBuff = !has(cov1) ? cov1
                      : !has(cov2) ? cov2
                      : 0;
    if (firstBuff) moves[3] = firstBuff;

    // Tier 2+: ensure BOTH type STABs present.
    if (tier >= 2) {
        uint8_t secondBuff = (cov1 != firstBuff && !has(cov1)) ? cov1
                          : (cov2 != firstBuff && !has(cov2)) ? cov2
                          : 0;
        if (secondBuff) moves[2] = secondBuff;
    }

    // Tier 3+: Hyper Beam (id 99) as the closer — overrides slot 3.
    if (tier >= 3 && !has(99)) moves[3] = 99;
}
