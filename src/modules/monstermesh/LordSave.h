// SPDX-License-Identifier: MIT
//
// Legend of the Red Dragon (LORD) — persistent save for the MonsterMesh
// door-game layer. Gym badges, per-day explore-run gating, and a small
// local news ring.
//
// On-disk layout is fixed-size and platform-endian (single-device save);
// mirrors the PokemonDaycare::saveState pattern (FSCom at /monstermesh/lord.dat).

#pragma once

#include <Arduino.h>

static constexpr uint32_t LORD_MAGIC   = 0x4C4F5244u;   // 'LORD'
static constexpr uint8_t  LORD_VERSION = 1;

static constexpr uint8_t  LORD_NEWS_CAP = 8;

enum LordNewsType : uint8_t {
    LORD_NEWS_NONE      = 0,
    LORD_NEWS_BADGE     = 1,   // arg1 = gym idx
    LORD_NEWS_BEST_RUN  = 2,   // arg2 = waves
    LORD_NEWS_CENTURY   = 3,   // arg2 = milestone (100 runs, etc.)
    LORD_NEWS_RUN_ENDED = 4,   // arg2 = waves beaten this run
};

#pragma pack(push, 1)
struct LordNewsEntry {
    uint32_t ts;           // getTime() when recorded
    uint8_t  type;
    uint8_t  arg1;
    uint16_t arg2;
    uint32_t reserved;
};
static_assert(sizeof(LordNewsEntry) == 12, "LordNewsEntry must be 12 bytes");

struct LordSave {
    uint32_t magic;                         // LORD_MAGIC
    uint8_t  version;                       // LORD_VERSION
    uint8_t  badges;                        // bit n = gym n cleared
    uint16_t _pad0;

    uint32_t nextResetEpoch;                // unix seconds — next daily rollover
    uint8_t  exploreRunsToday;
    uint8_t  exploreUnlimited;              // 1 = debug, ignore daily cap
    uint16_t _pad1;

    uint32_t totalRuns;
    uint32_t totalWavesBeaten;
    uint32_t bestRunXp;
    uint16_t bestRunWaves;
    uint8_t  bestRunHighestLevel;
    uint8_t  _pad2;

    uint8_t  gymProgress[8];                // per-gym trainer idx (0..5; 5 = leader cleared)

    uint8_t  newsHead;                      // ring write cursor
    uint8_t  newsCount;                     // 0..LORD_NEWS_CAP
    uint16_t _pad3;
    LordNewsEntry news[LORD_NEWS_CAP];      // 96 bytes

    // Indigo Plateau / NG+ extension. Carved from the original reserved
    // block at fixed offsets so older saves load with these fields = 0.
    uint8_t  e4Progress;                    // 0..5 (4 = Champion next, 5 = league cleared)
    uint8_t  leagueCleared;                 // 0/1 — gate for NG+ unlock (one-shot)
    uint8_t  ngPlusTier;                    // 0 = base game, 1..5 = NG+ tier
    uint8_t  _pad4;

    // Highest NG+ tier each gym was cleared at. 0 = beaten in base game.
    // 1+ = beaten at NG+N. The gym listing shows "[NG+T]" for any gym
    // where gymTierCleared[i] < ngPlusTier — those are the league-revisit
    // fights still owed at the current tier.
    uint8_t  gymTierCleared[8];

    uint8_t  reserved[116];                 // forward-compat (PvP, weather)
};
#pragma pack(pop)

static_assert(sizeof(LordSave) <= 512, "LordSave must fit in 512 bytes");

// Zero-init a save to defaults.
void lordInitDefaults(LordSave &s);

// Read /monstermesh/lord.dat. Returns true if a valid save was loaded.
// On any failure (missing file, bad magic, short read), `s` is zero-inited
// via lordInitDefaults and the function returns false.
bool lordLoad(LordSave &s);

// Atomic-ish write: truncate + write + close. Returns true on success.
bool lordSave(const LordSave &s);

// Append a news entry (ring-buffer).
void lordAppendNews(LordSave &s, uint8_t type, uint8_t arg1, uint16_t arg2);
