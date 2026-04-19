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
