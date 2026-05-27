// SPDX-License-Identifier: MIT
//
// GauntletGyms — built-in Kanto gym roster presets (8 gyms × 5 trainers).
//
// Header-only data extracted from LordGyms.cpp so the gym build can pull in
// the rosters WITHOUT compiling LordGyms.cpp (which depends on
// Gen1BattleEngine + LordLogic). This file has no .cpp dep — the gauntlet
// module looks up presets via inline accessors and builds parties through
// the engine-free Gen1MinimalStats helper.
//
// Admin can switch the gym to any of these presets via the
// `admin set N` DM command (N = 1..8).
//
// All Pokemon use Gen 1 dex numbers (1-151). Move IDs come from the
// Showdown Gen 1 move table (showdown_gen1_moves.h indices). Levels mirror
// the canonical Red/Blue rosters with light scaling.

#pragma once
#include <stdint.h>

struct GymPresetMon {
    uint8_t dex;
    uint8_t level;
    uint8_t moves[4];
};

struct GymPresetTrainer {
    const char         *name;
    uint8_t             monCount;
    const GymPresetMon *party;
};

struct GymPreset {
    const char            *name;       // "Pewter", "Cerulean", …
    const char            *leaderName; // "Brock", "Misty", …
    const char            *badgeName;  // "Boulder", "Cascade", …
    uint8_t                minLevelHint;
    GymPresetTrainer       trainers[5]; // 4 grunts + leader at index 4
};

static constexpr uint8_t GAUNTLET_GYM_COUNT = 8;
static constexpr uint8_t GAUNTLET_GYM_LEADER_IDX = 4;

// ─── Per-trainer parties ─────────────────────────────────────────────────────

// Pewter — Brock (Rock/Ground)
static const GymPresetMon gym1_t0[] = { { 74, 10, {33,111, 0, 0} } };
static const GymPresetMon gym1_t1[] = { { 16,  9, {16, 98, 0, 0} },
                                         { 19,  9, {33, 45, 0, 0} } };
static const GymPresetMon gym1_t2[] = { { 27, 11, {10, 28, 0, 0} } };
static const GymPresetMon gym1_t3[] = { { 74, 11, {88, 33, 0, 0} },
                                         { 95, 12, {88, 20, 0, 0} } };
static const GymPresetMon gym1_t4[] = { { 74, 12, {33,111,88, 0} },
                                         { 95, 14, {33, 20,88,43} } };

// Cerulean — Misty (Water)
static const GymPresetMon gym2_t0[] = { {120, 16, {33, 55, 0, 0} } };
static const GymPresetMon gym2_t1[] = { { 54, 19, {10, 39,55, 0} } };
static const GymPresetMon gym2_t2[] = { {118, 17, {64, 39, 0, 0} },
                                         { 72, 18, {40, 51, 0, 0} } };
static const GymPresetMon gym2_t3[] = { { 86, 20, {44, 45,62, 0} } };
static const GymPresetMon gym2_t4[] = { {120, 18, {33, 55, 0, 0} },
                                         {121, 21, {33, 55,61,50} } };

// Vermilion — Lt. Surge (Electric)
static const GymPresetMon gym3_t0[] = { { 25, 20, {84, 28,98, 0} } };
static const GymPresetMon gym3_t1[] = { {100, 21, {33,103,84, 0} } };
static const GymPresetMon gym3_t2[] = { { 81, 20, {33, 48,84, 0} },
                                         { 82, 22, {33, 84,49, 0} } };
static const GymPresetMon gym3_t3[] = { {100, 23, {33, 84,103,0} },
                                         {125, 23, {98, 85,50, 0} } };
static const GymPresetMon gym3_t4[] = { {100, 21, {84,103,153,0} },
                                         { 26, 24, {85, 28,86,98} },
                                         { 25, 21, {98, 84,45, 0} } };

// Celadon — Erika (Grass)
static const GymPresetMon gym4_t0[] = { { 43, 24, {71, 78,51, 0} } };
static const GymPresetMon gym4_t1[] = { { 69, 24, {22, 74, 0, 0} } };
static const GymPresetMon gym4_t2[] = { {102, 26, {79, 77,71, 0} },
                                         { 44, 25, {78, 51, 0, 0} } };
static const GymPresetMon gym4_t3[] = { { 70, 26, {22, 77,79, 0} } };
static const GymPresetMon gym4_t4[] = { {114, 29, {22, 20,75, 0} },
                                         { 71, 29, {77, 79,22,72} },
                                         { 45, 29, {78, 79,80,72} } };

// Fuchsia — Koga (Poison)
static const GymPresetMon gym5_t0[] = { { 41, 31, {44,103,109,0} },
                                         { 41, 31, {44, 48, 0, 0} } };
static const GymPresetMon gym5_t1[] = { { 48, 32, {48, 50,93, 0} } };
static const GymPresetMon gym5_t2[] = { { 88, 33, {106,51,124,0} },
                                         { 89, 33, {124,106,34,0} } };
static const GymPresetMon gym5_t3[] = { { 42, 34, {44, 48,100,0} } };
static const GymPresetMon gym5_t4[] = { {109, 37, {123,108,0, 0} },
                                         { 89, 39, {139,51,34,106} },
                                         {109, 37, {139,123,108,0} },
                                         {110, 43, {124,123,139,108} } };

// Saffron — Sabrina (Psychic)
static const GymPresetMon gym6_t0[] = { { 96, 34, {95, 50,93, 0} },
                                         { 97, 34, {95, 50,93, 0} } };
static const GymPresetMon gym6_t1[] = { { 64, 38, {93, 50,105,0} } };
static const GymPresetMon gym6_t2[] = { {122, 39, {50, 93,113,115} } };
static const GymPresetMon gym6_t3[] = { { 96, 36, {95, 93,138,0} },
                                         { 96, 36, {95, 93,138,0} } };
static const GymPresetMon gym6_t4[] = { { 64, 38, {93, 50,115,105} },
                                         {122, 37, {93,113,115,0} },
                                         { 49, 38, {48, 78,60, 0} },
                                         { 65, 43, {94,105,115,93} } };

// Cinnabar — Blaine (Fire)
static const GymPresetMon gym7_t0[] = { { 77, 34, {52, 45,39, 0} },
                                         {126, 38, {108,52,98, 0} } };
static const GymPresetMon gym7_t1[] = { { 58, 40, {52, 43,36, 0} } };
static const GymPresetMon gym7_t2[] = { { 78, 41, {52, 39,23,83} } };
static const GymPresetMon gym7_t3[] = { { 37, 42, {52, 45,98,46} },
                                         { 38, 42, {52, 46,98,53} } };
static const GymPresetMon gym7_t4[] = { { 58, 42, {52, 43,36,46} },
                                         {126, 40, {52, 83,109,34} },
                                         { 78, 42, {52, 23,83,36} },
                                         { 59, 47, {52, 46,36,53} } };

// Viridian — Giovanni (Ground)
static const GymPresetMon gym8_t0[] = { { 24, 37, {40, 43,35, 0} } };
static const GymPresetMon gym8_t1[] = { {111, 42, {88, 43,34, 0} } };
static const GymPresetMon gym8_t2[] = { { 50, 41, {10, 28,91, 0} },
                                         { 51, 42, {10, 28,91,89} } };
static const GymPresetMon gym8_t3[] = { { 34, 45, {40, 30,31, 0} } };
static const GymPresetMon gym8_t4[] = { {111, 45, {88, 34,31,43} },
                                         { 51, 42, {28, 91,89,163} },
                                         { 31, 44, {40, 89,34,43} },
                                         {112, 50, {89, 23,43,88} },
                                         { 34, 45, {40, 31,89,30} } };

// ─── Gym table ───────────────────────────────────────────────────────────────

#define GAUNTLET_MON_COUNT(arr) (uint8_t)(sizeof(arr) / sizeof((arr)[0]))

static const GymPreset GAUNTLET_GYMS[GAUNTLET_GYM_COUNT] = {
    { "Pewter",    "Brock",     "Boulder",  10,
        {{ "Camper Liam",       GAUNTLET_MON_COUNT(gym1_t0), gym1_t0 },
         { "Lass Crissy",       GAUNTLET_MON_COUNT(gym1_t1), gym1_t1 },
         { "Youngster Ben",     GAUNTLET_MON_COUNT(gym1_t2), gym1_t2 },
         { "Hiker Marcos",      GAUNTLET_MON_COUNT(gym1_t3), gym1_t3 },
         { "Brock",             GAUNTLET_MON_COUNT(gym1_t4), gym1_t4 }}},

    { "Cerulean",  "Misty",     "Cascade",  18,
        {{ "Swimmer Luis",      GAUNTLET_MON_COUNT(gym2_t0), gym2_t0 },
         { "Trainer Diana",     GAUNTLET_MON_COUNT(gym2_t1), gym2_t1 },
         { "Lass Haley",        GAUNTLET_MON_COUNT(gym2_t2), gym2_t2 },
         { "Swimmer Parker",    GAUNTLET_MON_COUNT(gym2_t3), gym2_t3 },
         { "Misty",             GAUNTLET_MON_COUNT(gym2_t4), gym2_t4 }}},

    { "Vermilion", "Lt. Surge", "Thunder",  22,
        {{ "Gentleman Tucker",  GAUNTLET_MON_COUNT(gym3_t0), gym3_t0 },
         { "Rocker Luca",       GAUNTLET_MON_COUNT(gym3_t1), gym3_t1 },
         { "Sailor Dwayne",     GAUNTLET_MON_COUNT(gym3_t2), gym3_t2 },
         { "Engineer Baily",    GAUNTLET_MON_COUNT(gym3_t3), gym3_t3 },
         { "Lt. Surge",         GAUNTLET_MON_COUNT(gym3_t4), gym3_t4 }}},

    { "Celadon",   "Erika",     "Rainbow",  26,
        {{ "Lass Michelle",     GAUNTLET_MON_COUNT(gym4_t0), gym4_t0 },
         { "Beauty Lola",       GAUNTLET_MON_COUNT(gym4_t1), gym4_t1 },
         { "Picnicker Tanya",   GAUNTLET_MON_COUNT(gym4_t2), gym4_t2 },
         { "Trainer Miranda",   GAUNTLET_MON_COUNT(gym4_t3), gym4_t3 },
         { "Erika",             GAUNTLET_MON_COUNT(gym4_t4), gym4_t4 }}},

    { "Fuchsia",   "Koga",      "Soul",     32,
        {{ "Juggler Nate",      GAUNTLET_MON_COUNT(gym5_t0), gym5_t0 },
         { "Tamer Edgar",       GAUNTLET_MON_COUNT(gym5_t1), gym5_t1 },
         { "Juggler Kirk",      GAUNTLET_MON_COUNT(gym5_t2), gym5_t2 },
         { "Ninja Shin",        GAUNTLET_MON_COUNT(gym5_t3), gym5_t3 },
         { "Koga",              GAUNTLET_MON_COUNT(gym5_t4), gym5_t4 }}},

    { "Saffron",   "Sabrina",   "Marsh",    36,
        {{ "Medium Martha",     GAUNTLET_MON_COUNT(gym6_t0), gym6_t0 },
         { "Channeler Abigail", GAUNTLET_MON_COUNT(gym6_t1), gym6_t1 },
         { "Medium Grace",      GAUNTLET_MON_COUNT(gym6_t2), gym6_t2 },
         { "Psychic Rodney",    GAUNTLET_MON_COUNT(gym6_t3), gym6_t3 },
         { "Sabrina",           GAUNTLET_MON_COUNT(gym6_t4), gym6_t4 }}},

    { "Cinnabar",  "Blaine",    "Volcano",  42,
        {{ "Burglar Derek",     GAUNTLET_MON_COUNT(gym7_t0), gym7_t0 },
         { "Super Nerd Sam",    GAUNTLET_MON_COUNT(gym7_t1), gym7_t1 },
         { "Burglar Ramon",     GAUNTLET_MON_COUNT(gym7_t2), gym7_t2 },
         { "Super Nerd Zac",    GAUNTLET_MON_COUNT(gym7_t3), gym7_t3 },
         { "Blaine",            GAUNTLET_MON_COUNT(gym7_t4), gym7_t4 }}},

    { "Viridian",  "Giovanni",  "Earth",    47,
        {{ "Tough Guy Nick",    GAUNTLET_MON_COUNT(gym8_t0), gym8_t0 },
         { "Cooltrainer Leo",   GAUNTLET_MON_COUNT(gym8_t1), gym8_t1 },
         { "Tough Guy Shane",   GAUNTLET_MON_COUNT(gym8_t2), gym8_t2 },
         { "Cooltrainer Gwen",  GAUNTLET_MON_COUNT(gym8_t3), gym8_t3 },
         { "Giovanni",          GAUNTLET_MON_COUNT(gym8_t4), gym8_t4 }}},
};

#undef GAUNTLET_MON_COUNT

static inline const GymPreset *gauntletGymPreset(uint8_t idx)
{
    return (idx < GAUNTLET_GYM_COUNT) ? &GAUNTLET_GYMS[idx] : nullptr;
}
