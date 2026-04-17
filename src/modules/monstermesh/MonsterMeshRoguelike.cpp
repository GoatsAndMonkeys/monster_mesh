// SPDX-License-Identifier: MIT
// See MonsterMeshRoguelike.h.

#include "MonsterMeshRoguelike.h"
#include "Gen1BattleEngine.h"
#include "showdown_gen1_basestats.h"
#include "showdown_gen1_moves.h"
#include <string.h>
#include <stdio.h>

// ── Curated wild encounter pool: indices into the Gen 1 species table ───────
// Common, mid-tier wild Pokemon. Avoids legendaries and starter-evolutions.

static const uint8_t WILD_POOL[] = {
     16, // Pidgey
     19, // Rattata
     21, // Spearow
     23, // Ekans
     27, // Sandshrew
     29, // Nidoran♀
     32, // Nidoran♂
     41, // Zubat
     43, // Oddish
     46, // Paras
     48, // Venonat
     50, // Diglett
     54, // Psyduck
     56, // Mankey
     58, // Growlithe
     60, // Poliwag
     63, // Abra
     66, // Machop
     69, // Bellsprout
     72, // Tentacool
     74, // Geodude
     77, // Ponyta
     79, // Slowpoke
     81, // Magnemite
     84, // Doduo
     86, // Seel
     88, // Grimer
     90, // Shellder
     92, // Gastly
     95, // Onix
     96, // Drowzee
     98, // Krabby
    100, // Voltorb
    102, // Exeggcute
    104, // Cubone
    109, // Koffing
    111, // Rhyhorn
    114, // Tangela
    116, // Horsea
    118, // Goldeen
    120, // Staryu
    127, // Pinsir
    129, // Magikarp
    133, // Eevee
};
static constexpr uint8_t WILD_POOL_LEN = sizeof(WILD_POOL) / sizeof(WILD_POOL[0]);

// Boss roster — picks one per boss floor, modulo by index. Levels scale.
struct BossDef { uint8_t species; const char *name; };
static const BossDef BOSSES[] = {
    {  68, "MACHAMP"  },
    {  76, "GOLEM"    },
    {  94, "GENGAR"   },
    { 130, "GYARADOS" },
    { 142, "AERODACTYL"},
    { 149, "DRAGONITE"},
};
static constexpr uint8_t BOSS_COUNT = sizeof(BOSSES) / sizeof(BOSSES[0]);

// ── Pick reasonable starter moves for an arbitrary species. ─────────────────
// Just pick the first 4 moves whose type matches one of the species' types,
// preferring damaging ones. Falls back to Tackle (33) and Growl (45).

static void pickMovesForSpecies(uint8_t species, uint8_t outMoves[4])
{
    const Gen1BaseStats &b = GEN1_BASE_STATS[species < 152 ? species : 0];
    uint8_t picked = 0;
    outMoves[0] = outMoves[1] = outMoves[2] = outMoves[3] = 0;

    // Pass 1: STAB damaging moves
    for (uint8_t i = 0; i < GEN1_MOVE_COUNT && picked < 4; ++i) {
        const Gen1MoveData &m = GEN1_MOVES[i];
        if (m.power == 0) continue;
        if (m.type != b.type1 && m.type != b.type2) continue;
        if (m.power < 40 || m.power > 100) continue;  // skip weak/overkill
        outMoves[picked++] = m.num;
    }
    // Pass 2: any normal-type damaging move (filler)
    for (uint8_t i = 0; i < GEN1_MOVE_COUNT && picked < 4; ++i) {
        const Gen1MoveData &m = GEN1_MOVES[i];
        if (m.power == 0 || m.type != 0) continue;
        if (m.power < 30 || m.power > 80) continue;
        bool dup = false;
        for (uint8_t j = 0; j < picked; ++j) if (outMoves[j] == m.num) dup = true;
        if (!dup) outMoves[picked++] = m.num;
    }
    if (picked == 0) { outMoves[0] = 33; outMoves[1] = 45; }  // Tackle, Growl
}

// ── RNG ─────────────────────────────────────────────────────────────────────

uint32_t MonsterMeshRoguelike::rand32()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    if (rng_ == 0) rng_ = 0xCAFEBABE;
    return rng_;
}

// ── Party builders ──────────────────────────────────────────────────────────

void MonsterMeshRoguelike::buildWildOpponent(Gen1Party &out, uint8_t playerLevel)
{
    memset(&out, 0, sizeof(out));
    out.count = 1;
    uint8_t species = WILD_POOL[rand32() % WILD_POOL_LEN];
    // ±2 levels around player to keep difficulty fair.
    int lvl = (int)playerLevel + ((int)(rand32() % 5) - 2);
    if (lvl < 2)  lvl = 2;
    if (lvl > 99) lvl = 99;

    Gen1BattleEngine::BattlePoke tmp;
    uint8_t moves[4]; pickMovesForSpecies(species, moves);
    Gen1BattleEngine::initBattlePokeFromBase(tmp, species, lvl, moves);

    // Project tmp back into Gen1Pokemon save layout (loose: only what initFromSave reads).
    Gen1Pokemon &p = out.mons[0];
    p.species = species;
    p.boxLevel = lvl; p.level = lvl;
    setBe16(p.maxHp, tmp.maxHp); setBe16(p.hp, tmp.hp);
    setBe16(p.atk,  tmp.atk);    setBe16(p.def, tmp.def);
    setBe16(p.spd,  tmp.spd);    setBe16(p.spc, tmp.spc);
    p.dvs[0] = 0x88; p.dvs[1] = 0x88;        // average DVs (we already used 8/8/8/8/8)
    memcpy(p.moves, moves, 4);
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *m = gen1Move(moves[i]);
        p.pp[i] = m ? m->pp : 0;
    }
    out.species[0] = species;
    snprintf((char *)out.nicknames[0], 11, "WILD %s",
             species < 152 ? "MON" : "?");
}

void MonsterMeshRoguelike::buildBossOpponent(Gen1Party &out, uint8_t floorNum)
{
    memset(&out, 0, sizeof(out));
    const BossDef &boss = BOSSES[(floorNum / FLOORS_PER_BOSS) % BOSS_COUNT];
    out.count = 1;
    int lvl = 5 + floorNum;  // scales with depth
    if (lvl > 99) lvl = 99;

    Gen1BattleEngine::BattlePoke tmp;
    uint8_t moves[4]; pickMovesForSpecies(boss.species, moves);
    Gen1BattleEngine::initBattlePokeFromBase(tmp, boss.species, lvl, moves);

    Gen1Pokemon &p = out.mons[0];
    p.species = boss.species; p.boxLevel = lvl; p.level = lvl;
    setBe16(p.maxHp, tmp.maxHp); setBe16(p.hp, tmp.hp);
    setBe16(p.atk,  tmp.atk);    setBe16(p.def, tmp.def);
    setBe16(p.spd,  tmp.spd);    setBe16(p.spc, tmp.spc);
    p.dvs[0] = 0xFF; p.dvs[1] = 0xFF;        // bosses: max DVs
    memcpy(p.moves, moves, 4);
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *m = gen1Move(moves[i]);
        p.pp[i] = m ? m->pp : 0;
    }
    out.species[0] = boss.species;
    snprintf((char *)out.nicknames[0], 11, "%s", boss.name);
}

// ── Heal / announce ─────────────────────────────────────────────────────────

void MonsterMeshRoguelike::healFullParty()
{
    for (uint8_t i = 0; i < playerParty_.count; ++i) {
        Gen1Pokemon &p = playerParty_.mons[i];
        uint16_t maxHp = be16(p.maxHp);
        setBe16(p.hp, maxHp);
        p.status = 0;
        for (int j = 0; j < 4; ++j) {
            const Gen1MoveData *m = gen1Move(p.moves[j]);
            if (m) p.pp[j] = m->pp;
        }
    }
}

void MonsterMeshRoguelike::announce(const char *line1, const char *line2)
{
    snprintf(banner1_, sizeof(banner1_), "%s", line1 ? line1 : "");
    snprintf(banner2_, sizeof(banner2_), "%s", line2 ? line2 : "");
    state_ = State::BETWEEN_BATTLES;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void MonsterMeshRoguelike::start(const Gen1Party &playerParty)
{
    playerParty_ = playerParty;
    floor_ = 1; encounterIdx_ = 0;
    rng_ = (uint32_t)millis() ^ 0xA5A5A5A5;
    healFullParty();
    char l2[40]; snprintf(l2, sizeof(l2), "Encounters this floor: %u", ENCOUNTERS_PER_FLOOR);
    announce("Floor 1", l2);
}

void MonsterMeshRoguelike::prepareNextEncounter()
{
    bool isBoss = (encounterIdx_ == 0) && (floor_ % FLOORS_PER_BOSS == 0);
    Gen1Party opp;
    // Player level estimate for scaling: average of party.
    uint16_t totalLvl = 0;
    for (uint8_t i = 0; i < playerParty_.count; ++i)
        totalLvl += playerParty_.mons[i].level ? playerParty_.mons[i].level
                                                : playerParty_.mons[i].boxLevel;
    uint8_t avgLvl = playerParty_.count ? (uint8_t)(totalLvl / playerParty_.count) : 5;

    if (isBoss) buildBossOpponent(opp, floor_);
    else        buildWildOpponent(opp, avgLvl);

    battle_.startLocal(playerParty_, opp);
    state_ = State::IN_BATTLE;
}

// ── Tick: drive the run state machine ───────────────────────────────────────

void MonsterMeshRoguelike::tick(uint32_t nowMs)
{
    (void)nowMs;
    if (state_ != State::IN_BATTLE) return;
    if (battle_.isActive()) return;

    // Battle ended. Inspect player party for survival.
    bool anyAlive = false;
    for (uint8_t i = 0; i < playerParty_.count; ++i) {
        if (be16(playerParty_.mons[i].hp) > 0) { anyAlive = true; break; }
    }
    if (!anyAlive) {
        announce("Run over!", "Your party fainted.");
        state_ = State::RUN_OVER;
        return;
    }

    encounterIdx_++;
    if (encounterIdx_ >= ENCOUNTERS_PER_FLOOR) {
        // Floor cleared — heal, advance.
        encounterIdx_ = 0;
        floor_++;
        healFullParty();
        char l1[40], l2[40];
        snprintf(l1, sizeof(l1), "Floor %u cleared!", (unsigned)(floor_ - 1));
        snprintf(l2, sizeof(l2), "Party healed. Press space.");
        announce(l1, l2);
    } else {
        char l1[40], l2[40];
        snprintf(l1, sizeof(l1), "Floor %u — encounter %u/%u",
                 (unsigned)floor_, (unsigned)(encounterIdx_ + 1),
                 (unsigned)ENCOUNTERS_PER_FLOOR);
        snprintf(l2, sizeof(l2), "Press space to continue.");
        announce(l1, l2);
    }
}

void MonsterMeshRoguelike::handleKey(uint8_t c)
{
    if (state_ == State::BETWEEN_BATTLES) {
        if (c == ' ' || c == '\n' || c == '\r') {
            prepareNextEncounter();
        } else if (c == 27 /*ESC*/) {
            state_ = State::OFF;
        }
    } else if (state_ == State::RUN_OVER) {
        state_ = State::OFF;
    }
    // During IN_BATTLE the host module routes keys directly to the battle.
}
