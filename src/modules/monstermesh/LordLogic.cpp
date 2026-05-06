// SPDX-License-Identifier: MIT
// See LordLogic.h.

#include "LordLogic.h"

#include <string.h>

// ── Daily reset ──────────────────────────────────────────────────────────────
//
// Boundary: 09:00 local time. Input timestamps are unix-epoch seconds;
// tzOffsetHours is added to translate to local before the floor operation.
//
// Math: pick the largest 9am-local boundary at or before `nowLocal`, then add
// 24h to get the next one. If nextResetEpoch already points past now, no-op.

void lordApplyDailyReset(LordSave &s, uint32_t nowEpoch, int8_t tzOffsetHours)
{
    if (nowEpoch == 0) return;              // RTC not synced yet — defensive

    constexpr uint32_t DAY = 86400u;
    constexpr uint32_t NINE = 9u * 3600u;

    int64_t tzSec = (int64_t)tzOffsetHours * 3600;
    int64_t local = (int64_t)nowEpoch + tzSec;

    // Floor to nearest 9am-local (shift so 9am maps to midnight, floor, shift back).
    int64_t shifted = local - (int64_t)NINE;
    if (shifted < 0) shifted -= (DAY - 1);   // floor toward -inf
    int64_t lastBoundaryLocal = (shifted / (int64_t)DAY) * (int64_t)DAY + (int64_t)NINE;
    int64_t nextBoundaryLocal = lastBoundaryLocal + (int64_t)DAY;
    uint32_t nextBoundaryUtc  = (uint32_t)(nextBoundaryLocal - tzSec);

    if (s.nextResetEpoch == 0 || nowEpoch >= s.nextResetEpoch) {
        s.exploreRunsToday = 0;
        s.nextResetEpoch   = nextBoundaryUtc;
    }
}

// ── Run / gym events ─────────────────────────────────────────────────────────

void lordOnRunEnd(LordSave &s, const LordRunStats &r)
{
    s.totalRuns        += 1;
    s.totalWavesBeaten += r.wavesBeaten;

    bool newBestWaves = r.wavesBeaten > s.bestRunWaves;
    bool newBestXp    = r.xpEarned    > s.bestRunXp;

    if (r.wavesBeaten > s.bestRunWaves) s.bestRunWaves       = r.wavesBeaten;
    if (r.xpEarned    > s.bestRunXp)    s.bestRunXp          = r.xpEarned;
    if (r.highestOppLevel > s.bestRunHighestLevel) s.bestRunHighestLevel = r.highestOppLevel;

    lordAppendNews(s, LORD_NEWS_RUN_ENDED, 0, r.wavesBeaten);
    if (newBestWaves || newBestXp) {
        lordAppendNews(s, LORD_NEWS_BEST_RUN, 0, s.bestRunWaves);
    }

    // Century milestones (every 100 total runs).
    if (s.totalRuns % 100 == 0) {
        lordAppendNews(s, LORD_NEWS_CENTURY, 0, (uint16_t)s.totalRuns);
    }
}

void lordOnGymCleared(LordSave &s, uint8_t gymIdx)
{
    if (gymIdx >= 8) return;
    uint8_t mask = (uint8_t)(1u << gymIdx);
    bool alreadyEarned = (s.badges & mask) != 0;
    s.badges        |= mask;
    s.gymProgress[gymIdx] = 5;
    if (!alreadyEarned) {
        lordAppendNews(s, LORD_NEWS_BADGE, gymIdx, 0);
    }
}

bool lordHasBadge(const LordSave &s, uint8_t gymIdx)
{
    if (gymIdx >= 8) return false;
    return (s.badges & (uint8_t)(1u << gymIdx)) != 0;
}

bool lordGymUnlocked(const LordSave &s, uint8_t gymIdx)
{
    if (gymIdx >= 8) return false;
    if (gymIdx == 0) return true;
    // All prior badges must be earned.
    for (uint8_t i = 0; i < gymIdx; ++i) {
        if (!lordHasBadge(s, i)) return false;
    }
    return true;
}

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

#include "showdown_gen1_basestats.h"

// Per-type "go-to" coverage moves. Index = Gen 1 type id from the
// typechart (NORMAL..DRAGON). 0 = no move available for that type.
static const uint8_t kCoverageByType[16] = {
    36,    // 0  NORMAL    Take Down
    70,    // 1  FIGHTING  Strength (closest to canonical Strength STAB)
    19,    // 2  FLYING    Fly
    188,   // 3  POISON    Sludge (fallback to Acid 51 if not in moves table)
    89,    // 4  GROUND    Earthquake
    88,    // 5  ROCK      Rock Throw (Rock Slide isn't in our move list)
    19,    // 6  BIRD      Fly (BIRD type unused but keep harmless default)
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

    // Tier 1+: guarantee at least one NEW coverage move. If the species
    // already knows its primary STAB (e.g. Brock's Onix already has Rock
    // Throw), fall through to the secondary type so the overlay is
    // visible. Picks slot 3 because that's the canonical "finisher" slot
    // most leaders dedicate to status / setup.
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
