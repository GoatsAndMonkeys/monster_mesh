// SPDX-License-Identifier: MIT
//
// Legend of Charizard — wild encounter pools keyed to gym progress.
//
// Each "route" represents the area between gym N and gym N+1 in canonical
// Kanto. The encounter pool is sourced from the Bulbapedia route lists for
// Pokemon Red/Blue, narrowed to ~5 species per route to keep the data tables
// small. Species use pokedex numbers (Gen1BattleEngine handles the
// dex→internal conversion via the same path SAV-loaded parties take).
//
// `loc explore` picks a route based on the player's badge count: 0 badges →
// route 0 (Viridian Forest / Route 1), 8 badges → route 7 (Cerulean Cave).

#pragma once

#include <Arduino.h>
#include "PokemonData.h"

struct LordRouteEncounter {
    uint8_t species;     // pokedex number
    uint8_t levelMin;
    uint8_t levelMax;
    uint8_t moves[4];    // showdown move IDs; engine pads PP from move table
    uint8_t weight;      // relative spawn weight 1..10
};

struct LordRoute {
    const char *name;                    // "Viridian Forest" etc.
    const LordRouteEncounter *pool;
    uint8_t poolCount;
};

// Returns nullptr if `routeIdx >= 8`.
const LordRoute *lordRoute(uint8_t routeIdx);

// Pick one weighted-random encounter and write it into `out` slot 0 as a
// solo wild party. Returns false on bad routeIdx. The level rolls uniformly
// in [levelMin, levelMax]. RNG seeded from millis() ^ esp_random().
bool lordPickWildEncounter(uint8_t routeIdx, Gen1Party &out);
