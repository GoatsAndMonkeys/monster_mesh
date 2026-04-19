// SPDX-License-Identifier: MIT
// See MonsterMeshTerminal.h.

#include "MonsterMeshTerminal.h"
#include "LordGyms.h"
#include "gps/RTC.h"
#include "graphics/view/TFT/Themes.h"
#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Cozette 13px pixel font
LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_cozette_13;

// ── Wild encounter pools by badge count (area-appropriate) ────────────────
namespace {

// Each pool reflects the routes between the current and next gym.
// Badge 0 = Viridian/Pallet area; badge 7-8 = Victory Road.
static const uint8_t AREA_POOL_0[] = { 16,19,21,10,13,32,29,23,46,48 };
static const uint8_t AREA_POOL_1[] = { 74,41,35,39,95,66,23,46,19,21  };
static const uint8_t AREA_POOL_2[] = { 118,129,72,25,43,60,79,54,98,100};
static const uint8_t AREA_POOL_3[] = { 92,93,96,79,124,102,109,60,54,41};
static const uint8_t AREA_POOL_4[] = { 123,127,115,128,113,43,70,102,118,60};
static const uint8_t AREA_POOL_5[] = { 64,67,122,106,107,96,79,116,60,102};
static const uint8_t AREA_POOL_6[] = { 58,77,126,111,100,88,90,69,109,77};
static const uint8_t AREA_POOL_7[] = { 67,74,75,105,42,104,111,112,95,66};

struct AreaPool { const uint8_t *data; uint8_t len; };
static const AreaPool AREA_POOLS[8] = {
    { AREA_POOL_0, sizeof(AREA_POOL_0) },
    { AREA_POOL_1, sizeof(AREA_POOL_1) },
    { AREA_POOL_2, sizeof(AREA_POOL_2) },
    { AREA_POOL_3, sizeof(AREA_POOL_3) },
    { AREA_POOL_4, sizeof(AREA_POOL_4) },
    { AREA_POOL_5, sizeof(AREA_POOL_5) },
    { AREA_POOL_6, sizeof(AREA_POOL_6) },
    { AREA_POOL_7, sizeof(AREA_POOL_7) },
};

// Rogue campaign floor theme pools (5 themes, cycling per floor)
static const uint8_t ROGUE_POOL_NORMAL[]  = { 16,19,21,39,52,113,115,128,143,132 };
static const uint8_t ROGUE_POOL_WATER[]   = { 54,60,72,79,90,98,116,118,120,129  };
static const uint8_t ROGUE_POOL_FIRE[]    = { 4,5,6,58,77,78,126,136             };
static const uint8_t ROGUE_POOL_PSYCHIC[] = { 49,64,65,80,96,97,121,122,124,150  };
static const uint8_t ROGUE_POOL_DRAGON[]  = { 6,59,62,130,131,147,148,149        };
static const uint8_t ROGUE_BOSS[5]        = { 143,131,6,150,149 }; // Snorlax/Lapras/Charizard/Mewtwo/Dragonite

struct RoguePool { const uint8_t *data; uint8_t len; };
static const RoguePool ROGUE_POOLS[5] = {
    { ROGUE_POOL_NORMAL,  sizeof(ROGUE_POOL_NORMAL)  },
    { ROGUE_POOL_WATER,   sizeof(ROGUE_POOL_WATER)   },
    { ROGUE_POOL_FIRE,    sizeof(ROGUE_POOL_FIRE)    },
    { ROGUE_POOL_PSYCHIC, sizeof(ROGUE_POOL_PSYCHIC) },
    { ROGUE_POOL_DRAGON,  sizeof(ROGUE_POOL_DRAGON)  },
};
static const char *ROGUE_THEME_NAMES[5] = { "Normal","Water","Fire","Psychic","Dragon" };

} // namespace

// ── LVGL wiring ─────────────────────────────────────────────────────────────

void MonsterMeshTerminal::init(lv_obj_t *outputPanel, lv_obj_t *inputTextarea)
{
    outputPanel_   = outputPanel;
    inputTextarea_ = inputTextarea;
    lineCount_     = 0;
    rng_           = (uint32_t)millis() ^ 0xA5A5A5A5u;

    print("Legend of Charizard");
    if (savParty_.count > 0) {
        print("Party loaded. Type 'help'.");
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

// ── Command normalization ───────────────────────────────────────────────────

void MonsterMeshTerminal::normNoSpaces(const char *in, char *out, size_t outLen)
{
    size_t n = 0;
    for (; *in && n < outLen - 1; ++in) {
        char c = *in;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != ' ' && c != '\t') out[n++] = c;
    }
    out[n] = '\0';
}

// ── Command processing ──────────────────────────────────────────────────────

void MonsterMeshTerminal::handleCommand(const char *cmd)
{
    // Skip leading whitespace.
    while (*cmd == ' ' || *cmd == '\t') ++cmd;
    if (!*cmd) return;

    // Normalized (no spaces, lowercase) for single-word command matching.
    char norm[32];
    normNoSpaces(cmd, norm, sizeof(norm));

    // Also plain lowercase (spaces preserved) for argument extraction.
    char low[64];
    size_t li = 0;
    for (const char *p = cmd; *p && li < sizeof(low)-1; ++p)
        low[li++] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
    low[li] = '\0';

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print("-- Legend of Charizard --");
        print("gym list   list Kanto gyms");
        print("gym go     challenge next gym");
        print("gym N      challenge gym N directly");
        print("explore    daily roguelike (wild route)");
        print("stats      badges, best run, totals");
        print("badges     earned badges");
        print("news       recent events");
        print("party      view your Pokemon");
        print("pick N     swap to Pokemon N in battle");
        print("W/E/R/S or 1/2/3/4 - use move");
        print("quit       forfeit battle");
        printSep();
        return;
    }

    // ── LORD commands ────────────────────────────────────────────────────────

    if (strcmp(cmd, "gym") == 0 || strcmp(cmd, "gym list") == 0) {
        if (state_ == State::IN_BATTLE || state_ == State::IN_RUN_BATTLE ||
            state_ == State::IN_GYM_BATTLE) {
            print("Finish your current battle first.");
            return;
        }
        if (savParty_.count == 0) {
            print("Load a Pokemon save first.");
            return;
        }
        showGymSelect();
        return;
    }

    if (strcmp(cmd, "gym go") == 0) {
        if (state_ == State::IN_BATTLE || state_ == State::IN_RUN_BATTLE ||
            state_ == State::IN_GYM_BATTLE) {
            print("Finish your current battle first.");
            return;
        }
        if (savParty_.count == 0) {
            print("Load a Pokemon save first.");
            return;
        }
        lordEnsureLoaded();
        // Find first unlocked gym without a badge
        int8_t next = -1;
        for (uint8_t i = 0; i < LORD_GYM_COUNT; ++i) {
            if (lordGymUnlocked(lord_, i) && !lordHasBadge(lord_, i)) {
                next = (int8_t)i;
                break;
            }
        }
        if (next < 0) {
            print("You've conquered all 8 gyms!");
            return;
        }
        startGymGauntlet((uint8_t)next);
        return;
    }

    if (strncmp(cmd, "gym ", 4) == 0) {
        if (state_ == State::IN_BATTLE || state_ == State::IN_RUN_BATTLE ||
            state_ == State::IN_GYM_BATTLE) {
            print("Finish your current battle first.");
            return;
        }
        if (savParty_.count == 0) {
            print("Load a Pokemon save first.");
            return;
        }
        int g = atoi(cmd + 4);
        if (g < 1 || g > 8) { print("Usage: gym 1..8"); return; }
        startGymGauntlet((uint8_t)(g - 1));
        return;
    }

    if (strcmp(cmd, "explore") == 0) {
        if (savParty_.count == 0) {
            print("Load a Pokemon save first.");
            return;
        }
        if (state_ == State::IN_BATTLE || state_ == State::IN_RUN_BATTLE ||
            state_ == State::IN_GYM_BATTLE) {
            print("Finish your current battle first.");
            return;
        }
        lordEnsureLoaded();
        lordApplyDailyReset(lord_, getTime(), tzOffsetHours_);
        if (!lord_.exploreUnlimited && lord_.exploreRunsToday >= 1) {
            print("You're exhausted. Come back tomorrow.");
            return;
        }
        lord_.exploreRunsToday++;
        lordSave(lord_);
        runWildOnly_ = true;
        lordResetRunStats(currentRun_);
        startRun();
        return;
    }

    if (strcmp(cmd, "stats") == 0)       { showStats();       return; }
    if (strcmp(cmd, "badges") == 0)      { showBadges();      return; }
    if (strcmp(cmd, "news") == 0)        { showNews();        return; }
    if (strcmp(cmd, "leaderboard") == 0) { showLeaderboard(); return; }

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

    if (strcmp(cmd, "fight") == 0) {
        if (state_ == State::IN_RUN) {
            startRunWave();
            return;
        }
        if (state_ == State::IN_GYM_SELECT) {
            startGymFight();
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
        } else if (state_ == State::IN_GYM_BATTLE || state_ == State::IN_GYM_SELECT) {
            state_ = State::READY;
            currentGymIdx_ = 0xFF;
            print("Gym challenge abandoned.");
            printSep();
        } else {
            print("Not in battle.");
        }
        return;
    }

    // Move: 1/W  2/E  3/R  4/S  (letter aliases avoid SYM key)
    {
        uint8_t slot = 0xFF;
        char c = cmd[0];
        if ((c >= '1' && c <= '4') && (cmd[1] == 0 || cmd[1] == ' '))
            slot = (uint8_t)(c - '1');
        else if ((c == 'w' || c == 'W') && (cmd[1] == 0)) slot = 0;
        else if ((c == 'e' || c == 'E') && (cmd[1] == 0)) slot = 1;
        else if ((c == 'r' || c == 'R') && (cmd[1] == 0)) slot = 2;
        else if ((c == 's' || c == 'S') && (cmd[1] == 0)) slot = 3;

        if (slot != 0xFF) {
        if (state_ != State::IN_BATTLE && state_ != State::IN_RUN_BATTLE &&
            state_ != State::IN_GYM_BATTLE) {
            print("Not in battle. Type 'fight'.");
            return;
        }
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
        } // if (slot != 0xFF)
    } // move block

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

    print("Your party:");
    char buf[48];
    for (uint8_t i = 0; i < savParty_.count; ++i) {
        Gen1Pokemon &p = savParty_.mons[i];
        uint8_t lvl = p.level ? p.level : p.boxLevel;
        const char *marker = (i == chosenSlot_) ? "*" : "";
        snprintf(buf, sizeof(buf), " %u) %.10s L%u %u/%u HP %s",
                 (unsigned)(i + 1),
                 (const char *)savParty_.nicknames[i],
                 (unsigned)lvl,
                 (unsigned)be16(p.hp),
                 (unsigned)be16(p.maxHp),
                 marker);
        print(buf);
    }
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
    if (state_ == State::IN_BATTLE || state_ == State::IN_RUN_BATTLE ||
        state_ == State::IN_GYM_BATTLE) {
        // Mid-battle switch — costs a turn (CPU attacks while you switch)
        if (slot >= engine_.party(0).count) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Only %u Pokemon available.", (unsigned)engine_.party(0).count);
            print(buf);
            return;
        }
        if (engine_.party(0).mons[slot].hp == 0) {
            print("That Pokemon has fainted.");
            return;
        }
        if (slot == engine_.party(0).active) {
            print("That Pokemon is already out.");
            return;
        }
        resolvePlayerAction(1, slot);
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
            currentRun_.wavesBeaten = waveNum_;
            const auto &foe = engine_.party(1);
            if (foe.count > 0) {
                uint8_t lvl = foe.mons[0].level;
                if (lvl > currentRun_.highestOppLevel) currentRun_.highestOppLevel = lvl;
                currentRun_.xpEarned += (uint16_t)lvl * 4;   // rough proxy
            }
            char msg[48];
            snprintf(msg, sizeof(msg), "Wave %u cleared!", (unsigned)waveNum_);
            print(msg);
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
            if (runWildOnly_) {
                lordEnsureLoaded();
                lordOnRunEnd(lord_, currentRun_);
                lordSave(lord_);
                runWildOnly_ = false;
            }
            runActive_ = false;
            state_ = State::READY;
        }
        printSep();
        return;
    }

    if (state_ == State::IN_GYM_BATTLE) {
        if (res == Gen1BattleEngine::Result::P1_WIN) {
            syncGymPartyHpFromEngine();
            char msg[48];
            const LordGym *g = lordGym(currentGymIdx_);
            const char *who = g ? g->trainers[currentGymTrainer_].name : "Trainer";
            snprintf(msg, sizeof(msg), "%s defeated!", who);
            print(msg);
            advanceGymTrainer();
        } else {
            const LordGym *g = lordGym(currentGymIdx_);
            char msg[64];
            snprintf(msg, sizeof(msg), "You lost at %s. Try 'gym go' to retry.",
                     g ? g->city : "the gym");
            print(msg);
            currentGymIdx_ = 0xFF;
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

    static const char MOVE_KEYS[4] = {'W','E','R','S'};
    for (uint8_t i = 0; i < 4; ++i) {
        if (m.moves[i] == 0) continue;
        const Gen1MoveData *mv = gen1Move(m.moves[i]);
        snprintf(buf, sizeof(buf), " %c/%u) %-12s PP %u",
                 MOVE_KEYS[i], (unsigned)(i + 1), mv ? mv->name : "?", (unsigned)m.pp[i]);
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

    if (meshPeerCount_ > 0 && !runWildOnly_) {
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
        // Pick from badge-appropriate area pool
        uint8_t badges = lordLoaded_ ? __builtin_popcount(lord_.badges) : 0;
        if (badges > 7) badges = 7;
        const AreaPool &pool = AREA_POOLS[badges];
        species = pool.data[rand32() % pool.len];
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

// ── Legend of Charizard (LORD) ──────────────────────────────────────────────

void MonsterMeshTerminal::lordEnsureLoaded()
{
    if (lordLoaded_) return;
    lordLoad(lord_);     // zero-inits on failure — safe
    lordLoaded_ = true;
}

void MonsterMeshTerminal::syncGymPartyHpFromEngine()
{
    const auto &ep = engine_.party(0);
    for (uint8_t i = 0; i < gymParty_.count && i < ep.count; i++) {
        setBe16(gymParty_.mons[i].hp, ep.mons[i].hp);
        gymParty_.mons[i].status = ep.mons[i].status;
        for (int j = 0; j < 4; j++) gymParty_.mons[i].pp[j] = ep.mons[i].pp[j];
    }
}

void MonsterMeshTerminal::showGymSelect()
{
    lordEnsureLoaded();
    print("Kanto Gyms:");
    {
        char buf[64];
        for (uint8_t i = 0; i < LORD_GYM_COUNT; ++i) {
            const LordGym *g = lordGym(i);
            if (!g) continue;
            const char *tag =
                lordHasBadge(lord_, i) ? "[earned]" :
                lordGymUnlocked(lord_, i) ? "[open]" : "[locked]";
            snprintf(buf, sizeof(buf), " %u) %s - %s %s",
                     (unsigned)(i + 1), g->city, g->leaderName, tag);
            print(buf);
        }
    }
    print("'gym go' for next gym  'gym N' for specific.");
    printSep();
    state_ = State::IN_GYM_SELECT;
}

void MonsterMeshTerminal::startGymGauntlet(uint8_t gymIdx)
{
    lordEnsureLoaded();
    if (gymIdx >= LORD_GYM_COUNT) { print("Invalid gym."); return; }
    if (!lordGymUnlocked(lord_, gymIdx)) {
        print("Clear earlier gyms first.");
        return;
    }

    const LordGym *g = lordGym(gymIdx);
    if (!g) return;

    // Snapshot and heal the player's party for the gauntlet.
    memcpy(&gymParty_, &savParty_, sizeof(Gen1Party));
    for (uint8_t i = 0; i < gymParty_.count; i++) {
        Gen1Pokemon &p = gymParty_.mons[i];
        setBe16(p.hp, be16(p.maxHp));
        p.status = 0;
        for (int j = 0; j < 4; j++) {
            const Gen1MoveData *mv = gen1Move(p.moves[j]);
            if (mv) p.pp[j] = mv->pp;
        }
    }

    currentGymIdx_     = gymIdx;
    currentGymTrainer_ = 0;

    char msg[80];
    snprintf(msg, sizeof(msg), "-- %s Gym -- Leader: %s (%s Badge)",
             g->city, g->leaderName, g->badgeName);
    print(msg);
    print("5 trainers to beat. No healing between fights.");
    printSep();
    startGymFight();
}

void MonsterMeshTerminal::startGymFight()
{
    const LordGym *g = lordGym(currentGymIdx_);
    if (!g) return;
    if (currentGymTrainer_ >= LORD_GYM_TRAINERS) return;

    Gen1Party foe{};
    if (!lordBuildGymParty(currentGymIdx_, currentGymTrainer_, foe)) {
        print("Trainer roster missing. Aborting.");
        state_ = State::READY;
        currentGymIdx_ = 0xFF;
        return;
    }
    oppParty_ = foe;   // reuse terminal's slot
    lastFoeSource_[0] = '\0';

    // Seed from local RNG — determinism not required for local play.
    engine_.start(gymParty_, oppParty_, rand32(), 1);

    char msg[64];
    const LordGymTrainer &tr = g->trainers[currentGymTrainer_];
    if (currentGymTrainer_ == LORD_GYM_LEADER_INDEX) {
        snprintf(msg, sizeof(msg), "Leader %s challenges you!", tr.name);
    } else {
        snprintf(msg, sizeof(msg), "%s wants to battle!", tr.name);
    }
    print(msg);
    describeBattleStatus();
    print("Use 1..4 to attack.");
    state_ = State::IN_GYM_BATTLE;
}

void MonsterMeshTerminal::advanceGymTrainer()
{
    // Carry engine state back into gymParty_ before building the next fight.
    syncGymPartyHpFromEngine();

    // Check any of the player's party still conscious.
    uint8_t alive = 0;
    for (uint8_t i = 0; i < gymParty_.count; i++)
        if (be16(gymParty_.mons[i].hp) > 0) alive++;
    if (alive == 0) {
        const LordGym *g = lordGym(currentGymIdx_);
        char msg[64];
        snprintf(msg, sizeof(msg), "All fainted. %s stands.",
                 g ? g->leaderName : "The leader");
        print(msg);
        currentGymIdx_ = 0xFF;
        state_ = State::READY;
        printSep();
        return;
    }

    currentGymTrainer_++;
    if (currentGymTrainer_ >= LORD_GYM_TRAINERS) {
        // Leader cleared!
        const LordGym *g = lordGym(currentGymIdx_);
        lordOnGymCleared(lord_, currentGymIdx_);
        lordSave(lord_);

        char msg[80];
        snprintf(msg, sizeof(msg), "You earned the %s Badge!",
                 g ? g->badgeName : "???");
        print(msg);
        print("Head home to rest.");
        currentGymIdx_ = 0xFF;
        state_ = State::READY;
        printSep();
        return;
    }

    // Next trainer.
    char msg[48];
    snprintf(msg, sizeof(msg), "Next up: %u/5",
             (unsigned)(currentGymTrainer_ + 1));
    print(msg);
    startGymFight();
}

void MonsterMeshTerminal::showStats()
{
    lordEnsureLoaded();
    lordApplyDailyReset(lord_, getTime(), tzOffsetHours_);

    uint8_t earned = 0;
    for (uint8_t i = 0; i < 8; ++i) if (lordHasBadge(lord_, i)) earned++;

    char buf[64];
    snprintf(buf, sizeof(buf), "Badges: %u/8", (unsigned)earned);
    print(buf);
    snprintf(buf, sizeof(buf), "Runs: %lu  Waves: %lu",
             (unsigned long)lord_.totalRuns, (unsigned long)lord_.totalWavesBeaten);
    print(buf);
    snprintf(buf, sizeof(buf), "Best: %u waves L%u foe",
             (unsigned)lord_.bestRunWaves, (unsigned)lord_.bestRunHighestLevel);
    print(buf);
    uint8_t remaining = lord_.exploreUnlimited ? 99 :
        (lord_.exploreRunsToday >= 1 ? 0 : 1);
    snprintf(buf, sizeof(buf), "Explore today: %u left", (unsigned)remaining);
    print(buf);
    printSep();
}

void MonsterMeshTerminal::showBadges()
{
    lordEnsureLoaded();
    static const char INIT[8] = {'B','C','T','R','S','M','V','E'};
    char row[17];
    int pos = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        row[pos++] = '[';
        row[pos++] = lordHasBadge(lord_, i) ? INIT[i] : '-';
        row[pos++] = ']';
    }
    row[pos] = 0;

    char line[48];
    snprintf(line, sizeof(line), "Badges: %s", row);
    print(line);
    printSep();
}

void MonsterMeshTerminal::showNews()
{
    lordEnsureLoaded();
    lordApplyDailyReset(lord_, getTime(), tzOffsetHours_);

    if (lord_.newsCount == 0) {
        print("No news yet. Earn a badge!");
        printSep();
        return;
    }

    print("News:");
    // Walk the ring newest-first.
    for (uint8_t k = 0; k < lord_.newsCount; ++k) {
        uint8_t idx = (uint8_t)((lord_.newsHead + LORD_NEWS_CAP - 1 - k) % LORD_NEWS_CAP);
        const LordNewsEntry &e = lord_.news[idx];
        char line[80];
        switch (e.type) {
        case LORD_NEWS_BADGE: {
            const LordGym *g = lordGym(e.arg1);
            snprintf(line, sizeof(line), " - Earned %s Badge",
                     g ? g->badgeName : "???");
            break; }
        case LORD_NEWS_BEST_RUN:
            snprintf(line, sizeof(line), " - New best run: %u waves",
                     (unsigned)e.arg2);
            break;
        case LORD_NEWS_CENTURY:
            snprintf(line, sizeof(line), " - %u-run milestone",
                     (unsigned)e.arg2);
            break;
        case LORD_NEWS_RUN_ENDED:
            snprintf(line, sizeof(line), " - Run ended at wave %u",
                     (unsigned)e.arg2);
            break;
        default:
            continue;
        }
        print(line);
    }
    printSep();
}

void MonsterMeshTerminal::showLeaderboard()
{
    lordEnsureLoaded();
    uint8_t earned = 0;
    for (uint8_t i = 0; i < 8; ++i) if (lordHasBadge(lord_, i)) earned++;
    char line[64];
    snprintf(line, sizeof(line),
             "You: %u badges | best %u waves | %lu xp",
             (unsigned)earned,
             (unsigned)lord_.bestRunWaves,
             (unsigned long)lord_.bestRunXp);
    print("Local leaderboard:");
    print(line);
    printSep();
}
