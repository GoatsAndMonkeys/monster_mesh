// SPDX-License-Identifier: MIT
// See LordE4.h. Rosters mirror Pokemon Red/Blue's canonical Elite Four + the
// post-Indigo-Plateau Champion fight (rival's third-evolution Charizard team).

#include "LordE4.h"

#include "Gen1BattleEngine.h"
#include "DaycareSavPatcher.h"      // dexToInternal[]
#include "showdown_gen1_moves.h"

#include <string.h>

// Move IDs (curated from showdown_gen1_moves.h, mirroring LordGyms taste):
//   33 Tackle, 36 Take Down, 23 Stomp, 44 Bite, 53 Flamethrower, 62 Aurora Beam,
//   58 Ice Beam, 65 Drill Peck, 56 Hydro Pump, 71 Absorb, 76 Solar Beam,
//   55 Water Gun, 41 Twineedle, 87 Thunderbolt, 84 Thunder Shock, 91 Dig,
//   59 Blizzard, 70 Strength, 8 Body Slam, 89 Earthquake, 50 Disable,
//   122 Lick, 95 Hypnosis, 93 Confusion, 94 Psychic, 60 Psybeam, 7 Fire Punch,
//   5 Mega Punch, 12 Guillotine, 82 Dragon Rage, 99 Hyper Beam, 25 Mega Kick,
//   88 Rock Throw, 90 Fissure, 67 Low Kick, 24 Double Kick, 134 Slash,
//   100 Teleport, 86 Thunder Wave, 17 Wing Attack, 19 Fly, 47 Sing, 31 Fury Attack.

// ── Lorelei (Ice) ────────────────────────────────────────────────────────────
static const LordGymMon kLorelei[] = {
    {  87 /*Dewgong*/,    54, { 33, 16,  62,  58 } }, // Tackle, Gust(?), Aurora Beam, Ice Beam
    {  91 /*Cloyster*/,   53, { 33, 88,  59, 110 } }, // Tackle, Rock Throw(?), Blizzard, Withdraw — keep simple
    {  80 /*Slowbro*/,    54, { 33,  8,  93,  10 } }, // Tackle, Body Slam, Confusion, Scratch
    { 124 /*Jynx*/,       56, { 47, 95,  93,  58 } }, // Sing, Hypnosis, Confusion, Ice Beam
    { 131 /*Lapras*/,     56, { 44,  8,  56,  58 } }, // Bite, Body Slam, Hydro Pump, Ice Beam
};

// ── Bruno (Fighting) ─────────────────────────────────────────────────────────
static const LordGymMon kBruno[] = {
    {  95 /*Onix*/,       53, { 88, 36,  20,  90 } }, // Rock Throw, Take Down, Bind, Fissure
    { 107 /*Hitmonchan*/, 55, {  4,  7,   2, 152 } }, // Comet Punch(?), Fire Punch, Karate Chop, Mega Punch
    { 106 /*Hitmonlee*/,  55, { 26, 25, 136,  67 } }, // Jump Kick(?), Mega Kick, Hi Jump Kick(?), Low Kick
    {  95 /*Onix*/,       56, { 88, 36,  20,  91 } }, // Rock Throw, Take Down, Bind, Dig
    {  68 /*Machamp*/,    58, { 36,  8,  43,  70 } }, // Take Down, Body Slam, Leer, Strength
};

// ── Agatha (Ghost / Poison) ──────────────────────────────────────────────────
static const LordGymMon kAgatha[] = {
    {  94 /*Gengar*/,     56, { 95, 50, 122, 153 } }, // Hypnosis, Disable, Lick, Night Shade(?)
    {  42 /*Golbat*/,     56, { 44, 95, 100, 109 } }, // Bite, Hypnosis, Teleport, Confuse Ray(?)
    {  93 /*Haunter*/,    55, { 95, 50, 122,  93 } }, // Hypnosis, Disable, Lick, Confusion
    {  24 /*Arbok*/,      58, { 40, 51, 144,  44 } }, // Poison Sting, Acid, Glare(?), Bite
    {  94 /*Gengar*/,     60, { 95, 94, 122,  50 } }, // Hypnosis, Psychic, Lick, Disable
};

// ── Lance (Dragon) ───────────────────────────────────────────────────────────
static const LordGymMon kLance[] = {
    { 130 /*Gyarados*/,   58, { 44,  8,  82,  58 } }, // Bite, Body Slam, Dragon Rage, Ice Beam
    { 148 /*Dragonair*/,  56, { 44, 86,  82,  86 } }, // Bite, Thunder Wave, Dragon Rage, Thunder Wave
    { 148 /*Dragonair*/,  56, {  8, 36,  82,  58 } }, // Body Slam, Take Down, Dragon Rage, Ice Beam
    { 142 /*Aerodactyl*/, 60, { 17,  8, 134, 102 } }, // Wing Attack, Body Slam, Slash, Hyper Beam(?)
    { 149 /*Dragonite*/,  62, {  8,  7,  82,  31 } }, // Body Slam, Fire Punch, Dragon Rage, Fury Attack(?)
};

// ── Champion (rival, Charizard line) ─────────────────────────────────────────
// Red/Blue varies the Champion's roster by which starter the player picked.
// We pin it to Charizard since that's the LoC namesake.
static const LordGymMon kChampion[] = {
    {  18 /*Pidgeot*/,    61, { 17, 16,  19,  31 } }, // Wing Attack, Gust, Fly, Quick Attack(?)
    {  65 /*Alakazam*/,   59, {  6, 100, 60,  94 } }, // Pay Day(?), Teleport, Psybeam, Psychic
    { 112 /*Rhydon*/,     61, { 91, 88,  36, 102 } }, // Dig, Rock Throw, Take Down, Hyper Beam(?)
    {  59 /*Arcanine*/,   61, { 33, 39,  53,  36 } }, // Tackle, Tail Whip, Flamethrower, Take Down
    { 103 /*Exeggutor*/,  63, { 71, 76, 153,  47 } }, // Absorb, Solar Beam, Night Shade(?), Sing(?)
    {   6 /*Charizard*/,  65, { 35, 56,  53, 102 } }, // Wrap(?), Hydro Pump(?), Flamethrower, Hyper Beam(?)
};

static const LordE4Member kE4[LORD_E4_COUNT] = {
    { "Lorelei",  "Elite Four", "Ice",      kLorelei,  sizeof(kLorelei)  / sizeof(kLorelei[0])  },
    { "Bruno",    "Elite Four", "Fighting", kBruno,    sizeof(kBruno)    / sizeof(kBruno[0])    },
    { "Agatha",   "Elite Four", "Ghost",    kAgatha,   sizeof(kAgatha)   / sizeof(kAgatha[0])   },
    { "Lance",    "Elite Four", "Dragon",   kLance,    sizeof(kLance)    / sizeof(kLance[0])    },
    { "Blue",     "Champion",   "Mixed",    kChampion, sizeof(kChampion) / sizeof(kChampion[0]) },
};

const LordE4Member *lordE4Member(uint8_t idx)
{
    return idx < LORD_E4_COUNT ? &kE4[idx] : nullptr;
}

// Internal helper, mirrors LordGyms::writeMonToParty: dex → engine base-stats,
// then store internal hex code in the Gen1Party so the engine's SAV-aware
// loader resolves the correct base-stats column.
static void writeMonToParty(Gen1Party &out, uint8_t slot,
                            const LordGymMon &src, const char *nick)
{
    Gen1BattleEngine::BattlePoke tmp;
    Gen1BattleEngine::initBattlePokeFromBase(tmp, src.species, src.level, src.moves);

    uint8_t internal = (src.species < 152) ? dexToInternal[src.species] : 0;

    Gen1Pokemon &p = out.mons[slot];
    memset(&p, 0, sizeof(p));
    p.species  = internal;
    p.boxLevel = src.level;
    p.level    = src.level;
    auto setBe16 = [](uint8_t *dst, uint16_t v) {
        dst[0] = (uint8_t)(v >> 8); dst[1] = (uint8_t)v;
    };
    setBe16(p.maxHp, tmp.maxHp);
    setBe16(p.hp,    tmp.hp);
    setBe16(p.atk,   tmp.atk);
    setBe16(p.def,   tmp.def);
    setBe16(p.spd,   tmp.spd);
    setBe16(p.spc,   tmp.spc);
    p.type1   = tmp.type1;
    p.type2   = tmp.type2;
    p.dvs[0]  = 0x88;
    p.dvs[1]  = 0x88;
    memcpy(p.moves, src.moves, 4);
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *m = gen1Move(src.moves[i]);
        p.pp[i] = m ? m->pp : 0;
    }
    out.species[slot] = internal;
    snprintf((char *)out.nicknames[slot], 11, "%s", nick ? nick : "MON");
}

bool lordBuildE4Party(uint8_t idx, Gen1Party &out)
{
    const LordE4Member *m = lordE4Member(idx);
    if (!m || !m->roster || m->roster_count == 0) return false;
    memset(&out, 0, sizeof(out));
    uint8_t n = m->roster_count > 6 ? 6 : m->roster_count;
    out.count = n;
    for (uint8_t i = 0; i < n; ++i) {
        writeMonToParty(out, i, m->roster[i], m->name);
    }
    return true;
}
