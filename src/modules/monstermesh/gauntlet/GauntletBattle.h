// SPDX-License-Identifier: MIT
//
// GauntletBattle — party building / parsing / display utilities for the gym.
//
// The gym does NOT simulate battles. The MM Terminal does. This file exists
// so the gym can build a Gen1Party (e.g. its current leader's roster, or a
// fallback Elite Four trainer) and ship it to the T-Deck via
// TEXT_BATTLE_PARTY chunks. The T-Deck plugs that party into its existing
// Gen1BattleEngine and runs the fight locally.
//
// No Gen1BattleEngine.cpp dependency — see Gen1MinimalStats.h for the small
// stat-computation helper used here. This keeps the gym build small enough
// to fit on flash-constrained boards (T114, etc.).

#pragma once
#include "GauntletData.h"

// Build a "party" from a comma-separated string. Format:
//   "Pikachu,Charizard:60,Blastoise"
// Pokemon may be referenced by name (case-insensitive prefix match) or
// pokedex number. Default level is 50, range 1..100. Each mon gets a default
// 4-move set keyed off its primary type. Returns number of mons parsed.
uint8_t gauntletParseParty(const char *csv, Gen1Party &out, const char *otName);

// Render a party as a short comma-separated nickname list, max ~60 chars.
void gauntletFormatParty(const Gen1Party &party, char *out, uint8_t maxLen);

// Build the four canonical Elite Four parties (Lorelei, Bruno, Agatha, Lance)
// at a fixed level. Used as the gym's fallback party when no leader has
// claimed the title yet (BBS_FIGHT_REQUEST → gym replies with E4 #0's party).
// slot ∈ {0,1,2,3}.  Returns the trainer's display name.
const char *gauntletBuildE4Party(uint8_t slot, Gen1Party &out);

// Build a Gen1Party from one of the built-in Kanto gym presets.
//   gymIdx           = 0..7 (Pewter..Viridian)
//   trainerIdx       = 0..4 (4 grunts + leader at index 4)
//   overrideBossLvl  = 0 → use canonical preset levels (stock).
//                      else → boss (highest-level mon) becomes this level
//                             and every other mon shifts by the SAME delta
//                             (preserves the canonical -1..-6 spread).
// Returns the trainer's display name, or nullptr on bad indices.
const char *gauntletBuildPresetTrainerParty(uint8_t gymIdx, uint8_t trainerIdx,
                                              Gen1Party &out,
                                              uint8_t overrideBossLvl = 0);
