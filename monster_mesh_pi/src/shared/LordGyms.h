// SPDX-License-Identifier: MIT
//
// Kanto gym rosters for the Legend of Charizard (LORD) door game.
// Rosters intentionally hew to Red/Blue's actual gym teams (4 grunts + leader),
// not perfectly — plausible L-vs-L matchups for a hand-baked v1.

#pragma once

#include "platform.h"

struct LordGymMon {
    uint8_t species;   // National dex 1..151
    uint8_t level;
    uint8_t moves[4];  // Gen 1 move IDs (0 = empty)
};

struct LordGymTrainer {
    const char       *name;
    uint8_t           count;
    const LordGymMon *party;
};

static constexpr uint8_t LORD_GYM_COUNT         = 8;
static constexpr uint8_t LORD_GYM_MAX_TRAINERS  = 10;  // array bound (biggest gym + leader)

struct LordGym {
    const char       *city;
    const char       *leaderName;
    const char       *badgeName;
    uint8_t           badgeBit;        // 0..7 — matches bit in LordSave::badges
    uint8_t           minLevelHint;    // advisory
    uint8_t           trainerCount;    // real # of trainers in this gym (varies per gym)
    LordGymTrainer    trainers[LORD_GYM_MAX_TRAINERS];  // leader is the LAST one (index trainerCount-1)
};

// The gym leader is always the final trainer fought.
inline uint8_t lordGymLeaderIndex(const LordGym *g) {
    return (g && g->trainerCount) ? (uint8_t)(g->trainerCount - 1) : 0;
}

extern const LordGym LORD_GYMS[LORD_GYM_COUNT];

// Returns LORD_GYMS[i] or nullptr for out-of-range.
const LordGym *lordGym(uint8_t i);

// Build a Gen1Party (6-mon save layout) for trainer `trainerIdx` of gym `gymIdx`.
// Returns false on bad indices or empty roster.
struct Gen1Party;
bool lordBuildGymParty(uint8_t gymIdx, uint8_t trainerIdx, Gen1Party &out);
