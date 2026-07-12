#pragma once
// ── Gen 1 (Red/Blue) evolutions ──────────────────────────────────────────────
// Used by the pentest journey so a caught mon evolves as its trip level climbs
// (e.g. a Weedle levelled to 68 fights as Beedrill and inherits Beedrill's
// level-up learnset, instead of being a moveless Weedle).
//
// Level-up evolutions use their canonical Gen-1 level. There are no evolution
// stones or trading in the journey, so stone/trade evolutions are given a
// pseudo-level so a high-level mon still reaches its final form:
//     stone  -> level 30      trade -> level 37
// Eevee has no stone to pick, so it defaults to Vaporeon.
//
// Move IDs / dex numbers match the rest of the Gen-1 tables.

#include "platform.h"

struct Gen1Evo { uint8_t from; uint8_t to; uint8_t level; };

// Sorted-ish by `from`; each species has at most one level-up evolution edge.
static const Gen1Evo gen1EvoTable[] = {
    {1,2,16},   {2,3,32},               // Bulbasaur line
    {4,5,16},   {5,6,36},               // Charmander line
    {7,8,16},   {8,9,36},               // Squirtle line
    {10,11,7},  {11,12,10},             // Caterpie line
    {13,14,7},  {14,15,10},             // Weedle line
    {16,17,18}, {17,18,36},             // Pidgey line
    {19,20,20},                         // Rattata
    {21,22,20},                         // Spearow
    {23,24,22},                         // Ekans
    {25,26,30},                         // Pikachu (Thunder Stone)
    {27,28,22},                         // Sandshrew
    {29,30,16}, {30,31,30},             // Nidoran F (Moon Stone)
    {32,33,16}, {33,34,30},             // Nidoran M (Moon Stone)
    {35,36,30},                         // Clefairy (Moon Stone)
    {37,38,30},                         // Vulpix (Fire Stone)
    {39,40,30},                         // Jigglypuff (Moon Stone)
    {41,42,22},                         // Zubat
    {43,44,21}, {44,45,30},             // Oddish (Leaf Stone)
    {46,47,24},                         // Paras
    {48,49,31},                         // Venonat
    {50,51,26},                         // Diglett
    {52,53,28},                         // Meowth
    {54,55,33},                         // Psyduck
    {56,57,28},                         // Mankey
    {58,59,30},                         // Growlithe (Fire Stone)
    {60,61,25}, {61,62,30},             // Poliwag (Water Stone)
    {63,64,16}, {64,65,37},             // Abra (Kadabra->Alakazam trade)
    {66,67,28}, {67,68,37},             // Machop (Machoke->Machamp trade)
    {69,70,21}, {70,71,30},             // Bellsprout (Weepinbell->Victreebel stone)
    {72,73,30},                         // Tentacool
    {74,75,25}, {75,76,37},             // Geodude (Graveler->Golem trade)
    {77,78,40},                         // Ponyta
    {79,80,37},                         // Slowpoke
    {81,82,30},                         // Magnemite
    {84,85,31},                         // Doduo
    {86,87,34},                         // Seel
    {88,89,38},                         // Grimer
    {90,91,30},                         // Shellder (Water Stone)
    {92,93,25}, {93,94,37},             // Gastly (Haunter->Gengar trade)
    {96,97,26},                         // Drowzee
    {98,99,28},                         // Krabby
    {100,101,30},                       // Voltorb
    {102,103,30},                       // Exeggcute (Leaf Stone)
    {104,105,28},                       // Cubone
    {109,110,35},                       // Koffing
    {111,112,42},                       // Rhyhorn
    {116,117,32},                       // Horsea
    {118,119,33},                       // Goldeen
    {120,121,30},                       // Staryu (Water Stone)
    {129,130,20},                       // Magikarp
    {133,134,30},                       // Eevee -> Vaporeon (default)
    {138,139,40},                       // Omanyte
    {140,141,40},                       // Kabuto
    {147,148,30}, {148,149,55},         // Dratini line
};

// Return the species `dex` evolves INTO at `level`, or 0 if it does not evolve
// at this level (or is not an evolving species).
static inline uint8_t gen1EvolveStep(uint8_t dex, uint8_t level) {
    for (const Gen1Evo &e : gen1EvoTable)
        if (e.from == dex && level >= e.level) return e.to;
    return 0;
}

// Walk the whole evolution chain and return the final form reachable at `level`.
// A non-evolving species (or one below its first evolution level) returns `dex`.
static inline uint8_t gen1FinalFormAtLevel(uint8_t dex, uint8_t level) {
    uint8_t cur = dex;
    for (int guard = 0; guard < 3; ++guard) {   // Gen-1 chains are <= 3 stages
        uint8_t nxt = gen1EvolveStep(cur, level);
        if (!nxt || nxt == cur) break;
        cur = nxt;
    }
    return cur;
}
