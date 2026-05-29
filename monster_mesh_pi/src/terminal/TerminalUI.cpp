// TerminalUI.cpp - MonsterMesh Pi menu-driven ncurses UI
// Navigation: D-pad moves within/between tabs. A=select, B=back, Start=menu, Select=help.
// No text input - every action is reached through the MESH / LOCAL / SYSTEM menu tree.

#include "TerminalUI.h"
#include "../battle/showdown_gen1_moves.h"
#include "../shared/DaycareSavPatcher.h"   // dexToInternal[] table
#include "../shared/Gen1BaseExp.h"         // gen1XpYield()
#include <ncurses.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// ── Local helpers ─────────────────────────────────────────────────────────────

static const char *typeName(uint8_t t) {
    static const char *N[] = {
        "Normal","Fight","Flying","Poison","Ground","Rock",
        "Bird","Bug","Ghost","Fire","Water","Grass",
        "Elec","Psychic","Ice","Dragon"
    };
    return t < 16 ? N[t] : "?";
}

static const char *dexName(uint8_t dex) {
    static const char *N[152] = {
        "???","Bulbasaur","Ivysaur","Venusaur","Charmander","Charmeleon",
        "Charizard","Squirtle","Wartortle","Blastoise","Caterpie","Metapod",
        "Butterfree","Weedle","Kakuna","Beedrill","Pidgey","Pidgeotto",
        "Pidgeot","Rattata","Raticate","Spearow","Fearow","Ekans","Arbok",
        "Pikachu","Raichu","Sandshrew","Sandslash","NidoranF","Nidorina",
        "Nidoqueen","NidoranM","Nidorino","Nidoking","Clefairy","Clefable",
        "Vulpix","Ninetales","Jigglypuff","Wigglytuff","Zubat","Golbat",
        "Oddish","Gloom","Vileplume","Paras","Parasect","Venonat","Venomoth",
        "Diglett","Dugtrio","Meowth","Persian","Psyduck","Golduck","Mankey",
        "Primeape","Growlithe","Arcanine","Poliwag","Poliwhirl","Poliwrath",
        "Abra","Kadabra","Alakazam","Machop","Machoke","Machamp","Bellsprout",
        "Weepinbell","Victreebel","Tentacool","Tentacruel","Geodude","Graveler",
        "Golem","Ponyta","Rapidash","Slowpoke","Slowbro","Magnemite","Magneton",
        "Farfetchd","Doduo","Dodrio","Seel","Dewgong","Grimer","Muk",
        "Shellder","Cloyster","Gastly","Haunter","Gengar","Onix","Drowzee",
        "Hypno","Krabby","Kingler","Voltorb","Electrode","Exeggcute","Exeggutor",
        "Cubone","Marowak","Hitmonlee","Hitmonchan","Lickitung","Koffing",
        "Weezing","Rhyhorn","Rhydon","Chansey","Tangela","Kangaskhan",
        "Horsea","Seadra","Goldeen","Seaking","Staryu","Starmie","MrMime",
        "Scyther","Jynx","Electabuzz","Magmar","Pinsir","Tauros","Magikarp",
        "Gyarados","Lapras","Ditto","Eevee","Vaporeon","Jolteon","Flareon",
        "Porygon","Omanyte","Omastar","Kabuto","Kabutops","Aerodactyl",
        "Snorlax","Articuno","Zapdos","Moltres","Dratini","Dragonair",
        "Dragonite","Mewtwo","Mew"
    };
    return dex < 152 ? N[dex] : "???";
}

static const char *moveName(uint8_t id) {
    const Gen1MoveData *m = gen1Move(id);
    return m ? m->name : "---";
}

// ── TerminalUI ────────────────────────────────────────────────────────────────

TerminalUI::TerminalUI() {}

TerminalUI::~TerminalUI() { shutdown(); }

bool TerminalUI::init() {
    // IPC client
    ipc_.setMessageCallback([this](const std::string &msg){ onIpcMessage(msg); });

    // Input handler
    input_.setButtonCallback([this](const ButtonEvent &ev){ handleButton(ev); });
    if (!input_.open()) {
        // Non-fatal - ncurses keyboard fallback still works
        LOG_WARN("GPI input device not found; using ncurses keyboard fallback");
    }

    // Resize terminal window to the device's screen resolution.
    // 640x480 @ standard 8x16 console font = 80 columns x 30 rows.
    // ANSI escape: CSI 8 ; rows ; cols t  - works in xterm, iTerm2, Terminal.app
    printf("\033[8;30;80t");
    fflush(stdout);

    // ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    getmaxyx(stdscr, rows_, cols_);

    // Cap to device dimensions so the dev terminal mirrors what users see
    static constexpr int DEV_COLS = 80;
    static constexpr int DEV_ROWS = 30;
    if (cols_ > DEV_COLS) cols_ = DEV_COLS;
    if (rows_ > DEV_ROWS) rows_ = DEV_ROWS;

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN,   -1);  // HP high
        init_pair(2, COLOR_YELLOW,  -1);  // HP mid
        init_pair(3, COLOR_RED,     -1);  // HP low / selected
        init_pair(4, COLOR_CYAN,    -1);  // header / tab active
        init_pair(5, COLOR_WHITE,   -1);  // normal text
        init_pair(6, COLOR_MAGENTA, -1);  // event / achievement
    }

    int infoRows = rows_ - STATUS_ROWS - MENU_ROWS;

    winStatus_ = newwin(STATUS_ROWS, cols_, 0,                  0);
    winInfo_   = newwin(infoRows,   cols_, STATUS_ROWS,         0);
    winMenu_   = newwin(MENU_ROWS,  cols_, rows_ - MENU_ROWS,   0);

    // Must enable keypad + nodelay on the window we actually call wgetch() on
    if (winMenu_) {
        keypad(winMenu_, TRUE);
        nodelay(winMenu_, TRUE);
    }
    scrollok(winInfo_, TRUE);

    // Seed RNG for battle
    srand((unsigned)time(nullptr));

    startMs_ = millis();
    pushActivity("MonsterMesh Pi v1 - waiting for daemon...");
    return true;
}

void TerminalUI::shutdown() {
    ipc_.disconnect();
    input_.close();
    if (winStatus_) { delwin(winStatus_); winStatus_ = nullptr; }
    if (winInfo_)   { delwin(winInfo_);   winInfo_   = nullptr; }
    if (winMenu_)   { delwin(winMenu_);   winMenu_   = nullptr; }
    if (isendwin() == FALSE) endwin();
}

void TerminalUI::run() {
    while (!shouldQuit_) {
        // IPC reconnect
        if (!ipc_.isConnected()) {
            if (millis() - lastConnectMs_ > RECONNECT_MS) {
                lastConnectMs_ = millis();
                if (ipc_.connect(MMD_SOCK_PATH)) {
                    pushActivity("> Daemon connected");
                    ipc_.send("{\"cmd\":\"GET_STATUS\"}");
                    ipc_.send("{\"cmd\":\"GET_PARTY\"}");
                }
            }
        } else {
            ipc_.poll();
        }

        // GPI button poll
        if (input_.isOpen()) input_.poll();

        // Keyboard layout matching MonsterMesh T-Deck terminal:
        //   W/A/S/D = up/left/down/right (D-pad)
        //   K or Enter = A (confirm/select)
        //   L or Space  = B (back/cancel)
        //   Tab         = Start (menu)
        //   Backspace   = Select (help)
        //   Q           = quit
        int ch = wgetch(winMenu_);
        if (ch != ERR) {
            ButtonEvent ev;
            ev.pressed = true;
            switch (ch) {
                case 'w': case 'W': case KEY_UP:
                    ev.button = GpiButton::UP;     handleButton(ev); break;
                case 's': case 'S': case KEY_DOWN:
                    ev.button = GpiButton::DOWN;   handleButton(ev); break;
                case 'a': case 'A': case KEY_LEFT:
                    ev.button = GpiButton::LEFT;   handleButton(ev); break;
                case 'd': case 'D': case KEY_RIGHT:
                    ev.button = GpiButton::RIGHT;  handleButton(ev); break;
                case 'k': case 'K': case '\n': case KEY_ENTER:
                    ev.button = GpiButton::A;      handleButton(ev); break;
                case 'l': case 'L': case ' ':
                    ev.button = GpiButton::B;      handleButton(ev); break;
                case '\t':
                    ev.button = GpiButton::START;  handleButton(ev); break;
                case KEY_BACKSPACE: case 127:
                    ev.button = GpiButton::SELECT; handleButton(ev); break;
                case 'q': case 'Q':
                    shouldQuit_ = true; break;
                default: break;
            }
        }

        render();
        usleep(16666);
    }
}

// ── Rendering dispatch ────────────────────────────────────────────────────────

void TerminalUI::render() {
    if (!winStatus_ || !winInfo_ || !winMenu_) return;

    renderStatusBar();

    switch (screen_) {
        case Screen::MENU:          renderInfoPanel();   renderMenu();       break;
        case Screen::PARTY:         renderParty();                           break;
        case Screen::NEIGHBORS:     renderNeighbors();                       break;
        case Screen::DAYCARE_EVENT: renderDaycareEvent();                    break;
        case Screen::BATTLE:        renderBattle();                          break;
        case Screen::BATTLE_END:    renderBattleEnd();                       break;
        case Screen::GYM_SELECT:    renderGymSelect();                       break;
        case Screen::PVP_BATTLE:    renderPvpBattle();                       break;
        case Screen::PVP_BATTLE_END:renderPvpBattleEnd();                    break;
        case Screen::HELP:          renderHelp();                            break;
        case Screen::CONFIRM_QUIT:  renderConfirmQuit();                     break;
        case Screen::CHALLENGE:     renderChallenge();                       break;
    }

    wrefresh(winStatus_);
    wrefresh(winInfo_);
    wrefresh(winMenu_);
}

// ── Status bar ────────────────────────────────────────────────────────────────

void TerminalUI::renderStatusBar() {
    werase(winStatus_);
    wattron(winStatus_, A_REVERSE);

    // Build status string
    char buf[128];
    const char *leadName = "---";
    int leadLv = 0;
    if (hasParty_ && partyCount_ > 0) {
        const PartySlot &lead = partySlots_[0];
        leadName = lead.nick[0] ? lead.nick : (lead.name[0] ? lead.name : "???");
        leadLv   = lead.level;
    }

    snprintf(buf, sizeof(buf), " MonsterMesh | Nbrs:%-2d | %-10s Lv%d",
             neighborCount_, leadName, leadLv);
    buf[cols_-1] = '\0';

    // Right-align daemon connection status
    const char *connTag = ipc_.isConnected() ? "" : " [offline]";
    int tagLen = (int)strlen(connTag);
    int txtLen = (int)strlen(buf);
    if (txtLen + tagLen < cols_) {
        for (int i = txtLen; i < cols_ - tagLen - 1; i++) buf[i] = ' ';
        buf[cols_ - tagLen - 1] = '\0';
        strncat(buf, connTag, sizeof(buf) - strlen(buf) - 1);
    }

    mvwprintw(winStatus_, 0, 0, "%-*s", cols_, buf);
    wattroff(winStatus_, A_REVERSE);
}

// ── Info panel (MENU screen) ──────────────────────────────────────────────────

// Push a printf-style line into the activity feed.  Newest at end; cap at 64.
void TerminalUI::pushActivity(const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (activityLog_.size() > 64) activityLog_.erase(activityLog_.begin());
    activityLog_.push_back(buf);
}

void TerminalUI::renderInfoPanel() {
    werase(winInfo_);

    int infoRows = getmaxy(winInfo_);

    // Pending challenge alert (top, always visible)
    int firstLogRow = 0;
    if (hasPendingChallenge_) {
        wattron(winInfo_, COLOR_PAIR(6) | A_BOLD);
        mvwprintw(winInfo_, 0, 0, " ! CHALLENGE from %s !", challengerName_);
        wattroff(winInfo_, COLOR_PAIR(6) | A_BOLD);
        firstLogRow = 1;
    }

    // Activity feed - show the most recent (infoRows - firstLogRow) lines.
    int logRows = infoRows - firstLogRow;
    if (logRows < 1) return;

    int total = (int)activityLog_.size();
    int start = total - logRows;
    if (start < 0) start = 0;

    for (int i = 0; start + i < total && i < logRows; i++) {
        const std::string &line = activityLog_[start + i];
        // Truncate to width
        std::string trimmed = line.substr(0, (size_t)(cols_ - 2));
        mvwprintw(winInfo_, firstLogRow + i, 1, "%s", trimmed.c_str());
    }
}

// ── Tabbed menu ───────────────────────────────────────────────────────────────

void TerminalUI::renderMenu() {
    werase(winMenu_);
    box(winMenu_, 0, 0);

    // Tab headers
    int tabW = (cols_ - 2) / TAB_COUNT;
    for (int t = 0; t < TAB_COUNT; t++) {
        int x = 1 + t * tabW;
        if (t == activeTab_) wattron(winMenu_, COLOR_PAIR(4) | A_REVERSE | A_BOLD);
        else                 wattron(winMenu_, A_DIM);
        mvwprintw(winMenu_, 1, x, " %-*s", tabW - 1, TAB_NAMES[t]);
        wattroff(winMenu_, COLOR_PAIR(4) | A_REVERSE | A_BOLD | A_DIM);
    }

    mvwhline(winMenu_, 2, 1, ACS_HLINE, cols_ - 2);

    // Items - max rows = MENU_ROWS - 5 (borders + tab row + separator + hint)
    const char **items = tabItems(activeTab_);
    int count = tabItemCount(activeTab_);
    int maxRows = MENU_ROWS - 5;
    for (int i = 0; i < count && i < maxRows; i++) {
        if (i == activeItem_) wattron(winMenu_, A_REVERSE | COLOR_PAIR(3));
        mvwprintw(winMenu_, 3 + i, 2, "%-*s", cols_ - 4, items[i]);
        if (i == activeItem_) wattroff(winMenu_, A_REVERSE | COLOR_PAIR(3));
    }

    // Controls hint
    mvwprintw(winMenu_, MENU_ROWS - 2, 1, "A/D:tab  W/S:move  K:select  L:back");
}

// ── Party detail ──────────────────────────────────────────────────────────────

void TerminalUI::renderParty() {
    werase(winInfo_);
    werase(winMenu_);

    if (!hasParty_ || partyCount_ == 0) {
        mvwprintw(winInfo_, 2, 2, "No party loaded.");
        mvwprintw(winInfo_, 3, 2, "Drop a .sav in /tmp/mm-test-saves/");
        mvwprintw(winMenu_, 1, 1, "[B] Back");
        return;
    }

    int infoRows = getmaxy(winInfo_);
    int row      = 0;
    int scroll   = infoScroll_;

    for (int i = 0; i < partyCount_; i++) {
        const PartySlot &s = partySlots_[i];

        // Header: slot number, nickname (or species name), level
        const char *displayName = s.nick[0] ? s.nick : (s.name[0] ? s.name : "???");
        if (row - scroll >= 0 && row - scroll < infoRows) {
            wattron(winInfo_, A_BOLD);
            mvwprintw(winInfo_, row - scroll, 0,
                      "%d. %-10s  Lv%d", i + 1, displayName, s.level);
            wattroff(winInfo_, A_BOLD);
        }
        row++;

        // XP gained in daycare
        if (row - scroll >= 0 && row - scroll < infoRows) {
            wattron(winInfo_, COLOR_PAIR(1));
            mvwprintw(winInfo_, row - scroll, 3,
                      "+%u daycare XP  (%uh in care)", s.xpGained, s.hours);
            wattroff(winInfo_, COLOR_PAIR(1));
        }
        row++;

        // Blank separator
        row++;
    }

    mvwprintw(winMenu_, 1, 1, "[W/S] Scroll   [L] Back");
}

// ── Neighbor list ─────────────────────────────────────────────────────────────

void TerminalUI::renderNeighbors() {
    werase(winInfo_);
    werase(winMenu_);

    if (neighborDisplayCount_ == 0) {
        mvwprintw(winInfo_, 2, 2, "No neighbors on mesh.");
        mvwprintw(winInfo_, 3, 2, "Send a Beacon to attract trainers.");
    } else {
        int infoRows = getmaxy(winInfo_);
        for (int i = 0; i < neighborDisplayCount_ && i * 2 < infoRows - 1; i++) {
            const NeighborEntry &n = neighbors_[i];
            bool sel = (i == neighborSel_);
            if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
            wattron(winInfo_, A_BOLD);
            mvwprintw(winInfo_, i * 2, 0, " %s (%s)%s",
                      n.shortName, n.gameName, sel ? " <" : "  ");
            wattroff(winInfo_, A_BOLD);
            if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
            if (n.partyCount > 0)
                mvwprintw(winInfo_, i * 2 + 1, 3, "%s Lv%d  (%d in party)",
                          n.lead, n.leadLevel, n.partyCount);
        }
    }

    mvwprintw(winMenu_, 1, 1, "[W/S] Select  [K] Challenge  [L] Back");
}

// ── Daycare event detail ──────────────────────────────────────────────────────

void TerminalUI::renderDaycareEvent() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);

    mvwprintw(winInfo_, 0, 1, "=== Last Daycare Event ===");

    if (lastEventText_.empty()) {
        mvwprintw(winInfo_, 2, 2, "No event yet.");
    } else {
        // Word-wrap the event text
        int row = 2;
        const std::string &txt = lastEventText_;
        int start = 0;
        int len = (int)txt.size();
        int lineW = cols_ - 4;
        while (start < len && row < infoRows - 3) {
            int end = start + lineW;
            if (end >= len) {
                mvwprintw(winInfo_, row++, 2, "%s", txt.c_str() + start);
                break;
            }
            // Back up to last space
            while (end > start && txt[end] != ' ') end--;
            if (end == start) end = start + lineW;
            char linebuf[128];
            int w = end - start;
            if (w >= (int)sizeof(linebuf)) w = (int)sizeof(linebuf) - 1;
            memcpy(linebuf, txt.c_str() + start, w);
            linebuf[w] = '\0';
            mvwprintw(winInfo_, row++, 2, "%s", linebuf);
            start = end + 1;
        }

        if (lastEventXp_ > 0) {
            wattron(winInfo_, COLOR_PAIR(1) | A_BOLD);
            mvwprintw(winInfo_, row + 1, 2, "+%d XP gained!", lastEventXp_);
            wattroff(winInfo_, COLOR_PAIR(1) | A_BOLD);
        }
    }

    mvwprintw(winMenu_, 1, 1, "[B] Back");
}

// ── Help ──────────────────────────────────────────────────────────────────────

void TerminalUI::renderHelp() {
    werase(winInfo_);
    werase(winMenu_);

    static const char *HELP[] = {
        "=== Controls ===",
        "",
        "D-pad LEFT/RIGHT  Switch tab (Mesh/Local/System)",
        "D-pad UP/DOWN     Move between items",
        "A (Z key)         Select / confirm",
        "B (X key)         Back / cancel",
        "Start (Tab)       Open menu from anywhere",
        "Select (Space)    Toggle this help",
        "",
        "=== Tabs ===",
        "",
        "MESH   Neighbors, daycare events, beacon",
        "LOCAL  Party, fight CPU, roguelike",
        "SYSTEM Help, about, quit",
        "",
        "=== Notes ===",
        "Connect an nRF52 Meshtastic node via USB.",
        "Keep a Gen1 save in RetroPie/saves/gb/",
        "to sync your party automatically.",
        nullptr
    };

    int infoRows = getmaxy(winInfo_);
    for (int i = 0; HELP[i] && i < infoRows; i++) {
        if (HELP[i][0] == '=')
            wattron(winInfo_, A_BOLD | COLOR_PAIR(4));
        mvwprintw(winInfo_, i, 1, "%s", HELP[i]);
        if (HELP[i][0] == '=')
            wattroff(winInfo_, A_BOLD | COLOR_PAIR(4));
    }

    mvwprintw(winMenu_, 1, 1, "[B] Back    [Sel] Close help");
}

// ── Confirm quit ──────────────────────────────────────────────────────────────

void TerminalUI::renderConfirmQuit() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);
    int mid = infoRows / 2;
    mvwprintw(winInfo_, mid - 1, (cols_ - 22) / 2, "Exit MonsterMesh?");
    mvwprintw(winInfo_, mid + 1, (cols_ - 22) / 2, "  [A] Yes   [B] No  ");
}

// ── Incoming challenge ────────────────────────────────────────────────────────

void TerminalUI::renderChallenge() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);
    int mid = infoRows / 2;

    wattron(winInfo_, A_BOLD | COLOR_PAIR(6));
    mvwprintw(winInfo_, mid - 2, 2, "! BATTLE CHALLENGE !");
    wattroff(winInfo_, A_BOLD | COLOR_PAIR(6));
    mvwprintw(winInfo_, mid, 2, "From: %s", challengerName_);
    mvwprintw(winInfo_, mid + 1, 2, "Node: 0x%08X", challengeNodeId_);

    // Accept / Decline selector
    static const char *OPTS[] = { "Accept", "Decline" };
    for (int i = 0; i < 2; i++) {
        if (i == challengeSel_) wattron(winInfo_, A_REVERSE);
        mvwprintw(winInfo_, mid + 3, 4 + i * 10, "[%s]", OPTS[i]);
        if (i == challengeSel_) wattroff(winInfo_, A_REVERSE);
    }

    mvwprintw(winMenu_, 1, 1, "[L/R] Choose  [A] Confirm  [B] Cancel");
}

// ── Battle screen ─────────────────────────────────────────────────────────────

void TerminalUI::renderBattle() {
    werase(winInfo_);
    werase(winMenu_);

    const Gen1BattleEngine::BattleParty &pp = engine_.party(localSide_);
    const Gen1BattleEngine::BattleParty &ep = engine_.party(1 - localSide_);
    const Gen1BattleEngine::BattlePoke  &pm = pp.mons[pp.active];
    const Gen1BattleEngine::BattlePoke  &em = ep.mons[ep.active];

    int infoRows = getmaxy(winInfo_);

    // Gym gauntlet header (only shown while in a gym run)
    int row = 0;
    if (inGymBattle_) {
        const LordGym *g = lordGym(pendingGymIdx_);
        if (g) {
            char hdr[64];
            int round = pendingTrainerIdx_ + 1;   // 1-based for display
            const char *who = (pendingTrainerIdx_ >= LORD_GYM_LEADER_INDEX)
                                ? g->leaderName
                                : g->trainers[pendingTrainerIdx_].name;
            snprintf(hdr, sizeof(hdr),
                     "%s Gym  Round %d/5  vs %s",
                     g->city, round, who);
            wattron(winInfo_, A_BOLD | COLOR_PAIR(4));
            mvwprintw(winInfo_, row, 0, "%-*.*s", cols_, cols_, hdr);
            wattroff(winInfo_, A_BOLD | COLOR_PAIR(4));
        }
        row++;
    }

    // Player HP bar
    drawHpBar(winInfo_, row++, 0, cols_, pm.hp, pm.maxHp, pm.nickname);
    // Enemy HP bar
    drawHpBar(winInfo_, row++, 0, cols_, em.hp, em.maxHp, em.nickname);

    // Battle log starts right below the HP bars (offset by the optional
    // gauntlet header)
    int logStart = row;
    int logEnd   = logStart + BATTLE_LOG_LINES;
    int logOff   = (int)battleLog_.size() - BATTLE_LOG_LINES;
    if (logOff < 0) logOff = 0;
    for (int i = 0; i < BATTLE_LOG_LINES; i++) {
        int idx = logOff + i;
        if (idx < (int)battleLog_.size() && logStart + i < infoRows)
            mvwprintw(winInfo_, logStart + i, 1, "%s", battleLog_[idx].c_str());
    }

    // Move selector or switch list
    int menuRow = 0;
    if (!switchMode_) {
        mvwprintw(winMenu_, menuRow++, 1, "FIGHT:");
        for (int i = 0; i < 4; i++) {
            uint8_t mv = pm.moves[i];
            if (mv == 0) break;
            const char *name = moveName(mv);
            uint8_t pp_cur = pm.pp[i];
            const Gen1MoveData *md = gen1Move(mv);
            uint8_t pp_max = md ? md->pp : 0;
            if (i == moveSel_) wattron(winMenu_, A_REVERSE | COLOR_PAIR(3));
            mvwprintw(winMenu_, menuRow++, 2, "%-14s PP:%2d/%-2d", name, pp_cur, pp_max);
            if (i == moveSel_) wattroff(winMenu_, A_REVERSE | COLOR_PAIR(3));
        }
        mvwprintw(winMenu_, MENU_ROWS - 2, 1, "[A]Use [B]Switch [Start]Flee");
    } else {
        mvwprintw(winMenu_, menuRow++, 1, "SWITCH TO:");
        for (int i = 0; i < (int)pp.count; i++) {
            const Gen1BattleEngine::BattlePoke &s = pp.mons[i];
            bool fainted = (s.hp == 0);
            bool active  = (i == (int)pp.active);
            if (i == switchSel_) wattron(winMenu_, A_REVERSE);
            if (fainted || active) wattron(winMenu_, A_DIM);
            mvwprintw(winMenu_, menuRow++, 2, "%-10s Lv%d HP%d",
                      s.nickname, s.level, s.hp);
            if (fainted || active) wattroff(winMenu_, A_DIM);
            if (i == switchSel_) wattroff(winMenu_, A_REVERSE);
            if (menuRow >= MENU_ROWS - 2) break;
        }
        mvwprintw(winMenu_, MENU_ROWS - 2, 1, "[A]Switch [B]Back to moves");
    }
}

// ── Battle end ────────────────────────────────────────────────────────────────

void TerminalUI::renderBattleEnd() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);
    int mid = infoRows / 2;

    const char *resultTxt = "BATTLE ENDED";
    if (battleResult_ == Gen1BattleEngine::Result::P1_WIN) {
        if (localSide_ == 0) resultTxt = "YOU WIN!";
        else                 resultTxt = "YOU LOSE...";
    } else if (battleResult_ == Gen1BattleEngine::Result::P2_WIN) {
        if (localSide_ == 0) resultTxt = "YOU LOSE...";
        else                 resultTxt = "YOU WIN!";
    } else if (battleResult_ == Gen1BattleEngine::Result::DRAW) {
        resultTxt = "DRAW!";
    }

    wattron(winInfo_, A_BOLD | COLOR_PAIR(4));
    mvwprintw(winInfo_, mid, (cols_ - (int)strlen(resultTxt)) / 2, "%s", resultTxt);
    wattroff(winInfo_, A_BOLD | COLOR_PAIR(4));

    // Show last few log lines
    int logOff = (int)battleLog_.size() - 4;
    if (logOff < 0) logOff = 0;
    for (int i = 0; i < 4; i++) {
        int idx = logOff + i;
        if (idx < (int)battleLog_.size())
            mvwprintw(winInfo_, mid + 2 + i, 1, "%s", battleLog_[idx].c_str());
    }

    mvwprintw(winMenu_, 1, 1, "[A] or [B] to return to menu");
}

// ── HP bar ────────────────────────────────────────────────────────────────────

void TerminalUI::drawHpBar(WINDOW *w, int y, int x, int width,
                            uint16_t hp, uint16_t maxHp, const char *label) {
    if (maxHp == 0) return;
    char name[12];
    snprintf(name, sizeof(name), "%-10s", label ? label : "");
    int nameW = 11;
    int barW  = width - nameW - 8;
    if (barW < 4) barW = 4;
    int filled = (barW * hp) / maxHp;
    double pct = (double)hp / maxHp;
    int colorPair = (pct > 0.5) ? 1 : (pct > 0.25) ? 2 : 3;

    mvwprintw(w, y, x, "%s [", name);
    wattron(w, COLOR_PAIR(colorPair));
    for (int i = 0; i < barW; i++) waddch(w, i < filled ? ACS_BLOCK : '.');
    wattroff(w, COLOR_PAIR(colorPair));
    wprintw(w, "] %3d/%3d", hp, maxHp);
}

void TerminalUI::clearInfo() { if (winInfo_) werase(winInfo_); }

// ── Input dispatch ────────────────────────────────────────────────────────────

void TerminalUI::handleButton(const ButtonEvent &ev) {
    if (!ev.pressed) return;

    // Select always toggles help (from any screen except battle)
    if (ev.button == GpiButton::SELECT && screen_ != Screen::BATTLE) {
        screen_ = (screen_ == Screen::HELP) ? Screen::MENU : Screen::HELP;
        return;
    }
    // Start always returns to menu
    if (ev.button == GpiButton::START && screen_ != Screen::BATTLE) {
        screen_ = Screen::MENU;
        return;
    }

    switch (screen_) {
        case Screen::MENU:          menuButton(ev);         break;
        case Screen::PARTY:         partyButton(ev);        break;
        case Screen::NEIGHBORS:     neighborsButton(ev);    break;
        case Screen::DAYCARE_EVENT: daycareEventButton(ev); break;
        case Screen::BATTLE:        battleButton(ev);       break;
        case Screen::BATTLE_END:    battleEndButton(ev);    break;
        case Screen::GYM_SELECT:    gymSelectButton(ev);    break;
        case Screen::PVP_BATTLE:    pvpBattleButton(ev);    break;
        case Screen::PVP_BATTLE_END:pvpBattleEndButton(ev); break;
        case Screen::HELP:          helpButton(ev);         break;
        case Screen::CONFIRM_QUIT:  confirmQuitButton(ev);  break;
        case Screen::CHALLENGE:     challengeButton(ev);    break;
    }
}

void TerminalUI::menuButton(const ButtonEvent &ev) {
    int count = tabItemCount(activeTab_);
    switch (ev.button) {
        case GpiButton::LEFT:
            activeTab_  = (activeTab_ + TAB_COUNT - 1) % TAB_COUNT;
            activeItem_ = 0;
            break;
        case GpiButton::RIGHT:
            activeTab_  = (activeTab_ + 1) % TAB_COUNT;
            activeItem_ = 0;
            break;
        case GpiButton::UP:
            activeItem_ = (activeItem_ + count - 1) % count;
            break;
        case GpiButton::DOWN:
            activeItem_ = (activeItem_ + 1) % count;
            break;
        case GpiButton::A:
            activateItem(activeItem_);
            break;
        default: break;
    }
}

void TerminalUI::partyButton(const ButtonEvent &ev) {
    int infoRows = getmaxy(winInfo_);
    switch (ev.button) {
        case GpiButton::UP:   if (infoScroll_ > 0) infoScroll_--; break;
        case GpiButton::DOWN: infoScroll_++; break;
        case GpiButton::B:    screen_ = Screen::MENU; infoScroll_ = 0; break;
        default: break;
    }
}

void TerminalUI::neighborsButton(const ButtonEvent &ev) {
    switch (ev.button) {
        case GpiButton::UP:
            if (neighborDisplayCount_ > 0)
                neighborSel_ = (neighborSel_ + neighborDisplayCount_ - 1) % neighborDisplayCount_;
            break;
        case GpiButton::DOWN:
            if (neighborDisplayCount_ > 0)
                neighborSel_ = (neighborSel_ + 1) % neighborDisplayCount_;
            break;
        case GpiButton::A: {
            // Async fight: ask the daemon for the neighbor's full party
            // (it's already in our neighbor cache from their beacon), then
            // run a local CPU battle against that snapshot. On the result
            // screen we ANNOUNCE_RESULT back to the mesh as a chat msg.
            if (neighborSel_ < neighborDisplayCount_) {
                const NeighborEntry &n = neighbors_[neighborSel_];
                if (n.nodeId == 0) {
                    pushActivity("> Can't challenge: missing node ID");
                    break;
                }
                asyncFightActive_   = true;
                asyncFightNodeId_   = n.nodeId;
                snprintf(asyncFightTrainer_, sizeof(asyncFightTrainer_),
                         "%s-%s", n.shortName, n.gameName);

                char cmd[80];
                snprintf(cmd, sizeof(cmd),
                         "{\"cmd\":\"GET_NEIGHBOR_PARTY\",\"node_id\":%u}",
                         (unsigned)n.nodeId);
                ipc_.send(cmd);
                pushActivity("> Challenging %s...", asyncFightTrainer_);
            }
            break;
        }
        case GpiButton::B:
            screen_ = Screen::MENU;
            neighborSel_ = 0;
            break;
        default: break;
    }
}

void TerminalUI::daycareEventButton(const ButtonEvent &ev) {
    if (ev.button == GpiButton::B) { screen_ = Screen::MENU; }
}

void TerminalUI::helpButton(const ButtonEvent &ev) {
    if (ev.button == GpiButton::B || ev.button == GpiButton::SELECT)
        screen_ = Screen::MENU;
}

void TerminalUI::confirmQuitButton(const ButtonEvent &ev) {
    if (ev.button == GpiButton::A)      shouldQuit_ = true;
    else if (ev.button == GpiButton::B) screen_ = Screen::MENU;
}

void TerminalUI::challengeButton(const ButtonEvent &ev) {
    switch (ev.button) {
        case GpiButton::LEFT:  challengeSel_ = 0; break;
        case GpiButton::RIGHT: challengeSel_ = 1; break;
        case GpiButton::A:
            if (challengeSel_ == 0) {
                // Accept - reset PvP state
                strncpy(pvpEnemyName_, challengerName_, sizeof(pvpEnemyName_) - 1);
                pvpMyHp_ = pvpMyMaxHp_ = pvpEnemyHp_ = pvpEnemyMaxHp_ = 0;
                pvpTurn_ = pvpResult_ = 0;
                pvpMoveSel_ = 0;
                pvpLog_.clear();
                memset(pvpMyPp_, 35, 4);  // default PP until first UPDATE
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "{\"cmd\":\"ACCEPT_CHALLENGE\",\"node_id\":%u}", challengeNodeId_);
                ipc_.send(cmd);
                screen_ = Screen::PVP_BATTLE;
            } else {
                // Decline
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "{\"cmd\":\"DECLINE_CHALLENGE\",\"node_id\":%u}", challengeNodeId_);
                ipc_.send(cmd);
            }
            hasPendingChallenge_ = false;
            screen_ = Screen::MENU;
            break;
        case GpiButton::B:
            screen_ = Screen::MENU;
            break;
        default: break;
    }
}

void TerminalUI::battleButton(const ButtonEvent &ev) {
    if (inBattle_) {
        battleHandleButton(ev);
    } else {
        battleEndButton(ev);
    }
}

void TerminalUI::battleEndButton(const ButtonEvent &ev) {
    if (ev.button != GpiButton::A && ev.button != GpiButton::B) return;

    // Gym gauntlet: chain wins into the next trainer without healing.
    if (inGymBattle_ && battleResult_ == Gen1BattleEngine::Result::P1_WIN) {
        if (pendingTrainerIdx_ >= LORD_GYM_LEADER_INDEX) {
            // Beat the leader -> award badge, mark gym cleared, return to menu
            const LordGym *g = lordGym(pendingGymIdx_);
            if (g) lordSave_.badges |= (uint8_t)(1u << g->badgeBit);
            lordSave_.gymProgress[pendingGymIdx_] = LORD_GYM_TRAINERS;
            lordSave(lordSave_);
            inGymBattle_ = false;
        } else {
            // Advance to the next trainer. Engine.replaceOpponent keeps our
            // party's HP/PP/status -- that's the no-healing gauntlet rule.
            pendingTrainerIdx_++;
            Gen1Party nextGymParty;
            if (lordBuildGymParty(pendingGymIdx_, pendingTrainerIdx_, nextGymParty)) {
                engine_.replaceOpponent(nextGymParty);
                battleLog_.clear();
                moveSel_      = 0;
                switchMode_   = false;
                inBattle_     = true;
                battleResult_ = Gen1BattleEngine::Result::ONGOING;
                screen_       = Screen::BATTLE;
                return;  // skip the back-to-menu fallthrough
            }
            // Couldn't build next opponent: bail out
            inGymBattle_ = false;
        }
    }

    // Loss, leader cleared, or fallthrough error -> push battle XP to daemon
    // (which adds it to totalXpGained), trigger SAV writeback, then back to
    // menu.  Skipped silently if no XP was earned this run.
    flushBattleXpToDaemon();

    // Async-fight result broadcast: tell the mesh how the snapshot fight
    // against the neighbor went so they see it in their chat / activity feed.
    if (asyncFightActive_) {
        const char *verb = "drew with";
        if (battleResult_ == Gen1BattleEngine::Result::P1_WIN)      verb = "beat";
        else if (battleResult_ == Gen1BattleEngine::Result::P2_WIN) verb = "lost to";
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "[MM] GPI %s %s's team in async fight!",
                 verb, asyncFightTrainer_);
        // Escape for JSON
        char esc[220] = {};
        int ep = 0;
        for (int j = 0; msg[j] && ep < (int)sizeof(esc) - 2; j++) {
            if (msg[j] == '"' || msg[j] == '\\') esc[ep++] = '\\';
            esc[ep++] = msg[j];
        }
        char cmd[260];
        snprintf(cmd, sizeof(cmd), "{\"cmd\":\"ANNOUNCE_RESULT\",\"text\":\"%s\"}", esc);
        ipc_.send(cmd);
        pushActivity("> Announced result to mesh");
        asyncFightActive_ = false;
    }

    inGymBattle_ = false;
    inBattle_    = false;
    battleLog_.clear();
    screen_      = Screen::MENU;
}

// Push every slot's accumulated sessionXp_ to the daemon as CREDIT_XP
// commands, then ask the daemon to write the SAV file.  Resets the
// per-slot counters so the next battle starts from zero.
void TerminalUI::flushBattleXpToDaemon() {
    bool anyXp = false;
    char cmd[96];
    for (int i = 0; i < 6; i++) {
        if (sessionXp_[i] == 0) continue;
        snprintf(cmd, sizeof(cmd),
                 "{\"cmd\":\"CREDIT_XP\",\"slot\":%d,\"xp\":%u}",
                 i, (unsigned)sessionXp_[i]);
        ipc_.send(cmd);
        anyXp = true;
        sessionXp_[i] = 0;
    }
    if (anyXp) {
        ipc_.send("{\"cmd\":\"WRITEBACK_SAV\"}");
        pushActivity("> XP saved to .sav file");
    }
}

// ── Menu actions ──────────────────────────────────────────────────────────────

void TerminalUI::activateItem(int item) {
    switch (activeTab_) {
        case 0: activateMeshItem(item);   break;
        case 1: activateLocalItem(item);  break;
        case 2: activateSystemItem(item); break;
    }
}

void TerminalUI::activateMeshItem(int item) {
    switch (item) {
        case 0: // Beacon
            ipc_.send("{\"cmd\":\"FORCE_BEACON\"}");
            break;
        case 1: // Neighbors
            ipc_.send("{\"cmd\":\"GET_STATUS\"}");
            infoScroll_ = 0;
            screen_ = Screen::NEIGHBORS;
            break;
        case 2: // Daycare
            ipc_.send("{\"cmd\":\"GET_STATUS\"}");
            screen_ = Screen::DAYCARE_EVENT;
            break;
    }
}

void TerminalUI::activateLocalItem(int item) {
    switch (item) {
        case 0: // Party
            ipc_.send("{\"cmd\":\"GET_PARTY\"}");
            infoScroll_ = 0;
            screen_ = Screen::PARTY;
            break;
        case 1: startLocalBattle(); break;
        case 2: // Gyms
            loadLordSave();
            gymSel_ = 0;
            screen_ = Screen::GYM_SELECT;
            break;
    }
}

void TerminalUI::activateSystemItem(int item) {
    switch (item) {
        case 0: screen_ = Screen::HELP;         break;
        case 1: screen_ = Screen::CONFIRM_QUIT; break;
    }
}

int TerminalUI::tabItemCount(int tab) const {
    switch (tab) {
        case 0: return MESH_COUNT;
        case 1: return LOCAL_COUNT;
        case 2: return SYSTEM_COUNT;
        default: return 0;
    }
}

const char **TerminalUI::tabItems(int tab) const {
    switch (tab) {
        case 0: return (const char **)MESH_ITEMS;
        case 1: return (const char **)LOCAL_ITEMS;
        case 2: return (const char **)SYSTEM_ITEMS;
        default: return nullptr;
    }
}

const char **TerminalUI::tabDescs(int tab) const {
    switch (tab) {
        case 0: return (const char **)MESH_DESC;
        case 1: return (const char **)LOCAL_DESC;
        case 2: return (const char **)SYSTEM_DESC;
        default: return nullptr;
    }
}

// ── Battle ────────────────────────────────────────────────────────────────────

void TerminalUI::battleLogSinkStatic(const char *line, void *ctx) {
    static_cast<TerminalUI*>(ctx)->battleLogSink(line);
}

void TerminalUI::battleLogSink(const char *line) {
    if (battleLog_.size() > 100) battleLog_.erase(battleLog_.begin());
    battleLog_.push_back(line ? line : "");
}

uint8_t TerminalUI::avgPartyLevel() const {
    if (!hasParty_ || party_.count == 0) return 10;
    int sum = 0;
    for (int i = 0; i < (int)party_.count; i++) {
        uint8_t lv = party_.mons[i].level ? party_.mons[i].level : party_.mons[i].boxLevel;
        sum += lv;
    }
    return (uint8_t)(sum / party_.count);
}

void TerminalUI::startLocalBattle() {
    if (!hasParty_ || partyCount_ == 0) {
        lastEventText_ = "No party loaded! Drop a .sav in /tmp/mm-test-saves/";
        screen_ = Screen::DAYCARE_EVENT;
        return;
    }

    // Build Gen1Party from partySlots_ - uses REAL moves from SAV
    buildPlayerPartyForBattle();

    battleLog_.clear();
    roguelike_  = false;
    localSide_  = 0;
    moveSel_    = 0;
    switchMode_ = false;
}

// Shared party builder used by Fight, Run, and Gyms.  Moves come from the
// SAV (parsed in parsePartyUpdate); HP / stats are derived from base stats
// by initBattlePokeFromBase so they match the engine's expectations.
void TerminalUI::buildPlayerPartyForBattle() {
    memset(&party_, 0, sizeof(party_));
    party_.count = (uint8_t)partyCount_;
    for (int i = 0; i < partyCount_; i++) {
        const PartySlot &s = partySlots_[i];

        // engine.start() runs initBattlePokeFromSave() on each Gen1Pokemon,
        // which derives stats from species + level + dvs + stat-experience.
        // Populate ALL of those fields from the SAV so a trained Lv70 Mewtwo
        // gets its real ~360 HP / 250 SPC instead of the ~200 / 165 you'd
        // get from base stats with avg DVs and 0 stat exp.
        party_.mons[i].species  = (s.dex < 152) ? dexToInternal[s.dex] : 0;
        party_.mons[i].level    = s.level;
        party_.mons[i].boxLevel = s.level;
        party_.mons[i].dvs[0]   = s.dvs[0];
        party_.mons[i].dvs[1]   = s.dvs[1];
        auto setBE16 = [](uint8_t *b, uint16_t v){
            b[0] = (v >> 8) & 0xFF; b[1] = v & 0xFF;
        };
        setBE16(party_.mons[i].hpExp,  s.statExp[0]);
        setBE16(party_.mons[i].atkExp, s.statExp[1]);
        setBE16(party_.mons[i].defExp, s.statExp[2]);
        setBE16(party_.mons[i].spdExp, s.statExp[3]);
        setBE16(party_.mons[i].spcExp, s.statExp[4]);
        memcpy(party_.mons[i].moves, s.moves, 4);
        // PP defaults to canonical max for each move
        for (int m = 0; m < 4; m++) {
            const Gen1MoveData *md = gen1Move(s.moves[m]);
            party_.mons[i].pp[m] = md ? md->pp : 0;
        }
        // Leave hp[2] at zero - initBattlePokeFromSave heals to maxHp on
        // start when source HP is zero.

        // Encode nickname in Gen 1 charset (engine decodes back to ASCII)
        memset(party_.nicknames[i], 0x50, 11);
        const char *nick = s.nick[0] ? s.nick : s.name;
        for (int j = 0; nick[j] && j < 10; j++) {
            char c = nick[j];
            if      (c >= 'A' && c <= 'Z') party_.nicknames[i][j] = 0x80 + (c - 'A');
            else if (c >= 'a' && c <= 'z') party_.nicknames[i][j] = 0xA0 + (c - 'a');
            else                           party_.nicknames[i][j] = 0x7F;
        }
    }
    inBattle_   = true;

    uint8_t avgLv = avgPartyLevel();

    // CPU party: Rival-style team at similar level
    static const uint8_t RIVAL_MONS[][4] = {
        {4, 1, 33, 45},   // Charmander: Scratch, Growl, ...
        {7, 1, 33, 10},   // Squirtle: Tackle, Tail Whip
        {1, 45, 22, 33},  // Bulbasaur: Tackle, Vine Whip, Tail Whip, Growl
        {25, 84, 9, 73},  // Pikachu: ThunderShock, Thunder Wave, Tail Whip, Quick Attack
        {52, 10, 44, 98}, // Meowth: Scratch, Growl, Bite, Pay Day
        {6, 52, 19, 17},  // Charizard: Flamethrower, Slash, Fly, Wing Attack
    };
    Gen1Party cpu = {};
    int cpuCount  = (int)party_.count;
    if (cpuCount > 6) cpuCount = 6;
    cpu.count = (uint8_t)cpuCount;
    for (int i = 0; i < cpuCount; i++) {
        uint8_t idx  = i % 6;
        uint8_t mon  = RIVAL_MONS[idx][0];
        cpu.species[i] = mon;
        Gen1BattleEngine::BattlePoke tmp;
        Gen1BattleEngine::initBattlePokeFromBase(tmp, mon, avgLv, RIVAL_MONS[idx] + 1);
        // Pack back into Gen1Pokemon format (simplified - engine re-reads from party)
        cpu.mons[i].species  = mon;
        cpu.mons[i].level    = avgLv;
        cpu.mons[i].boxLevel = avgLv;
        memcpy(cpu.mons[i].moves, RIVAL_MONS[idx] + 1, 4);
    }
    cpu.count = (uint8_t)cpuCount;

    uint32_t seed = (uint32_t)(millis() ^ (uint32_t)(uintptr_t)this);
    engine_.start(party_, cpu, seed);
    screen_  = Screen::BATTLE;
}

void TerminalUI::startRoguelike() {
    if (!hasParty_ || partyCount_ == 0) {
        lastEventText_ = "No party loaded! Drop a .sav in /tmp/mm-test-saves/";
        screen_ = Screen::DAYCARE_EVENT;
        return;
    }

    battleLog_.clear();
    roguelike_  = true;
    localSide_  = 0;
    moveSel_    = 0;
    switchMode_ = false;
    inBattle_   = true;

    // Wild encounter: random species from a pool
    static const uint8_t WILD_POOL[] = {
        16, 19, 21, 39, 41, 43, 46, 48, 52, 54,  // early route mons
        60, 69, 72, 74, 79, 81, 84, 86, 90, 92,  // water / cave mons
    };
    uint8_t avgLv = avgPartyLevel();
    int lvVar = (rand() % 7) - 3;
    uint8_t wildLv = (uint8_t)(avgLv + lvVar);
    if (wildLv < 2) wildLv = 2;
    if (wildLv > 100) wildLv = 100;

    uint8_t wildDex = WILD_POOL[rand() % 20];
    // Four moves chosen by level
    uint8_t wildMoves[4] = {1, 33, 0, 0};  // Pound + Growl default
    if (wildLv >= 10) wildMoves[2] = (wildDex % 2 == 0) ? 9 : 45;
    if (wildLv >= 20) wildMoves[3] = (wildDex % 3 == 0) ? 10 : 22;

    Gen1Party wild = {};
    wild.count = 1;
    wild.species[0] = wildDex;
    wild.mons[0].species  = wildDex;
    wild.mons[0].level    = wildLv;
    wild.mons[0].boxLevel = wildLv;
    memcpy(wild.mons[0].moves, wildMoves, 4);

    uint32_t seed = (uint32_t)(millis() ^ (uint32_t)wildDex ^ (uint32_t)wildLv);
    engine_.start(party_, wild, seed);
    screen_ = Screen::BATTLE;
}

// Snapshot enemy HP, execute the turn, detect newly-fainted enemies, and
// credit XP to the player slot that was active at the moment of the kill.
// Gen-1-ish formula: enemyLevel * 12 (trainer multiplier baked in).  Real
// Pokered uses base_exp * level / 7; without per-species base_exp the linear
// approximation is close enough at low levels.
void TerminalUI::runTurnWithXp() {
    Gen1BattleEngine::BattleParty &ep = engine_.party(1 - localSide_);
    Gen1BattleEngine::BattleParty &pp = engine_.party(localSide_);

    uint16_t preHp[Gen1BattleEngine::MAX_PARTY] = {};
    for (uint8_t i = 0; i < ep.count; i++) preHp[i] = ep.mons[i].hp;
    uint8_t killerSlot = pp.active;

    engine_.executeTurn(battleLogSinkStatic, this);

    // Any enemy mon whose HP went from >0 to 0 was just KO'd this turn.
    for (uint8_t i = 0; i < ep.count; i++) {
        if (preHp[i] > 0 && ep.mons[i].hp == 0) {
            uint8_t enemyLevel = ep.mons[i].level;
            uint8_t enemyDex   = ep.mons[i].species;  // engine stores dex
            // Real Gen 1 formula: (baseExp * level / 7) * 1.5 (trainer mult).
            // Magikarp Lv5 = 23*5/7*1.5 = 24 XP.
            // Mewtwo Lv70 = 220*70/7*1.5 = 3300 XP.
            // Brock's Onix Lv14 = 108*14/7*1.5 = 324 XP.
            uint32_t xp = gen1XpYield(enemyDex, enemyLevel, /*isTrainer=*/true);
            if (killerSlot < 6) {
                sessionXp_[killerSlot]   += xp;
                slotLevelXp_[killerSlot] += xp;
            }

            char line[64];
            snprintf(line, sizeof(line), "%s gained %u EXP!",
                     pp.mons[killerSlot].nickname, (unsigned)xp);
            battleLog_.push_back(line);

            // Medium-fast level-up: XP needed to gain ONE level from L to
            // L+1 = (L+1)^3 - L^3.  slotLevelXp_ accumulates until it
            // crosses that delta, then we bump the engine's BattlePoke
            // level, scale stats linearly, and add the maxHp delta to
            // current HP (Gen 1 heal-on-level).
            Gen1BattleEngine::BattlePoke &mon = pp.mons[killerSlot];
            while (mon.level < 100 && killerSlot < 6) {
                uint32_t L = mon.level;
                uint32_t levelDelta = (L + 1) * (L + 1) * (L + 1) - L * L * L;
                if (slotLevelXp_[killerSlot] < levelDelta) break;
                slotLevelXp_[killerSlot] -= levelDelta;

                uint8_t newLevel = mon.level + 1;
                uint16_t oldMax  = mon.maxHp;
                mon.maxHp = (uint16_t)((uint32_t)mon.maxHp * newLevel / mon.level);
                mon.atk   = (uint16_t)((uint32_t)mon.atk   * newLevel / mon.level);
                mon.def   = (uint16_t)((uint32_t)mon.def   * newLevel / mon.level);
                mon.spd   = (uint16_t)((uint32_t)mon.spd   * newLevel / mon.level);
                mon.spc   = (uint16_t)((uint32_t)mon.spc   * newLevel / mon.level);
                if (mon.maxHp > oldMax) mon.hp += (mon.maxHp - oldMax);
                mon.level = newLevel;

                snprintf(line, sizeof(line), "%s grew to L%u!",
                         mon.nickname, (unsigned)newLevel);
                battleLog_.push_back(line);
            }
        }
    }

    engine_.autoReplaceIfFainted(0, battleLogSinkStatic, this);
    engine_.autoReplaceIfFainted(1, battleLogSinkStatic, this);
    battleResult_ = engine_.result();
    if (battleResult_ != Gen1BattleEngine::Result::ONGOING) {
        inBattle_ = false;
        screen_   = Screen::BATTLE_END;
    }
}

void TerminalUI::battleHandleButton(const ButtonEvent &ev) {
    const Gen1BattleEngine::BattleParty &pp = engine_.party(localSide_);

    switch (ev.button) {
        case GpiButton::UP:
            if (switchMode_) {
                switchSel_ = (switchSel_ + (int)pp.count - 1) % (int)pp.count;
            } else {
                int moveCount = 0;
                for (int i = 0; i < 4; i++) if (pp.mons[pp.active].moves[i]) moveCount++;
                if (moveCount == 0) moveCount = 1;
                moveSel_ = (moveSel_ + moveCount - 1) % moveCount;
            }
            break;
        case GpiButton::DOWN:
            if (switchMode_) {
                switchSel_ = (switchSel_ + 1) % (int)pp.count;
            } else {
                int moveCount = 0;
                for (int i = 0; i < 4; i++) if (pp.mons[pp.active].moves[i]) moveCount++;
                if (moveCount == 0) moveCount = 1;
                moveSel_ = (moveSel_ + 1) % moveCount;
            }
            break;
        case GpiButton::A:
            if (switchMode_) {
                // Switch
                if (switchSel_ != (int)pp.active && pp.mons[switchSel_].hp > 0) {
                    engine_.submitAction(localSide_, 1, (uint8_t)switchSel_);
                    uint8_t cpuA, cpuI;
                    engine_.cpuPickAction(1 - localSide_, cpuA, cpuI);
                    engine_.submitAction(1 - localSide_, cpuA, cpuI);
                    runTurnWithXp();
                    switchMode_ = false;
                }
            } else {
                // Use move
                if (pp.mons[pp.active].moves[moveSel_] != 0) {
                    engine_.submitAction(localSide_, 0, (uint8_t)moveSel_);
                    uint8_t cpuA, cpuI;
                    engine_.cpuPickAction(1 - localSide_, cpuA, cpuI);
                    engine_.submitAction(1 - localSide_, cpuA, cpuI);
                    runTurnWithXp();
                }
            }
            break;
        case GpiButton::B:
            switchMode_ = !switchMode_;
            switchSel_  = (int)pp.active;
            break;
        case GpiButton::START:
            // Flee (roguelike only) / forfeit
            engine_.forfeit(localSide_, battleLogSinkStatic, this);
            battleResult_ = engine_.result();
            inBattle_ = false;
            screen_   = Screen::BATTLE_END;
            break;
        default: break;
    }
}

// ── PvP battle (daemon-driven) ────────────────────────────────────────────────

void TerminalUI::renderPvpBattle() {
    werase(winInfo_);
    werase(winMenu_);

    // HP bars
    char myLabel[16], enemyLabel[16];
    snprintf(myLabel,    sizeof(myLabel),    "YOU");
    snprintf(enemyLabel, sizeof(enemyLabel), "%s", pvpEnemyName_[0] ? pvpEnemyName_ : "ENEMY");
    drawHpBar(winInfo_, 0, 0, cols_, pvpMyHp_,    pvpMyMaxHp_    ? pvpMyMaxHp_    : 1, myLabel);
    drawHpBar(winInfo_, 1, 0, cols_, pvpEnemyHp_, pvpEnemyMaxHp_ ? pvpEnemyMaxHp_ : 1, enemyLabel);

    // PvP log (last BATTLE_LOG_LINES lines)
    int infoRows = getmaxy(winInfo_);
    int logOff = (int)pvpLog_.size() - BATTLE_LOG_LINES;
    if (logOff < 0) logOff = 0;
    for (int i = 0; i < BATTLE_LOG_LINES; i++) {
        int idx = logOff + i;
        if (idx < (int)pvpLog_.size() && 2 + i < infoRows)
            mvwprintw(winInfo_, 2 + i, 1, "%s", pvpLog_[idx].c_str());
    }

    // Turn counter
    mvwprintw(winInfo_, infoRows - 2, 1, "Turn %u  |  T-Deck is server", (unsigned)pvpTurn_);

    // Move menu
    int menuRow = 0;
    mvwprintw(winMenu_, menuRow++, 1, "FIGHT (T-Deck is server):");
    // Show generic move slots 1-4 - PP comes from pvpMyPp_
    static const char *SLOT_NAMES[] = {"Slot 1", "Slot 2", "Slot 3", "Slot 4"};
    for (int i = 0; i < 4; i++) {
        if (i == pvpMoveSel_) wattron(winMenu_, A_REVERSE | COLOR_PAIR(3));
        if (pvpMyPp_[i] == 0 && i > 0) {
            mvwprintw(winMenu_, menuRow++, 2, "%-14s  ---", SLOT_NAMES[i]);
        } else {
            mvwprintw(winMenu_, menuRow++, 2, "%-14s  PP:%2u", SLOT_NAMES[i], (unsigned)pvpMyPp_[i]);
        }
        if (i == pvpMoveSel_) wattroff(winMenu_, A_REVERSE | COLOR_PAIR(3));
        if (menuRow >= MENU_ROWS - 2) break;
    }
    mvwprintw(winMenu_, MENU_ROWS - 2, 1, "[A]Use [UP/DN]Select [Start]Flee");
}

void TerminalUI::renderPvpBattleEnd() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);
    int mid = infoRows / 2;

    const char *resultTxt = "BATTLE ENDED";
    if      (pvpResult_ == 1) resultTxt = "YOU WIN!";
    else if (pvpResult_ == 2) resultTxt = "YOU LOSE...";
    else if (pvpResult_ == 3) resultTxt = "DRAW!";
    else if (pvpResult_ == 4) resultTxt = "Got away!";

    wattron(winInfo_, A_BOLD | COLOR_PAIR(4));
    mvwprintw(winInfo_, mid, (cols_ - (int)strlen(resultTxt)) / 2, "%s", resultTxt);
    wattroff(winInfo_, A_BOLD | COLOR_PAIR(4));

    int logOff = (int)pvpLog_.size() - 4;
    if (logOff < 0) logOff = 0;
    for (int i = 0; i < 4; i++) {
        int idx = logOff + i;
        if (idx < (int)pvpLog_.size())
            mvwprintw(winInfo_, mid + 2 + i, 1, "%s", pvpLog_[idx].c_str());
    }

    mvwprintw(winMenu_, 1, 1, "[A] or [B] to return to menu");
}

void TerminalUI::pvpBattleButton(const ButtonEvent &ev) {
    switch (ev.button) {
        case GpiButton::UP:
            pvpMoveSel_ = (pvpMoveSel_ + 3) % 4;
            break;
        case GpiButton::DOWN:
            pvpMoveSel_ = (pvpMoveSel_ + 1) % 4;
            break;
        case GpiButton::A: {
            // Send BATTLE_ACTION to daemon
            char cmd[128];
            snprintf(cmd, sizeof(cmd),
                     "{\"cmd\":\"BATTLE_ACTION\",\"action\":0,\"index\":%d,\"turn\":%d}",
                     pvpMoveSel_, (int)pvpTurn_);
            ipc_.send(cmd);
            break;
        }
        case GpiButton::START: {
            // Flee
            char cmd[64];
            snprintf(cmd, sizeof(cmd),
                     "{\"cmd\":\"BATTLE_ACTION\",\"action\":2,\"index\":0,\"turn\":%d}",
                     (int)pvpTurn_);
            ipc_.send(cmd);
            break;
        }
        default: break;
    }
}

void TerminalUI::pvpBattleEndButton(const ButtonEvent &ev) {
    if (ev.button == GpiButton::A || ev.button == GpiButton::B) {
        screen_ = Screen::MENU;
        pvpLog_.clear();
    }
}

void TerminalUI::parseBattleUpdate(const std::string &msg) {
    pvpMyHp_    = (uint16_t)jsonGetInt(msg, "my_hp",    0);
    pvpEnemyHp_ = (uint16_t)jsonGetInt(msg, "enemy_hp", 0);
    pvpTurn_    = (uint8_t) jsonGetInt(msg, "turn",     0);
    pvpResult_  = (uint8_t) jsonGetInt(msg, "result",   0);
    pvpMyStatus_    = (uint8_t)jsonGetInt(msg, "my_status",    0);
    pvpEnemyStatus_ = (uint8_t)jsonGetInt(msg, "enemy_status", 0);
    pvpNeedSwitch_  = jsonGetInt(msg, "need_switch", 0) != 0;

    // Extract PP array: "my_pp":[25,30,0,0]
    {
        size_t pos = msg.find("\"my_pp\":[");
        if (pos != std::string::npos) {
            pos += 9;
            for (int i = 0; i < 4; i++) {
                pvpMyPp_[i] = (uint8_t)atoi(msg.c_str() + pos);
                pos = msg.find(',', pos);
                if (pos == std::string::npos) break;
                pos++;
            }
        }
    }

    // Extract log lines from "log":["line1","line2"]
    {
        size_t pos = msg.find("\"log\":[");
        if (pos != std::string::npos) {
            pos += 7;
            while (pos < msg.size() && msg[pos] != ']') {
                if (msg[pos] == '"') {
                    size_t end = msg.find('"', pos + 1);
                    if (end == std::string::npos) break;
                    std::string line = msg.substr(pos + 1, end - pos - 1);
                    if (!line.empty()) {
                        if (pvpLog_.size() > 100) pvpLog_.erase(pvpLog_.begin());
                        pvpLog_.push_back(line);
                    }
                    pos = end + 1;
                } else {
                    pos++;
                }
            }
        }
    }

    // Set max HP on first update if not set
    if (pvpMyMaxHp_ == 0 && pvpMyHp_ > 0) pvpMyMaxHp_ = pvpMyHp_;
    if (pvpEnemyMaxHp_ == 0 && pvpEnemyHp_ > 0) pvpEnemyMaxHp_ = pvpEnemyHp_;

    // Switch to PvP battle screen if not already there
    if (screen_ != Screen::PVP_BATTLE && screen_ != Screen::PVP_BATTLE_END)
        screen_ = Screen::PVP_BATTLE;

    // Check for battle end
    if (pvpResult_ != 0 && pvpResult_ != 0xFF) {
        screen_ = Screen::PVP_BATTLE_END;
    }
}

// ── IPC message handling ──────────────────────────────────────────────────────

void TerminalUI::onIpcMessage(const std::string &msg) {
    std::string type = jsonGetStr(msg, "type");
    if      (type == "PARTY_UPDATE")       parsePartyUpdate(msg);
    else if (type == "STATUS")             parseStatus(msg);
    else if (type == "DAYCARE_EVENT")      parseDaycareEvent(msg);
    else if (type == "CHALLENGE_RECEIVED") parseChallenge(msg);
    else if (type == "ACHIEVEMENT")        parseAchievement(msg);
    else if (type == "BATTLE_UPDATE")      parseBattleUpdate(msg);
    else if (type == "NEIGHBORS")          parseNeighbors(msg);
    else if (type == "NEIGHBOR_PARTY")     parseNeighborParty(msg);
    else if (type == "SAV_WRITEBACK") {
        bool ok = jsonGetInt(msg, "ok", 0) != 0;
        int applied = jsonGetInt(msg, "applied", 0);
        if (ok && applied) pushActivity("> SAV write OK -- levels persisted");
        else if (ok)       pushActivity("> SAV write skipped (no XP to apply)");
        else               pushActivity("> SAV write FAILED");
    }
    else if (type == "BEACON_RESULT") {
        bool ok = jsonGetInt(msg, "ok", 0) != 0;
        int party = jsonGetInt(msg, "party", 0);
        uint32_t nid = (uint32_t)jsonGetInt(msg, "node_id", 0);
        if (ok && party > 0)
            pushActivity("> Beacon sent (party=%d, node=0x%08X)", party, nid);
        else if (ok)
            pushActivity("> Beacon sent (no party - load a .sav)");
        else
            pushActivity("> Beacon FAILED - no serial");
    }
    else if (type == "NODE_INFO") {
        uint32_t nid = (uint32_t)jsonGetInt(msg, "node_id", 0);
        std::string sn = jsonGetStr(msg, "short_name");
        pushActivity("> Radio: 0x%08X %s", nid, sn.c_str());
    }
    else if (type == "PONG") { /* keepalive ack */ }
}

void TerminalUI::parsePartyUpdate(const std::string &msg) {
    int count = jsonGetInt(msg, "count", 0);
    if (count <= 0 || count > 6) return;

    bool first = !hasParty_;
    partyCount_ = count;
    hasParty_   = true;

    // Parse the "party":[{...},{...}] array
    // Each slot: {"dex":N,"name":"...","nick":"...","level":N,"sav_level":N,
    //             "total_xp_gained":N,"total_hours":N,"mood":N}
    size_t pos = msg.find("\"party\":[");
    if (pos == std::string::npos) return;
    pos += 9;  // skip past "party":[

    for (int i = 0; i < count && pos < msg.size(); i++) {
        // Find next { opening the slot object
        pos = msg.find('{', pos);
        if (pos == std::string::npos) break;
        size_t end = msg.find('}', pos);
        if (end == std::string::npos) break;
        std::string slot = msg.substr(pos, end - pos + 1);

        partySlots_[i].dex      = (uint8_t)jsonGetInt(slot, "dex",            0);
        partySlots_[i].level    = (uint8_t)jsonGetInt(slot, "level",          0);
        partySlots_[i].savLevel = (uint8_t)jsonGetInt(slot, "sav_level",      0);
        partySlots_[i].xpGained = (uint32_t)jsonGetInt(slot, "total_xp_gained", 0);
        partySlots_[i].hours    = (uint32_t)jsonGetInt(slot, "total_hours",    0);

        std::string nick = jsonGetStr(slot, "nick");
        std::string name = jsonGetStr(slot, "name");
        strncpy(partySlots_[i].nick, nick.c_str(), sizeof(partySlots_[i].nick) - 1);
        strncpy(partySlots_[i].name, name.c_str(), sizeof(partySlots_[i].name) - 1);

        // Parse moves array: "moves":[N,N,N,N]
        memset(partySlots_[i].moves, 0, 4);
        size_t mp = slot.find("\"moves\":[");
        if (mp != std::string::npos) {
            mp += 9;
            for (int j = 0; j < 4 && mp < slot.size(); j++) {
                partySlots_[i].moves[j] = (uint8_t)atoi(slot.c_str() + mp);
                size_t comma = slot.find(',', mp);
                if (comma == std::string::npos) break;
                mp = comma + 1;
            }
        }

        // Parse "dvs":[N,N]
        size_t dp = slot.find("\"dvs\":[");
        if (dp != std::string::npos) {
            dp += 7;
            partySlots_[i].dvs[0] = (uint8_t)atoi(slot.c_str() + dp);
            size_t c = slot.find(',', dp);
            if (c != std::string::npos)
                partySlots_[i].dvs[1] = (uint8_t)atoi(slot.c_str() + c + 1);
        }

        // Parse "stat_exp":[hp,atk,def,spd,spc]
        size_t sp = slot.find("\"stat_exp\":[");
        if (sp != std::string::npos) {
            sp += 12;
            for (int j = 0; j < 5 && sp < slot.size(); j++) {
                partySlots_[i].statExp[j] = (uint16_t)atoi(slot.c_str() + sp);
                size_t comma = slot.find(',', sp);
                if (comma == std::string::npos) break;
                sp = comma + 1;
            }
        }

        pos = end + 1;
    }

    // On first party load, dump T-Deck-style listing into the activity feed
    if (first) {
        uint32_t mins = (millis() - startMs_) / 60000;
        pushActivity("[t+%um] Party (%d):", (unsigned)mins, count);
        for (int i = 0; i < count; i++) {
            const PartySlot &s = partySlots_[i];
            const char *nick = s.nick[0] ? s.nick : (s.name[0] ? s.name : "(noname)");
            pushActivity("%d. %-10s Lv %3d", i + 1, nick, (int)s.level);
        }
    }
}

void TerminalUI::parseDaycareEvent(const std::string &msg) {
    pushActivity("> DAYCARE_EVENT rx (%zuB)", msg.size());
    lastEventText_ = jsonGetStr(msg, "text");
    lastEventXp_   = jsonGetInt(msg, "xp", 0);
    lastEventSlot_ = jsonGetInt(msg, "slot", 0);
    if ((size_t)lastEventSlot_ < 6)
        sessionXp_[lastEventSlot_] += (uint32_t)lastEventXp_;
    // Activity feed entry - truncate to fit one line
    std::string snippet = lastEventText_.substr(0, 30);
    if (lastEventXp_ > 0)
        pushActivity("> %s (+%d XP)", snippet.c_str(), lastEventXp_);
    else
        pushActivity("> %s", snippet.c_str());
}

void TerminalUI::parseChallenge(const std::string &msg) {
    challengeNodeId_ = (uint32_t)jsonGetInt(msg, "node_id", 0);
    std::string tr   = jsonGetStr(msg, "trainer");
    strncpy(challengerName_, tr.c_str(), sizeof(challengerName_) - 1);
    challengerName_[sizeof(challengerName_) - 1] = '\0';
    hasPendingChallenge_ = true;
    challengeSel_ = 0;
    screen_ = Screen::CHALLENGE;
}

void TerminalUI::parseAchievement(const std::string &msg) {
    std::string name = jsonGetStr(msg, "name");
    std::string desc = jsonGetStr(msg, "description");
    lastEventText_ = "Achievement: " + name + " - " + desc;
    lastEventXp_   = 0;
    pushActivity("* Achievement: %s", name.c_str());
}

// Parse the daemon's NEIGHBORS push and populate the display cache so the
// MESH > Neighbors screen shows MM trainers by short name + lead pokemon.
void TerminalUI::parseNeighbors(const std::string &msg) {
    int count = jsonGetInt(msg, "count", 0);
    pushActivity("> NEIGHBORS rx (count=%d, %zuB)", count, msg.size());
    if (count <= 0) { neighborDisplayCount_ = 0; return; }
    if (count > MAX_NEIGHBORS_DISPLAY) count = MAX_NEIGHBORS_DISPLAY;

    size_t pos = msg.find("\"list\":[");
    if (pos == std::string::npos) return;
    pos += 8;

    for (int i = 0; i < count && pos < msg.size(); i++) {
        pos = msg.find('{', pos);
        if (pos == std::string::npos) break;
        size_t end = msg.find('}', pos);
        if (end == std::string::npos) break;
        std::string slot = msg.substr(pos, end - pos + 1);

        std::string sn   = jsonGetStr(slot, "short_name");
        std::string gn   = jsonGetStr(slot, "game_name");
        std::string nick = jsonGetStr(slot, "lead_nick");
        int pc           = jsonGetInt(slot, "party_count", 0);
        int lv           = jsonGetInt(slot, "lead_level", 0);
        uint32_t nid     = (uint32_t)jsonGetInt(slot, "node_id", 0);

        NeighborEntry &n = neighbors_[i];
        n.nodeId = nid;
        strncpy(n.shortName, sn.c_str(),   sizeof(n.shortName) - 1);
        strncpy(n.gameName,  gn.c_str(),   sizeof(n.gameName)  - 1);
        strncpy(n.lead,      nick.c_str(), sizeof(n.lead)      - 1);
        n.partyCount = pc;
        n.leadLevel  = lv;

        pushActivity("> Neighbor: %s (%s) %s Lv%d", n.shortName, n.gameName,
                     n.lead[0] ? n.lead : "(?)", n.leadLevel);
        pos = end + 1;
    }
    neighborDisplayCount_ = count;
    neighborCount_        = count;  // also update the status-bar count
}

// Async fight: daemon ships back the chosen neighbor's full party.  Build a
// Gen1Party from it and immediately enter a local battle.  battleEndButton
// sees `asyncFightActive_` and broadcasts the win/loss back to the mesh.
void TerminalUI::parseNeighborParty(const std::string &msg) {
    bool ok = jsonGetInt(msg, "ok", 0) != 0;
    if (!ok) {
        pushActivity("> Could not load %s's party", asyncFightTrainer_);
        asyncFightActive_ = false;
        return;
    }
    int count = jsonGetInt(msg, "count", 0);
    if (count <= 0 || count > 6) {
        pushActivity("> %s has no party loaded", asyncFightTrainer_);
        asyncFightActive_ = false;
        return;
    }

    // Build the opponent's Gen1Party from the JSON.
    Gen1Party foe = {};
    foe.count = (uint8_t)count;

    size_t pos = msg.find("\"party\":[");
    if (pos == std::string::npos) { asyncFightActive_ = false; return; }
    pos += 9;

    for (int i = 0; i < count; i++) {
        pos = msg.find('{', pos);
        if (pos == std::string::npos) break;
        size_t end = msg.find('}', pos);
        if (end == std::string::npos) break;
        std::string slot = msg.substr(pos, end - pos + 1);

        uint8_t dex   = (uint8_t)jsonGetInt(slot, "dex", 0);
        uint8_t level = (uint8_t)jsonGetInt(slot, "level", 5);
        if (level < 2)  level = 2;
        if (level > 100) level = 100;
        uint8_t moves[4] = {0};
        size_t mp = slot.find("\"moves\":[");
        if (mp != std::string::npos) {
            mp += 9;
            for (int j = 0; j < 4 && mp < slot.size(); j++) {
                moves[j] = (uint8_t)atoi(slot.c_str() + mp);
                size_t comma = slot.find(',', mp);
                if (comma == std::string::npos) break;
                mp = comma + 1;
            }
        }

        // Stats derived from base + avg DVs / 0 stat-exp (their beacon
        // doesn't carry training data -- this is a faithful snapshot at
        // their *current visible* level).
        Gen1BattleEngine::BattlePoke tmp;
        Gen1BattleEngine::initBattlePokeFromBase(tmp, dex, level, moves);
        foe.mons[i].species  = (dex < 152) ? dexToInternal[dex] : 0;
        foe.mons[i].level    = level;
        foe.mons[i].boxLevel = level;
        foe.mons[i].dvs[0]   = 0x88;
        foe.mons[i].dvs[1]   = 0x88;
        memcpy(foe.mons[i].moves, moves, 4);
        for (int k = 0; k < 4; k++) {
            const Gen1MoveData *md = gen1Move(moves[k]);
            foe.mons[i].pp[k] = md ? md->pp : 0;
        }
        std::string nick = jsonGetStr(slot, "nick");
        memset(foe.nicknames[i], 0x50, 11);
        for (int j = 0; j < 10 && j < (int)nick.size(); j++) {
            char c = nick[j];
            if      (c >= 'A' && c <= 'Z') foe.nicknames[i][j] = 0x80 + (c - 'A');
            else if (c >= 'a' && c <= 'z') foe.nicknames[i][j] = 0xA0 + (c - 'a');
            else                           foe.nicknames[i][j] = 0x7F;
        }
        pos = end + 1;
    }

    // Build our party and start the battle.
    buildPlayerPartyForBattle();
    battleLog_.clear();
    moveSel_      = 0;
    switchMode_   = false;
    inBattle_     = true;
    inGymBattle_  = false;
    localSide_    = 0;
    battleResult_ = Gen1BattleEngine::Result::ONGOING;
    uint32_t seed = (uint32_t)(millis() ^ asyncFightNodeId_);
    engine_.start(party_, foe, seed);
    screen_       = Screen::BATTLE;
}

void TerminalUI::parseStatus(const std::string &msg) {
    neighborCount_  = jsonGetInt(msg, "neighbors", 0);
    daycareActive_  = jsonGetInt(msg, "active", 0) != 0;
    std::string ev  = jsonGetStr(msg, "last_event");
    if (!ev.empty()) lastEventText_ = ev;
    lastEventXp_    = jsonGetInt(msg, "last_event_xp", 0);

    // Parse neighbor entries (simple: look for shortName fields)
    neighborDisplayCount_ = 0;
    // TODO: daemon should push structured NEIGHBORS; for now use neighborCount_
}

// ── Gym select ────────────────────────────────────────────────────────────────

void TerminalUI::loadLordSave()
{
    if (!lordLoaded_) {
        lordLoad(lordSave_);
        lordLoaded_ = true;
    }
}

void TerminalUI::renderGymSelect()
{
    werase(winInfo_);
    werase(winMenu_);

    wattron(winInfo_, A_BOLD);
    mvwprintw(winInfo_, 0, 1, "=== Legend of Charizard ===");
    wattroff(winInfo_, A_BOLD);

    // Count badges
    int badges = __builtin_popcount(lordSave_.badges);
    mvwprintw(winInfo_, 1, 1, "Badges: %d/8", badges);

    int infoRows = getmaxy(winInfo_);
    for (int i = 0; i < LORD_GYM_COUNT && i + 3 < infoRows; i++) {
        const LordGym *g = lordGym((uint8_t)i);
        if (!g) continue;

        bool    cleared = (lordSave_.badges >> g->badgeBit) & 1;
        bool    sel     = (i == gymSel_);

        if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));

        char badge_mark = cleared ? '*' : ' ';
        mvwprintw(winInfo_, 2 + i, 0,
                  " %c%d. %-10s %-8s  %-12s  %s",
                  badge_mark, i + 1,
                  g->city, g->badgeName,
                  g->leaderName, cleared ? "Cleared" : "5-round gauntlet");

        if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
    }

    mvwprintw(winMenu_, 1, 1, "[W/S] Select  [K] Fight  [L] Back");
}

void TerminalUI::gymSelectButton(const ButtonEvent &ev)
{
    switch (ev.button) {
        case GpiButton::UP:
            gymSel_ = (gymSel_ + LORD_GYM_COUNT - 1) % LORD_GYM_COUNT;
            break;
        case GpiButton::DOWN:
            gymSel_ = (gymSel_ + 1) % LORD_GYM_COUNT;
            break;
        case GpiButton::A:
            // Gauntlet: always start at the first trainer.  No mid-gym
            // resume.  Battle-end handler chains us through 0->4 on wins.
            startGymBattle((uint8_t)gymSel_, 0);
            break;
        case GpiButton::B:
            screen_ = Screen::MENU;
            break;
        default: break;
    }
}

void TerminalUI::startGymBattle(uint8_t gymIdx, uint8_t trainerIdx)
{
    if (!hasParty_ || partyCount_ == 0) {
        lastEventText_ = "No party! Load a .sav first.";
        screen_ = Screen::DAYCARE_EVENT;
        return;
    }

    // Build player party from partySlots_ - using REAL moves from SAV
    buildPlayerPartyForBattle();

    // Build gym party
    Gen1Party gymParty;
    if (!lordBuildGymParty(gymIdx, trainerIdx, gymParty)) {
        lastEventText_ = "Could not build gym party.";
        screen_ = Screen::DAYCARE_EVENT;
        return;
    }

    // Start battle
    battleLog_.clear();
    roguelike_  = false;
    localSide_  = 0;
    moveSel_    = 0;
    switchMode_ = false;
    inBattle_   = true;

    // Store gym context for post-battle handling
    pendingGymIdx_     = gymIdx;
    pendingTrainerIdx_ = trainerIdx;
    inGymBattle_       = true;

    uint32_t seed = (uint32_t)(millis() ^ ((uint32_t)gymIdx << 8) ^ trainerIdx);
    engine_.start(party_, gymParty, seed);
    screen_ = Screen::BATTLE;
}

// ── JSON helpers ──────────────────────────────────────────────────────────────

std::string TerminalUI::jsonGetStr(const std::string &j, const char *key) {
    std::string search = std::string("\"") + key + "\":\"";
    size_t pos = j.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    size_t end = j.find('"', pos);
    if (end == std::string::npos) return "";
    return j.substr(pos, end - pos);
}

int TerminalUI::jsonGetInt(const std::string &j, const char *key, int def) {
    std::string search = std::string("\"") + key + "\":";
    size_t pos = j.find(search);
    if (pos == std::string::npos) return def;
    pos += search.size();
    if (pos >= j.size()) return def;
    // Skip optional space
    while (pos < j.size() && j[pos] == ' ') pos++;
    if (pos >= j.size()) return def;
    return atoi(j.c_str() + pos);
}
