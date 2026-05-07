// SPDX-License-Identifier: MIT
// See MonsterMeshTerminal.h.

#include "MonsterMeshTerminal.h"
#include "Gen1Species.h"
#include "LordLogic.h"
#include "LordGyms.h"
#include "LordRoutes.h"
#include "LordE4.h"
#include "configuration.h"
#include "gps/RTC.h"
#include <time.h>

#if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH && HAS_TFT

#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include "graphics/view/TFT/Themes.h"

LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_cozette_13;

// LVGL event callbacks need C-style functions; we keep an instance pointer
// so they can dispatch to the right MonsterMeshTerminal.
static MonsterMeshTerminal *g_termInstance = nullptr;

static void term_input_ready_cb(lv_event_t *e)
{
    if (!g_termInstance) return;
    lv_obj_t *ta = lv_event_get_target_obj(e);
    if (!ta) return;
    const char *txt = lv_textarea_get_text(ta);
    g_termInstance->onSubmit(txt);
    lv_textarea_set_text(ta, "");
}

void MonsterMeshTerminal::open(lv_obj_t *parent)
{
    if (panel_) {
        // Already built — re-entry path. Preserve the scrollback so the user
        // can read prior output. Just unhide, reset the input field, refocus.
        lv_obj_clear_flag(panel_, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(input_, "");
        inbuf_[0] = '\0'; inlen_ = 0;
        open_ = true;
        lv_group_focus_obj(input_);
        return;
    }

    panel_ = lv_obj_create(parent);
    lv_obj_set_size(panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_align(panel_, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(panel_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(panel_, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel_, lv_color_hex(Themes::darkest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(panel_, lv_color_hex(Themes::lightest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(panel_, &lv_font_cozette_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(panel_, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(panel_, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(panel_, LV_SCROLLBAR_MODE_OFF);

    output_ = lv_obj_create(panel_);
    lv_obj_set_size(output_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(output_, 1);
    lv_obj_set_scrollbar_mode(output_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(output_, LV_DIR_VER);
    lv_obj_set_style_bg_color(output_, lv_color_hex(Themes::darkest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(output_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(output_, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(output_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(output_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(output_, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(output_, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);

    input_ = lv_textarea_create(panel_);
    lv_obj_set_size(input_, LV_PCT(100), 22);
    lv_textarea_set_one_line(input_, true);
    lv_textarea_set_placeholder_text(input_, "type and press enter");
    lv_obj_set_style_bg_color(input_, lv_color_hex(Themes::dark()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(input_, lv_color_hex(Themes::lightest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(input_, &lv_font_cozette_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(input_, lv_color_hex(Themes::accent()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(input_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(input_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(input_, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Make the typing caret stand out — solid accent-color block instead
    // of LVGL's default thin underline. The character it covers gets
    // inverted to the dark bg color so it stays legible.
    lv_obj_set_style_bg_color(input_, lv_color_hex(Themes::accent()),  LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(input_,   LV_OPA_COVER,                    LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(input_, lv_color_hex(Themes::darkest()), LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(input_, 0,                            LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_set_style_anim_duration(input_, 400,                         LV_PART_CURSOR | LV_STATE_DEFAULT);

    open_ = true;
    g_termInstance = this;
    lv_obj_add_event_cb(input_, term_input_ready_cb, LV_EVENT_READY, nullptr);
    lv_group_focus_obj(input_);

    // Legend of Charizard — load saved badge state once per session and
    // apply the daily-reset gate so explore-run quotas roll over at the
    // user's local-clock 9am boundary.
    if (!lordLoaded_) {
        if (!lordLoad(lord_)) lordInitDefaults(lord_);
        lordLoaded_ = true;
    }
    lordSetCurrentNgPlusTier(lord_.ngPlusTier);
    {
        uint32_t epoch = getValidTime(RTCQualityFromNet, true);
        if (epoch > 0) lordApplyDailyReset(lord_, epoch, /*tzOffsetHours=*/-4);
    }
    // First-time open per session: greet, then show the party listing if
    // it's already loaded; otherwise just prompt and let refreshParty()
    // append the listing when the deferred SAV load completes. Re-entries
    // skip this entirely so the user keeps their scrollback (handled by the
    // early-return above).
    println("Type 'help' for main menu");
    if (partyLoaded_) {
        showParty();
    }
    prompt();
}

void MonsterMeshTerminal::onSubmit(const char *line)
{
    if (!line) line = "";
    println(line);
    executeLine(line);
    inbuf_[0] = '\0';
    inlen_    = 0;
    prompt();
}

void MonsterMeshTerminal::close()
{
    if (panel_) lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    open_ = false;
}

void MonsterMeshTerminal::onKey(uint8_t key)
{
    if (!open_) return;
    if (key == 0x0D || key == '\n') {
        println(inbuf_);
        executeLine(inbuf_);
        inbuf_[0] = '\0'; inlen_ = 0;
        if (input_) lv_textarea_set_text(input_, "");
        prompt();
        return;
    }
    if (key == 0x08) {
        if (inlen_ > 0) {
            inlen_--;
            inbuf_[inlen_] = '\0';
            if (input_) lv_textarea_set_text(input_, inbuf_);
        }
        return;
    }
    if (key < 32 || key > 126) return;
    if (inlen_ + 1 >= sizeof(inbuf_)) return;
    inbuf_[inlen_++] = (char)key;
    inbuf_[inlen_] = '\0';
    if (input_) lv_textarea_set_text(input_, inbuf_);
}

void MonsterMeshTerminal::setParty(const Gen1Party &p)
{
    party_ = p;
    partyLoaded_ = (p.count > 0 && p.count <= 6);
}

void MonsterMeshTerminal::refocus()
{
    if (input_) lv_group_focus_obj(input_);
}

void MonsterMeshTerminal::printLine(const char *s)
{
    println(s);
}

// Cube-root for exp → level using medium-fast curve (level^3 = exp).
// Picks the largest level whose level^3 fits within `exp`. Returns 1..100.
static uint8_t levelFromExpMediumFast(uint32_t exp)
{
    uint32_t lvl = 1;
    while (lvl < 100) {
        uint32_t cube = (lvl + 1) * (lvl + 1) * (lvl + 1);
        if (cube > exp) break;
        ++lvl;
    }
    return (uint8_t)lvl;
}

void MonsterMeshTerminal::creditBattleXpPerSlot(const uint32_t xp[6])
{
    if (!partyLoaded_ || party_.count == 0) return;
    char buf[80];
    for (uint8_t i = 0; i < party_.count && i < 6; ++i) {
        uint32_t add = xp[i];
        if (add == 0) continue;
        Gen1Pokemon &p = party_.mons[i];
        uint32_t exp = ((uint32_t)p.exp[0] << 16) |
                       ((uint32_t)p.exp[1] << 8)  |
                       (uint32_t)p.exp[2];
        uint32_t newExp = exp + add;
        if (newExp > 1000000u) newExp = 1000000u;
        p.exp[0] = (newExp >> 16) & 0xFF;
        p.exp[1] = (newExp >> 8)  & 0xFF;
        p.exp[2] =  newExp        & 0xFF;
        uint8_t newLevel = levelFromExpMediumFast(newExp);
        if (newLevel > p.level) {
            snprintf(buf, sizeof(buf), "%.10s grew to level %u!",
                     (const char *)party_.nicknames[i], (unsigned)newLevel);
            println(buf);
            p.level    = newLevel;
            p.boxLevel = newLevel;
        }
    }
}

void MonsterMeshTerminal::refreshParty()
{
    // Append the party listing to the existing scrollback rather than
    // clearing — runOnce stages the party asynchronously after the panel is
    // already painted, and the user wants to keep prior output visible.
    if (!output_) return;
    showParty();
    prompt();
}

void MonsterMeshTerminal::println(const char *s)
{
    if (!output_) return;
    lv_obj_t *lbl = lv_label_create(output_);
    lv_label_set_text(lbl, s ? s : "");
    lv_obj_set_style_text_color(lbl, lv_color_hex(Themes::lightest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_cozette_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Wrap long lines (e.g. daycare event messages up to ~200 chars) to the
    // output container's width instead of overflowing horizontally.
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    // Auto-scroll to bottom by scrolling to the new label.
    lv_obj_scroll_to_view(lbl, LV_ANIM_OFF);
}

void MonsterMeshTerminal::prompt()
{
    // No-op — the user types in the textarea at the bottom of the panel, so
    // a "> _" label in the scrollback was just visual noise.
}

void MonsterMeshTerminal::showParty()
{
    if (!partyLoaded_ || party_.count == 0) {
        println("no party loaded.");
        println("open a ROM in the boot loader first.");
        return;
    }
    char buf[64];
    // Prefix the listing with a wall-clock timestamp so the user can tell at
    // a glance when this snapshot was taken. Falls back to seconds-since-boot
    // if the RTC hasn't been set yet.
    uint32_t epoch = getValidTime(RTCQualityFromNet, true);
    if (epoch > 0) {
        time_t tt = (time_t)epoch;
        struct tm tm_;
        gmtime_r(&tt, &tm_);
        snprintf(buf, sizeof(buf), "[%02d/%02d %02d:%02d] Party (%u):",
                 tm_.tm_mday, tm_.tm_mon + 1,
                 tm_.tm_hour, tm_.tm_min, (unsigned)party_.count);
    } else {
        snprintf(buf, sizeof(buf), "[t+%um] Party (%u):",
                 (unsigned)(millis() / 60000), (unsigned)party_.count);
    }
    println(buf);
    for (uint8_t i = 0; i < party_.count && i < 6; i++) {
        char nick[12] = {};
        gen1NameToAscii(party_.nicknames[i], 11, nick, sizeof(nick));
        // Gen 1 stores HP and maxHP as big-endian 2-byte values in the SAV.
        const Gen1Pokemon &m = party_.mons[i];
        uint16_t hp    = ((uint16_t)m.hp[0]    << 8) | m.hp[1];
        uint16_t maxHp = ((uint16_t)m.maxHp[0] << 8) | m.maxHp[1];
        // Cozette is monospace — pad to fixed widths so columns line up:
        //   "1. NICKNAME    Lv 50  120/200"
        snprintf(buf, sizeof(buf), "%u. %-10s Lv %3u  %3u/%3u",
                 (unsigned)(i + 1),
                 nick[0] ? nick : "(noname)",
                 (unsigned)m.level,
                 (unsigned)hp,
                 (unsigned)maxHp);
        println(buf);
    }
}

void MonsterMeshTerminal::onGymBattleEnded(uint8_t gymIdx,
                                           uint8_t trainerIdx,
                                           bool playerWon)
{
    if (gymIdx >= 8) return;
    if (!playerWon) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Gym %u: defeated by %s.", (unsigned)(gymIdx + 1),
                 lordGym(gymIdx) ? lordGym(gymIdx)->trainers[trainerIdx].name
                                 : "trainer");
        println(buf);
        // Don't advance progress on loss — the user has to retry the same trainer.
        return;
    }
    // Win: advance progress. trainerIdx 0..3 = grunt; 4 = leader.
    if (trainerIdx < LORD_GYM_LEADER_INDEX) {
        if (lord_.gymProgress[gymIdx] <= trainerIdx) {
            lord_.gymProgress[gymIdx] = trainerIdx + 1;
        }
        char buf[64];
        const LordGym *g = lordGym(gymIdx);
        snprintf(buf, sizeof(buf), "Beat %s! Next: %s",
                 g ? g->trainers[trainerIdx].name : "trainer",
                 g ? g->trainers[trainerIdx + 1].name : "next");
        println(buf);
        lordSave(lord_);
    } else {
        // Leader cleared — award badge, record the NG+ tier this clear
        // happened at, and persist. gymTierCleared lets the gym listing
        // distinguish "beaten at the current tier" from "beaten at a lower
        // tier and still owed at this NG+".
        lordOnGymCleared(lord_, gymIdx);
        if (gymIdx < 8) lord_.gymTierCleared[gymIdx] = lord_.ngPlusTier;
        lordSave(lord_);
        char buf[64];
        const LordGym *g = lordGym(gymIdx);
        if (lord_.ngPlusTier > 0) {
            snprintf(buf, sizeof(buf),
                     "Cleared %s at NG+%u.",
                     g ? g->leaderName : "?", (unsigned)lord_.ngPlusTier);
        } else {
            snprintf(buf, sizeof(buf), "Earned the %s Badge!",
                     g ? g->badgeName : "?");
        }
        println(buf);
    }
    // XP is credited per-faint by the engine + module pump, not here.
    refreshParty();
}

void MonsterMeshTerminal::clearOutput()
{
    if (!output_) return;
    while (lv_obj_get_child_count(output_) > 0) {
        lv_obj_t *c = lv_obj_get_child(output_, 0);
        if (c) lv_obj_delete(c);
    }
}

void MonsterMeshTerminal::executeLine(const char *line)
{
    while (*line == ' ') line++;
    if (*line == '\0') return;

    // Pending rematch confirmation: the previous line ran `gym fight N`
    // against an already-cleared gym, so we asked y/n. Consume this line
    // as the answer regardless of what it says.
    if (pendingRematchGym_ >= 0) {
        int8_t gym = pendingRematchGym_;
        pendingRematchGym_ = -1;
        bool yes = (line[0] == 'y' || line[0] == 'Y');
        if (!yes) {
            println("Cancelled.");
            return;
        }
        if (!gymFightFn_) { println("gym fight not wired"); return; }
        if (!partyLoaded_) { println("no party loaded — load a SAV first"); return; }
        const LordGym *g = lordGym((uint8_t)gym);
        char buf[64];
        snprintf(buf, sizeof(buf), "Rematch %s of %s...",
                 g ? g->leaderName : "?", g ? g->city : "?");
        println(buf);
        // Rematch starts from trainer 0 — full 5-trainer gauntlet. Badge
        // already in lord_, so onGymBattleEnded won't double-award.
        gymFightFn_(gymFightCtx_, (uint8_t)gym, 0);
        return;
    }

    if (strncmp(line, "help", 4) == 0) {
        const char *args = line + 4;
        while (*args == ' ') ++args;
        if (strncmp(args, "sys", 3) == 0) {
            println("sys commands:");
            println("  help        - game commands");
            println("  version     - firmware build");
            println("  echo <text> - print <text>");
            println("  clear       - wipe screen");
            println("  beacon      - broadcast presence (daycare + mmt)");
            return;
        }
        println("commands:");
        println("  party       - show your loaded SAV party");
        println("  daycare     - daycare status + neighbors");
        println("  gym         - Legend of Charizard gym list");
        println("  gym fight N - challenge gym N (1-9)");
        println("  mmg         - discover MonsterMesh Gyms");
        println("  mmg fight N - challenge MM gym N");
        println("  fight       - local CPU battle vs neighbor");
        println("  explore     - Wild encounters nearby");
        println("  news        - LoC news ring");
        println("  achievements- list achievements earned");
        println("  help sys    - system commands");
        return;
    }
    if (strncmp(line, "daycare", 7) == 0) {
        if (!daycareStatusFn_) {
            println("daycare not wired.");
            return;
        }
        // `daycare event` forces an event cycle immediately so the user can
        // verify the event generator without waiting the full 5-min interval.
        const char *args = line + 7;
        while (*args == ' ') args++;
        if (strncmp(args, "event", 5) == 0 || strncmp(args, "force", 5) == 0) {
            if (daycareForceFn_) {
                daycareForceFn_(daycareForceCtx_);
                println("daycare: forced event");
            } else {
                println("daycare event not wired.");
                return;
            }
        }
        char buf[1024] = {};
        daycareStatusFn_(daycareStatusCtx_, buf, sizeof(buf));
        // Print line by line; daycareStatusFn fills buf with '\n'-separated text.
        const char *p = buf;
        while (*p) {
            const char *nl = p;
            while (*nl && *nl != '\n') ++nl;
            char tmp[128];
            size_t n = (size_t)(nl - p);
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, p, n);
            tmp[n] = '\0';
            println(tmp);
            p = (*nl == '\n') ? (nl + 1) : nl;
        }
        return;
    }
    if (strncmp(line, "version", 7) == 0) {
        char buf[64];
#ifdef MONSTERMESH_BUILD
#define MM_STRINGIFY_(x) #x
#define MM_STRINGIFY(x) MM_STRINGIFY_(x)
        snprintf(buf, sizeof(buf), "MonsterMesh build " MM_STRINGIFY(MONSTERMESH_BUILD));
#else
        snprintf(buf, sizeof(buf), "MonsterMesh build (unknown)");
#endif
        println(buf);
        return;
    }
    if (strncmp(line, "echo ", 5) == 0) {
        println(line + 5);
        return;
    }
    if (strcmp(line, "echo") == 0) {
        println("");
        return;
    }
    if (strncmp(line, "clear", 5) == 0) {
        clearOutput();
        return;
    }
    if (strncmp(line, "party", 5) == 0) {
        showParty();
        return;
    }
    if (strncmp(line, "achievements", 12) == 0) {
        if (!daycareAchFn_) {
            println("achievements not wired.");
            return;
        }
        char buf[1024] = {};
        daycareAchFn_(daycareAchCtx_, buf, sizeof(buf));
        const char *p = buf;
        while (*p) {
            const char *nl = p;
            while (*nl && *nl != '\n') ++nl;
            char tmp[128];
            size_t n = (size_t)(nl - p);
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, p, n);
            tmp[n] = '\0';
            println(tmp);
            p = (*nl) ? nl + 1 : nl;
        }
        return;
    }
    if (strncmp(line, "news", 4) == 0) {
        if (lord_.newsCount == 0) {
            println("No news yet. Win a badge to start.");
            return;
        }
        uint32_t now = getValidTime(RTCQualityFromNet, true);
        // Walk the ring newest -> oldest. newsHead points one past the
        // most recent entry; back up by 1..newsCount inclusive.
        for (uint8_t i = 0; i < lord_.newsCount; ++i) {
            uint8_t idx = (uint8_t)((lord_.newsHead + LORD_NEWS_CAP - 1 - i) % LORD_NEWS_CAP);
            const LordNewsEntry &e = lord_.news[idx];
            // "Nh" / "Nd" suffix when both timestamps are valid.
            char age[16] = "";
            if (now != 0 && e.ts != 0 && now >= e.ts) {
                uint32_t s = now - e.ts;
                if (s < 3600)        snprintf(age, sizeof(age), " (%um)", (unsigned)(s / 60));
                else if (s < 86400)  snprintf(age, sizeof(age), " (%uh)", (unsigned)(s / 3600));
                else                 snprintf(age, sizeof(age), " (%ud)", (unsigned)(s / 86400));
            }
            char buf[96];
            switch (e.type) {
                case LORD_NEWS_BADGE: {
                    const LordGym *g = lordGym(e.arg1);
                    snprintf(buf, sizeof(buf), "+ Badge: %s (%s)%s",
                             g ? g->leaderName : "?",
                             g ? g->city       : "?", age);
                    break;
                }
                case LORD_NEWS_BEST_RUN:
                    snprintf(buf, sizeof(buf), "* New best run: %u waves%s",
                             (unsigned)e.arg2, age);
                    break;
                case LORD_NEWS_CENTURY:
                    snprintf(buf, sizeof(buf), "= %u total runs%s",
                             (unsigned)e.arg2, age);
                    break;
                case LORD_NEWS_RUN_ENDED:
                    snprintf(buf, sizeof(buf), ". Run ended: %u waves%s",
                             (unsigned)e.arg2, age);
                    break;
                default:
                    snprintf(buf, sizeof(buf), "? type=%u arg=%u%s",
                             (unsigned)e.type, (unsigned)e.arg2, age);
                    break;
            }
            println(buf);
        }
        return;
    }
    if (strncmp(line, "gym", 3) == 0) {
        const char *args = line + 3;
        while (*args == ' ') ++args;
        // `gym fight [N]` — N is 1..9 (1-8 = Kanto gyms, 9 = Indigo Plateau
        // / Elite Four). With no number, auto-picks the lowest open +
        // uncleared entry so the user can just keep typing `gym fight` to
        // march through the league.
        if (strncmp(args, "fight", 5) == 0) {
            const char *p = args + 5;
            while (*p == ' ') ++p;
            int n = 0;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; }
            uint8_t gymIdx;
            bool isE4;
            if (n == 0) {
                // Auto-pick: first gym not yet cleared at the current NG+
                // tier. Linear unlock: gym 0 first, then 1, etc. Once all
                // 8 are cleared at this tier, jump to the Indigo Plateau.
                int auto_n = -1;
                for (uint8_t i = 0; i < 8; ++i) {
                    bool clearedAtTier = lordHasBadge(lord_, i) &&
                                         lord_.gymTierCleared[i] >= lord_.ngPlusTier;
                    if (!clearedAtTier && lordGymUnlocked(lord_, i)) {
                        auto_n = i;
                        break;
                    }
                }
                if (auto_n < 0 && lord_.badges == 0xFF && lord_.e4Progress < 5) {
                    auto_n = 8;   // Indigo Plateau
                }
                if (auto_n < 0) {
                    println("All gyms + the Elite Four cleared at this tier.");
                    return;
                }
                gymIdx = (uint8_t)auto_n;
            } else if (n >= 1 && n <= 9) {
                gymIdx = (uint8_t)(n - 1);
            } else {
                println("usage: gym fight [1-9]");
                return;
            }
            isE4 = (gymIdx == 8);

            if (!partyLoaded_) {
                println("no party loaded — load a SAV first");
                return;
            }
            if (isE4) {
                // Indigo Plateau as gym 9 — gated on all 8 badges. No
                // rematch prompt: leagueCleared is a one-shot flag, repeat
                // runs are always welcome since they restart from member 0.
                if (lord_.badges != 0xFF) {
                    char buf[80];
                    uint8_t got = (uint8_t)__builtin_popcount(lord_.badges);
                    snprintf(buf, sizeof(buf),
                             "Indigo Plateau locked — clear all 8 gyms first (you have %u/8).",
                             (unsigned)got);
                    println(buf);
                    return;
                }
                if (!e4FightFn_) { println("e4 not wired"); return; }
                if (lord_.leagueCleared && lord_.e4Progress >= 5) {
                    lord_.e4Progress = 0;
                }
                uint8_t startIdx = lord_.e4Progress;
                if (startIdx > 4) startIdx = 4;
                const LordE4Member *m = lordE4Member(startIdx);
                char buf[80];
                snprintf(buf, sizeof(buf), "Indigo Plateau — %s %s (%s)...",
                         m ? m->title : "?", m ? m->name : "?",
                         m ? m->typeFlavor : "?");
                println(buf);
                e4FightFn_(e4FightCtx_, startIdx);
                return;
            }
            if (!lordGymUnlocked(lord_, gymIdx)) {
                println("gym is locked — clear earlier gyms first");
                return;
            }
            if (lordHasBadge(lord_, gymIdx)) {
                // NG+ revisit: gym already cleared at lower tier than
                // ngPlusTier — fight directly, no rematch prompt.
                if (lord_.gymTierCleared[gymIdx] < lord_.ngPlusTier) {
                    // Reset trainer progress for a clean gauntlet.
                    lord_.gymProgress[gymIdx] = 0;
                } else {
                    pendingRematchGym_ = (int8_t)gymIdx;
                    const LordGym *g = lordGym(gymIdx);
                    char buf[80];
                    snprintf(buf, sizeof(buf),
                             "%s already cleared. Rematch? (y/n)",
                             g ? g->leaderName : "Gym");
                    println(buf);
                    return;
                }
            }
            if (!gymFightFn_) { println("gym fight not wired"); return; }
            const LordGym *g = lordGym(gymIdx);
            char buf[64];
            snprintf(buf, sizeof(buf), "Challenging %s of %s...",
                     g ? g->leaderName : "?", g ? g->city : "?");
            println(buf);
            uint8_t trainerIdx = lord_.gymProgress[gymIdx];
            if (trainerIdx > 4) trainerIdx = 4;
            gymFightFn_(gymFightCtx_, gymIdx, trainerIdx);
            return;
        }
        if (strncmp(args, "dump", 4) != 0) {
            // Listing: 8 gym names + Indigo Plateau as the 9th entry.
            // lordGymUnlocked() walks the badge bitmask; the E4 row is
            // gated on the full 8/8 + leagueCleared flag.
            char buf[80];
            char tierLabel[8];
            if (lord_.ngPlusTier == 0) snprintf(tierLabel, sizeof(tierLabel), "Kanto");
            else                       snprintf(tierLabel, sizeof(tierLabel), "NG+%u",
                                                 (unsigned)lord_.ngPlusTier);
            snprintf(buf, sizeof(buf),
                     "Legend of Charizard — %s  runs %u  badges %u/8",
                     tierLabel,
                     (unsigned)lord_.exploreRunsToday,
                     (unsigned)__builtin_popcount(lord_.badges));
            println(buf);
            char st[8];
            for (uint8_t i = 0; i < 8; ++i) {
                const LordGym *g = lordGym(i);
                if (!g) continue;
                bool hadBadge = lordHasBadge(lord_, i);
                bool atTier   = (lord_.gymTierCleared[i] >= lord_.ngPlusTier);
                if (hadBadge && atTier) {
                    snprintf(st, sizeof(st), "CLEAR");
                } else if (hadBadge && !atTier) {
                    // Beaten at a lower tier; player needs to revisit at
                    // current NG+. Marker shows the tier required.
                    snprintf(st, sizeof(st), "NG+%u",
                             (unsigned)lord_.ngPlusTier);
                } else if (lordGymUnlocked(lord_, i)) {
                    snprintf(st, sizeof(st), "open");
                } else {
                    snprintf(st, sizeof(st), "lock");
                }
                snprintf(buf, sizeof(buf), "  %u %-12.12s %-10.10s [%s]",
                         (unsigned)i + 1, g->city, g->leaderName, st);
                println(buf);
            }
            // Indigo Plateau row mirrors the gym tier-cleared logic. CLEAR
            // only when the league has been beaten AT-OR-ABOVE the current
            // ngPlusTier. Otherwise show NG+T if badges are full, or
            // open/lock for a base-game player who hasn't beaten it yet.
            char e4buf[8];
            const char *e4st;
            bool e4ClearedAtTier = lord_.leagueCleared &&
                                   lord_.e4TierCleared >= lord_.ngPlusTier;
            if (e4ClearedAtTier) {
                e4st = "CLEAR";
            } else if (lord_.badges == 0xFF) {
                if (lord_.ngPlusTier > 0) {
                    snprintf(e4buf, sizeof(e4buf), "NG+%u",
                             (unsigned)lord_.ngPlusTier);
                    e4st = e4buf;
                } else {
                    e4st = "open";
                }
            } else {
                e4st = "lock";
            }
            snprintf(buf, sizeof(buf), "  9 %-12.12s %-10.10s [%s]",
                     "Indigo Plat.", "Elite Four", e4st);
            println(buf);
            return;
        }
        // `gym dev all8` — debug helper. Drops the player to "first-time
        // ready-for-Elite-Four" state: 8 badges all earned at base-game
        // (tier 0), E4 untouched, NG+ tier 0, leagueCleared cleared. Lets
        // the user jump straight to the Indigo Plateau to test the
        // base-game → NG+1 transition without grinding eight gyms.
        if (strncmp(args, "dev all8", 8) == 0) {
            lord_.badges        = 0xFF;
            lord_.e4Progress    = 0;
            lord_.leagueCleared = 0;
            lord_.e4TierCleared = 0;
            lord_.ngPlusTier    = 0;
            for (uint8_t i = 0; i < 8; ++i) {
                lord_.gymProgress[i]    = 5;   // leader cleared
                lord_.gymTierCleared[i] = 0;   // beaten at base-game tier
            }
            lordSetCurrentNgPlusTier(0);
            lordSave(lord_);
            println("dev: 8 badges granted, E4 reset, NG+0.");
            return;
        }
        // `gym dev clear` — debug helper: wipe LoC state back to a fresh
        // start. Useful for re-testing the early game.
        if (strncmp(args, "dev clear", 9) == 0) {
            lordInitDefaults(lord_);
            lordSetCurrentNgPlusTier(0);
            lordSave(lord_);
            println("dev: LoC state wiped.");
            return;
        }
        // `gym dump` — debug view of LordSave.
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "gym: badges=0x%02X runsToday=%u total=%u best=%u",
                 (unsigned)lord_.badges,
                 (unsigned)lord_.exploreRunsToday,
                 (unsigned)lord_.totalRuns,
                 (unsigned)lord_.bestRunWaves);
        println(buf);
        return;
    }
    if (strncmp(line, "fight", 5) == 0) {
        if (!partyLoaded_) {
            println("no party loaded — load a SAV first");
            return;
        }
        if (!fightFn_) {
            println("fight not wired");
            return;
        }
        println("starting local battle vs CPU rival...");
        fightFn_(fightCtx_);
        return;
    }
    // ── MMG gym discovery + fight (Phase C) ─────────────────────────────────
    // `mmg`            — broadcast probe + list discovered gyms
    // `mmg list`       — re-show last cached list (no probe)
    // `mmg fight N`    — start networked battle vs gym N (1-based)
    bool isMmg = (strncmp(line, "mmg", 3) == 0) &&
                 (line[3] == '\0' || line[3] == ' ' || line[3] == '\t');
    if (isMmg) {
        const char *args = line + 3;
        while (*args == ' ') ++args;

        if (strncmp(args, "fight", 5) == 0) {
            const char *p = args + 5;
            while (*p == ' ') ++p;
            int n = 0;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; }
            if (n < 1 || n > discoveredCount_) {
                if (discoveredCount_ == 0) println("no gyms discovered — type `mmg` first");
                else                       println("usage: mmg fight <1..N>");
                return;
            }
            if (!bbsFightFn_) { println("mmg fight not wired"); return; }
            uint32_t target = discoveredGyms_[n - 1].nodeNum;
            char buf[64];
            snprintf(buf, sizeof(buf), "Challenging gym #%d (%s)...",
                     n, discoveredGyms_[n - 1].gymName);
            println(buf);
            bbsFightFn_(bbsFightCtx_, target);
            return;
        }

        if (strncmp(args, "list", 4) == 0) {
            if (discoveredCount_ == 0) {
                println("no gyms cached — type `mmg` to probe");
                return;
            }
            char buf[80];
            for (uint8_t i = 0; i < discoveredCount_; ++i) {
                const DiscoveredGym &g = discoveredGyms_[i];
                snprintf(buf, sizeof(buf), "%d. %s [%s] ldr:%s rank:%u",
                         i + 1, g.gymName, g.badgeName, g.leader, g.rosterSize);
                println(buf);
            }
            return;
        }

        // Default: trigger a fresh probe.
        if (!bbsProbeFn_) { println("mmg probe not wired"); return; }
        discoveredCount_ = 0;     // reset cache; replies will repopulate
        bbsLastProbeMs_  = millis();
        println("Probing for MMG gyms (5s)...");
        bbsProbeFn_(bbsProbeCtx_);
        return;
    }

    // Indigo Plateau is reached via `gym fight 9`, no standalone command.
    if (strncmp(line, "beacon", 6) == 0) {
        if (!beaconFn_) { println("beacon not wired"); return; }
        beaconFn_(beaconCtx_);
        println("Beacon broadcast — peers should pick you up shortly.");
        return;
    }
    if (strncmp(line, "mmt ", 4) == 0) {
        // T4 wire-format ping: `mmt <short>` sends an MMT:ON DM to the
        // peer whose Meshtastic short_name matches. Module resolves via
        // NodeDB; we just hand it the typed string.
        const char *p = line + 4;
        while (*p == ' ' || *p == '@') ++p;
        if (!*p) { println("usage: mmt <short_name>"); return; }
        if (!mmtFn_) { println("mmt not wired"); return; }
        char buf[64];
        snprintf(buf, sizeof(buf), "Challenging %s...", p);
        println(buf);
        mmtFn_(mmtCtx_, p);
        return;
    }
    if (strncmp(line, "explore", 7) == 0) {
        // `explore` — wild encounter on the route appropriate to the player's
        // current badge count. Route 0 = Viridian Forest (pre-Brock), route 7
        // = Cerulean Cave (post-Blaine, pre-Giovanni). The route table lives
        // in LordRoutes.cpp; module-side resolves the encounter via
        // lordPickWildEncounter and starts a local text battle.
        if (!partyLoaded_) {
            println("no party loaded — load a SAV first");
            return;
        }
        if (!exploreFn_) {
            println("explore not wired");
            return;
        }
        uint8_t routeIdx = (uint8_t)__builtin_popcount(lord_.badges);
        if (routeIdx > 7) routeIdx = 7;
        const LordRoute *r = lordRoute(routeIdx);
        char buf[80];
        snprintf(buf, sizeof(buf), "Heading into %s...",
                 r ? r->name : "the wild");
        println(buf);
        exploreFn_(exploreCtx_, routeIdx);
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "unknown: %s", line);
    println(buf);
}

void MonsterMeshTerminal::onExploreBattleEnded(uint8_t routeIdx,
                                               bool playerWon, uint8_t lvl)
{
    const LordRoute *r = lordRoute(routeIdx);
    if (playerWon) {
        if (lord_.exploreRunsToday < 0xFF)         lord_.exploreRunsToday++;
        if (lord_.totalRuns        < 0xFFFFFFFFu)  lord_.totalRuns++;
        // Per-encounter run tracking — simple model: each won wild battle
        // counts as one cleared "wave", level becomes our highestOppLevel.
        // No multi-wave chaining yet (will land in a follow-up alongside
        // healing logic). The single-battle model still feeds best-run news.
        if (lvl > lord_.bestRunWaves) lord_.bestRunWaves = lvl;
        char buf[80];
        snprintf(buf, sizeof(buf), "Defeated wild %s lv%u. Run #%u today.",
                 r ? r->name : "?", (unsigned)lvl,
                 (unsigned)lord_.exploreRunsToday);
        println(buf);
    } else {
        println("Wiped out. Returning to base.");
    }
    if (playerWon) refreshParty();
    lordSave(lord_);
    (void)lvl;
}

void MonsterMeshTerminal::onE4BattleEnded(uint8_t memberIdx, bool playerWon)
{
    const LordE4Member *m = lordE4Member(memberIdx);
    if (!playerWon) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s %s defeated you. Run again.",
                 m ? m->title : "?", m ? m->name : "?");
        println(buf);
        // Don't bump progress on loss — retry the same member.
        lordSave(lord_);
        return;
    }
    if (memberIdx < 4) {
        // Beat one of the Elite Four. Module's gauntlet chain advances to
        // the next member without healing the player.
        if (lord_.e4Progress <= memberIdx) lord_.e4Progress = memberIdx + 1;
        char buf[80];
        snprintf(buf, sizeof(buf), "Beat %s! Next: %s.",
                 m ? m->name : "?",
                 lordE4Member(memberIdx + 1) ? lordE4Member(memberIdx + 1)->name : "Champion");
        println(buf);
        // XP credited per-faint during the gauntlet — just refresh the panel.
        refreshParty();
        lordSave(lord_);
        return;
    }
    // Beat the Champion — league cleared. Record the tier this clear
    // happened at (mirrors gymTierCleared), then bump NG+ tier (cap 5).
    bool firstClear      = !lord_.leagueCleared;
    lord_.leagueCleared  = 1;
    lord_.e4TierCleared  = lord_.ngPlusTier;
    uint8_t prevTier     = lord_.ngPlusTier;
    if (lord_.ngPlusTier < 5) {
        lord_.ngPlusTier++;
        lordSetCurrentNgPlusTier(lord_.ngPlusTier);
    }
    // Reset gym trainer progress + E4 progress so each gym restarts at
    // trainer 0 next NG+ run. Badges stay set: gymTierCleared[] tracks
    // which gyms are owed at the new tier instead of clearing the badge
    // bitmap. ngPlusTier=5 still resets so the run is repeatable.
    lord_.e4Progress = 0;
    for (uint8_t i = 0; i < 8; ++i) lord_.gymProgress[i] = 0;

    char buf[80];
    if (firstClear) {
        snprintf(buf, sizeof(buf),
                 "YOU ARE THE CHAMPION! NG+%u unlocked.",
                 (unsigned)lord_.ngPlusTier);
        println(buf);
        lordAppendNews(lord_, LORD_NEWS_BADGE, 8 /* champion sentinel */, 0);
    } else if (lord_.ngPlusTier > prevTier) {
        snprintf(buf, sizeof(buf),
                 "Champion defeated. NG+%u → NG+%u.",
                 (unsigned)prevTier, (unsigned)lord_.ngPlusTier);
        println(buf);
    } else {
        println("Champion defeated again. (NG+5 cap reached.)");
    }
    println("Gym league reset — challenge them all again at the new tier.");
    refreshParty();
    lordSave(lord_);
    (void)prevTier;
}

// ── BBS gym discovery callback (Phase C) ────────────────────────────────────

bool MonsterMeshTerminal::isBbsProbing() const
{
    if (bbsLastProbeMs_ == 0) return false;
    return (millis() - bbsLastProbeMs_) < 10000;
}

void MonsterMeshTerminal::onBbsReply(uint32_t fromNodeNum, const char *gymName,
                                       const char *badge, const char *leader,
                                       uint8_t roster)
{
    // Dedupe: if we've already cached this nodeNum, just refresh in-place.
    DiscoveredGym *slot = nullptr;
    for (uint8_t i = 0; i < discoveredCount_; ++i) {
        if (discoveredGyms_[i].nodeNum == fromNodeNum) {
            slot = &discoveredGyms_[i];
            break;
        }
    }
    bool isNew = false;
    if (!slot) {
        if (discoveredCount_ >= MAX_DISCOVERED_GYMS) {
            // Cache full — drop the reply but mention it once.
            char buf[64];
            snprintf(buf, sizeof(buf), "...+1 more (%s)",
                     gymName ? gymName : "?");
            println(buf);
            return;
        }
        slot = &discoveredGyms_[discoveredCount_++];
        isNew = true;
    }
    slot->nodeNum = fromNodeNum;
    snprintf(slot->gymName,   sizeof(slot->gymName),   "%s", gymName  ? gymName  : "?");
    snprintf(slot->badgeName, sizeof(slot->badgeName), "%s", badge    ? badge    : "?");
    snprintf(slot->leader,    sizeof(slot->leader),    "%s", leader   ? leader   : "open");
    slot->rosterSize = roster;

    if (isNew) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%u. %s [%s] ldr:%s rank:%u",
                 (unsigned)discoveredCount_, slot->gymName, slot->badgeName,
                 slot->leader, slot->rosterSize);
        println(buf);
    }
}

#endif // T_DECK && !MESHTASTIC_EXCLUDE_MONSTERMESH && HAS_TFT
