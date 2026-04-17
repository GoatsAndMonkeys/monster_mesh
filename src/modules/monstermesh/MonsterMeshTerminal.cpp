// SPDX-License-Identifier: MIT
// See MonsterMeshTerminal.h.

#include "MonsterMeshTerminal.h"
#include "graphics/view/TFT/Themes.h"
#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Cozette 13px pixel font
LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_cozette_13;

// ── Wild encounter pool ────────────────────────────────────────────────────
namespace {

const uint8_t WILD_POOL[] = {
     16, 19, 21, 23, 27, 29, 32, 41, 43, 46, 48, 50, 54, 56, 58,
     60, 63, 66, 69, 72, 74, 77, 79, 81, 84, 86, 88, 90, 92, 95,
     96, 98,100,102,104,109,111,114,116,118,120,127,129,133,
};
constexpr uint8_t WILD_POOL_LEN = sizeof(WILD_POOL) / sizeof(WILD_POOL[0]);

} // namespace

// ── LVGL wiring ─────────────────────────────────────────────────────────────

void MonsterMeshTerminal::init(lv_obj_t *outputPanel, lv_obj_t *inputTextarea)
{
    outputPanel_   = outputPanel;
    inputTextarea_ = inputTextarea;
    lineCount_     = 0;
    rng_           = (uint32_t)millis() ^ 0xA5A5A5A5u;

    if (savParty_.count > 0) {
        print("Party loaded. Type 'party' to view.");
    } else {
        print("Loading party from SAV...");
        needsLoad_ = true;
    }
    printSep();
}

void MonsterMeshTerminal::loadParty(const Gen1Party &party)
{
    memcpy(&savParty_, &party, sizeof(Gen1Party));
    needsLoad_ = false;

    if (ready()) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%u Pokemon loaded.", (unsigned)savParty_.count);
        print(buf);
        printSep();
        showParty();
        print("Type 'pick N' to choose.");
        printSep();
    }
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
    lv_obj_set_style_text_font(label, &lv_font_cozette_13, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(Themes::lightest()), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lineCount_++;

    // Auto-scroll to bottom.
    lv_obj_scroll_to_view(label, LV_ANIM_OFF);
}

void MonsterMeshTerminal::printSep()
{
    if (!outputPanel_) return;

    if (lineCount_ >= MAX_OUTPUT_LINES) {
        lv_obj_t *first = lv_obj_get_child(outputPanel_, 0);
        if (first) lv_obj_del(first);
        else lineCount_ = 0;
    }

    lv_obj_t *label = lv_label_create(outputPanel_);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, &lv_font_cozette_13, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(Themes::mid()), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, "--------------------------------");
    lineCount_++;

    lv_obj_scroll_to_view(label, LV_ANIM_OFF);
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
        print("party    show your 6 Pokemon");
        print("pick N   choose Pokemon N for battle");
        print("fight    battle a random opponent");
        print("run      roguelike: fight til all faint");
        print("1..4     use move in battle");
        print("status   show battle status");
        print("quit     forfeit battle");
        printSep();
        return;
    }

    if (strcmp(cmd, "party") == 0) {
        showParty();
        return;
    }

    if (strncmp(cmd, "pick ", 5) == 0 || strncmp(cmd, "pick", 4) == 0) {
        const char *arg = cmd + 4;
        while (*arg == ' ') ++arg;
        int slot = atoi(arg);
        if (slot < 1 || slot > 6) {
            print("Usage: pick 1..6");
            return;
        }
        pickPokemon((uint8_t)(slot - 1));
        return;
    }

    if (strcmp(cmd, "run") == 0) {
        if (savParty_.count == 0) {
            print("No party loaded. Waiting for SAV...");
            return;
        }
        startRun();
        return;
    }

    if (strcmp(cmd, "fight") == 0) {
        if (state_ == State::IN_RUN) {
            startRunWave();
            return;
        }
        if (state_ != State::READY) {
            if (savParty_.count == 0)
                print("No party loaded. Waiting for SAV...");
            else if (chosenSlot_ == 0xFF)
                print("Pick a Pokemon first: pick 1..6");
            else
                print("Already in battle.");
            return;
        }
        startBattle();
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        if (state_ == State::IN_BATTLE)
            describeBattleStatus();
        else if (state_ == State::READY) {
            char buf[64];
            Gen1Pokemon &p = savParty_.mons[chosenSlot_];
            snprintf(buf, sizeof(buf), "Ready: %.10s L%u",
                     (const char *)savParty_.nicknames[chosenSlot_],
                     (unsigned)p.level);
            print(buf);
        } else {
            print("No Pokemon selected. Type 'party'.");
        }
        return;
    }

    if (strcmp(cmd, "quit") == 0) {
        if (state_ == State::IN_BATTLE || state_ == State::IN_RUN_BATTLE) {
            runActive_ = false;
            state_ = State::READY;
            print("Battle forfeited.");
            printSep();
        } else if (state_ == State::IN_RUN) {
            runActive_ = false;
            state_ = State::READY;
            print("Run abandoned.");
            printSep();
        } else {
            print("Not in battle.");
        }
        return;
    }

    // Move: 1..4
    if (cmd[0] >= '1' && cmd[0] <= '4' &&
        (cmd[1] == 0 || cmd[1] == ' ')) {
        if (state_ != State::IN_BATTLE && state_ != State::IN_RUN_BATTLE) {
            print("Not in battle. Type 'fight'.");
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

void MonsterMeshTerminal::showParty()
{
    if (savParty_.count == 0) {
        print("No party loaded. Waiting for SAV...");
        needsLoad_ = true;
        return;
    }

    // Build entire party as one print so scroll-to-view lands at the header,
    // not just the last line (which would push the party off screen).
    char buf[512];
    int pos = snprintf(buf, sizeof(buf), "Your party:");
    for (uint8_t i = 0; i < savParty_.count; ++i) {
        Gen1Pokemon &p = savParty_.mons[i];
        uint8_t lvl = p.level ? p.level : p.boxLevel;
        const char *marker = (i == chosenSlot_) ? " *" : "";
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\n %u) %.10s L%u  %u/%u HP%s",
                        (unsigned)(i + 1),
                        (const char *)savParty_.nicknames[i],
                        (unsigned)lvl,
                        (unsigned)be16(p.hp),
                        (unsigned)be16(p.maxHp),
                        marker);
    }
    print(buf);
    printSep();
}

void MonsterMeshTerminal::pickPokemon(uint8_t slot)
{
    if (savParty_.count == 0) {
        print("No party loaded.");
        return;
    }
    if (slot >= savParty_.count) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Only %u Pokemon in party.", (unsigned)savParty_.count);
        print(buf);
        return;
    }
    if (state_ == State::IN_BATTLE) {
        print("Can't switch during battle. 'quit' first.");
        return;
    }

    chosenSlot_ = slot;
    Gen1Pokemon &p = savParty_.mons[slot];
    uint8_t lvl = p.level ? p.level : p.boxLevel;
    char buf[64];
    snprintf(buf, sizeof(buf), "Chose %.10s L%u. Type 'fight'!",
             (const char *)savParty_.nicknames[slot], (unsigned)lvl);
    print(buf);
    state_ = State::READY;
}

void MonsterMeshTerminal::startBattle()
{
    // Build single-Pokemon party for the player from the chosen SAV slot.
    memset(&battleParty_, 0, sizeof(battleParty_));
    battleParty_.count = 1;
    battleParty_.mons[0] = savParty_.mons[chosenSlot_];
    battleParty_.species[0] = savParty_.species[chosenSlot_];
    memcpy(battleParty_.nicknames[0], savParty_.nicknames[chosenSlot_], 11);

    // Heal the chosen Pokemon for the battle.
    Gen1Pokemon &p = battleParty_.mons[0];
    setBe16(p.hp, be16(p.maxHp));
    p.status = 0;
    for (int j = 0; j < 4; ++j) {
        const Gen1MoveData *mv = gen1Move(p.moves[j]);
        if (mv) p.pp[j] = mv->pp;
    }

    // Build wild opponent scaled to player level.
    uint8_t lvl = p.level ? p.level : p.boxLevel;
    buildWildOpponent(oppParty_, lvl);

    engine_.start(battleParty_, oppParty_, rand32(), 1);

    printSep();
    if (lastFoeSource_[0] != '\0') {
        char msg[40];
        snprintf(msg, sizeof(msg), "%s's Pokemon appears!", lastFoeSource_);
        print(msg);
    } else {
        print("A wild foe appears!");
    }
    describeBattleStatus();
    print("Use 1..4 to attack.");
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

    auto res = engine_.result();
    if (res == Gen1BattleEngine::Result::ONGOING) {
        describeBattleStatus();
        return;
    }

    printSep();
    if (state_ == State::IN_RUN_BATTLE) {
        if (res == Gen1BattleEngine::Result::P1_WIN) {
            syncRunPartyHpFromEngine();
            char msg[48];
            snprintf(msg, sizeof(msg), "Wave %u cleared!", (unsigned)waveNum_);
            print(msg);
            // Show remaining HP so player knows their state
            uint8_t alive = 0;
            for (uint8_t i = 0; i < runParty_.count; i++)
                if (be16(runParty_.mons[i].hp) > 0) alive++;
            snprintf(msg, sizeof(msg), "%u Pokemon still standing.", (unsigned)alive);
            print(msg);
            print("Type 'fight' for next wave.");
            state_ = State::IN_RUN;
        } else {
            char msg[48];
            snprintf(msg, sizeof(msg), "All Pokemon fainted. Wave %u.", (unsigned)waveNum_);
            print(msg);
            snprintf(msg, sizeof(msg), "Run over! You reached wave %u.", (unsigned)waveNum_);
            print(msg);
            runActive_ = false;
            state_ = State::READY;
        }
        printSep();
        return;
    }

    if (res == Gen1BattleEngine::Result::P1_WIN) {
        print("You won!");
    } else {
        print("You lost!");
    }
    print("Type 'fight' for another battle.");
    state_ = State::READY;
    printSep();
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

// ── Wild opponent building ──────────────────────────────────────────────────

void MonsterMeshTerminal::pickMovesForSpecies(uint8_t species, uint8_t outMoves[4])
{
    const Gen1BaseStats &b = GEN1_BASE_STATS[species < 152 ? species : 0];
    uint8_t picked = 0;
    outMoves[0] = outMoves[1] = outMoves[2] = outMoves[3] = 0;
    // STAB moves first.
    for (uint8_t i = 0; i < GEN1_MOVE_COUNT && picked < 4; ++i) {
        const Gen1MoveData &m = GEN1_MOVES[i];
        if (m.power == 0) continue;
        if (m.type != b.type1 && m.type != b.type2) continue;
        if (m.power < 40 || m.power > 100) continue;
        outMoves[picked++] = m.num;
    }
    // Fill with Normal moves.
    for (uint8_t i = 0; i < GEN1_MOVE_COUNT && picked < 4; ++i) {
        const Gen1MoveData &m = GEN1_MOVES[i];
        if (m.power == 0 || m.type != 0) continue;
        if (m.power < 30 || m.power > 80) continue;
        bool dup = false;
        for (uint8_t j = 0; j < picked; ++j) if (outMoves[j] == m.num) dup = true;
        if (!dup) outMoves[picked++] = m.num;
    }
    if (picked == 0) { outMoves[0] = 33; outMoves[1] = 45; } // Tackle, Growl
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

void MonsterMeshTerminal::buildWildOpponent(Gen1Party &out, uint8_t avgLvl)
{
    memset(&out, 0, sizeof(out));
    out.count = 1;
    lastFoeSource_[0] = '\0';

    uint8_t species = 0;
    const char *nick = "WILD MON";

    if (meshPeerCount_ > 0) {
        // Pick a random mesh peer's Pokemon as the opponent
        const DaycareNeighborPokemon &peer = meshPeers_[rand32() % meshPeerCount_];
        species = peer.speciesDex;
        // Use Pokemon's nickname if set, otherwise the node's short name
        nick = (peer.nickname[0] != '\0') ? peer.nickname : peer.shortName;
        // Store trainer name for fight announcement
        strncpy(lastFoeSource_, peer.shortName[0] ? peer.shortName : peer.gameName, 11);
        lastFoeSource_[11] = '\0';
    }

    if (species == 0 || species > 151) {
        species = WILD_POOL[rand32() % WILD_POOL_LEN];
        lastFoeSource_[0] = '\0';
    }

    int lvl = (int)avgLvl + ((int)(rand32() % 5) - 2);
    if (lvl < 2)  lvl = 2;
    if (lvl > 99) lvl = 99;
    Gen1BattleEngine::BattlePoke tmp;
    uint8_t moves[4];
    pickMovesForSpecies(species, moves);
    Gen1BattleEngine::initBattlePokeFromBase(tmp, species, lvl, moves);
    writeBattlePokeToSave(out, 0, species, lvl, moves, tmp, 0x88, nick);
}

uint32_t MonsterMeshTerminal::rand32()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    if (rng_ == 0) rng_ = 0xCAFEBABEu;
    return rng_;
}

// ── Roguelike run ────────────────────────────────────────────────────────────

uint8_t MonsterMeshTerminal::runAvgLevel() const
{
    if (savParty_.count == 0) return 5;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < savParty_.count; i++) {
        uint8_t lvl = savParty_.mons[i].level ? savParty_.mons[i].level : savParty_.mons[i].boxLevel;
        sum += lvl;
    }
    return (uint8_t)(sum / savParty_.count);
}

void MonsterMeshTerminal::startRun()
{
    if (savParty_.count == 0) { print("No party loaded."); return; }

    // Copy full party and heal all Pokemon to full HP
    memcpy(&runParty_, &savParty_, sizeof(Gen1Party));
    for (uint8_t i = 0; i < runParty_.count; i++) {
        Gen1Pokemon &p = runParty_.mons[i];
        setBe16(p.hp, be16(p.maxHp));
        p.status = 0;
        for (int j = 0; j < 4; j++) {
            const Gen1MoveData *mv = gen1Move(p.moves[j]);
            if (mv) p.pp[j] = mv->pp;
        }
    }

    runActive_ = true;
    waveNum_   = 0;

    char msg[48];
    snprintf(msg, sizeof(msg), "Run started! %u Pokemon healed.", (unsigned)runParty_.count);
    print(msg);
    if (meshPeerCount_ > 0) {
        snprintf(msg, sizeof(msg), "%u mesh trainers nearby.", (unsigned)meshPeerCount_);
        print(msg);
    } else {
        print("No mesh trainers nearby — fighting wild.");
    }
    print("Type 'fight' to begin wave 1.");
    printSep();
    state_ = State::IN_RUN;
}

void MonsterMeshTerminal::startRunWave()
{
    if (!runActive_ || runParty_.count == 0) { print("No run active."); return; }

    // Check all fainted (shouldn't happen but guard it)
    uint8_t alive = 0;
    for (uint8_t i = 0; i < runParty_.count; i++)
        if (be16(runParty_.mons[i].hp) > 0) alive++;
    if (alive == 0) {
        print("All Pokemon fainted. Run over.");
        runActive_ = false;
        state_ = State::READY;
        return;
    }

    waveNum_++;

    // Opponent level scales with wave: avg party level + wave * 3 (capped 99)
    uint8_t avgLvl = runAvgLevel();
    uint8_t oppLvl = avgLvl + waveNum_ * 3;
    if (oppLvl > 99) oppLvl = 99;

    buildWildOpponent(oppParty_, oppLvl);
    engine_.start(runParty_, oppParty_, rand32(), 1);

    char msg[48];
    snprintf(msg, sizeof(msg), "-- Wave %u -- (foe L%u)", (unsigned)waveNum_, (unsigned)oppLvl);
    print(msg);
    if (lastFoeSource_[0] != '\0') {
        snprintf(msg, sizeof(msg), "%s's Pokemon appears!", lastFoeSource_);
        print(msg);
    } else {
        print("A wild foe appears!");
    }
    describeBattleStatus();
    print("Use 1..4 to attack.");
    state_ = State::IN_RUN_BATTLE;
}

void MonsterMeshTerminal::syncRunPartyHpFromEngine()
{
    const auto &ep = engine_.party(0);
    for (uint8_t i = 0; i < runParty_.count && i < ep.count; i++) {
        setBe16(runParty_.mons[i].hp, ep.mons[i].hp);
        runParty_.mons[i].status = ep.mons[i].status;
        // Sync PP
        for (int j = 0; j < 4; j++) runParty_.mons[i].pp[j] = ep.mons[i].pp[j];
    }
}
