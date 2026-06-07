// SPDX-License-Identifier: MIT
//
// KantoZones — canonical Gen 1 Kanto progression for Pentest Pikachu.
// Pikachu's level maps to a zone; each zone has a wild pool (Red/Blue route
// encounters) and an optional gym leader.  Ported from the T-Deck pentest
// module (src/modules/pentest/KantoZones.h) to the Pi platform shim.
//
// Encounter mix at scan time (in TerminalUI::startPentestBattle):
//   60 % wild from the current zone pool
//   30 % current zone's gym leader (if any)
//   10 % completely random Gen 1 mon

#pragma once
#include "platform.h"

struct KantoWildMon {
    uint8_t dex;        // Pokedex number (1..151)
    uint8_t minLvl;
    uint8_t maxLvl;
};

struct KantoZone {
    const char         *name;
    uint8_t             minPikaLvl;
    uint8_t             gymIdx;         // 0..7 or 255 = no leader
    uint8_t             gymLvl;
    const KantoWildMon *wilds;
    uint8_t             wildCount;
    uint8_t             unlockedAfter;  // gym badges required (0 = start)
};

// ── Wild encounter tables — Red/Blue route data ────────────────────────────

static const KantoWildMon KANTO_WILDS_RT1[] = {
    { 16,  2,  5 }, { 19,  2,  5 }, { 21,  3,  5 }, { 56,  3,  5 },
};
static const KantoWildMon KANTO_WILDS_VIRIDIAN_FOREST[] = {
    { 10,  3,  5 }, { 11,  4,  5 }, { 13,  3,  5 }, { 14,  4,  5 },
    { 16,  3,  5 }, { 25,  3,  5 },
};
static const KantoWildMon KANTO_WILDS_MT_MOON[] = {
    { 41,  6, 10 }, { 74,  7,  9 }, { 46,  8, 10 }, { 27,  6,  9 },
    { 35,  8, 10 }, { 21,  6,  9 }, { 19,  6,  9 }, { 39,  8, 10 },
    {138,  9, 11 }, {140,  9, 11 },
};
static const KantoWildMon KANTO_WILDS_ROUTE_5[] = {
    { 16, 10, 13 }, { 43, 10, 13 }, { 69, 10, 13 }, { 52, 10, 13 },
    { 19, 10, 12 }, { 39, 11, 14 }, { 48, 10, 13 }, { 63, 10, 12 },
};
static const KantoWildMon KANTO_WILDS_VERMILION[] = {
    { 21, 13, 17 }, { 23, 13, 15 }, { 96, 11, 15 }, { 56, 11, 13 },
    { 27, 14, 17 }, { 32, 13, 17 }, { 29, 13, 17 }, { 50, 15, 22 },
    { 51, 29, 31 }, { 83, 15, 18 },
};
static const KantoWildMon KANTO_WILDS_ROCK_TUNNEL[] = {
    { 41, 15, 17 }, { 74, 15, 17 }, { 95, 15, 17 }, {104, 16, 19 },
    { 66, 15, 17 }, { 21, 17, 19 }, { 19, 17, 19 }, { 79, 18, 22 },
    { 88, 22, 25 },
};
static const KantoWildMon KANTO_WILDS_CELADON[] = {
    { 37, 18, 20 }, { 58, 18, 20 }, { 39, 18, 20 }, { 52, 18, 20 },
    { 64, 18, 20 }, { 63, 18, 20 }, { 96, 18, 22 }, { 97, 24, 28 },
    {122, 24, 26 }, {137, 20, 24 },
};
static const KantoWildMon KANTO_WILDS_SAFARI[] = {
    {114, 22, 24 }, {115, 25, 28 }, {123, 22, 25 }, {127, 22, 25 },
    {128, 21, 26 }, { 84, 22, 25 }, { 33, 22, 25 }, { 30, 22, 25 },
    { 49, 22, 25 }, {102, 22, 26 }, {111, 23, 27 }, {113, 23, 25 },
    { 47, 23, 25 }, {108, 22, 24 },
};
static const KantoWildMon KANTO_WILDS_CINNABAR[] = {
    { 88, 23, 27 }, { 89, 28, 32 }, {109, 29, 32 }, {110, 30, 34 },
    { 77, 28, 34 }, { 78, 33, 38 }, {126, 30, 34 }, { 58, 32, 36 },
    { 37, 32, 36 }, {116, 25, 30 }, {118, 25, 28 }, { 60, 25, 30 },
    { 72, 25, 30 },
};
static const KantoWildMon KANTO_WILDS_VICTORY_ROAD[] = {
    { 95, 32, 36 }, { 66, 33, 36 }, { 67, 36, 39 }, {105, 33, 35 },
    { 75, 32, 36 }, { 49, 34, 38 }, { 23, 38, 42 }, { 33, 40, 44 },
    {142, 36, 41 }, {147, 36, 41 }, {148, 38, 42 }, {149, 40, 45 },
};
static const KantoWildMon KANTO_WILDS_POKEMON_TOWER[] = {
    { 92, 20, 24 }, { 93, 22, 28 }, {104, 19, 22 }, {105, 28, 30 },
};
static const KantoWildMon KANTO_WILDS_POWER_PLANT[] = {
    { 81, 21, 33 }, { 82, 30, 35 }, {100, 21, 33 }, {101, 33, 36 },
    { 25, 24, 25 }, { 26, 33, 33 }, {145, 50, 50 },
};
static const KantoWildMon KANTO_WILDS_SEAFOAM[] = {
    { 41, 28, 32 }, { 42, 32, 39 }, { 86, 28, 32 }, { 87, 38, 42 },
    { 79, 28, 32 }, { 98, 28, 32 }, { 54, 28, 32 }, {116, 30, 32 },
    {144, 50, 50 },
};
static const KantoWildMon KANTO_WILDS_MANSION[] = {
    { 19, 30, 37 }, { 20, 35, 37 }, { 88, 30, 36 }, { 89, 35, 39 },
    {109, 30, 36 }, {110, 34, 39 }, {126, 34, 36 }, {132, 30, 30 },
    {146, 50, 50 },
};
static const KantoWildMon KANTO_WILDS_CERULEAN_CAVE[] = {
    {132, 45, 50 }, { 82, 49, 54 }, { 64, 45, 50 }, { 67, 46, 49 },
    {113, 47, 51 }, { 39, 49, 53 }, { 95, 50, 55 }, {143, 30, 30 },
    {150, 70, 70 },
};

// ── Zone table (canonical Red/Blue progression) ────────────────────────────
static const KantoZone KANTO_ZONES[] = {
    { "Route 1",            1, 255,  0, KANTO_WILDS_RT1,             sizeof(KANTO_WILDS_RT1)/sizeof(KantoWildMon),             0 },
    { "Viridian Forest",    3,   0, 14, KANTO_WILDS_VIRIDIAN_FOREST, sizeof(KANTO_WILDS_VIRIDIAN_FOREST)/sizeof(KantoWildMon), 0 },
    { "Mt Moon",            6, 255,  0, KANTO_WILDS_MT_MOON,         sizeof(KANTO_WILDS_MT_MOON)/sizeof(KantoWildMon),         1 },
    { "Route 5 / Cerulean",10,   1, 21, KANTO_WILDS_ROUTE_5,         sizeof(KANTO_WILDS_ROUTE_5)/sizeof(KantoWildMon),         1 },
    { "Vermilion",         13,   2, 28, KANTO_WILDS_VERMILION,       sizeof(KANTO_WILDS_VERMILION)/sizeof(KantoWildMon),       2 },
    { "Rock Tunnel",       16, 255,  0, KANTO_WILDS_ROCK_TUNNEL,     sizeof(KANTO_WILDS_ROCK_TUNNEL)/sizeof(KantoWildMon),     3 },
    { "Pokemon Tower",     20, 255,  0, KANTO_WILDS_POKEMON_TOWER,   sizeof(KANTO_WILDS_POKEMON_TOWER)/sizeof(KantoWildMon),   3 },
    { "Celadon",           18,   3, 29, KANTO_WILDS_CELADON,         sizeof(KANTO_WILDS_CELADON)/sizeof(KantoWildMon),         3 },
    { "Fuchsia / Safari",  22,   4, 43, KANTO_WILDS_SAFARI,          sizeof(KANTO_WILDS_SAFARI)/sizeof(KantoWildMon),          4 },
    { "Saffron",           26,   5, 50, KANTO_WILDS_CELADON,         sizeof(KANTO_WILDS_CELADON)/sizeof(KantoWildMon),         5 },
    { "Cinnabar",          28,   6, 47, KANTO_WILDS_CINNABAR,        sizeof(KANTO_WILDS_CINNABAR)/sizeof(KantoWildMon),        6 },
    { "Power Plant",       30, 255,  0, KANTO_WILDS_POWER_PLANT,     sizeof(KANTO_WILDS_POWER_PLANT)/sizeof(KantoWildMon),     5 },
    { "Seafoam Islands",   33, 255,  0, KANTO_WILDS_SEAFOAM,         sizeof(KANTO_WILDS_SEAFOAM)/sizeof(KantoWildMon),         6 },
    { "Pokemon Mansion",   33, 255,  0, KANTO_WILDS_MANSION,         sizeof(KANTO_WILDS_MANSION)/sizeof(KantoWildMon),         6 },
    { "Viridian Gym",      36,   7, 50, KANTO_WILDS_VICTORY_ROAD,    sizeof(KANTO_WILDS_VICTORY_ROAD)/sizeof(KantoWildMon),    7 },
    { "Cerulean Cave",     45, 255,  0, KANTO_WILDS_CERULEAN_CAVE,   sizeof(KANTO_WILDS_CERULEAN_CAVE)/sizeof(KantoWildMon),   8 },
};

static constexpr uint8_t KANTO_ZONE_COUNT = sizeof(KANTO_ZONES) / sizeof(KantoZone);

inline const KantoZone &kantoZoneForLevel(uint8_t pikaLvl)
{
    uint8_t pick = 0;
    for (uint8_t i = 0; i < KANTO_ZONE_COUNT; ++i) {
        if (KANTO_ZONES[i].minPikaLvl <= pikaLvl) pick = i;
        else break;
    }
    return KANTO_ZONES[pick];
}
