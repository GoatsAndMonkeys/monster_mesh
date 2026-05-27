// SPDX-License-Identifier: MIT
// See GauntletBattle.h.
//
// As of Phase C-2 (send-party-once + run-locally-on-T-Deck), the gym side
// no longer runs the Gen1BattleEngine. This file is reduced to:
//   - party CSV parsing  (`gauntletParseParty`)
//   - party display      (`gauntletFormatParty`)
//   - E4 party builder   (`gauntletBuildE4Party`)
// All battle simulation moved to the T-Deck. We use a tiny stat helper
// (Gen1MinimalStats.h) that doesn't pull in Gen1BattleEngine.cpp.

#include "GauntletBattle.h"
#include "GauntletGyms.h"
#include "Gen1MinimalStats.h"
#include "../Gen1Species.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>

// ── Default movesets by Gen 1 type ────────────────────────────────────────────
// 4 move IDs per primary type (showdown move-table indices). The gym tags
// these onto each gym/E4 mon so the T-Deck (which has the move table) can
// run a real battle. PP defaults are baked in at GAUNTLET_DEFAULT_PP since
// the gym intentionally does NOT pull in showdown_gen1_moves.h.

struct DefaultMoveset { uint8_t moves[4]; };
static constexpr uint8_t GAUNTLET_DEFAULT_PP = 25;

static const DefaultMoveset DEFAULT_MOVES[16] = {
    /*  0 Normal  */ { { 33,  34,  98,  63 } }, // Tackle, Body Slam, Quick Attack, Hyper Beam
    /*  1 Fight   */ { {  2,  67,  66,  69 } }, // Karate Chop, Low Kick, Submission, Seismic Toss
    /*  2 Flying  */ { { 17,  64,  65,  19 } }, // Wing Attack, Peck, Drill Peck, Fly
    /*  3 Poison  */ { { 40, 124, 123,  51 } }, // Poison Sting, Sludge, Smog, Acid
    /*  4 Ground  */ { { 89,  91,  28,  31 } }, // Earthquake, Dig, Sand Attack, Fury Attack
    /*  5 Rock    */ { { 88, 157,  89,  33 } }, // Rock Throw, Rock Slide, Earthquake, Tackle
    /*  6 Bird    */ { { 17,  64,  65,  19 } }, // (treated as Flying)
    /*  7 Bug     */ { { 42,  41, 141,  33 } }, // Pin Missile, Twineedle, Leech Life, Tackle
    /*  8 Ghost   */ { { 122, 109, 101,  95 } },// Lick, Confuse Ray, Night Shade, Hypnosis
    /*  9 Fire    */ { { 52,  53, 126,   7 } }, // Ember, Flamethrower, Fire Blast, Fire Punch
    /* 10 Water   */ { { 55,  57,  56, 145 } }, // Water Gun, Surf, Hydro Pump, Bubble
    /* 11 Grass   */ { { 22,  75,  76,  79 } }, // Vine Whip, Razor Leaf, Solar Beam, Sleep Powder
    /* 12 Elec    */ { { 84,  85,  87,  86 } }, // Thunder Shock, Thunderbolt, Thunder, Thunder Wave
    /* 13 Psychic */ { { 93,  94,  95,  60 } }, // Confusion, Psychic, Hypnosis, Psybeam
    /* 14 Ice     */ { { 58,  59,  62,   8 } }, // Ice Beam, Blizzard, Aurora Beam, Ice Punch
    /* 15 Dragon  */ { { 82,  63,  21,  33 } }, // Dragon Rage, Hyper Beam, Slam, Tackle
};

// ── E4 trainer data ──────────────────────────────────────────────────────────
struct E4Spec {
    const char *name;
    struct { uint8_t dex; uint8_t level; } party[6];
};

static const E4Spec E4_TABLE[4] = {
    { "Lorelei", { {87,52},{91,53},{80,54},{124,56},{91,54},{131,56} } },
    { "Bruno",   { {95,53},{107,55},{106,55},{95,56},{68,58},{0,0} } },
    { "Agatha",  { {94,56},{42,56},{93,55},{24,56},{94,60},{0,0} } },
    { "Lance",   { {130,58},{148,56},{142,60},{149,56},{149,60},{6,60} } },
};

// ── writeMonByDex ────────────────────────────────────────────────────────────
// Build a single Gen1Pokemon (44-byte SAV format) from (dex, level, moves[4]).
// Stats computed via Gen1MinimalStats — no Gen1BattleEngine dependency.

static void writeMonByDex(Gen1Party &out, uint8_t slot,
                           uint8_t dex, uint8_t level,
                           const uint8_t moves[4], const char *nick)
{
    if (dex == 0 || dex > 151 || level == 0) return;

    Gen1MinimalStats s = gen1MinimalStats(dex, level);
    uint8_t internal   = gen1DexToInternal(dex);

    Gen1Pokemon &p = out.mons[slot];
    memset(&p, 0, sizeof(p));
    p.species  = internal;
    p.boxLevel = level;
    p.level    = level;
    setBe16(p.maxHp, s.hp);
    setBe16(p.hp,    s.hp);  // start full
    setBe16(p.atk,   s.atk);
    setBe16(p.def,   s.def);
    setBe16(p.spd,   s.spd);
    setBe16(p.spc,   s.spc);
    p.type1 = s.type1;
    p.type2 = s.type2;
    p.dvs[0] = 0x88;          // matches IV=8 used in stat calc
    p.dvs[1] = 0x88;
    memcpy(p.moves, moves, 4);
    for (int i = 0; i < 4; ++i) p.pp[i] = (moves[i] != 0) ? GAUNTLET_DEFAULT_PP : 0;
    out.species[slot] = internal;
    snprintf((char *)out.nicknames[slot], 11, "%s", nick ? nick : "MON");
}

// Pick a default 4-move set for a Pokemon based on its primary type.
static void pickDefaultMoves(uint8_t dex, uint8_t out[4])
{
    if (dex == 0 || dex > 151) {
        memcpy(out, DEFAULT_MOVES[0].moves, 4);
        return;
    }
    uint8_t type1 = GEN1_BASE_STATS[dex].type1;
    if (type1 >= 16) type1 = 0;
    memcpy(out, DEFAULT_MOVES[type1].moves, 4);
}

// Case-insensitive prefix match against gen1SpeciesName(internal).
// Returns the pokedex number (1..151), or 0 if no match.
static uint8_t lookupDexByName(const char *name)
{
    if (!name || !*name) return 0;
    char want[12];
    uint8_t i = 0;
    while (name[i] && i < 11) { want[i] = (char)toupper(name[i]); i++; }
    want[i] = '\0';
    if (i == 0) return 0;

    for (uint8_t dex = 1; dex <= 151; dex++) {
        uint8_t internal = gen1DexToInternal(dex);
        const char *nm = gen1SpeciesName(internal);
        if (!nm || !*nm) continue;
        if (strncmp(nm, want, i) == 0) return dex;
    }
    return 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

uint8_t gauntletParseParty(const char *csv, Gen1Party &out, const char *otName)
{
    memset(&out, 0, sizeof(out));
    if (!csv || !*csv) return 0;

    char buf[140];
    strncpy(buf, csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    uint8_t added = 0;
    char *save  = nullptr;
    char *token = strtok_r(buf, ",;/", &save);
    while (token && added < 6) {
        while (*token == ' ' || *token == '\t') token++;
        size_t len = strlen(token);
        while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\r' ||
                            token[len - 1] == '\n' || token[len - 1] == '\t')) {
            token[--len] = '\0';
        }
        if (!*token) { token = strtok_r(nullptr, ",;/", &save); continue; }

        uint8_t level = 50;
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            int lv = atoi(colon + 1);
            if (lv < 1)   lv = 1;
            if (lv > 100) lv = 100;
            level = (uint8_t)lv;
        }

        uint8_t dex = 0;
        bool allDigit = true;
        for (char *p = token; *p; p++) if (!isdigit((unsigned char)*p)) { allDigit = false; break; }
        if (allDigit && *token) {
            int n = atoi(token);
            if (n >= 1 && n <= 151) dex = (uint8_t)n;
        }
        if (!dex) dex = lookupDexByName(token);

        if (dex) {
            uint8_t moves[4];
            pickDefaultMoves(dex, moves);
            const char *nick = gen1SpeciesName(gen1DexToInternal(dex));
            writeMonByDex(out, added, dex, level, moves, nick);
            added++;
        }
        token = strtok_r(nullptr, ",;/", &save);
    }

    out.count = added;
    if (otName) {
        for (uint8_t s = 0; s < added; s++) {
            snprintf((char *)out.otNames[s], 11, "%s", otName);
        }
    }
    return added;
}

void gauntletFormatParty(const Gen1Party &party, char *out, uint8_t maxLen)
{
    if (!out || maxLen == 0) return;
    out[0] = '\0';
    uint8_t pos = 0;
    for (uint8_t i = 0; i < party.count && pos + 2 < maxLen; i++) {
        const char *nm = gen1SpeciesName(party.species[i]);
        if (!nm || !*nm) continue;
        if (pos > 0 && pos + 1 < maxLen) { out[pos++] = ','; out[pos] = '\0'; }
        uint8_t rem = (uint8_t)(maxLen - pos - 1);
        strncat(out + pos, nm, rem);
        pos = (uint8_t)strlen(out);
    }
}

const char *gauntletBuildE4Party(uint8_t slot, Gen1Party &out)
{
    memset(&out, 0, sizeof(out));
    if (slot >= 4) return "?";

    const E4Spec &e = E4_TABLE[slot];
    uint8_t added = 0;
    for (uint8_t i = 0; i < 6; i++) {
        if (e.party[i].dex == 0) continue;
        uint8_t moves[4];
        pickDefaultMoves(e.party[i].dex, moves);
        writeMonByDex(out, added, e.party[i].dex, e.party[i].level, moves, "FOE");
        added++;
    }
    out.count = added;
    return e.name;
}

const char *gauntletBuildPresetTrainerParty(uint8_t gymIdx, uint8_t trainerIdx,
                                              Gen1Party &out,
                                              uint8_t overrideBossLvl)
{
    memset(&out, 0, sizeof(out));
    const GymPreset *gym = gauntletGymPreset(gymIdx);
    if (!gym || trainerIdx >= 5) return nullptr;
    const GymPresetTrainer &tr = gym->trainers[trainerIdx];
    if (tr.monCount == 0 || !tr.party) return nullptr;

    // Find the canonical "boss" — highest-level mon in the party. Level
    // scaling preserves the delta between each member and the boss, so if
    // canonical Brock = Onix L14 + Geodude L12 (Δ=−2) and the admin sets
    // boss level to 60, Onix → 60, Geodude → 58.
    uint8_t canonicalBossLvl = 0;
    for (uint8_t i = 0; i < tr.monCount; ++i)
        if (tr.party[i].level > canonicalBossLvl) canonicalBossLvl = tr.party[i].level;
    if (canonicalBossLvl == 0) canonicalBossLvl = 1;

    uint8_t added = 0;
    for (uint8_t i = 0; i < tr.monCount && added < 6; ++i) {
        const GymPresetMon &m = tr.party[i];
        if (m.dex == 0) continue;
        uint8_t moves[4];
        bool hasMoves = (m.moves[0] || m.moves[1] || m.moves[2] || m.moves[3]);
        if (hasMoves) memcpy(moves, m.moves, 4);
        else          pickDefaultMoves(m.dex, moves);

        uint8_t lvl = m.level;
        if (overrideBossLvl > 0) {
            // Negative delta from boss in canonical roster.
            int delta = (int)m.level - (int)canonicalBossLvl;   // ≤ 0
            int newLvl = (int)overrideBossLvl + delta;
            if (newLvl < 1)   newLvl = 1;
            if (newLvl > 100) newLvl = 100;
            lvl = (uint8_t)newLvl;
        }

        writeMonByDex(out, added, m.dex, lvl, moves, "FOE");
        added++;
    }
    out.count = added;
    return tr.name;
}
