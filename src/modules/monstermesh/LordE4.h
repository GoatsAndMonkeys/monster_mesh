// SPDX-License-Identifier: MIT
//
// Indigo Plateau — Elite Four + Champion rosters for Legend of Charizard.
// Gated on all 8 badges. Beating member 4 (Champion) sets `leagueCleared=1`
// in LordSave, which is the unlock gate for NG+ tiers.
//
// Movesets sourced from Pokemon Red/Blue's actual E4 teams.

#pragma once

#include <Arduino.h>
#include "PokemonData.h"
#include "LordGyms.h"   // reuse LordGymMon (species/level/moves layout)

struct LordE4Member {
    const char *name;
    const char *title;          // "Elite Four" or "Champion"
    const char *typeFlavor;     // "Ice", "Fighting", etc. — UI hint, no engine effect
    const LordGymMon *roster;
    uint8_t roster_count;
};

static constexpr uint8_t LORD_E4_COUNT = 5;   // 4 Elite Four + 1 Champion

const LordE4Member *lordE4Member(uint8_t idx);

// Build a 6-slot Gen1Party for E4 member `idx` (0..4) with default DVs and
// auto-padding (extra slots zeroed out — engine treats hp=0 as "not present").
// Returns false on bad idx. Mirrors lordBuildGymParty's contract.
bool lordBuildE4Party(uint8_t idx, Gen1Party &out);
