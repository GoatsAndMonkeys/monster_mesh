// SPDX-License-Identifier: MIT
// See ../shared/LordGyms.h.
//
// Ported from T-Deck MonsterMesh firmware.
// Gym rosters are the canonical Pokemon Red/Blue Kanto gym trainers — the real
// junior trainers AND the leader, in fight order with the leader LAST.  Each
// gym has its actual trainer count (Pewter 2 ... Viridian 9), and each trainer
// fields their real team (no padding to 6).

#include "../shared/LordGyms.h"
#include "Gen1BattleEngine.h"
#include "../shared/PokemonData.h"
#include "../shared/DaycareSavPatcher.h"      // dexToInternal[]
#include "../shared/LordLogic.h"              // NG+ scaling + coverage moves
#include "showdown_gen1_moves.h"

#include <string.h>
#include <stdio.h>

// ── Per-trainer rosters (national dex, Red/Blue level, Gen-1 move IDs) ────────

// 1. Pewter — Brock (Rock). 2 trainers.
static const LordGymMon g1_t0[] = {  // Jr. Trainer
    {  50 /*Diglett*/,  11, { 10, 28, 45,  0 } },
    {  27 /*Sandshrew*/,11, { 10, 28,111,  0 } },
};
static const LordGymMon g1_t1[] = {  // Brock
    {  74 /*Geodude*/,  12, { 33,111, 88,  0 } },
    {  95 /*Onix*/,     14, { 33, 20, 88, 43 } },
};

// 2. Cerulean — Misty (Water). 3 trainers.
static const LordGymMon g2_t0[] = {  // Swimmer
    { 116 /*Horsea*/,   16, {145,109, 55,  0 } },
    {  90 /*Shellder*/, 16, { 33,110, 62,  0 } },
};
static const LordGymMon g2_t1[] = {  // Jr. Trainer
    { 118 /*Goldeen*/,  19, { 64, 39, 30, 55 } },
};
static const LordGymMon g2_t2[] = {  // Misty
    { 120 /*Staryu*/,   18, { 33, 55,  0,  0 } },
    { 121 /*Starmie*/,  21, { 33, 55, 61, 50 } },
};

// 3. Vermilion — Lt. Surge (Electric). 4 trainers.
static const LordGymMon g3_t0[] = {  // Sailor
    {  25 /*Pikachu*/,  21, { 84, 98, 39,  0 } },
    {  25 /*Pikachu*/,  21, { 84, 98, 39,  0 } },
};
static const LordGymMon g3_t1[] = {  // Rocker
    { 100 /*Voltorb*/,  20, { 33,103, 49,  0 } },
    {  81 /*Magnemite*/,20, { 33, 84, 48,  0 } },
    { 100 /*Voltorb*/,  20, { 33,103, 49,  0 } },
};
static const LordGymMon g3_t2[] = {  // Gentleman
    {  25 /*Pikachu*/,  23, { 84, 98, 86, 45 } },
};
static const LordGymMon g3_t3[] = {  // Lt. Surge
    { 100 /*Voltorb*/,  21, { 84,103,108,  0 } },
    {  25 /*Pikachu*/,  18, { 98, 84, 45,  0 } },
    {  26 /*Raichu*/,   24, { 85, 86, 98, 28 } },
};

// 4. Celadon — Erika (Grass). 8 trainers.
static const LordGymMon g4_t0[] = {  // Lass
    {  69 /*Bellsprout*/,23, { 22, 74, 35,  0 } },
    {  70 /*Weepinbell*/,23, { 22, 77, 75,  0 } },
};
static const LordGymMon g4_t1[] = {  // Beauty
    {  43 /*Oddish*/,    21, { 71, 77, 51,  0 } },
    {  69 /*Bellsprout*/,21, { 22, 74,  0,  0 } },
    {  43 /*Oddish*/,    21, { 71, 77,  0,  0 } },
    {  69 /*Bellsprout*/,21, { 22, 35,  0,  0 } },
};
static const LordGymMon g4_t2[] = {  // Beauty
    {  69 /*Bellsprout*/,24, { 22, 77, 35,  0 } },
    {  69 /*Bellsprout*/,24, { 22, 77, 35,  0 } },
};
static const LordGymMon g4_t3[] = {  // Jr. Trainer (behind a Cut tree)
    {   1 /*Bulbasaur*/, 24, { 33, 22, 73, 45 } },
    {   2 /*Ivysaur*/,   24, { 22, 75, 77, 74 } },
};
static const LordGymMon g4_t4[] = {  // Beauty
    { 102 /*Exeggcute*/, 26, { 79, 78, 71, 93 } },
};
static const LordGymMon g4_t5[] = {  // Cooltrainer
    {  70 /*Weepinbell*/,24, { 22, 79, 75,  0 } },
    {  44 /*Gloom*/,     24, { 71, 78, 51,  0 } },
    {   2 /*Ivysaur*/,   24, { 22, 75, 77,  0 } },
};
static const LordGymMon g4_t6[] = {  // Lass
    {  43 /*Oddish*/,    23, { 71, 77, 51,  0 } },
    {  44 /*Gloom*/,     23, { 71, 78, 51,  0 } },
};
static const LordGymMon g4_t7[] = {  // Erika
    {  71 /*Victreebel*/,29, { 75, 79, 51, 35 } },
    { 114 /*Tangela*/,   24, { 22, 20, 71,  0 } },
    {  45 /*Vileplume*/, 29, { 80, 79, 78, 72 } },
};

// 5. Fuchsia — Koga (Poison). 7 trainers.
static const LordGymMon g5_t0[] = {  // Juggler
    {  96 /*Drowzee*/,  34, { 95, 93, 50,  0 } },
    {  64 /*Kadabra*/,  34, { 93, 50, 60,  0 } },
};
static const LordGymMon g5_t1[] = {  // Juggler
    {  97 /*Hypno*/,    38, { 95, 93, 94, 50 } },
};
static const LordGymMon g5_t2[] = {  // Juggler
    {  96 /*Drowzee*/,  31, { 95, 93,  0,  0 } },
    {  96 /*Drowzee*/,  31, { 95, 93,  0,  0 } },
    {  64 /*Kadabra*/,  31, { 93, 50,  0,  0 } },
    {  96 /*Drowzee*/,  31, { 95, 93,  0,  0 } },
};
static const LordGymMon g5_t3[] = {  // Tamer
    {  24 /*Arbok*/,    33, { 35, 40, 44, 43 } },
    {  28 /*Sandslash*/,33, {163, 28,154,  0 } },
    {  24 /*Arbok*/,    33, { 35, 40, 44,  0 } },
};
static const LordGymMon g5_t4[] = {  // Tamer
    {  28 /*Sandslash*/,34, {163, 28,154,  0 } },
    {  24 /*Arbok*/,    34, { 35, 40, 44, 43 } },
};
static const LordGymMon g5_t5[] = {  // Juggler
    {  96 /*Drowzee*/,  34, { 95, 93, 50,  0 } },
    {  97 /*Hypno*/,    34, { 95, 93, 94,  0 } },
};
static const LordGymMon g5_t6[] = {  // Koga
    { 109 /*Koffing*/,  37, {123,108,124,  0 } },
    {  89 /*Muk*/,      39, {124,106, 34,139 } },
    { 109 /*Koffing*/,  37, {123,139,108,  0 } },
    { 110 /*Weezing*/,  43, {124,123,139,108 } },
};

// 6. Saffron — Sabrina (Psychic). 8 trainers.
static const LordGymMon g6_t0[] = {  // Psychic
    {  79 /*Slowpoke*/, 33, { 93, 55, 50,  0 } },
    {  79 /*Slowpoke*/, 33, { 93, 55, 50,  0 } },
    {  80 /*Slowbro*/,  33, { 93, 55,110,133 } },
};
static const LordGymMon g6_t1[] = {  // Psychic
    { 122 /*Mr. Mime*/, 34, { 93,113,115, 50 } },
    {  64 /*Kadabra*/,  34, { 93, 50, 60,  0 } },
};
static const LordGymMon g6_t2[] = {  // Channeler
    {  93 /*Haunter*/,  38, {101, 95, 93,138 } },
};
static const LordGymMon g6_t3[] = {  // Psychic
    {  80 /*Slowbro*/,  38, { 93, 55,133, 50 } },
};
static const LordGymMon g6_t4[] = {  // Channeler
    {  92 /*Gastly*/,   34, {101, 95,122,  0 } },
    {  93 /*Haunter*/,  34, {101, 93, 95,  0 } },
};
static const LordGymMon g6_t5[] = {  // Channeler
    {  92 /*Gastly*/,   33, {101, 95,  0,  0 } },
    {  92 /*Gastly*/,   33, {101, 95,  0,  0 } },
    {  93 /*Haunter*/,  33, {101, 93, 95,  0 } },
};
static const LordGymMon g6_t6[] = {  // Psychic
    {  64 /*Kadabra*/,  31, { 93, 50,  0,  0 } },
    {  79 /*Slowpoke*/, 31, { 93, 55,  0,  0 } },
    { 122 /*Mr. Mime*/, 31, { 93,113,115,  0 } },
    {  64 /*Kadabra*/,  31, { 93, 50,  0,  0 } },
};
static const LordGymMon g6_t7[] = {  // Sabrina
    {  64 /*Kadabra*/,  38, { 60, 50,115,105 } },
    { 122 /*Mr. Mime*/, 37, { 93,113,115,  0 } },
    {  49 /*Venomoth*/, 38, { 60, 78, 48, 93 } },
    {  65 /*Alakazam*/, 43, { 94,105,115, 93 } },
};

// 7. Cinnabar — Blaine (Fire). 8 trainers.
static const LordGymMon g7_t0[] = {  // Burglar
    {  58 /*Growlithe*/,36, { 52, 43, 36, 46 } },
    {  37 /*Vulpix*/,   36, { 52, 98, 46,  0 } },
    {  38 /*Ninetales*/,36, { 52, 53, 98, 46 } },
};
static const LordGymMon g7_t1[] = {  // Super Nerd
    {  37 /*Vulpix*/,   36, { 52, 98, 39,  0 } },
    {  37 /*Vulpix*/,   36, { 52, 98, 39,  0 } },
    {  38 /*Ninetales*/,36, { 52, 53, 98,  0 } },
};
static const LordGymMon g7_t2[] = {  // Super Nerd
    {  77 /*Ponyta*/,   34, { 52, 23, 39,  0 } },
    {   4 /*Charmander*/,34,{ 10, 52, 43,  0 } },
    {  37 /*Vulpix*/,   34, { 52, 98,  0,  0 } },
    {  58 /*Growlithe*/,34, { 52, 43, 36,  0 } },
};
static const LordGymMon g7_t3[] = {  // Burglar
    {  77 /*Ponyta*/,   41, { 52, 23, 83, 36 } },
};
static const LordGymMon g7_t4[] = {  // Super Nerd
    {  78 /*Rapidash*/, 41, { 52, 23, 83, 36 } },
};
static const LordGymMon g7_t5[] = {  // Burglar
    {  37 /*Vulpix*/,   37, { 52, 98, 46,  0 } },
    {  58 /*Growlithe*/,37, { 52, 43, 36,  0 } },
};
static const LordGymMon g7_t6[] = {  // Super Nerd
    {  58 /*Growlithe*/,37, { 52, 43, 36,  0 } },
    {  37 /*Vulpix*/,   37, { 52, 98, 46,  0 } },
};
static const LordGymMon g7_t7[] = {  // Blaine
    {  58 /*Growlithe*/,42, { 52, 43, 36, 46 } },
    {  77 /*Ponyta*/,   40, { 52, 23, 83,  0 } },
    {  78 /*Rapidash*/, 42, { 52, 23, 83, 36 } },
    {  59 /*Arcanine*/, 47, { 52, 46, 36, 53 } },
};

// 8. Viridian — Giovanni (Ground). 9 trainers.
static const LordGymMon g8_t0[] = {  // Tamer
    {  24 /*Arbok*/,    39, { 35, 40, 44, 43 } },
    { 128 /*Tauros*/,   39, { 33, 23, 39, 36 } },
};
static const LordGymMon g8_t1[] = {  // Black Belt
    {  67 /*Machoke*/,  43, {  2, 67, 43, 69 } },
};
static const LordGymMon g8_t2[] = {  // Cooltrainer
    {  33 /*Nidorino*/, 39, { 30, 40, 43, 31 } },
    {  34 /*Nidoking*/, 39, { 30, 40, 89, 37 } },
};
static const LordGymMon g8_t3[] = {  // Tamer
    { 111 /*Rhyhorn*/,  43, { 30, 23, 39, 31 } },
};
static const LordGymMon g8_t4[] = {  // Black Belt
    {  66 /*Machop*/,   40, {  2, 67, 43,116 } },
    {  67 /*Machoke*/,  40, {  2, 67, 69,  0 } },
};
static const LordGymMon g8_t5[] = {  // Cooltrainer
    {  28 /*Sandslash*/,39, {163, 28,154,  0 } },
    {  51 /*Dugtrio*/,  39, { 91, 28,163, 89 } },
};
static const LordGymMon g8_t6[] = {  // Cooltrainer
    { 111 /*Rhyhorn*/,  43, { 30, 23, 39, 31 } },
};
static const LordGymMon g8_t7[] = {  // Black Belt
    {  67 /*Machoke*/,  38, {  2, 67,  0,  0 } },
    {  66 /*Machop*/,   38, {  2, 67, 43,  0 } },
    {  67 /*Machoke*/,  38, {  2, 67, 69,  0 } },
};
static const LordGymMon g8_t8[] = {  // Giovanni
    { 111 /*Rhyhorn*/,  45, { 30, 23, 31, 43 } },
    {  51 /*Dugtrio*/,  42, { 28, 91, 89,163 } },
    {  31 /*Nidoqueen*/,44, { 34, 89, 40, 39 } },
    {  34 /*Nidoking*/, 45, { 30, 40, 89, 37 } },
    { 112 /*Rhydon*/,   50, { 89, 23, 31, 88 } },
};

// ── Trainer tables per gym ───────────────────────────────────────────────────
#define MON_COUNT(arr) (uint8_t)(sizeof(arr) / sizeof((arr)[0]))

const LordGym LORD_GYMS[LORD_GYM_COUNT] = {
    { "Pewter",    "Brock",     "Boulder",  0, 10, 2,
        {{ "Jr.Trainer",  MON_COUNT(g1_t0), g1_t0 },
         { "Brock",       MON_COUNT(g1_t1), g1_t1 }}},

    { "Cerulean",  "Misty",     "Cascade",  1, 18, 3,
        {{ "Swimmer",     MON_COUNT(g2_t0), g2_t0 },
         { "Jr.Trainer",  MON_COUNT(g2_t1), g2_t1 },
         { "Misty",       MON_COUNT(g2_t2), g2_t2 }}},

    { "Vermilion", "Lt. Surge", "Thunder",  2, 22, 4,
        {{ "Sailor",      MON_COUNT(g3_t0), g3_t0 },
         { "Rocker",      MON_COUNT(g3_t1), g3_t1 },
         { "Gentleman",   MON_COUNT(g3_t2), g3_t2 },
         { "Lt. Surge",   MON_COUNT(g3_t3), g3_t3 }}},

    { "Celadon",   "Erika",     "Rainbow",  3, 26, 8,
        {{ "Lass",        MON_COUNT(g4_t0), g4_t0 },
         { "Beauty",      MON_COUNT(g4_t1), g4_t1 },
         { "Beauty",      MON_COUNT(g4_t2), g4_t2 },
         { "Jr.Trainer",  MON_COUNT(g4_t3), g4_t3 },
         { "Beauty",      MON_COUNT(g4_t4), g4_t4 },
         { "Cooltrainer", MON_COUNT(g4_t5), g4_t5 },
         { "Lass",        MON_COUNT(g4_t6), g4_t6 },
         { "Erika",       MON_COUNT(g4_t7), g4_t7 }}},

    { "Fuchsia",   "Koga",      "Soul",     4, 32, 7,
        {{ "Juggler",     MON_COUNT(g5_t0), g5_t0 },
         { "Juggler",     MON_COUNT(g5_t1), g5_t1 },
         { "Juggler",     MON_COUNT(g5_t2), g5_t2 },
         { "Tamer",       MON_COUNT(g5_t3), g5_t3 },
         { "Tamer",       MON_COUNT(g5_t4), g5_t4 },
         { "Juggler",     MON_COUNT(g5_t5), g5_t5 },
         { "Koga",        MON_COUNT(g5_t6), g5_t6 }}},

    { "Saffron",   "Sabrina",   "Marsh",    5, 36, 8,
        {{ "Psychic",     MON_COUNT(g6_t0), g6_t0 },
         { "Psychic",     MON_COUNT(g6_t1), g6_t1 },
         { "Channeler",   MON_COUNT(g6_t2), g6_t2 },
         { "Psychic",     MON_COUNT(g6_t3), g6_t3 },
         { "Channeler",   MON_COUNT(g6_t4), g6_t4 },
         { "Channeler",   MON_COUNT(g6_t5), g6_t5 },
         { "Psychic",     MON_COUNT(g6_t6), g6_t6 },
         { "Sabrina",     MON_COUNT(g6_t7), g6_t7 }}},

    { "Cinnabar",  "Blaine",    "Volcano",  6, 42, 8,
        {{ "Burglar",     MON_COUNT(g7_t0), g7_t0 },
         { "Super Nerd",  MON_COUNT(g7_t1), g7_t1 },
         { "Super Nerd",  MON_COUNT(g7_t2), g7_t2 },
         { "Burglar",     MON_COUNT(g7_t3), g7_t3 },
         { "Super Nerd",  MON_COUNT(g7_t4), g7_t4 },
         { "Burglar",     MON_COUNT(g7_t5), g7_t5 },
         { "Super Nerd",  MON_COUNT(g7_t6), g7_t6 },
         { "Blaine",      MON_COUNT(g7_t7), g7_t7 }}},

    { "Viridian",  "Giovanni",  "Earth",    7, 47, 9,
        {{ "Tamer",       MON_COUNT(g8_t0), g8_t0 },
         { "Black Belt",  MON_COUNT(g8_t1), g8_t1 },
         { "Cooltrainer", MON_COUNT(g8_t2), g8_t2 },
         { "Tamer",       MON_COUNT(g8_t3), g8_t3 },
         { "Black Belt",  MON_COUNT(g8_t4), g8_t4 },
         { "Cooltrainer", MON_COUNT(g8_t5), g8_t5 },
         { "Cooltrainer", MON_COUNT(g8_t6), g8_t6 },
         { "Black Belt",  MON_COUNT(g8_t7), g8_t7 },
         { "Giovanni",    MON_COUNT(g8_t8), g8_t8 }}},
};

#undef MON_COUNT

// ── Accessors ────────────────────────────────────────────────────────────────

const LordGym *lordGym(uint8_t i)
{
    if (i >= LORD_GYM_COUNT) return nullptr;
    return &LORD_GYMS[i];
}

// ── Gen1Party builder ────────────────────────────────────────────────────────
//
// Writes a Gen1Party (the 44-byte-per-mon save layout) directly from a
// LordGymTrainer roster.  Stats are computed via Gen1BattleEngine's base-stats
// helper, then serialised back into Gen1Pokemon.  Each trainer fields their
// real team — no filler padding — so the gauntlet matches the actual game.

static void writeMonToParty(Gen1Party &out, uint8_t slot,
                            const LordGymMon &src, const char *nick)
{
    uint8_t tier = lordCurrentNgPlusTier();
    uint8_t lvl  = lordScaleLevel(src.level, tier, /*isE4=*/false);

    uint8_t moves[4];
    memcpy(moves, src.moves, 4);
    lordApplyNgPlusMoves(src.species, tier, moves);

    Gen1BattleEngine::BattlePoke tmp;
    Gen1BattleEngine::initBattlePokeFromBase(tmp, src.species, lvl, moves);

    uint8_t internal = (src.species < 152) ? dexToInternal[src.species] : 0;

    Gen1Pokemon &p = out.mons[slot];
    memset(&p, 0, sizeof(p));
    p.species  = internal;
    p.boxLevel = lvl;
    p.level    = lvl;
    setBe16(p.maxHp, tmp.maxHp);
    setBe16(p.hp,    tmp.hp);
    setBe16(p.atk,   tmp.atk);
    setBe16(p.def,   tmp.def);
    setBe16(p.spd,   tmp.spd);
    setBe16(p.spc,   tmp.spc);
    p.type1    = tmp.type1;
    p.type2    = tmp.type2;
    // NG+ bosses are fully trained: max DVs + full stat-exp so a scaled Lvl-N
    // mon actually fights at Lvl-N (a DV-8 / zero-EV trainer mon plays ~10
    // levels weaker than the player's trained team).  Base game stays canonical.
    bool buff = (tier > 0);
    p.dvs[0]   = buff ? 0xFF : 0x88;
    p.dvs[1]   = buff ? 0xFF : 0x88;
    if (buff) {
        setBe16(p.hpExp,  0xFFFF);
        setBe16(p.atkExp, 0xFFFF);
        setBe16(p.defExp, 0xFFFF);
        setBe16(p.spdExp, 0xFFFF);
        setBe16(p.spcExp, 0xFFFF);
        setBe16(p.hp, 0xFFFF);   // engine clamps current HP to the recomputed full maxHp
    }
    memcpy(p.moves, moves, 4);
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *m = gen1Move(moves[i]);
        p.pp[i] = m ? m->pp : 0;
    }
    out.species[slot] = internal;
    snprintf((char *)out.nicknames[slot], 11, "%s", nick ? nick : "MON");
    // OT name field intentionally left zero.
}

bool lordBuildGymParty(uint8_t gymIdx, uint8_t trainerIdx, Gen1Party &out)
{
    const LordGym *g = lordGym(gymIdx);
    if (!g) return false;
    if (trainerIdx >= g->trainerCount) return false;

    const LordGymTrainer &tr = g->trainers[trainerIdx];
    if (tr.count == 0 || !tr.party) return false;

    memset(&out, 0, sizeof(out));
    uint8_t n = tr.count < 6 ? tr.count : 6;

    for (uint8_t i = 0; i < n; ++i)
        writeMonToParty(out, i, tr.party[i], "FOE");

    out.count = n;
    return true;
}
