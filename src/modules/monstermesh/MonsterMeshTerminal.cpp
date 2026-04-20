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
#include <esp_heap_caps.h>
#include <esp_timer.h>

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

    if (savParty_.count > 0) {
        print("Ready. Type 'help'.");
    } else {
        print("Loading party from SAV...");
        needsLoad_ = true;
    }
}

void MonsterMeshTerminal::loadParty(const Gen1Party &party)
{
    memcpy(&savParty_, &party, sizeof(Gen1Party));
    needsLoad_ = false;
    if (state_ == State::IDLE) state_ = State::READY;

    if (ready()) {
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

    // Force layout before scroll — scroll_to_view uses the label's
    // position, which is 0/0 until LVGL lays out the new child.
    lv_obj_update_layout(outputPanel_);
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

    lv_obj_update_layout(outputPanel_);
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

    // ── Y/N response to incoming text battle challenge ────────────────────────
    if (state_ == State::IN_NET_CHALLENGE_WAIT) {
        bool yes = (norm[0] == 'y');
        bool no  = (norm[0] == 'n');
        if (yes || no) {
            if (yes) {
                uint32_t seed = rand32();
                char accept[24];
                snprintf(accept, sizeof(accept), "MMT:ACCEPT:%08lX", (unsigned long)seed);
                pendingDM_ = true;
                dmTarget_  = netPartner_;
                strncpy(dmText_, accept, sizeof(dmText_));
                Gen1Party oppP = {};
                for (uint8_t i = 0; i < meshPeerCount_; i++) {
                    if (meshPeers_[i].nodeId == netPartner_) {
                        buildAsyncOpponent(meshPeers_[i], oppP);
                        break;
                    }
                }
                startNetBattle(netPartner_, seed, oppP);
                print("Challenge accepted! Battle starting...");
            } else {
                pendingDM_ = true;
                dmTarget_  = netPartner_;
                strncpy(dmText_, "MMT:REJECT", sizeof(dmText_));
                netPartner_ = 0;
                state_      = State::READY;
                print("You fled!");
            }
            return;
        }
        print("Type 'y' to accept or 'n' to decline.");
        return;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        bool inBattle = (state_ == State::IN_BATTLE   || state_ == State::IN_RUN_BATTLE ||
                         state_ == State::IN_ROGUE_BATTLE || state_ == State::IN_GYM_BATTLE ||
                         state_ == State::IN_NET_BATTLE   || state_ == State::IN_NET_BATTLE_WAIT);
        bool inRun    = (state_ == State::IN_RUN);
        bool inRogue  = (state_ == State::IN_ROGUE);
        if (inBattle) {
            print("1/W 2/E 3/R 4/S  use move");
            print("pick N          switch Pokemon");
            print("status          show HP");
            print("quit            forfeit");
        } else if (inRun || inRogue) {
            print("fight           start next wave");
            print("party           show HP");
            print("home            leave");
        } else if (state_ == State::IN_NET_CHALLENGE_WAIT) {
            print("y               accept battle");
            print("n               flee");
        } else if (state_ == State::IN_NET_CHALLENGE_SENT) {
            print("Waiting for opponent response...");
        } else {
            print("gym list        list Kanto gyms");
            print("gym go          challenge next gym");
            print("explore         daily wild route");
            print("rogue           unlimited roguelike");
            print("fight list      show nearby trainers");
            print("fight <name>    async battle");
            print("mmt <name>      live PvP challenge (DM)");
            print("mml <name>      cable club link (DM)");
            print("stats / badges / news");
            print("party           view your Pokemon");
        }
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

    if (strcmp(cmd, "rogue") == 0) {
        if (savParty_.count == 0) { print("Load a Pokemon save first."); return; }
        if (state_ == State::IN_BATTLE || state_ == State::IN_RUN_BATTLE ||
            state_ == State::IN_ROGUE_BATTLE || state_ == State::IN_GYM_BATTLE) {
            print("Finish your current battle first.");
            return;
        }
        startRogue();
        return;
    }

    if (strcmp(cmd, "home") == 0) {
        if (state_ == State::IN_RUN || state_ == State::IN_RUN_BATTLE) {
            if (runWildOnly_) {
                lordEnsureLoaded();
                lordOnRunEnd(lord_, currentRun_);
                lordSave(lord_);
                runWildOnly_ = false;
            }
            runActive_ = false;
            state_ = State::READY;
            print("You head home to rest.");
            printSep();
        } else if (state_ == State::IN_ROGUE || state_ == State::IN_ROGUE_BATTLE) {
            rogueActive_ = false;
            state_ = State::READY;
            char msg[48];
            snprintf(msg, sizeof(msg), "Rogue run ended at wave %u.", (unsigned)rogueWave_);
            print(msg);
            printSep();
        } else {
            print("You're already in town.");
        }
        return;
    }

    if (strcmp(cmd, "stats") == 0)       { showStats();       return; }
    if (strcmp(cmd, "badges") == 0)      { showBadges();      return; }
    if (strcmp(cmd, "news") == 0)        { showNews();        return; }
    if (strcmp(cmd, "leaderboard") == 0) { showLeaderboard(); return; }

    if (strcmp(cmd, "sysinfo") == 0) {
        char buf[64];
        uint32_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t freePsram    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        uint32_t totPsram     = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        uint32_t upSec        = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        uint32_t h = upSec / 3600, m = (upSec % 3600) / 60, s = upSec % 60;
        print("-- System --");
        snprintf(buf, sizeof(buf), "RAM free:  %u KB", freeInternal/1024);
        print(buf);
        snprintf(buf, sizeof(buf), "PSRAM free: %u/%u KB", freePsram/1024, totPsram/1024);
        print(buf);
        snprintf(buf, sizeof(buf), "Up: %uh%um%us  Peers: %u", h, m, s, (unsigned)meshPeerCount_);
        print(buf);
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

    // ── mmt <name> — targeted live PvP text battle challenge ────────────────
    if (strncmp(cmd, "mmt ", 4) == 0) {
        const char *name = cmd + 4;
        if (state_ == State::IN_NET_CHALLENGE_SENT) {
            print("Already waiting for a response. Stand by...");
            return;
        }
        if (state_ != State::READY) {
            const char *what =
                (state_ == State::IN_NET_CHALLENGE_WAIT) ? "pending challenge - type 'y' or 'n'" :
                (state_ == State::IN_NET_BATTLE || state_ == State::IN_NET_BATTLE_WAIT) ? "in a live battle (type 'cancel')" :
                (state_ == State::IN_BATTLE)      ? "in battle (type 'quit')" :
                (state_ == State::IN_RUN || state_ == State::IN_RUN_BATTLE)   ? "on a route (type 'home')" :
                (state_ == State::IN_ROGUE || state_ == State::IN_ROGUE_BATTLE) ? "in rogue mode (type 'quit')" :
                (state_ == State::IN_GYM_SELECT || state_ == State::IN_GYM_BATTLE) ? "at a gym (type 'quit')" :
                "busy";
            char msg[64];
            snprintf(msg, sizeof(msg), "Still %s.", what);
            print(msg);
            return;
        }
        if (savParty_.count == 0) { print("Load a save first."); return; }
        // Find target peer by short name
        uint32_t targetId = 0;
        for (uint8_t i = 0; i < meshPeerCount_; i++) {
            if (strncasecmp(meshPeers_[i].shortName, name, 4) == 0 ||
                strncasecmp(meshPeers_[i].gameName,  name, 7) == 0) {
                targetId = meshPeers_[i].nodeId;
                break;
            }
        }
        if (targetId == 0) {
            if (meshPeerCount_ == 0) print("No trainers in range.");
            else { char msg[48]; snprintf(msg, sizeof(msg), "Trainer '%s' not in range.", name); print(msg); }
            return;
        }
        pendingNetChallenge_ = true;
        pendingNetChallengeTarget_ = targetId;
        netPartner_ = 0;
        state_ = State::IN_NET_CHALLENGE_SENT;
        char msg[32];
        snprintf(msg, sizeof(msg), "Challenge sent to %.6s!", name);
        print(msg);
        print("Waiting for response...");
        return;
    }

    // ── mml <name> — cable club link invite ─────────────────────────────────
    if (strncmp(cmd, "mml ", 4) == 0) {
        const char *name = cmd + 4;
        uint32_t targetId = 0;
        for (uint8_t i = 0; i < meshPeerCount_; i++) {
            if (strncasecmp(meshPeers_[i].shortName, name, 4) == 0 ||
                strncasecmp(meshPeers_[i].gameName,  name, 7) == 0) {
                targetId = meshPeers_[i].nodeId;
                break;
            }
        }
        if (targetId == 0) {
            if (meshPeerCount_ == 0) print("No trainers in range.");
            else { char msg[48]; snprintf(msg, sizeof(msg), "Trainer '%s' not in range.", name); print(msg); }
            return;
        }
        // Queue DM to target: human-readable + protocol trigger
        pendingDM_ = true;
        dmTarget_  = targetId;
        const char *me = localShortName_[0] ? localShortName_ : "???";
        snprintf(dmText_, sizeof(dmText_), "[%.4s] invites Cable Club! Go to Cable Club. mmc on", me);
        char msg[32];
        snprintf(msg, sizeof(msg), "Cable Club invite sent to %.6s.", name);
        print(msg);
        return;
    }

    // ── fight list — show nearby trainers ────────────────────────────────────
    if (strcmp(cmd, "fight list") == 0) {
        if (meshPeerCount_ == 0) { print("No trainers nearby."); return; }
        print("Nearby trainers:");
        for (uint8_t i = 0; i < meshPeerCount_; i++) {
            const DaycareNeighborPokemon &p = meshPeers_[i];
            char line[48];
            snprintf(line, sizeof(line), "  %s  (%s L%u)",
                     p.shortName, p.nickname[0] ? p.nickname : "???", p.level);
            print(line);
        }
        return;
    }

    // ── fight <name> — async battle vs neighbor beacon party ─────────────────
    if (strncmp(cmd, "fight ", 6) == 0) {
        if (savParty_.count == 0) { print("Load a save first."); return; }
        if (state_ != State::READY) {
            const char *what =
                (state_ == State::IN_NET_CHALLENGE_SENT) ? "waiting for challenge response (type 'cancel')" :
                (state_ == State::IN_NET_CHALLENGE_WAIT) ? "pending challenge - type 'y' or 'n'" :
                (state_ == State::IN_NET_BATTLE || state_ == State::IN_NET_BATTLE_WAIT) ? "in a live battle (type 'cancel')" :
                (state_ == State::IN_BATTLE)      ? "in battle (type 'quit')" :
                (state_ == State::IN_RUN || state_ == State::IN_RUN_BATTLE)   ? "on a route (type 'home')" :
                (state_ == State::IN_ROGUE || state_ == State::IN_ROGUE_BATTLE) ? "in rogue mode (type 'quit')" :
                (state_ == State::IN_GYM_SELECT || state_ == State::IN_GYM_BATTLE) ? "at a gym (type 'quit')" :
                "busy";
            char msg[64];
            snprintf(msg, sizeof(msg), "Still %s.", what);
            print(msg);
            return;
        }
        const char *name = cmd + 6;
        bool found = false;
        for (uint8_t i = 0; i < meshPeerCount_; i++) {
            const DaycareNeighborPokemon &p = meshPeers_[i];
            if (strncasecmp(p.shortName, name, 4) == 0 ||
                strncasecmp(p.gameName,  name, 7) == 0 ||
                strncasecmp(p.nickname,  name, 10) == 0) {
                buildAsyncOpponent(p, oppParty_);
                asyncOpponentNodeId_ = p.nodeId;
                strncpy(asyncOpponentName_, p.shortName, sizeof(asyncOpponentName_) - 1);

                // Heal a copy of our party for the fight
                Gen1Party myParty = savParty_;
                for (uint8_t j = 0; j < myParty.count; j++) {
                    setBe16(myParty.mons[j].hp, be16(myParty.mons[j].maxHp));
                    myParty.mons[j].status = 0;
                    for (int k = 0; k < 4; k++) {
                        const Gen1MoveData *mv = gen1Move(myParty.mons[j].moves[k]);
                        if (mv) myParty.mons[j].pp[k] = mv->pp;
                    }
                }

                engine_.start(myParty, oppParty_, rand32(), 1);
                char msg[48];
                snprintf(msg, sizeof(msg), "Challenging %s!", p.shortName);
                print(msg);
                describeBattleStatus();
                print("Use 1..4 to attack.");
                state_ = State::IN_BATTLE;
                found = true;
                break;
            }
        }
        if (!found) {
            if (meshPeerCount_ == 0)
                print("No mesh trainers nearby.");
            else {
                char msg[48];
                snprintf(msg, sizeof(msg), "Trainer '%s' not found nearby.", name);
                print(msg);
            }
        }
        return;
    }

    if (strcmp(cmd, "fight") == 0) {
        if (state_ == State::IN_RUN) {
            startRunWave();
            return;
        }
        if (state_ == State::IN_ROGUE) {
            startRogueWave();
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

    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "cancel") == 0) {
        if (state_ == State::IN_BATTLE || state_ == State::IN_RUN_BATTLE ||
            state_ == State::IN_ROGUE_BATTLE) {
            runActive_ = false;
            rogueActive_ = false;
            state_ = State::READY;
            print("Battle forfeited.");
            printSep();
        } else if (state_ == State::IN_RUN) {
            runActive_ = false;
            state_ = State::READY;
            print("Run abandoned.");
            printSep();
        } else if (state_ == State::IN_ROGUE) {
            rogueActive_ = false;
            state_ = State::READY;
            print("Rogue run abandoned.");
            printSep();
        } else if (state_ == State::IN_GYM_BATTLE || state_ == State::IN_GYM_SELECT) {
            state_ = State::READY;
            currentGymIdx_ = 0xFF;
            print("Gym challenge abandoned.");
            printSep();
        } else if (state_ == State::IN_NET_CHALLENGE_SENT) {
            state_ = State::READY;
            netPartner_ = 0;
            pendingNetChallenge_ = false;
            print("Challenge cancelled.");
        } else if (state_ == State::IN_NET_CHALLENGE_WAIT) {
            pendingDM_ = true; dmTarget_ = netPartner_;
            strncpy(dmText_, "MMT:REJECT", sizeof(dmText_));
            state_ = State::READY;
            netPartner_ = 0;
            print("You fled.");
        } else if (state_ == State::IN_NET_BATTLE || state_ == State::IN_NET_BATTLE_WAIT) {
            state_ = State::READY;
            netPartner_ = 0;
            print("Net battle ended.");
        } else {
            print("Nothing to cancel.");
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
        if (state_ == State::IN_NET_BATTLE) {
            // Live PvP — store action, transition to wait
            const auto &mine = engine_.party(0);
            const auto &m = mine.mons[mine.active];
            if (m.moves[slot] == 0) { print("Empty move slot."); return; }
            if (m.pp[slot] == 0)    { print("No PP left."); return; }
            netMyAction_ = 0;
            netMyIndex_  = slot;
            if (netActionReady_) {
                resolveNetTurn();
            } else {
                state_ = State::IN_NET_BATTLE_WAIT;
                print("Move chosen. Waiting for opponent...");
            }
            return;
        }
        if (state_ != State::IN_BATTLE && state_ != State::IN_RUN_BATTLE &&
            state_ != State::IN_ROGUE_BATTLE && state_ != State::IN_GYM_BATTLE) {
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

    // In run/rogue, show live HP from the active party, not the SAV party.
    bool inRun   = (state_ == State::IN_RUN   || state_ == State::IN_RUN_BATTLE);
    bool inRogue = (state_ == State::IN_ROGUE || state_ == State::IN_ROGUE_BATTLE);
    const Gen1Party &src = inRun ? runParty_ : (inRogue ? rogueParty_ : savParty_);

    print(inRun ? "Run party:" : (inRogue ? "Rogue party:" : "Your party:"));
    char buf[48];
    for (uint8_t i = 0; i < src.count; ++i) {
        const Gen1Pokemon &p = src.mons[i];
        uint8_t lvl = p.level ? p.level : p.boxLevel;
        const char *marker = (!inRun && !inRogue && i == chosenSlot_) ? "*" : "";
        snprintf(buf, sizeof(buf), " %u) %.10s L%u %u/%u HP %s",
                 (unsigned)(i + 1),
                 (const char *)src.nicknames[i],
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
        state_ == State::IN_ROGUE_BATTLE || state_ == State::IN_GYM_BATTLE) {
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

    if (state_ == State::IN_ROGUE_BATTLE) {
        if (res == Gen1BattleEngine::Result::P1_WIN) {
            syncRoguePartyHpFromEngine();
            char msg[48];
            bool isBoss = (rogueWave_ % 5 == 0);
            snprintf(msg, sizeof(msg), "%s %u cleared!", isBoss ? "Boss wave" : "Wave", (unsigned)rogueWave_);
            print(msg);
            uint8_t alive = 0;
            for (uint8_t i = 0; i < rogueParty_.count; i++)
                if (be16(rogueParty_.mons[i].hp) > 0) alive++;
            snprintf(msg, sizeof(msg), "%u Pokemon still standing.", (unsigned)alive);
            print(msg);
            print("Type 'fight' for next wave or 'home' to leave.");
            state_ = State::IN_ROGUE;
        } else {
            char msg[48];
            snprintf(msg, sizeof(msg), "All fainted. Rogue run ends at wave %u.", (unsigned)rogueWave_);
            print(msg);
            rogueActive_ = false;
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
        if (asyncOpponentNodeId_ != 0) queueResultDM(true);
    } else {
        print("You lost!");
        if (asyncOpponentNodeId_ != 0) queueResultDM(false);
    }
    asyncOpponentNodeId_ = 0;
    asyncOpponentName_[0] = '\0';
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
        snprintf(buf, sizeof(buf), " %u/%c) %-12s PP %u",
                 (unsigned)(i + 1), MOVE_KEYS[i], mv ? mv->name : "?", (unsigned)m.pp[i]);
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

// ── Rogue campaign ───────────────────────────────────────────────────────────

void MonsterMeshTerminal::startRogue()
{
    // Heal full party to max HP
    memcpy(&rogueParty_, &savParty_, sizeof(Gen1Party));
    for (uint8_t i = 0; i < rogueParty_.count; i++) {
        Gen1Pokemon &p = rogueParty_.mons[i];
        setBe16(p.hp, be16(p.maxHp));
        p.status = 0;
        for (int j = 0; j < 4; j++) {
            const Gen1MoveData *mv = gen1Move(p.moves[j]);
            if (mv) p.pp[j] = mv->pp;
        }
    }
    rogueActive_ = true;
    rogueWave_   = 0;
    print("Rogue run started! Party healed.");
    print("Type 'fight' to begin. 'home' to flee.");
    printSep();
    state_ = State::IN_ROGUE;
}

void MonsterMeshTerminal::startRogueWave()
{
    if (!rogueActive_ || rogueParty_.count == 0) { print("No rogue run active."); return; }

    uint8_t alive = 0;
    for (uint8_t i = 0; i < rogueParty_.count; i++)
        if (be16(rogueParty_.mons[i].hp) > 0) alive++;
    if (alive == 0) {
        print("All Pokemon fainted. Rogue run over.");
        rogueActive_ = false;
        state_ = State::READY;
        return;
    }

    rogueWave_++;
    bool isBoss = (rogueWave_ % 5 == 0);

    // Level scales: base 10 + wave * 4, capped 99
    uint8_t oppLvl = (uint8_t)(10 + rogueWave_ * 4);
    if (oppLvl > 99) oppLvl = 99;

    if (isBoss) {
        // Boss: fixed species from ROGUE_BOSS array, +5 levels
        uint8_t bossIdx = ((rogueWave_ / 5) - 1) % 5;
        uint8_t bossSpecies = ROGUE_BOSS[bossIdx];
        uint8_t moves[4] = {};
        pickMovesForSpecies(bossSpecies, moves);
        oppParty_ = {};
        writeBattlePokeToSave(oppParty_, 0, bossSpecies, (uint8_t)(oppLvl + 5 > 99 ? 99 : oppLvl + 5),
                              moves, Gen1BattleEngine::BattlePoke{}, 0x88, "BOSS");
        oppParty_.count = 1;
    } else {
        // Regular: pick from the themed pool for this floor (theme cycles every 5 waves)
        uint8_t theme = (rogueWave_ / 5) % 5;
        const RoguePool &pool = ROGUE_POOLS[theme];
        uint8_t species = pool.data[rand32() % pool.len];
        uint8_t moves[4] = {};
        pickMovesForSpecies(species, moves);
        oppParty_ = {};
        writeBattlePokeToSave(oppParty_, 0, species, oppLvl, moves,
                              Gen1BattleEngine::BattlePoke{}, 0x88, "FOE");
        oppParty_.count = 1;
    }

    engine_.start(rogueParty_, oppParty_, rand32(), 1);

    char msg[48];
    static const char *THEME_NAMES[5] = { "Route","Water","Fire","Psychic","Dragon" };
    uint8_t theme = (rogueWave_ / 5) % 5;
    if (isBoss) {
        snprintf(msg, sizeof(msg), "-- BOSS Wave %u --", (unsigned)rogueWave_);
    } else {
        snprintf(msg, sizeof(msg), "-- Wave %u (%s) --", (unsigned)rogueWave_, THEME_NAMES[theme]);
    }
    print(msg);
    describeBattleStatus();
    print("Use 1..4 to attack.");
    state_ = State::IN_ROGUE_BATTLE;
}

void MonsterMeshTerminal::syncRoguePartyHpFromEngine()
{
    const auto &ep = engine_.party(0);
    for (uint8_t i = 0; i < rogueParty_.count && i < ep.count; i++) {
        setBe16(rogueParty_.mons[i].hp, ep.mons[i].hp);
        rogueParty_.mons[i].status = ep.mons[i].status;
        for (int j = 0; j < 4; j++) rogueParty_.mons[i].pp[j] = ep.mons[i].pp[j];
    }
}

// ── Async / PvP helpers ──────────────────────────────────────────────────────

void MonsterMeshTerminal::buildAsyncOpponent(const DaycareNeighborPokemon &peer,
                                              Gen1Party &out)
{
    memset(&out, 0, sizeof(out));
    uint8_t n = peer.partyCount < 6 ? peer.partyCount : 6;
    if (n == 0) {
        // Fallback: single Pokemon from speciesDex/level fields
        uint8_t moves[4] = {};
        pickMovesForSpecies(peer.speciesDex, moves);
        Gen1BattleEngine::BattlePoke dummy{};
        writeBattlePokeToSave(out, 0, peer.speciesDex, peer.level,
                              moves, dummy, 0x88, peer.nickname);
        out.count = 1;
        return;
    }
    for (uint8_t i = 0; i < n; i++) {
        uint8_t moves[4] = {};
        bool hasRealMoves = peer.party[i].moves[0] != 0 || peer.party[i].moves[1] != 0 ||
                            peer.party[i].moves[2] != 0 || peer.party[i].moves[3] != 0;
        if (hasRealMoves) {
            for (int m = 0; m < 4; m++) moves[m] = peer.party[i].moves[m];
        } else {
            pickMovesForSpecies(peer.party[i].species, moves);
        }
        Gen1BattleEngine::BattlePoke dummy{};
        writeBattlePokeToSave(out, i, peer.party[i].species, peer.party[i].level,
                              moves, dummy, 0x88, peer.party[i].nickname);
    }
    out.count = n;
}

void MonsterMeshTerminal::queueResultDM(bool playerWon)
{
    uint32_t target = (asyncOpponentNodeId_ != 0) ? asyncOpponentNodeId_ : netPartner_;
    if (target == 0) return;
    pendingDM_ = true;
    dmTarget_  = target;
    const char *me = localShortName_[0] ? localShortName_ : "???";
    if (asyncOpponentNodeId_ != 0) {
        snprintf(dmText_, sizeof(dmText_), "[%s] %s your team in a text battle!",
                 me, playerWon ? "defeated" : "lost to");
    } else {
        snprintf(dmText_, sizeof(dmText_), "[%s] text battle: %s",
                 me, playerWon ? "GG, you lost!" : "GG, you won!");
    }
}

void MonsterMeshTerminal::startNetBattle(uint32_t partnerNodeId, uint32_t rngSeed,
                                          const Gen1Party &opponentParty)
{
    netPartner_      = partnerNodeId;
    netMyAction_     = 0xFF;
    netActionReady_  = false;
    oppParty_        = opponentParty;

    // Healed copy of full party for the player
    Gen1Party myParty = savParty_;
    for (uint8_t i = 0; i < myParty.count; i++) {
        setBe16(myParty.mons[i].hp, be16(myParty.mons[i].maxHp));
        myParty.mons[i].status = 0;
        for (int j = 0; j < 4; j++) {
            const Gen1MoveData *mv = gen1Move(myParty.mons[i].moves[j]);
            if (mv) myParty.mons[i].pp[j] = mv->pp;
        }
    }
    engine_.start(myParty, oppParty_, rngSeed, 1);
    print("Live battle started!");
    describeBattleStatus();
    print("Use 1..4 to attack.");
    state_ = State::IN_NET_BATTLE;
}

void MonsterMeshTerminal::receiveNetAction(uint8_t actionType, uint8_t index)
{
    netOppAction_ = actionType;
    netOppIndex_  = index;
    if (state_ == State::IN_NET_BATTLE_WAIT) {
        // We already submitted — resolve now
        resolveNetTurn();
    } else {
        // Opponent was faster — store and resolve when player submits
        netActionReady_ = true;
        if (state_ == State::IN_NET_BATTLE)
            print("Opponent chose their move. Pick yours!");
    }
}

void MonsterMeshTerminal::resolveNetTurn()
{
    engine_.submitAction(0, netMyAction_, netMyIndex_);
    engine_.submitAction(1, netOppAction_, netOppIndex_);
    engine_.executeTurn(&MonsterMeshTerminal::engineLogCb, this);
    engine_.autoReplaceIfFainted(0, &MonsterMeshTerminal::engineLogCb, this);
    engine_.autoReplaceIfFainted(1, &MonsterMeshTerminal::engineLogCb, this);

    netMyAction_    = 0xFF;
    netActionReady_ = false;

    auto res = engine_.result();
    if (res == Gen1BattleEngine::Result::ONGOING) {
        describeBattleStatus();
        state_ = State::IN_NET_BATTLE;
        return;
    }

    printSep();
    if (res == Gen1BattleEngine::Result::P1_WIN) {
        print("You won the live battle!");
        queueResultDM(true);
    } else {
        print("You lost the live battle!");
        queueResultDM(false);
    }
    netPartner_ = 0;
    state_      = State::READY;
    printSep();
}

void MonsterMeshTerminal::receiveNetChallenge(uint32_t fromNodeId, const char *shortName)
{
    // Receiver flow: the challenge arrives as a DM to the user; we track state
    // here so the DM Y/N reply can be routed back through the same accept/
    // reject path, but we do NOT print to the terminal so the user isn't
    // interrupted mid-task. If they do open the terminal while a challenge is
    // pending, the existing IN_NET_CHALLENGE_WAIT state gates the y/n command.
    (void)shortName;
    if (state_ == State::READY && savParty_.count > 0) {
        netPartner_ = fromNodeId;
        state_      = State::IN_NET_CHALLENGE_WAIT;
    }
}

void MonsterMeshTerminal::receiveNetReject(uint32_t /*fromNodeId*/)
{
    if (state_ == State::IN_NET_CHALLENGE_SENT) {
        state_      = State::READY;
        netPartner_ = 0;
        print("The trainer fled!");
    }
}

void MonsterMeshTerminal::respondToNetChallenge(bool accept)
{
    if (state_ != State::IN_NET_CHALLENGE_WAIT) return;
    if (accept) {
        uint32_t seed = rand32();
        char acceptMsg[24];
        snprintf(acceptMsg, sizeof(acceptMsg), "MMT:ACCEPT:%08lX", (unsigned long)seed);
        pendingDM_ = true;
        dmTarget_  = netPartner_;
        strncpy(dmText_, acceptMsg, sizeof(dmText_));
        Gen1Party oppP = {};
        for (uint8_t i = 0; i < meshPeerCount_; i++) {
            if (meshPeers_[i].nodeId == netPartner_) {
                buildAsyncOpponent(meshPeers_[i], oppP);
                break;
            }
        }
        startNetBattle(netPartner_, seed, oppP);
        print("Challenge accepted! Battle starting...");
    } else {
        pendingDM_ = true;
        dmTarget_  = netPartner_;
        strncpy(dmText_, "MMT:REJECT", sizeof(dmText_));
        netPartner_ = 0;
        state_      = State::READY;
    }
}

void MonsterMeshTerminal::receiveNetAccept(uint32_t fromNodeId, uint32_t seed,
                                            const Gen1Party &oppParty)
{
    startNetBattle(fromNodeId, seed, oppParty);
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
