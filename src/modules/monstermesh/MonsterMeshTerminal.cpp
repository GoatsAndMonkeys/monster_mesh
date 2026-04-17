// SPDX-License-Identifier: MIT
// See MonsterMeshTerminal.h.

#include "MonsterMeshTerminal.h"
#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// UNSCII 8px pixel font — declared manually because lv_conf.h may disable it
LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_unscii_8;

// ── Wild encounter pool (same as PokeBattleNRF / MonsterMeshRoguelike) ──────
namespace {

const uint8_t WILD_POOL[] = {
     16, 19, 21, 23, 27, 29, 32, 41, 43, 46, 48, 50, 54, 56, 58,
     60, 63, 66, 69, 72, 74, 77, 79, 81, 84, 86, 88, 90, 92, 95,
     96, 98,100,102,104,109,111,114,116,118,120,127,129,133,
};
constexpr uint8_t WILD_POOL_LEN = sizeof(WILD_POOL) / sizeof(WILD_POOL[0]);

struct BossDef { uint8_t species; const char *name; };
const BossDef BOSSES[] = {
    {  68, "MACHAMP"  },
    {  76, "GOLEM"    },
    {  94, "GENGAR"   },
    { 130, "GYARADOS" },
    { 142, "AERODACTYL"},
    { 149, "DRAGONITE"},
};
constexpr uint8_t BOSS_COUNT = sizeof(BOSSES) / sizeof(BOSSES[0]);

} // namespace

// ── LVGL wiring ─────────────────────────────────────────────────────────────

void MonsterMeshTerminal::init(lv_obj_t *outputPanel, lv_obj_t *inputTextarea)
{
    outputPanel_   = outputPanel;
    inputTextarea_ = inputTextarea;
    lineCount_     = 0;
    rng_           = (uint32_t)millis() ^ 0xA5A5A5A5u;

    print("MonsterMesh Terminal v1.0");
    print("Type 'help' for commands.");
    printSep();
}

void MonsterMeshTerminal::submitCommand()
{
    if (!inputTextarea_) return;
    const char *text = lv_textarea_get_text(inputTextarea_);
    if (!text || !*text) return;

    // Echo the command.
    char echo[64];
    snprintf(echo, sizeof(echo), "> %s", text);
    print(echo);

    // Copy before clearing (LVGL may reuse the buffer).
    char cmd[64];
    strncpy(cmd, text, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = 0;
    lv_textarea_set_text(inputTextarea_, "");

    handleCommand(cmd);
}

// ── Output ──────────────────────────────────────────────────────────────────

void MonsterMeshTerminal::print(const char *text)
{
    if (!outputPanel_ || !text) return;

    // Cap total lines to avoid eating all RAM.
    if (lineCount_ >= MAX_OUTPUT_LINES) {
        lv_obj_t *first = lv_obj_get_child(outputPanel_, 0);
        if (first) lv_obj_del(first);
        else lineCount_ = 0;
    }

    lv_obj_t *label = lv_label_create(outputPanel_);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xff88C070), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lineCount_++;

    // Auto-scroll to bottom.
    lv_obj_scroll_to_view(label, LV_ANIM_OFF);
}

void MonsterMeshTerminal::printSep()
{
    print("--------------------------------");
}

void MonsterMeshTerminal::engineLogCb(const char *line, void *ctx)
{
    auto *self = static_cast<MonsterMeshTerminal *>(ctx);
    if (self) self->print(line);
}

// ── Command processing ──────────────────────────────────────────────────────

void MonsterMeshTerminal::handleCommand(const char *cmd)
{
    // Skip leading whitespace.
    while (*cmd == ' ' || *cmd == '\t') ++cmd;
    if (!*cmd) return;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print("--- Roguelike ---");
        print("rogue    start a run");
        print("ok       next encounter");
        print("1..4     use move");
        print("s 1..6   switch pokemon");
        print("status   show party/battle");
        print("quit     abandon run");
        print("--- Battle ---");
        print("[PvP coming soon]");
        printSep();
        return;
    }

    if (strcmp(cmd, "rogue") == 0) {
        if (state_ != State::IDLE) {
            print("Run already active. Type 'quit' first.");
            return;
        }
        startRun();
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        if (state_ == State::IN_BATTLE)
            describeBattleStatus();
        else if (state_ == State::IDLE)
            print("No active run. Type 'rogue' to start.");
        else
            describePartyStatus();
        return;
    }

    if (strcmp(cmd, "quit") == 0) {
        if (state_ == State::IDLE) {
            print("Nothing to quit.");
            return;
        }
        state_ = State::IDLE;
        floor_ = 0;
        encIdx_ = 0;
        memset(&playerParty_, 0, sizeof(playerParty_));
        print("Run abandoned.");
        printSep();
        return;
    }

    if (strcmp(cmd, "ok") == 0) {
        if (state_ == State::BETWEEN_BATTLES) {
            prepareNextEncounter();
        } else if (state_ == State::RUN_OVER) {
            state_ = State::IDLE;
            print("Run cleared. Type 'rogue' to start again.");
            printSep();
        } else {
            print("Nothing to advance.");
        }
        return;
    }

    // Switch: s N
    if ((cmd[0] == 's' || cmd[0] == 'S') && cmd[1] == ' ') {
        if (state_ != State::IN_BATTLE) {
            print("Not in battle.");
            return;
        }
        int slot = atoi(cmd + 2);
        if (slot < 1 || slot > playerParty_.count) {
            print("Bad slot. Use s 1..6.");
            return;
        }
        const auto &mine = engine_.party(0);
        if (mine.mons[slot - 1].hp == 0) {
            print("That pokemon fainted.");
            return;
        }
        if (mine.active == slot - 1) {
            print("Already active.");
            return;
        }
        resolvePlayerAction(1, (uint8_t)(slot - 1));
        return;
    }

    // Move: 1..4
    if (cmd[0] >= '1' && cmd[0] <= '4' &&
        (cmd[1] == 0 || cmd[1] == ' ')) {
        if (state_ != State::IN_BATTLE) {
            print("Not in battle. Type 'ok' to start the encounter.");
            return;
        }
        uint8_t slot = (uint8_t)(cmd[0] - '1');
        const auto &mine = engine_.party(0);
        const auto &m = mine.mons[mine.active];
        if (m.moves[slot] == 0) {
            print("Empty move slot.");
            return;
        }
        if (m.pp[slot] == 0) {
            print("No PP left.");
            return;
        }
        resolvePlayerAction(0, slot);
        return;
    }

    print("Unknown command. Type 'help'.");
}

// ── Game logic ──────────────────────────────────────────────────────────────

void MonsterMeshTerminal::startRun()
{
    floor_   = 1;
    encIdx_  = 0;
    rng_     = (uint32_t)millis() ^ 0xCAFEBABEu;
    buildDemoParty(playerParty_);
    healFullParty();

    printSep();
    print("Roguelike run started!");
    print("Party: Bulbasaur, Charmander, Squirtle");
    char msg[60];
    snprintf(msg, sizeof(msg), "Floor %u, encounter 1/%u",
             (unsigned)floor_, (unsigned)ENCOUNTERS_PER_FLOOR);
    print(msg);
    print("Type 'ok' to face the first foe.");
    printSep();
    state_ = State::BETWEEN_BATTLES;
}

void MonsterMeshTerminal::prepareNextEncounter()
{
    bool isBoss = (encIdx_ == 0) && (floor_ % FLOORS_PER_BOSS == 0);

    uint16_t totalLvl = 0;
    for (uint8_t i = 0; i < playerParty_.count; ++i) {
        uint8_t lvl = playerParty_.mons[i].level
                          ? playerParty_.mons[i].level
                          : playerParty_.mons[i].boxLevel;
        totalLvl += lvl;
    }
    uint8_t avgLvl = playerParty_.count ? (uint8_t)(totalLvl / playerParty_.count) : 5;

    Gen1Party opp;
    if (isBoss) buildBossOpponent(opp, floor_);
    else        buildWildOpponent(opp, avgLvl);

    engine_.start(playerParty_, opp, rand32(), 1);

    printSep();
    print(isBoss ? "A boss appears!" : "A wild foe appears!");
    describeBattleStatus();
    print("Use 1..4 (move) or s 1..6 (switch).");
    state_ = State::IN_BATTLE;
}

void MonsterMeshTerminal::resolvePlayerAction(uint8_t actionType, uint8_t index)
{
    uint8_t cpuAct = 0, cpuIdx = 0;
    engine_.cpuPickAction(1, cpuAct, cpuIdx);
    engine_.submitAction(0, actionType, index);
    engine_.submitAction(1, cpuAct, cpuIdx);
    engine_.executeTurn(&MonsterMeshTerminal::engineLogCb, this);
    engine_.autoReplaceIfFainted(0, &MonsterMeshTerminal::engineLogCb, this);
    engine_.autoReplaceIfFainted(1, &MonsterMeshTerminal::engineLogCb, this);

    // Sync engine HP/PP back into save layout.
    const auto &mine = engine_.party(0);
    for (uint8_t i = 0; i < playerParty_.count && i < mine.count; ++i) {
        const auto &b = mine.mons[i];
        Gen1Pokemon &p = playerParty_.mons[i];
        setBe16(p.hp, b.hp);
        memcpy(p.pp, b.pp, 4);
        p.status = b.status;
    }

    auto res = engine_.result();
    if (res == Gen1BattleEngine::Result::ONGOING) {
        describeBattleStatus();
        return;
    }

    printSep();
    if (res == Gen1BattleEngine::Result::P1_WIN) {
        print("You won the encounter!");
        encIdx_++;
        if (encIdx_ >= ENCOUNTERS_PER_FLOOR) {
            encIdx_ = 0;
            floor_++;
            healFullParty();
            char m[80];
            snprintf(m, sizeof(m), "Floor cleared! Now on floor %u. Party healed.",
                     (unsigned)floor_);
            print(m);
        } else {
            char m[80];
            snprintf(m, sizeof(m), "Floor %u - encounter %u/%u next.",
                     (unsigned)floor_, (unsigned)(encIdx_ + 1),
                     (unsigned)ENCOUNTERS_PER_FLOOR);
            print(m);
        }
        print("Type 'ok' to continue.");
        state_ = State::BETWEEN_BATTLES;
    } else {
        print("Your party fainted. Run over!");
        print("Type 'ok' to return, 'rogue' for a new run.");
        state_ = State::RUN_OVER;
    }
}

void MonsterMeshTerminal::describeBattleStatus()
{
    const auto &mine = engine_.party(0);
    const auto &foe  = engine_.party(1);
    const auto &m    = mine.mons[mine.active];
    const auto &f    = foe.mons[foe.active];

    char buf[80];
    snprintf(buf, sizeof(buf), "Foe: %.10s L%u  %u/%u HP",
             f.nickname, (unsigned)f.level,
             (unsigned)f.hp, (unsigned)f.maxHp);
    print(buf);
    snprintf(buf, sizeof(buf), "You: %.10s L%u  %u/%u HP",
             m.nickname, (unsigned)m.level,
             (unsigned)m.hp, (unsigned)m.maxHp);
    print(buf);

    for (uint8_t i = 0; i < 4; ++i) {
        if (m.moves[i] == 0) continue;
        const Gen1MoveData *mv = gen1Move(m.moves[i]);
        snprintf(buf, sizeof(buf), " %u) %-12s PP %u",
                 (unsigned)(i + 1), mv ? mv->name : "?", (unsigned)m.pp[i]);
        print(buf);
    }
}

void MonsterMeshTerminal::describePartyStatus()
{
    char buf[80];
    snprintf(buf, sizeof(buf), "Party (Floor %u, Enc %u/%u):",
             (unsigned)floor_, (unsigned)(encIdx_ + 1),
             (unsigned)ENCOUNTERS_PER_FLOOR);
    print(buf);
    for (uint8_t i = 0; i < playerParty_.count; ++i) {
        Gen1Pokemon &p = playerParty_.mons[i];
        snprintf(buf, sizeof(buf), " %u) %.10s L%u  %u/%u HP",
                 (unsigned)(i + 1),
                 (const char *)playerParty_.nicknames[i],
                 (unsigned)p.level,
                 (unsigned)be16(p.hp),
                 (unsigned)be16(p.maxHp));
        print(buf);
    }
}

// ── Party building ──────────────────────────────────────────────────────────

void MonsterMeshTerminal::pickMovesForSpecies(uint8_t species, uint8_t outMoves[4])
{
    const Gen1BaseStats &b = GEN1_BASE_STATS[species < 152 ? species : 0];
    uint8_t picked = 0;
    outMoves[0] = outMoves[1] = outMoves[2] = outMoves[3] = 0;
    for (uint8_t i = 0; i < GEN1_MOVE_COUNT && picked < 4; ++i) {
        const Gen1MoveData &m = GEN1_MOVES[i];
        if (m.power == 0) continue;
        if (m.type != b.type1 && m.type != b.type2) continue;
        if (m.power < 40 || m.power > 100) continue;
        outMoves[picked++] = m.num;
    }
    for (uint8_t i = 0; i < GEN1_MOVE_COUNT && picked < 4; ++i) {
        const Gen1MoveData &m = GEN1_MOVES[i];
        if (m.power == 0 || m.type != 0) continue;
        if (m.power < 30 || m.power > 80) continue;
        bool dup = false;
        for (uint8_t j = 0; j < picked; ++j) if (outMoves[j] == m.num) dup = true;
        if (!dup) outMoves[picked++] = m.num;
    }
    if (picked == 0) { outMoves[0] = 33; outMoves[1] = 45; }
}

void MonsterMeshTerminal::writeBattlePokeToSave(Gen1Party &out, uint8_t slot,
                                                 uint8_t species, uint8_t lvl,
                                                 const uint8_t moves[4],
                                                 const Gen1BattleEngine::BattlePoke &tmp,
                                                 uint8_t dvByte, const char *nick)
{
    Gen1Pokemon &p = out.mons[slot];
    p.species  = species;
    p.boxLevel = lvl;
    p.level    = lvl;
    setBe16(p.maxHp, tmp.maxHp);
    setBe16(p.hp,    tmp.hp);
    setBe16(p.atk,   tmp.atk);
    setBe16(p.def,   tmp.def);
    setBe16(p.spd,   tmp.spd);
    setBe16(p.spc,   tmp.spc);
    p.dvs[0] = dvByte;
    p.dvs[1] = dvByte;
    memcpy(p.moves, moves, 4);
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *m = gen1Move(moves[i]);
        p.pp[i] = m ? m->pp : 0;
    }
    out.species[slot] = species;
    snprintf((char *)out.nicknames[slot], 11, "%s", nick ? nick : "MON");
}

void MonsterMeshTerminal::buildDemoParty(Gen1Party &out)
{
    memset(&out, 0, sizeof(out));
    out.count = 3;
    struct { uint8_t species; uint8_t lvl; const char *nick; } members[3] = {
        { 1, 5, "BULBASAUR" },
        { 4, 5, "CHARMANDER" },
        { 7, 5, "SQUIRTLE" },
    };
    for (uint8_t i = 0; i < 3; ++i) {
        Gen1BattleEngine::BattlePoke tmp;
        uint8_t moves[4];
        pickMovesForSpecies(members[i].species, moves);
        Gen1BattleEngine::initBattlePokeFromBase(
            tmp, members[i].species, members[i].lvl, moves);
        writeBattlePokeToSave(out, i, members[i].species, members[i].lvl,
                              moves, tmp, 0xCC, members[i].nick);
    }
}

void MonsterMeshTerminal::buildWildOpponent(Gen1Party &out, uint8_t avgLvl)
{
    memset(&out, 0, sizeof(out));
    out.count = 1;
    uint8_t species = WILD_POOL[rand32() % WILD_POOL_LEN];
    int lvl = (int)avgLvl + ((int)(rand32() % 5) - 2);
    if (lvl < 2)  lvl = 2;
    if (lvl > 99) lvl = 99;
    Gen1BattleEngine::BattlePoke tmp;
    uint8_t moves[4];
    pickMovesForSpecies(species, moves);
    Gen1BattleEngine::initBattlePokeFromBase(tmp, species, lvl, moves);
    writeBattlePokeToSave(out, 0, species, lvl, moves, tmp, 0x88, "WILD MON");
}

void MonsterMeshTerminal::buildBossOpponent(Gen1Party &out, uint8_t floorNum)
{
    memset(&out, 0, sizeof(out));
    const BossDef &boss = BOSSES[(floorNum / FLOORS_PER_BOSS) % BOSS_COUNT];
    out.count = 1;
    int lvl = 5 + floorNum;
    if (lvl > 99) lvl = 99;
    Gen1BattleEngine::BattlePoke tmp;
    uint8_t moves[4];
    pickMovesForSpecies(boss.species, moves);
    Gen1BattleEngine::initBattlePokeFromBase(tmp, boss.species, lvl, moves);
    writeBattlePokeToSave(out, 0, boss.species, lvl, moves, tmp, 0xFF, boss.name);
}

void MonsterMeshTerminal::healFullParty()
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

uint32_t MonsterMeshTerminal::rand32()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    if (rng_ == 0) rng_ = 0xCAFEBABEu;
    return rng_;
}
