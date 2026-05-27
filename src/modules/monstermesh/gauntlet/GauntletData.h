// SPDX-License-Identifier: MIT
//
// GauntletData — shared on-disk + in-memory types for the multiplayer
// "Pokemon Gym" gauntlet ladder.
//
// Concept (distinct from LORD): the gym node accepts DM-based challenges
// from MonsterMesh Terminal players. A persistent ladder ranks past
// challengers by progression depth: Elite Four (fixed) → current gym leader
// → previous leaders (most-recent first) → ranked challenger slots.
//
// Beat trainers 1..N-1 but lose to N → take slot N, everyone below shifts
// down one. Beat the entire ladder → become new gym leader.

#pragma once

#include <Arduino.h>
#include "../PokemonData.h"   // Gen1Party

// ── Limits ────────────────────────────────────────────────────────────────────
static constexpr uint8_t GAUNTLET_PARTY_MAX     = 6;
static constexpr uint8_t GAUNTLET_ROSTER_MAX    = 12;   // ranked challenger slots
static constexpr uint8_t GAUNTLET_PREV_MAX      = 4;    // historical leaders kept
static constexpr uint8_t GAUNTLET_SESSION_MAX   = 4;    // concurrent challenges
static constexpr uint8_t GAUNTLET_NAME_MAX      = 16;
static constexpr uint8_t GAUNTLET_BADGE_MAX     = 16;
static constexpr uint32_t GAUNTLET_SESSION_TTL  = 600;  // seconds

// ── Trainer record ────────────────────────────────────────────────────────────
struct GauntletTrainer {
    uint32_t  nodeNum;                     // 0 = NPC / unclaimed
    char      name[GAUNTLET_NAME_MAX];
    Gen1Party party;
    uint32_t  timestamp;                   // when they took this position
};

// ── Persistent gym state (written via FSCom) ──────────────────────────────────
static constexpr uint32_t GAUNTLET_MAGIC   = 0x47594D31u; // 'GYM1'
static constexpr uint8_t  GAUNTLET_VERSION = 5;           // v5: per-slot source

#pragma pack(push, 1)
struct GauntletState {
    uint32_t        magic;
    uint8_t         version;
    uint8_t         _pad0[3];

    uint32_t        nodeNum;               // this gym's own node number
    char            gymName[GAUNTLET_NAME_MAX];
    char            gymBadge[GAUNTLET_BADGE_MAX];

    GauntletTrainer leader;                // nodeNum==0 → no leader yet
    uint8_t         rosterSize;
    uint8_t         prevLeaderCount;
    uint8_t         _pad1[2];
    GauntletTrainer roster[GAUNTLET_ROSTER_MAX];
    GauntletTrainer prevLeaders[GAUNTLET_PREV_MAX];

    uint32_t        totalChallenges;
    uint32_t        totalBattles;
    uint32_t        lastUpdate;

    uint8_t         gymPresetIdx;          // 0..7 = Kanto preset, 0xFF = none
    uint8_t         memberLevels[5];       // 0 = stock, else boss level for that
                                           // member's party (1..100). Index 4 is
                                           // the gym leader.
    uint32_t        adminNodeNum;          // 0 = no admin claimed (unsafe — any
                                           // admin DM during the post-boot
                                           // claim window grants admin). On-
                                           // device slide ignores this (physical
                                           // presence implies trust).
    // ── Auto-fill ────────────────────────────────────────────────────────────
    // Used when no admin has configured the gym. autoSetup() picks 4 random
    // grunts (sorted ascending by canonical boss level) and 1 random leader
    // from the leader pool {8 gym leaders, 4 Elite Four, Champion}. Each slot
    // is stored as (sourceIdx, trainerIdx):
    //   sourceIdx 0..7 → that Kanto gym, trainerIdx 0..4
    //   sourceIdx 8     → Elite/Champion pool, trainerIdx 0..4
    uint8_t         autoEnabled;            // 1 = auto config active
    uint8_t         autoSlots[5][2];        // [source, trainer] per slot
    uint8_t         autoLeaderLevel;        // 0 = canonical, else override
    uint8_t         reserved[42];
};
#pragma pack(pop)

// ── Active challenge session (RAM only) ───────────────────────────────────────
enum GauntletSessionState : uint8_t {
    GS_IDLE = 0,
    GS_AWAIT_PARTY,
    GS_BATTLING,
};

struct GauntletSession {
    uint32_t             nodeNum;
    GauntletSessionState state;
    uint32_t             lastActivity;
    Gen1Party            party;
    uint8_t              e4Progress;       // E4 trainer fights cleared (0..4)
    uint8_t              rosterProgress;   // roster slots cleared in this run
    bool                 beatE4;           // cleared all 4 trainers
    bool                 beatBoss;         // cleared the fixed gym leader boss (Gary)
};

// ── Battle outcome (auto-resolve) ─────────────────────────────────────────────
enum GauntletOutcome : uint8_t { GO_WIN, GO_LOSS };

struct GauntletBattleResult {
    GauntletOutcome outcome;
    uint16_t        challengerSurvivors;
    uint16_t        defenderSurvivors;
    uint16_t        turns;
    char            summary[160];
};
