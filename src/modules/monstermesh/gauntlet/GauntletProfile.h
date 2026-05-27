// SPDX-License-Identifier: MIT
//
// GauntletProfile — per-player record of gym challenges. One file per node:
//   /monstermesh/gauntlet_profiles/<nodeHex>.bin
//
// Tracks: total challenges, wins, gym titles, best (lowest) gauntlet rank
// reached, last party. Used by `!gym profile` reply and by the central MQTT
// dashboard if it queries individual nodes.

#pragma once
#include "GauntletData.h"

static constexpr uint32_t GAUNTLET_PROFILE_MAGIC   = 0x47504631u; // 'GPF1'
static constexpr uint8_t  GAUNTLET_PROFILE_VERSION = 1;

#pragma pack(push, 1)
struct GauntletProfile {
    uint32_t magic;
    uint8_t  version;
    uint8_t  _pad0[3];
    uint32_t nodeNum;
    char     name[GAUNTLET_NAME_MAX];
    uint32_t totalChallenges;
    uint32_t totalWins;
    uint32_t totalLosses;
    uint8_t  gymTitles;        // times became leader (this gym only)
    uint8_t  bestRank;         // lowest (= hardest) rank ever achieved (1-based)
    uint8_t  reachedLeader;    // ever reached the leader fight? 0/1
    uint8_t  _pad1;
    uint32_t lastSeen;
    char     lastParty[64];
    uint8_t  reserved[32];
};
#pragma pack(pop)

bool gauntletProfileLoad(uint32_t nodeNum, GauntletProfile &p);
bool gauntletProfileSave(const GauntletProfile &p);

// Convenience: load (or zero-init), apply outcome, save.
//   ranked          — true if the player took a roster slot (= lost on the ladder)
//   rankAchieved    — 1-based rank of the slot they took (only meaningful if ranked)
//   becameLeader    — true if they swept the gauntlet
//   reachedLeader   — true if they at least faced the leader (won or lost)
void gauntletProfileUpdate(uint32_t nodeNum, const char *name,
                            const char *partyCsv,
                            bool challengeStarted,
                            bool ranked, uint8_t rankAchieved,
                            bool becameLeader, bool reachedLeader);
