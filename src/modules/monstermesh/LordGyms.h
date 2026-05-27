// SPDX-License-Identifier: MIT
//
// Kanto gym rosters for the Legend of Charizard (LORD) door game.
// Rosters intentionally hew to Red/Blue's actual gym teams (4 grunts + leader),
// not perfectly — plausible L-vs-L matchups for a hand-baked v1.
//
// Later iterations will generate this from data/lord_gyms.json via a Python
// packer. For now these are source-of-truth.

#pragma once

#include <Arduino.h>

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

struct LordGym {
    const char       *city;
    const char       *leaderName;
    const char       *badgeName;
    uint8_t           badgeBit;        // 0..7 — matches bit in LordSave::badges
    uint8_t           minLevelHint;    // advisory
    LordGymTrainer    trainers[5];     // 4 grunts + leader as index 4
};

static constexpr uint8_t LORD_GYM_COUNT         = 8;
static constexpr uint8_t LORD_GYM_TRAINERS      = 5;   // 4 grunts + leader
static constexpr uint8_t LORD_GYM_LEADER_INDEX  = 4;

extern const LordGym LORD_GYMS[LORD_GYM_COUNT];

// Returns LORD_GYMS[i] or nullptr for out-of-range.
const LordGym *lordGym(uint8_t i);

// Build a Gen1Party (6-mon save layout) for trainer `trainerIdx` of gym `gymIdx`.
// Uses initBattlePokeFromBase + writeBattlePokeToSave-equivalent internally.
// Returns false on bad indices or empty roster.
struct Gen1Party;
bool lordBuildGymParty(uint8_t gymIdx, uint8_t trainerIdx, Gen1Party &out);
