// SPDX-License-Identifier: MIT
// See LordGyms.h.

#include "LordGyms.h"

#include "Gen1BattleEngine.h"
#include "PokemonData.h"
#include "showdown_gen1_moves.h"

#include <string.h>
#include <stdio.h>

// ── Flat pool of LordGymMon records, grouped per-trainer. ────────────────────
//
// National dex species numbers, Red/Blue move IDs. Levels scaled so gym N is
// beatable by a player coming off gym N-1 without grinding, but not a pushover.

// Pewter — Brock (Rock/Ground).
static const LordGymMon g1_t0[] = {  // Jr. Trainer
    { 74 /*Geodude*/, 10, { 33 /*Tackle*/, 111 /*Defense Curl*/, 0, 0 } },
};
static const LordGymMon g1_t1[] = {  // Lass
    { 16 /*Pidgey*/,   9, { 16 /*Gust*/, 98 /*Quick Attack*/, 0, 0 } },
    { 19 /*Rattata*/,  9, { 33 /*Tackle*/, 45 /*Growl*/, 0, 0 } },
};
static const LordGymMon g1_t2[] = {  // Camper
    { 27 /*Sandshrew*/, 11, { 10 /*Scratch*/, 28 /*Sand Attack*/, 0, 0 } },
};
static const LordGymMon g1_t3[] = {  // Hiker
    { 74 /*Geodude*/, 11, { 88 /*Rock Throw*/, 33 /*Tackle*/, 0, 0 } },
    { 95 /*Onix*/,    12, { 88 /*Rock Throw*/, 20 /*Bind*/, 0, 0 } },
};
static const LordGymMon g1_t4[] = {  // Brock — leader
    { 74 /*Geodude*/, 12, { 33 /*Tackle*/, 111 /*Defense Curl*/, 88 /*Rock Throw*/, 0 } },
    { 95 /*Onix*/,    14, { 33 /*Tackle*/, 20  /*Bind*/,         88 /*Rock Throw*/, 43 /*Leer*/ } },
};

// Cerulean — Misty (Water).
static const LordGymMon g2_t0[] = {  // Swimmer
    { 120 /*Staryu*/, 16, { 33 /*Tackle*/, 55 /*Water Gun*/, 0, 0 } },
};
static const LordGymMon g2_t1[] = {  // Jr. Trainer
    {  54 /*Psyduck*/, 19, { 10 /*Scratch*/, 39 /*Tail Whip*/, 55 /*Water Gun*/, 0 } },
};
static const LordGymMon g2_t2[] = {  // Lass
    { 118 /*Goldeen*/, 17, { 64 /*Peck*/, 39 /*Tail Whip*/, 0, 0 } },
    {  72 /*Tentacool*/,18, { 40 /*Poison Sting*/, 51 /*Acid*/, 0, 0 } },
};
static const LordGymMon g2_t3[] = {  // Swimmer
    {  86 /*Seel*/,    20, { 44 /*Bite*/, 45 /*Growl*/, 62 /*Aurora Beam*/, 0 } },
};
static const LordGymMon g2_t4[] = {  // Misty — leader
    { 120 /*Staryu*/,  18, { 33 /*Tackle*/, 55 /*Water Gun*/, 0, 0 } },
    { 121 /*Starmie*/, 21, { 33 /*Tackle*/, 55 /*Water Gun*/, 61 /*Bubble Beam*/, 50 /*Disable*/ } },
};

// Vermilion — Lt. Surge (Electric).
static const LordGymMon g3_t0[] = {
    {  25 /*Pikachu*/, 20, { 84 /*Thunder Shock*/, 28 /*Sand Attack*/, 98 /*Quick Attack*/, 0 } },
};
static const LordGymMon g3_t1[] = {
    { 100 /*Voltorb*/, 21, { 33 /*Tackle*/, 103 /*Screech*/, 84 /*Thunder Shock*/, 0 } },
};
static const LordGymMon g3_t2[] = {
    {  81 /*Magnemite*/, 20, { 33 /*Tackle*/, 48 /*Supersonic*/, 84 /*Thunder Shock*/, 0 } },
    {  82 /*Magneton*/,  22, { 33 /*Tackle*/, 84 /*Thunder Shock*/, 49 /*Sonic Boom*/, 0 } },
};
static const LordGymMon g3_t3[] = {
    { 100 /*Voltorb*/,  23, { 33 /*Tackle*/, 84 /*Thunder Shock*/, 103 /*Screech*/, 0 } },
    { 125 /*Electabuzz*/,23, { 98 /*Quick Attack*/, 85 /*Thunderbolt*/, 50 /*Disable*/, 0 } },
};
static const LordGymMon g3_t4[] = {  // Lt. Surge
    { 100 /*Voltorb*/, 21, { 84 /*Thunder Shock*/, 103 /*Screech*/, 153 /*Explosion*/, 0 } },
    {  26 /*Raichu*/,  24, { 85 /*Thunderbolt*/, 28 /*Sand Attack*/, 86 /*Thunder Wave*/, 98 /*Quick Attack*/ } },
    {  25 /*Pikachu*/, 21, { 98 /*Quick Attack*/, 84 /*Thunder Shock*/, 45 /*Growl*/, 0 } },
};

// Celadon — Erika (Grass).
static const LordGymMon g4_t0[] = {
    {  43 /*Oddish*/,   24, { 71 /*Absorb*/, 78 /*Stun Spore*/, 51 /*Acid*/, 0 } },
};
static const LordGymMon g4_t1[] = {
    {  69 /*Bellsprout*/, 24, { 22 /*Vine Whip*/, 74 /*Growth*/, 0, 0 } },
};
static const LordGymMon g4_t2[] = {
    { 102 /*Exeggcute*/, 26, { 79 /*Sleep Powder*/, 77 /*Poison Powder*/, 71 /*Absorb*/, 0 } },
    {  44 /*Gloom*/,     25, { 78 /*Stun Spore*/, 51 /*Acid*/, 0, 0 } },
};
static const LordGymMon g4_t3[] = {
    {  70 /*Weepinbell*/, 26, { 22 /*Vine Whip*/, 77 /*Poison Powder*/, 79 /*Sleep Powder*/, 0 } },
};
static const LordGymMon g4_t4[] = {  // Erika
    { 114 /*Tangela*/,   29, { 22 /*Vine Whip*/, 20 /*Bind*/, 75 /*Razor Leaf*/, 0 } },
    {  71 /*Victreebel*/,29, { 77 /*Poison Powder*/, 79 /*Sleep Powder*/, 22 /*Vine Whip*/, 72 /*Mega Drain*/ } },
    {  45 /*Vileplume*/, 29, { 78 /*Stun Spore*/, 79 /*Sleep Powder*/, 80 /*Petal Dance*/, 72 /*Mega Drain*/ } },
};

// Fuchsia — Koga (Poison).
static const LordGymMon g5_t0[] = {
    {  41 /*Zubat*/,    31, { 44 /*Bite*/, 103 /*Screech*/, 109 /*Smokescreen*/, 0 } },
    {  41 /*Zubat*/,    31, { 44 /*Bite*/, 48 /*Supersonic*/, 0, 0 } },
};
static const LordGymMon g5_t1[] = {
    {  48 /*Venonat*/,  32, { 48 /*Supersonic*/, 50 /*Disable*/, 93 /*Confusion*/, 0 } },
};
static const LordGymMon g5_t2[] = {
    {  88 /*Grimer*/,   33, { 106 /*Harden*/, 51 /*Acid*/, 124 /*Sludge*/, 0 } },
    {  89 /*Muk*/,      33, { 124 /*Sludge*/, 106 /*Harden*/, 34 /*Body Slam*/, 0 } },
};
static const LordGymMon g5_t3[] = {
    {  42 /*Golbat*/,   34, { 44 /*Bite*/, 48 /*Supersonic*/, 100 /*Leech Life*/, 0 } },
};
static const LordGymMon g5_t4[] = {  // Koga
    { 109 /*Koffing*/,  37, { 123 /*Smog*/, 108 /*Self-Destruct*/, 0, 0 } },
    {  89 /*Muk*/,      39, { 139 /*Toxic*/, 51 /*Acid*/, 34 /*Body Slam*/, 106 /*Harden*/ } },
    { 109 /*Koffing*/,  37, { 139 /*Toxic*/, 123 /*Smog*/, 108 /*Self-Destruct*/, 0 } },
    { 110 /*Weezing*/,  43, { 124 /*Sludge*/, 123 /*Smog*/, 139 /*Toxic*/, 108 /*Self-Destruct*/ } },
};

// Saffron — Sabrina (Psychic).
static const LordGymMon g6_t0[] = {
    {  96 /*Drowzee*/,  34, { 95 /*Hypnosis*/, 50 /*Disable*/, 93 /*Confusion*/, 0 } },
    {  97 /*Hypno*/,    34, { 95 /*Hypnosis*/, 50 /*Disable*/, 93 /*Confusion*/, 0 } },
};
static const LordGymMon g6_t1[] = {
    {  64 /*Kadabra*/,  38, { 93 /*Confusion*/, 50 /*Disable*/, 105 /*Recover*/, 0 } },
};
static const LordGymMon g6_t2[] = {
    { 122 /*Mr. Mime*/, 39, { 50 /*Disable*/, 93 /*Confusion*/, 113 /*Light Screen*/, 115 /*Reflect*/ } },
};
static const LordGymMon g6_t3[] = {
    {  96 /*Drowzee*/,  36, { 95 /*Hypnosis*/, 93 /*Confusion*/, 138 /*Dream Eater*/, 0 } },
    {  96 /*Drowzee*/,  36, { 95 /*Hypnosis*/, 93 /*Confusion*/, 138 /*Dream Eater*/, 0 } },
};
static const LordGymMon g6_t4[] = {  // Sabrina
    {  64 /*Kadabra*/,  38, { 93 /*Confusion*/, 50 /*Disable*/, 115 /*Reflect*/, 105 /*Recover*/ } },
    { 122 /*Mr. Mime*/, 37, { 93 /*Confusion*/, 113 /*Light Screen*/, 115 /*Reflect*/, 0 } },
    {  49 /*Venomoth*/, 38, { 48 /*Supersonic*/, 78 /*Stun Spore*/, 60 /*Psybeam*/, 0 } },
    {  65 /*Alakazam*/, 43, { 94 /*Psychic*/, 105 /*Recover*/, 115 /*Reflect*/, 93 /*Confusion*/ } },
};

// Cinnabar — Blaine (Fire).
static const LordGymMon g7_t0[] = {
    {  77 /*Ponyta*/,  34, { 52 /*Ember*/, 45 /*Growl*/, 39 /*Tail Whip*/, 0 } },
    { 126 /*Magmar*/,  38, { 108 /*Self-Destruct*/, 52 /*Ember*/, 98 /*Quick Attack*/, 0 } },
};
static const LordGymMon g7_t1[] = {
    {  58 /*Growlithe*/, 40, { 52 /*Ember*/, 43 /*Leer*/, 36 /*Take Down*/, 0 } },
};
static const LordGymMon g7_t2[] = {
    {  78 /*Rapidash*/, 41, { 52 /*Ember*/, 39 /*Tail Whip*/, 23 /*Stomp*/, 83 /*Fire Spin*/ } },
};
static const LordGymMon g7_t3[] = {
    {  37 /*Vulpix*/,   42, { 52 /*Ember*/, 45 /*Growl*/, 98 /*Quick Attack*/, 46 /*Roar*/ } },
    {  38 /*Ninetales*/,42, { 52 /*Ember*/, 46 /*Roar*/, 98 /*Quick Attack*/, 53 /*Flamethrower*/ } },
};
static const LordGymMon g7_t4[] = {  // Blaine
    {  58 /*Growlithe*/, 42, { 52 /*Ember*/, 43 /*Leer*/, 36 /*Take Down*/, 46 /*Roar*/ } },
    { 126 /*Magmar*/,    40, { 52 /*Ember*/, 83 /*Fire Spin*/, 109 /*Smokescreen*/, 34 /*Body Slam*/ } },
    {  78 /*Rapidash*/,  42, { 52 /*Ember*/, 23 /*Stomp*/, 83 /*Fire Spin*/, 36 /*Take Down*/ } },
    {  59 /*Arcanine*/,  47, { 52 /*Ember*/, 46 /*Roar*/, 36 /*Take Down*/, 53 /*Flamethrower*/ } },
};

// Viridian — Giovanni (Ground).
static const LordGymMon g8_t0[] = {
    {  24 /*Arbok*/,   37, { 40 /*Poison Sting*/, 43 /*Leer*/, 35 /*Wrap*/, 0 } },
};
static const LordGymMon g8_t1[] = {
    { 111 /*Rhyhorn*/, 42, { 88 /*Rock Throw*/, 43 /*Leer*/, 34 /*Body Slam*/, 0 } },
};
static const LordGymMon g8_t2[] = {
    {  50 /*Diglett*/, 41, { 10 /*Scratch*/, 28 /*Sand Attack*/, 91 /*Dig*/, 0 } },
    {  51 /*Dugtrio*/, 42, { 10 /*Scratch*/, 28 /*Sand Attack*/, 91 /*Dig*/, 89 /*Earthquake*/ } },
};
static const LordGymMon g8_t3[] = {
    {  34 /*Nidoking*/, 45, { 40 /*Poison Sting*/, 30 /*Horn Attack*/, 31 /*Fury Attack*/, 0 } },
};
static const LordGymMon g8_t4[] = {  // Giovanni
    { 111 /*Rhyhorn*/, 45, { 88 /*Rock Throw*/, 34 /*Body Slam*/, 31 /*Fury Attack*/, 43 /*Leer*/ } },
    {  51 /*Dugtrio*/, 42, { 28 /*Sand Attack*/, 91 /*Dig*/, 89 /*Earthquake*/, 163 /*Slash*/ } },
    {  31 /*Nidoqueen*/,44, { 40 /*Poison Sting*/, 89 /*Earthquake*/, 34 /*Body Slam*/, 43 /*Leer*/ } },
    { 112 /*Rhydon*/,  50, { 89 /*Earthquake*/, 23 /*Stomp*/, 43 /*Leer*/, 88 /*Rock Throw*/ } },
    {  34 /*Nidoking*/, 45, { 40 /*Poison Sting*/, 31 /*Fury Attack*/, 89 /*Earthquake*/, 30 /*Horn Attack*/ } },
};

// ── Trainer tables per gym ───────────────────────────────────────────────────

#define MON_COUNT(arr) (uint8_t)(sizeof(arr) / sizeof((arr)[0]))

const LordGym LORD_GYMS[LORD_GYM_COUNT] = {
    { "Pewter",    "Brock",      "Boulder",  0, 10,
        {{ "Camper Liam",    MON_COUNT(g1_t0), g1_t0 },
         { "Lass Crissy",    MON_COUNT(g1_t1), g1_t1 },
         { "Youngster Ben",  MON_COUNT(g1_t2), g1_t2 },
         { "Hiker Marcos",   MON_COUNT(g1_t3), g1_t3 },
         { "Brock",          MON_COUNT(g1_t4), g1_t4 }}},

    { "Cerulean",  "Misty",      "Cascade",  1, 18,
        {{ "Swimmer Luis",   MON_COUNT(g2_t0), g2_t0 },
         { "Trainer Diana",  MON_COUNT(g2_t1), g2_t1 },
         { "Lass Haley",     MON_COUNT(g2_t2), g2_t2 },
         { "Swimmer Parker", MON_COUNT(g2_t3), g2_t3 },
         { "Misty",          MON_COUNT(g2_t4), g2_t4 }}},

    { "Vermilion", "Lt. Surge",  "Thunder",  2, 22,
        {{ "Gentleman Tucker", MON_COUNT(g3_t0), g3_t0 },
         { "Rocker Luca",      MON_COUNT(g3_t1), g3_t1 },
         { "Sailor Dwayne",    MON_COUNT(g3_t2), g3_t2 },
         { "Engineer Baily",   MON_COUNT(g3_t3), g3_t3 },
         { "Lt. Surge",        MON_COUNT(g3_t4), g3_t4 }}},

    { "Celadon",   "Erika",      "Rainbow",  3, 26,
        {{ "Lass Michelle",   MON_COUNT(g4_t0), g4_t0 },
         { "Beauty Lola",     MON_COUNT(g4_t1), g4_t1 },
         { "Picnicker Tanya", MON_COUNT(g4_t2), g4_t2 },
         { "Trainer Miranda", MON_COUNT(g4_t3), g4_t3 },
         { "Erika",           MON_COUNT(g4_t4), g4_t4 }}},

    { "Fuchsia",   "Koga",       "Soul",     4, 32,
        {{ "Juggler Nate",    MON_COUNT(g5_t0), g5_t0 },
         { "Tamer Edgar",     MON_COUNT(g5_t1), g5_t1 },
         { "Juggler Kirk",    MON_COUNT(g5_t2), g5_t2 },
         { "Ninja Shin",      MON_COUNT(g5_t3), g5_t3 },
         { "Koga",            MON_COUNT(g5_t4), g5_t4 }}},

    { "Saffron",   "Sabrina",    "Marsh",    5, 36,
        {{ "Medium Martha",   MON_COUNT(g6_t0), g6_t0 },
         { "Channeler Abigail",MON_COUNT(g6_t1), g6_t1 },
         { "Medium Grace",    MON_COUNT(g6_t2), g6_t2 },
         { "Psychic Rodney",  MON_COUNT(g6_t3), g6_t3 },
         { "Sabrina",         MON_COUNT(g6_t4), g6_t4 }}},

    { "Cinnabar",  "Blaine",     "Volcano",  6, 42,
        {{ "Burglar Derek",   MON_COUNT(g7_t0), g7_t0 },
         { "Super Nerd Sam",  MON_COUNT(g7_t1), g7_t1 },
         { "Burglar Ramon",   MON_COUNT(g7_t2), g7_t2 },
         { "Super Nerd Zac",  MON_COUNT(g7_t3), g7_t3 },
         { "Blaine",          MON_COUNT(g7_t4), g7_t4 }}},

    { "Viridian",  "Giovanni",   "Earth",    7, 47,
        {{ "Tough Guy Nick",  MON_COUNT(g8_t0), g8_t0 },
         { "Cooltrainer Leo", MON_COUNT(g8_t1), g8_t1 },
         { "Tough Guy Shane", MON_COUNT(g8_t2), g8_t2 },
         { "Cooltrainer Gwen",MON_COUNT(g8_t3), g8_t3 },
         { "Giovanni",        MON_COUNT(g8_t4), g8_t4 }}},
};

#undef MON_COUNT

// ── Per-gym filler pools ─────────────────────────────────────────────────────
// Used to pad trainers' parties to 6. Level is overridden at build time.

struct GymFiller { uint8_t species; uint8_t moves[4]; };

static const GymFiller GYM_FILL[8][6] = {
    // 0: Pewter — Rock/Ground
    {{ 74, {33,111,88, 0}},   // Geodude
     { 27, {10, 28,  0, 0}},  // Sandshrew
     { 95, {33, 20, 88,43}},  // Onix
     {111, {88, 43, 34, 0}},  // Rhyhorn
     { 66, {10,106,  0, 0}},  // Machop
     { 74, {88,111, 33, 0}}}, // Geodude (second)

    // 1: Cerulean — Water
    {{120, {33, 55,  0, 0}},  // Staryu
     {118, {64, 39,  0, 0}},  // Goldeen
     { 54, {10, 39, 55, 0}},  // Psyduck
     { 72, {40, 51,  0, 0}},  // Tentacool
     { 86, {44, 45, 62, 0}},  // Seel
     { 90, {33, 55,  0, 0}}}, // Shellder

    // 2: Vermilion — Electric
    {{ 25, {84, 28, 98, 0}},  // Pikachu
     {100, {33,103, 84, 0}},  // Voltorb
     { 81, {33, 48, 84, 0}},  // Magnemite
     { 82, {33, 84, 49, 0}},  // Magneton
     {125, {98, 85, 50, 0}},  // Electabuzz
     {100, {84,103,153, 0}}}, // Voltorb (Explosion variant)

    // 3: Celadon — Grass
    {{ 43, {71, 78, 51, 0}},  // Oddish
     { 69, {22, 74,  0, 0}},  // Bellsprout
     {102, {79, 77, 71, 0}},  // Exeggcute
     { 44, {78, 51,  0, 0}},  // Gloom
     { 46, {78, 77, 71, 0}},  // Paras
     {114, {22, 20, 75, 0}}}, // Tangela

    // 4: Fuchsia — Poison
    {{ 41, {44,103,109, 0}},  // Zubat
     { 48, {48, 50, 93, 0}},  // Venonat
     { 88, {106,51,124, 0}},  // Grimer
     { 89, {124,106,34, 0}},  // Muk
     { 23, {40, 43, 35, 0}},  // Ekans
     {109, {123,139,  0, 0}}},// Koffing

    // 5: Saffron — Psychic
    {{ 96, {95, 50, 93, 0}},  // Drowzee
     { 97, {95, 50, 93,138}}, // Hypno
     { 64, {93, 50,115,105}}, // Kadabra
     {122, {50, 93,113,115}}, // Mr. Mime
     { 79, {93, 95, 55, 0}},  // Slowpoke
     {124, {93, 95,  0, 0}}}, // Jynx

    // 6: Cinnabar — Fire
    {{ 77, {52, 45, 39, 0}},  // Ponyta
     { 58, {52, 43, 36, 0}},  // Growlithe
     {126, {52, 83,109, 0}},  // Magmar
     { 37, {52, 45, 98,46}},  // Vulpix
     { 78, {52, 39, 23,83}},  // Rapidash
     { 59, {52, 46, 36,53}}}, // Arcanine

    // 7: Viridian — Ground
    {{ 50, {10, 28, 91, 0}},  // Diglett
     { 51, {10, 28, 91,89}},  // Dugtrio
     {111, {88, 43, 34, 0}},  // Rhyhorn
     { 27, {10, 28,  0, 0}},  // Sandshrew
     { 31, {40, 89, 34,43}},  // Nidoqueen
     { 34, {40, 31, 89,30}}}, // Nidoking
};

// ── Accessors ────────────────────────────────────────────────────────────────

const LordGym *lordGym(uint8_t i)
{
    if (i >= LORD_GYM_COUNT) return nullptr;
    return &LORD_GYMS[i];
}

// ── Gen1Party builder ────────────────────────────────────────────────────────
//
// Writes a Gen1Party (the 44-byte-per-mon save layout) directly from a
// LordGymTrainer roster. Stats are computed via Gen1BattleEngine's base-stats
// helper (average DVs, zero stat-exp), then serialised back into Gen1Pokemon.
// Every trainer's party is padded to 6 with type-appropriate fillers so the
// engine's autoReplaceIfFainted() always has reserves to send out.

static void writeMonToParty(Gen1Party &out, uint8_t slot,
                            const LordGymMon &src, const char *nick)
{
    Gen1BattleEngine::BattlePoke tmp;
    Gen1BattleEngine::initBattlePokeFromBase(tmp, src.species, src.level, src.moves);

    Gen1Pokemon &p = out.mons[slot];
    memset(&p, 0, sizeof(p));
    p.species  = src.species;
    p.boxLevel = src.level;
    p.level    = src.level;
    setBe16(p.maxHp, tmp.maxHp);
    setBe16(p.hp,    tmp.hp);
    setBe16(p.atk,   tmp.atk);
    setBe16(p.def,   tmp.def);
    setBe16(p.spd,   tmp.spd);
    setBe16(p.spc,   tmp.spc);
    p.type1    = tmp.type1;
    p.type2    = tmp.type2;
    p.dvs[0]   = 0x88;
    p.dvs[1]   = 0x88;
    memcpy(p.moves, src.moves, 4);
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *m = gen1Move(src.moves[i]);
        p.pp[i] = m ? m->pp : 0;
    }
    out.species[slot] = src.species;
    snprintf((char *)out.nicknames[slot], 11, "%s", nick ? nick : "MON");
    // OT name field intentionally left zero.
}

bool lordBuildGymParty(uint8_t gymIdx, uint8_t trainerIdx, Gen1Party &out)
{
    const LordGym *g = lordGym(gymIdx);
    if (!g) return false;
    if (trainerIdx >= LORD_GYM_TRAINERS) return false;

    const LordGymTrainer &tr = g->trainers[trainerIdx];
    if (tr.count == 0 || !tr.party) return false;

    memset(&out, 0, sizeof(out));
    uint8_t n = tr.count < 6 ? tr.count : 6;

    // Compute average level from the defined roster for filler scaling.
    uint8_t avgLevel = 0;
    for (uint8_t i = 0; i < n; ++i) avgLevel += tr.party[i].level;
    avgLevel /= n;

    // Write the trainer's defined Pokemon.
    for (uint8_t i = 0; i < n; ++i)
        writeMonToParty(out, i, tr.party[i], "FOE");

    // Pad remaining slots to 6 with type-appropriate fillers.
    const GymFiller *fill = GYM_FILL[gymIdx];
    for (uint8_t i = n; i < 6; ++i) {
        const GymFiller &f = fill[i % 6];
        LordGymMon filler;
        filler.species = f.species;
        filler.level   = avgLevel;
        memcpy(filler.moves, f.moves, 4);
        writeMonToParty(out, i, filler, "FOE");
    }
    out.count = 6;
    return true;
}
