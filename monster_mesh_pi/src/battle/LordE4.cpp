// SPDX-License-Identifier: MIT
// See ../shared/LordE4.h. Rosters mirror Pokemon Red/Blue's canonical Elite
// Four + the post-Indigo-Plateau Champion (rival's Charizard team).
//
// Ported verbatim from the T-Deck MonsterMesh firmware (LordE4.cpp).

#include "../shared/LordE4.h"

#include "Gen1BattleEngine.h"
#include "../shared/DaycareSavPatcher.h"      // dexToInternal[]
#include "../shared/LordLogic.h"              // lordScaleLevel + tier accessor
#include "showdown_gen1_moves.h"

#include <string.h>
#include <stdio.h>

// ── Lorelei (Ice) ────────────────────────────────────────────────────────────
static const LordGymMon kLorelei[] = {
    {  87 /*Dewgong*/,    54, { 33, 16,  62,  58 } },
    {  91 /*Cloyster*/,   53, { 33, 88,  59, 110 } },
    {  80 /*Slowbro*/,    54, { 33,  8,  93,  10 } },
    { 124 /*Jynx*/,       56, { 47, 95,  93,  58 } },
    { 131 /*Lapras*/,     56, { 44,  8,  56,  58 } },
};

// ── Bruno (Fighting) ─────────────────────────────────────────────────────────
static const LordGymMon kBruno[] = {
    {  95 /*Onix*/,       53, { 88, 36,  20,  90 } },
    { 107 /*Hitmonchan*/, 55, {  4,  7,   2, 152 } },
    { 106 /*Hitmonlee*/,  55, { 26, 25, 136,  67 } },
    {  95 /*Onix*/,       56, { 88, 36,  20,  91 } },
    {  68 /*Machamp*/,    58, { 36,  8,  43,  70 } },
};

// ── Agatha (Ghost / Poison) ──────────────────────────────────────────────────
static const LordGymMon kAgatha[] = {
    {  94 /*Gengar*/,     56, { 95, 50, 122, 153 } },
    {  42 /*Golbat*/,     56, { 44, 95, 100, 109 } },
    {  93 /*Haunter*/,    55, { 95, 50, 122,  93 } },
    {  24 /*Arbok*/,      58, { 40, 51, 144,  44 } },
    {  94 /*Gengar*/,     60, { 95, 94, 122,  50 } },
};

// ── Lance (Dragon) ───────────────────────────────────────────────────────────
static const LordGymMon kLance[] = {
    { 130 /*Gyarados*/,   58, { 44,  8,  82,  58 } },
    { 148 /*Dragonair*/,  56, { 44, 86,  82,  86 } },
    { 148 /*Dragonair*/,  56, {  8, 36,  82,  58 } },
    { 142 /*Aerodactyl*/, 60, { 17,  8, 134, 102 } },
    { 149 /*Dragonite*/,  62, {  8,  7,  82,  31 } },
};

// ── Champion (rival, Charizard line) ─────────────────────────────────────────
static const LordGymMon kChampion[] = {
    {  18 /*Pidgeot*/,    61, { 17, 16,  19,  31 } },
    {  65 /*Alakazam*/,   59, {  6, 100, 60,  94 } },
    { 112 /*Rhydon*/,     61, { 91, 88,  36, 102 } },
    {  59 /*Arcanine*/,   61, { 33, 39,  53,  36 } },
    { 103 /*Exeggutor*/,  63, { 71, 76, 153,  47 } },
    {   6 /*Charizard*/,  65, { 35, 56,  53, 102 } },
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

// Mirrors LordGyms::writeMonToParty: dex → engine base-stats, then store the
// internal hex code in the Gen1Party so the engine's SAV-aware loader resolves
// the right base-stats column.  Applies NG+ level scaling + coverage moves.
static void writeMonToParty(Gen1Party &out, uint8_t slot,
                            const LordGymMon &src, const char *nick)
{
    uint8_t tier = lordCurrentNgPlusTier();
    uint8_t lvl  = lordScaleLevel(src.level, tier, /*isE4=*/true);

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
    // NG+ Elite Four / Champion are fully trained: max DVs + full stat-exp so
    // a scaled Lvl-N mon fights at Lvl-N.  Base game stays canonical.
    bool buff = (tier > 0);
    p.dvs[0]  = buff ? 0xFF : 0x88;
    p.dvs[1]  = buff ? 0xFF : 0x88;
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
    out.species[n] = 0xFF;
    return true;
}
