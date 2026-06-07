// TerminalUI.cpp - MonsterMesh Pi menu-driven ncurses UI
// Navigation: D-pad moves within/between tabs. A=select, B=back, Start=menu, Select=help.
// No text input - every action is reached through the MESH / LOCAL / SYSTEM menu tree.

#include "TerminalUI.h"
#include "SpriteRender.h"
#include "../battle/showdown_gen1_moves.h"
#include "../shared/DaycareSavPatcher.h"   // dexToInternal[] table
#include "../shared/Gen1BaseExp.h"         // gen1XpYield()
#include "../shared/LordE4.h"              // Indigo Plateau rosters
#include "../shared/LordLogic.h"           // NG+ tier state + scaling
#include "../shared/KantoZones.h"          // Pentest Pikachu zone encounters
#include <ncurses.h>
#include <locale.h>
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

    // Resize the host terminal to match the GPI Case 2W resolution.
    // 640x480 @ standard 8x16 console font = 80 cols x 30 rows.
    // CSI 8 ; rows ; cols t resizes; CSI 3 ; 0 ; 0 t moves to origin;
    // CSI 1 t de-iconifies.  iTerm2, xterm, and Terminal.app honor these;
    // the Pi's fbcon ignores them (it's already exactly sized by the
    // framebuffer driver).
    printf("\033[3;0;0t\033[1t\033[8;30;80t");
    fflush(stdout);

    // Honour the user's UTF-8 locale so ncurses can paint the upper-half-
    // block (▀, U+2580) sprite glyph used by SpriteRender.  Without this,
    // multibyte sequences get split per-byte and the sprites look like
    // garbled box-drawing on UTF-8 terminals.
    setlocale(LC_ALL, "");

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
        // GBC DMG green-scale palette: every window paints ink on paper, so
        // the entire terminal reads like a 1989 Game Boy screen.  Per-species
        // GBC palettes (yellow Pikachu, orange Charizard, etc.) get swapped
        // in at sprite-render time over a separate pool of color slots.
        //
        // The 4 DMG shades are the same values pikachu2/config2.h uses, in
        // RGB565 form — converted here to ncurses init_color's 0-1000 scale.
        //   #9bbc0f → R=608  G=737  B=59   (paper, lightest)
        //   #8bac0f → R=545  G=674  B=59   (light shade)
        //   #306232 → R=188  G=384  B=188  (dark shade)
        //   #0f380f → R=59   G=219  B=59   (ink, darkest)
        // The DMG green slots normally live at 16-19 (clear of the 16 ANSI
        // colors a 256-colour terminal reserves).  But the Pi's framebuffer
        // console reports only COLORS=8 — yet it CAN repaint its palette
        // (can_change_color()==1).  So on a small palette we redefine four of
        // the low ANSI slots instead.  Everything downstream goes through
        // init_pair(), so the rest of the UI is identical either way; only the
        // four slot indices change.  This is what makes the Pi render the real
        // Game Boy green instead of the generic ANSI fallback below.
        // Pentest Pikachu renders entirely in the SDL window, so it skips the
        // GB-green palette — that keeps the ncurses console plain black, with
        // no green-terminal flash before/after the battle.
        if (!pentestMode_ && can_change_color() && COLORS >= 8) {
            int paper, light, dark, ink;
            if (COLORS >= 20) {
                paper = GB_PAPER_; light = GB_LIGHT_; dark = GB_DARK_; ink = GB_INK_;
            } else {
                // Map onto low slots: white→paper (lightest), black→ink
                // (darkest), so even default/un-paired cells read as GB green.
                paper = COLOR_WHITE; light = COLOR_CYAN;
                dark  = COLOR_GREEN; ink   = COLOR_BLACK;
            }
            init_color(paper, 608, 737,  59);
            init_color(light, 545, 674,  59);
            init_color(dark,  188, 384, 188);
            init_color(ink,    59, 219,  59);
            init_pair(1, ink,   paper);  // HP high (default ink/paper)
            init_pair(2, dark,  paper);  // HP mid
            // Pair 3 stays ink/paper so the `A_REVERSE | COLOR_PAIR(3)`
            // selection-cursor pattern used throughout the UI actually
            // inverts to paper-on-ink.  If 3 were pre-inverted, A_REVERSE
            // would cancel back out and the selection would be invisible.
            init_pair(3, ink,   paper);  // selection / HP low (A_REVERSE flips)
            init_pair(4, dark,  paper);  // header / tab active
            init_pair(5, ink,   paper);  // normal text
            init_pair(6, light, ink);    // achievement / event (light on ink)
            init_pair(7, ink,   paper);  // (was battle white card — alias to default)
            gbColorsActive_ = true;
        } else {
            // Fallback for terminals without init_color() — use the GREEN
            // family which renders close-ish on a 16-color terminal.
            use_default_colors();
            init_pair(1, COLOR_GREEN,  -1);
            init_pair(2, COLOR_YELLOW, -1);
            init_pair(3, COLOR_RED,    -1);
            init_pair(4, COLOR_CYAN,   -1);
            init_pair(5, COLOR_WHITE,  -1);
            init_pair(6, COLOR_MAGENTA,-1);
            init_pair(7, COLOR_BLACK,  COLOR_WHITE);
        }

        // Reserve colour slots 32..39 and pairs 100..131 for per-species
        // GBC sprite palettes (pool A = foe, pool B = player).  Failure is
        // not fatal — drawSprite() bails silently if init() returned false.
        SpriteRender::init();
    }

    // Initial layout sized for the menu screen (the boot screen) — the
    // FIGHT-box variant is applied on demand by applyMenuRowsForScreen().
    menuRows_    = MENU_ROWS_DEFAULT;
    int infoRows = rows_ - STATUS_ROWS - menuRows_;

    winStatus_ = newwin(STATUS_ROWS, cols_, 0,                  0);
    winInfo_   = newwin(infoRows,   cols_, STATUS_ROWS,         0);
    winMenu_   = newwin(menuRows_,  cols_, rows_ - menuRows_,   0);

    // Paint every window paper-green so erases reset to the GB look rather
    // than to the host terminal's default.  stdscr too so resize/clear
    // operations don't expose black underneath.
    if (gbColorsActive_) {
        chtype paper = COLOR_PAIR(5);
        wbkgd(stdscr,    paper);
        wbkgd(winStatus_, paper);
        wbkgd(winInfo_,   paper);
        wbkgd(winMenu_,   paper);
    }

    // Pentest ROM: blank the console to black up front so there's no green (or
    // any) terminal visible in the moment before the SDL battle window opens.
    if (pentestMode_) {
        werase(stdscr);
        wrefresh(stdscr);
    }

    // Must enable keypad + nodelay on the window we actually call wgetch() on
    if (winMenu_) {
        keypad(winMenu_, TRUE);
        nodelay(winMenu_, TRUE);
    }
    // No scrollok — we manually paint every cell, and the auto-scroll
    // triggered when addch hits the bottom-right corner of a window was
    // shifting battle-screen box borders up by one row.

    // Seed RNG for battle
    srand((unsigned)time(nullptr));

    startMs_ = millis();
    pushActivity("MonsterMesh Pi v1 - waiting for daemon...");
    return true;
}

void TerminalUI::shutdown() {
    // Push any pending battle XP to the daemon and ask it to writeback
    // the .sav file before we drop the IPC connection.  Without this, XP
    // earned in a battle that was never closed out via BATTLE_END would
    // vanish on Ctrl+C / Q / SIGTERM.
    // Never write pentest XP back to the real SAV — the pentest Pikachu is a
    // standalone arcade character, not the player's party.
    if (!pentestMode_ && ipc_.isConnected()) {
        flushBattleXpToDaemon();
        // Give the daemon ~250ms to receive + acknowledge before close().
        for (int i = 0; i < 25; i++) {
            ipc_.poll();
            usleep(10000);
        }
    }
    ipc_.disconnect();
    input_.close();
    // Pentest ROM: blank the console to black before tearing down ncurses so no
    // green terminal flashes on the way back to the MonsterMesh system menu.
    if (pentestMode_ && !isendwin()) {
        werase(stdscr);
        wrefresh(stdscr);
    }
    if (winStatus_) { delwin(winStatus_); winStatus_ = nullptr; }
    if (winInfo_)   { delwin(winInfo_);   winInfo_   = nullptr; }
    if (winMenu_)   { delwin(winMenu_);   winMenu_   = nullptr; }
    if (isendwin() == FALSE) endwin();
}

void TerminalUI::run() {
    // Pentest Pikachu ROM: jump straight into the battle screen on boot.
    if (pentestMode_ && !pentestStarted_) {
        pentestStarted_ = true;
        startPentestBattle();
    }

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

        // Keep the SDL battle window responsive even when ncurses is busy.
        // pumpEvents is cheap and a no-op when the window isn't open; we
        // need it every frame so the OS doesn't mark the window as hung
        // and the user can close it via window controls if they wish.
        battleWindow_.pumpEvents();

        // Pentest Pikachu: the fight plays itself — auto-pick a move each beat.
        if (inPentestBattle_) pentestAutoTick();

        render();
        usleep(16666);
    }
}

// Auto-battle driver for the Pentest Pikachu ROM.  Every ~900 ms it lets the
// engine's CPU picker choose a move for BOTH sides and runs the turn, so the
// whole fight is hands-off.  Stops as soon as the battle ends (runTurnWithXp
// flips screen_ to BATTLE_END).
void TerminalUI::pentestAutoTick() {
    // Freeze the auto-fight + auto-advance while the status overlay is up.
    if (pentestShowStatus_) return;
    uint64_t now = millis();

    // Battle over: there's no "YOU WIN" screen for the pentest ROM — you can
    // read the result straight off the frozen battle frame.  Keep that frame
    // on screen for a couple of seconds, then chain into the next scan.
    if (battleResult_ != Gen1BattleEngine::Result::ONGOING) {
        if (pentestEndMs_ == 0) pentestEndMs_ = now;   // begin the linger
        screen_ = Screen::BATTLE;                       // suppress BATTLE_END
        if (now - pentestEndMs_ >= PENTEST_END_MS) startPentestBattle();
        return;
    }

    // Ongoing: auto-pick a move for both sides on a steady beat.
    if (!inBattle_ || screen_ != Screen::BATTLE) return;
    if (now - lastPentestTurnMs_ < PENTEST_TURN_MS) return;
    lastPentestTurnMs_ = now;

    switchMode_ = false;
    uint8_t pa, pi, ca, ci;
    engine_.cpuPickAction(localSide_,     pa, pi);   // auto-pick our move
    engine_.submitAction(localSide_,      pa, pi);
    engine_.cpuPickAction(1 - localSide_, ca, ci);   // foe's move
    engine_.submitAction(1 - localSide_,  ca, ci);
    runTurnWithXp();

    // runTurnWithXp() flips screen_ to BATTLE_END on the finishing blow; undo
    // it immediately so the win screen never shows, and start the linger.
    if (battleResult_ != Gen1BattleEngine::Result::ONGOING) {
        screen_       = Screen::BATTLE;
        pentestEndMs_ = now;
    }
}

// ── Rendering dispatch ────────────────────────────────────────────────────────

// Battle screens want a compact 5-row FIGHT box; menu/list screens want a
// taller (10-row) box so 5 selectable items fit between tab header and
// hint.  Resize on demand — cheap, only fires when screen_ flips between
// the two groups.
void TerminalUI::applyMenuRowsForScreen() {
    bool battle = (screen_ == Screen::BATTLE ||
                   screen_ == Screen::BATTLE_END ||
                   screen_ == Screen::PVP_BATTLE ||
                   screen_ == Screen::PVP_BATTLE_END);
    int want = battle ? MENU_ROWS_BATTLE : MENU_ROWS_DEFAULT;
    if (want == menuRows_) return;
    menuRows_ = want;
    int infoRows = rows_ - STATUS_ROWS - menuRows_;
    // Repaint stdscr first so old border cells in the now-shrunk-or-grown
    // region don't bleed through after the next refresh.
    werase(stdscr);
    wnoutrefresh(stdscr);
    wresize(winInfo_,  infoRows, cols_);
    wresize(winMenu_,  menuRows_, cols_);
    mvwin(winMenu_, rows_ - menuRows_, 0);
    werase(winInfo_);
    werase(winMenu_);
}

void TerminalUI::render() {
    if (!winStatus_ || !winInfo_ || !winMenu_) return;

    // Pentest Pikachu renders ONLY through the SDL window — no ncurses content
    // is painted, so the green terminal never flashes before/after the battle.
    if (pentestMode_) {
        if (!battleWindow_.isOpen()) battleWindow_.open();
        if (battleWindow_.isOpen()) {
            syncBattleWindow();
            battleWindow_.render();
        }
        return;
    }

    // Resize the info / menu split per screen so battle gets a tight
    // FIGHT box and menu screens get room for ~5 item rows.
    applyMenuRowsForScreen();

    // Per-screen background swap is no-op now that every screen runs on
    // the GBC green palette — wbkgd() was set once in startup() and the
    // werase() in each render fn picks it up.  The white-card BATTLE
    // override is intentionally retired.

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

    // SDL2 battle window lifecycle.  Open/sync while we're on a battle screen,
    // close as soon as we leave.  The ncurses sprite renderer stays in place
    // — SDL2 adds a second window, doesn't replace.
    bool battleScreen = (screen_ == Screen::BATTLE ||
                         screen_ == Screen::BATTLE_END ||
                         screen_ == Screen::PVP_BATTLE ||
                         screen_ == Screen::PVP_BATTLE_END);
    if (battleScreen) {
        if (!battleWindow_.isOpen()) battleWindow_.open();
        if (battleWindow_.isOpen()) {
            if (screen_ == Screen::BATTLE || screen_ == Screen::BATTLE_END) {
                syncBattleWindow();
            } else {
                syncPvpBattleWindow();
            }
            battleWindow_.render();
        }
    } else if (battleWindow_.isOpen()) {
        battleWindow_.close();
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
    char buf[160];
    const char *leadName = "---";
    int leadLv = 0;
    if (hasParty_ && partyCount_ > 0) {
        const PartySlot &lead = partySlots_[0];
        leadName = lead.nick[0] ? lead.nick : (lead.name[0] ? lead.name : "???");
        leadLv   = lead.level;
    }

    // Header reads " GPI / RED | Nbrs:N | LEAD Lv## " — shortName comes
    // from the Meshtastic radio identity, trainerName from the SAV's Gen 1
    // player-name field.  Both fall back to "?" until the daemon's
    // NODE_INFO push lands.  ASCII fallback (not em dash "—") because the
    // GPI's framebuffer console runs with TERM=linux which doesn't decode
    // UTF-8 — em dash would render as raw 0xE2 0x80 0x94 bytes ("M-b^@*T"
    // when cat -v'd).
    const char *sname = localShortName_[0]   ? localShortName_   : "?";
    const char *tname = localTrainerName_[0] ? localTrainerName_ : "?";
    snprintf(buf, sizeof(buf),
             " %s / %s | Nbrs:%-2d | %-10s Lv%d",
             sname, tname, neighborCount_, leadName, leadLv);
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
    // ncurses ACS box-drawing.  With CURSES_NEED_WIDE in CMakeLists.txt we
    // link against ncursesw, which emits UTF-8 sequences for ACS_* on a
    // UTF-8 locale (LANG=C.UTF-8 set by launch.sh).  Renders as ┌─┐│└┘.
    box(winMenu_, 0, 0);

    // Tab headers use the exact same colours as the vertical item list below:
    // the active tab gets A_REVERSE | COLOR_PAIR(3) (paper-on-ink chip, like a
    // selected list row) and inactive tabs get COLOR_PAIR(5) (normal ink-on-
    // paper text).  No A_BOLD — on the 8-colour framebuffer console bold
    // brightens the foreground to white, which is what made the tab text pop
    // out lighter than everything else.
    int tabW = (cols_ - 2) / TAB_COUNT;
    for (int t = 0; t < TAB_COUNT; t++) {
        int x = 1 + t * tabW;
        if (t == activeTab_) wattron(winMenu_, A_REVERSE | COLOR_PAIR(3));
        else                 wattron(winMenu_, COLOR_PAIR(5));
        mvwprintw(winMenu_, 1, x, " %-*s", tabW - 1, TAB_NAMES[t]);
        if (t == activeTab_) wattroff(winMenu_, A_REVERSE | COLOR_PAIR(3));
        else                 wattroff(winMenu_, COLOR_PAIR(5));
    }

    // ACS_HLINE under ncursesw → renders as ─ on a UTF-8 console.
    mvwhline(winMenu_, 2, 1, ACS_HLINE, cols_ - 2);

    // Items - max rows = menuRows_ - 5 (borders + tab row + separator + hint).
    // On menu screens menuRows_ is MENU_ROWS_DEFAULT (10) → 5 item rows.
    const char **items = tabItems(activeTab_);
    int count = tabItemCount(activeTab_);
    int maxRows = menuRows_ - 5;
    if (maxRows < 0) maxRows = 0;
    for (int i = 0; i < count && i < maxRows; i++) {
        if (i == activeItem_) wattron(winMenu_, A_REVERSE | COLOR_PAIR(3));
        mvwprintw(winMenu_, 3 + i, 2, "%-*s", cols_ - 4, items[i]);
        if (i == activeItem_) wattroff(winMenu_, A_REVERSE | COLOR_PAIR(3));
    }

    // Controls hint
    // (no on-screen key legend — the GPI Case has physical buttons)
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

    // 80-col terminal lets us pack everything onto one line per Pokémon.
    // Layout:
    //   "1. MEWTWO       Lv71    +1234 XP    3h in care"
    //   slot(3) name(12) level(7) xp(13) hours(12) = 47 cols, leaves 33 free
    for (int i = 0; i < partyCount_; i++) {
        const PartySlot &s = partySlots_[i];
        const char *displayName = s.nick[0] ? s.nick : (s.name[0] ? s.name : "???");

        if (row - scroll >= 0 && row - scroll < infoRows) {
            // Slot + name + level — bold, default ink.
            wattron(winInfo_, A_BOLD);
            mvwprintw(winInfo_, row - scroll, 0,
                      "%d. %-12s  Lv%-3d",
                      i + 1, displayName, s.level);
            wattroff(winInfo_, A_BOLD);
            // Daycare XP + hours — green so it reads as an accent, doesn't
            // compete with the name/level for attention.
            wattron(winInfo_, COLOR_PAIR(1));
            mvwprintw(winInfo_, row - scroll, 26,
                      "+%-5u XP   %2uh in care",
                      s.xpGained, s.hours);
            wattroff(winInfo_, COLOR_PAIR(1));
        }
        row++;
    }

    // (no on-screen key legend — physical GPI buttons)
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
        uint64_t now = millis();
        for (int i = 0; i < neighborDisplayCount_ && i * 2 < infoRows - 1; i++) {
            const NeighborEntry &n = neighbors_[i];
            bool sel = (i == neighborSel_);
            bool fresh = (n.firstSeenMs != 0 &&
                          now - n.firstSeenMs < NEW_NEIGHBOR_HIGHLIGHT_MS);
            if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
            // Leading "*" marker + cyan-bold short-name when this neighbor
            // is still inside the new-arrival highlight window.
            const char *marker = fresh ? "*" : " ";
            mvwprintw(winInfo_, i * 2, 0, "%s", marker);
            if (fresh) wattron(winInfo_, COLOR_PAIR(4) | A_BOLD);
            else       wattron(winInfo_, A_BOLD);
            mvwprintw(winInfo_, i * 2, 1, " %s", n.shortName);
            if (fresh) wattroff(winInfo_, COLOR_PAIR(4));
            wattroff(winInfo_, A_BOLD);
            mvwprintw(winInfo_, i * 2, 2 + (int)strlen(n.shortName),
                      " (%s)%s", n.gameName, sel ? " <" : "  ");
            if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
            if (n.partyCount > 0)
                mvwprintw(winInfo_, i * 2 + 1, 3, "%s Lv%d  (%d in party)",
                          n.lead, n.leadLevel, n.partyCount);
        }
    }

    // (no on-screen key legend — physical GPI buttons)
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
        "A                 Select / confirm",
        "B                 Back / cancel",
        "Start             Open menu from anywhere",
        "Select            Toggle this help",
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

    // (no on-screen key legend — physical GPI buttons)
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
    int infoCols = getmaxx(winInfo_);

    // ── Optional gauntlet header above the boxes ─────────────────────────
    int row = 0;
    if (inGymBattle_) {
        const LordGym *g = lordGym(pendingGymIdx_);
        if (g) {
            char hdr[80];
            int round = pendingTrainerIdx_ + 1;
            const char *who = (pendingTrainerIdx_ >= LORD_GYM_LEADER_INDEX)
                                ? g->leaderName
                                : g->trainers[pendingTrainerIdx_].name;
            snprintf(hdr, sizeof(hdr),
                     "%s Gym  Round %d/5  vs %s",
                     g->city, round, who);
            wattron(winInfo_, A_BOLD | COLOR_PAIR(4));
            mvwprintw(winInfo_, row, 0, "%-*.*s", infoCols, infoCols, hdr);
            wattroff(winInfo_, A_BOLD | COLOR_PAIR(4));
        }
        row++;
    } else if (inE4Battle_) {
        const LordE4Member *m = lordE4Member(pendingE4Idx_);
        if (m) {
            char hdr[80];
            snprintf(hdr, sizeof(hdr),
                     "Indigo Plateau  %d/%d  %s %s",
                     pendingE4Idx_ + 1, LORD_E4_COUNT, m->title, m->name);
            wattron(winInfo_, A_BOLD | COLOR_PAIR(4));
            mvwprintw(winInfo_, row, 0, "%-*.*s", infoCols, infoCols, hdr);
            wattroff(winInfo_, A_BOLD | COLOR_PAIR(4));
        }
        row++;
    } else if (inPentestBattle_) {
        char hdr[80];
        snprintf(hdr, sizeof(hdr), "PENTEST PIKACHU  >  %s", pentestSsid_);
        wattron(winInfo_, A_BOLD | COLOR_PAIR(4));
        mvwprintw(winInfo_, row, 0, "%-*.*s", infoCols, infoCols, hdr);
        wattroff(winInfo_, A_BOLD | COLOR_PAIR(4));
        row++;
    }

    // ── Gen 1/2 inspired layout ──────────────────────────────────────────
    //  ┌─ FOE ────────────────┐ ┌─ FOE SPRITE ─────────────────┐
    //  │ ONIX L14   HP:...    │ │                              │
    //  └──────────────────────┘ │      <species name>          │
    //                            │                              │
    //  ┌─ YOU SPRITE ─────────┐  │                              │
    //  │                      │  └──────────────────────────────┘
    //  │   <species name>     │  ┌─ YOU ────────────────────────┐
    //  │                      │  │ MEWTWO L70  HP:...           │
    //  └──────────────────────┘  └──────────────────────────────┘
    //  ┌─ MESSAGES ──────────────────────────────────────────────┐
    //  │ ... battle log ...                                       │
    //  └──────────────────────────────────────────────────────────┘
    int half        = infoCols / 2;
    int infoBoxW    = half - 1;            // info boxes: half width minus a gutter
    int spriteW     = infoCols - half;     // sprite boxes: other half
    int leftX       = 0;
    int rightX      = half;
    int statusBoxH  = 3;
    int spriteBoxH  = 8;

    // FOE info top-left, FOE sprite top-right
    drawStatusBox(winInfo_, row, leftX, infoBoxW, /*isFoe=*/true, em);
    drawBox(winInfo_, row, rightX, spriteBoxH, spriteW, "FOE SPRITE");
    {
        // Real Gen 2 front sprite, downscaled by /4 so 56×56 fits the
        // 8-row box (14 cols × 7 cell rows of upper-half-block art).  The
        // species's authentic GBC palette is installed into the foe colour
        // pool on the fly by SpriteRender::drawSprite().
        int sW, sH;
        SpriteRender::cellSize(/*isBack=*/false, /*scaleDiv=*/4, &sW, &sH);
        int sx = rightX + 1 + (spriteW - 2 - sW) / 2;
        int sy = row + 1 + (spriteBoxH - 2 - sH) / 2;
        if (sx < rightX + 1) sx = rightX + 1;
        if (sy < row + 1)    sy = row + 1;
        SpriteRender::drawSprite(winInfo_, sy, sx, em.species, /*isBack=*/false,
                                 /*scaleDiv=*/4, /*slot=*/0, /*bgPair=*/5);
    }

    // YOU sprite bottom-left, YOU info bottom-right
    int botRow = row + spriteBoxH;
    drawBox(winInfo_, botRow, leftX, spriteBoxH, infoBoxW, "YOU SPRITE");
    {
        // Real Gen 2 back sprite (48×48), /4 scale → 12 cols × 6 cell rows.
        int sW, sH;
        SpriteRender::cellSize(/*isBack=*/true, /*scaleDiv=*/4, &sW, &sH);
        int sx = leftX + 1 + (infoBoxW - 2 - sW) / 2;
        int sy = botRow + 1 + (spriteBoxH - 2 - sH) / 2;
        if (sx < leftX + 1) sx = leftX + 1;
        if (sy < botRow + 1) sy = botRow + 1;
        SpriteRender::drawSprite(winInfo_, sy, sx, pm.species, /*isBack=*/true,
                                 /*scaleDiv=*/4, /*slot=*/1, /*bgPair=*/5);
    }
    drawStatusBox(winInfo_, botRow + spriteBoxH - statusBoxH, rightX,
                  spriteW, /*isFoe=*/false, pm);

    // ── Message log: full-width box, fills remaining info-panel rows ────
    int logTop    = botRow + spriteBoxH;
    int logBoxH   = infoRows - logTop;
    if (logBoxH < 3) logBoxH = 3;
    drawBox(winInfo_, logTop, 0, logBoxH, infoCols, "MESSAGES");

    int logLines  = logBoxH - 2;        // inside the borders
    int logOff    = (int)battleLog_.size() - logLines;
    if (logOff < 0) logOff = 0;
    for (int i = 0; i < logLines; i++) {
        int idx = logOff + i;
        if (idx < (int)battleLog_.size()) {
            std::string s = battleLog_[idx];
            if ((int)s.size() > infoCols - 4) s.resize(infoCols - 4);
            mvwprintw(winInfo_, logTop + 1 + i, 2, "%s", s.c_str());
        }
    }

    // ── Menu region: boxed move list with PP, or boxed switch list ──────
    int menuCols  = getmaxx(winMenu_);

    // Pentest Pikachu: no move menu — a text readout of the WiFi target and
    // the vulnerability being exploited.  This IS the pentest "fight" UI.
    if (inPentestBattle_) {
        drawBox(winMenu_, 0, 0, menuRows_, menuCols, "TARGET");
        char l1[96], l2[96];
        snprintf(l1, sizeof(l1), "SSID: %s", pentestSsid_);
        snprintf(l2, sizeof(l2), "Vuln: %s", pentestVuln_);
        mvwprintw(winMenu_, 1, 2, "%-*.*s", menuCols - 4, menuCols - 4, l1);
        if (menuRows_ > 3)
            mvwprintw(winMenu_, 2, 2, "%-*.*s", menuCols - 4, menuCols - 4, l2);
        return;
    }

    // Switch list needs an extra inner row to fit 3 rows of party slots.
    int menuBoxH  = switchMode_ ? menuRows_ : menuRows_ - 1;
    drawBox(winMenu_, 0, 0, menuBoxH, menuCols,
            switchMode_ ? "SWITCH" : "FIGHT");

    if (!switchMode_) {
        // 4 moves in a 2x2 grid so we use the full menu width
        int colW = (menuCols - 4) / 2;
        for (int i = 0; i < 4; i++) {
            uint8_t mv = pm.moves[i];
            int cy = 1 + (i / 2);
            int cx = 2 + (i % 2) * colW;
            if (mv == 0) {
                mvwprintw(winMenu_, cy, cx, "  ---");
                continue;
            }
            const char *name = moveName(mv);
            uint8_t pp_cur = pm.pp[i];
            const Gen1MoveData *md = gen1Move(mv);
            uint8_t pp_max = md ? md->pp : 0;
            const char *cursor = (i == moveSel_) ? ">" : " ";
            if (i == moveSel_) wattron(winMenu_, A_REVERSE | COLOR_PAIR(3));
            mvwprintw(winMenu_, cy, cx, "%s %-12s PP%2d/%-2d",
                      cursor, name, pp_cur, pp_max);
            if (i == moveSel_) wattroff(winMenu_, A_REVERSE | COLOR_PAIR(3));
        }
        // (no on-screen key legend — physical GPI buttons)
    } else {
        // Switch list inside the box
        int colW = (menuCols - 4) / 2;
        for (int i = 0; i < (int)pp.count; i++) {
            const Gen1BattleEngine::BattlePoke &s = pp.mons[i];
            bool fainted = (s.hp == 0);
            bool active  = (i == (int)pp.active);
            int cy = 1 + (i / 2);
            int cx = 2 + (i % 2) * colW;
            const char *cursor = (i == switchSel_) ? ">" : " ";
            if (i == switchSel_) wattron(winMenu_, A_REVERSE);
            if (fainted || active) wattron(winMenu_, A_DIM);
            mvwprintw(winMenu_, cy, cx, "%s %-10s L%-3d HP%3d",
                      cursor, s.nickname, s.level, s.hp);
            if (fainted || active) wattroff(winMenu_, A_DIM);
            if (i == switchSel_) wattroff(winMenu_, A_REVERSE);
        }
        // (no on-screen key legend — physical GPI buttons)
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

// Draw a single-line bordered box at (y,x) with given height/width and an
// optional title chip baked into the top edge.  Uses ncurses ACS chars so it
// renders correctly on any terminal that speaks line drawing (which is
// every modern xterm-compatible one, plus the Linux fbcon).
void TerminalUI::drawBox(WINDOW *w, int y, int x, int h, int w_, const char *title) {
    if (h < 2 || w_ < 2) return;
    // Corners
    mvwaddch(w, y,           x,            ACS_ULCORNER);
    mvwaddch(w, y,           x + w_ - 1,   ACS_URCORNER);
    mvwaddch(w, y + h - 1,   x,            ACS_LLCORNER);
    mvwaddch(w, y + h - 1,   x + w_ - 1,   ACS_LRCORNER);
    // Edges
    mvwhline(w, y,           x + 1,        ACS_HLINE, w_ - 2);
    mvwhline(w, y + h - 1,   x + 1,        ACS_HLINE, w_ - 2);
    mvwvline(w, y + 1,       x,            ACS_VLINE, h - 2);
    mvwvline(w, y + 1,       x + w_ - 1,   ACS_VLINE, h - 2);
    // Title chip — bold against whatever the window's current background
    // is.  No color pair switch so it stays readable on both the default
    // dark terminal and the white battle-screen card.
    if (title && *title) {
        wattron(w, A_BOLD);
        mvwprintw(w, y, x + 2, " %s ", title);
        wattroff(w, A_BOLD);
    }
}

// Foe (top) and player (bottom) status boxes: 3 rows tall, inline HP bar.
//   ┌─ FOE ────────────────────────────────┐
//   │ ONIX L14   HP:█████░░░░░░░░  35/124   │
//   └───────────────────────────────────────┘
void TerminalUI::drawStatusBox(WINDOW *w, int y, int x, int box_w, bool isFoe,
                                const Gen1BattleEngine::BattlePoke &mon) {
    drawBox(w, y, x, 3, box_w, isFoe ? "FOE" : "YOU");

    // Inside line: " NAME L## HP:[bar] hp/max "
    char buf[160];
    snprintf(buf, sizeof(buf), "%s L%-3d", mon.nickname, (int)mon.level);
    int leftLen = (int)strlen(buf);
    int hpStrW = 12;            // " 999/999" room + " HP:"
    int barW   = box_w - 4 - leftLen - hpStrW;
    if (barW < 4) barW = 4;

    // Color the bar by HP percentage
    int filled = (mon.maxHp > 0) ? (barW * mon.hp) / mon.maxHp : 0;
    double pct = (mon.maxHp > 0) ? (double)mon.hp / mon.maxHp : 0.0;
    int colorPair = (pct > 0.5) ? 1 : (pct > 0.25) ? 2 : 3;

    mvwprintw(w, y + 1, x + 2, "%s HP:", buf);
    int barX = x + 2 + leftLen + 4;   // after "NAME L##" + " HP:"
    wattron(w, COLOR_PAIR(colorPair));
    for (int i = 0; i < barW; i++)
        mvwaddch(w, y + 1, barX + i, i < filled ? ACS_BLOCK : '.');
    wattroff(w, COLOR_PAIR(colorPair));
    mvwprintw(w, y + 1, barX + barW + 1, "%3d/%-3d",
              (int)mon.hp, (int)mon.maxHp);
}

void TerminalUI::drawHpBar(WINDOW *w, int y, int x, int width,
                            uint16_t hp, uint16_t maxHp, const char *label) {
    if (maxHp == 0) return;
    // Label slot is 14 chars so "CHARMANDR L100" / "MEWTWO L70" fit.
    static constexpr int NAME_W = 14;
    char name[NAME_W + 2];
    snprintf(name, sizeof(name), "%-*.*s", NAME_W, NAME_W, label ? label : "");
    int barW = width - NAME_W - 11;   // 11 = " [" + "] NNN/NNN"
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
    // Pentest Pikachu owns its own input: A toggles the status overlay, the
    // fight is automatic, and exit/save is handled centrally.
    if (inPentestBattle_) { pentestButton(ev); return; }
    if (inBattle_) {
        battleHandleButton(ev);
    } else {
        battleEndButton(ev);
    }
}

void TerminalUI::pentestButton(const ButtonEvent &ev) {
    if (ev.button == GpiButton::A) {           // toggle the status overlay
        pentestShowStatus_ = !pentestShowStatus_;
        return;
    }
    if (ev.button == GpiButton::B && pentestShowStatus_) {
        pentestShowStatus_ = false;            // B also dismisses the overlay
        return;
    }
    if (ev.button == GpiButton::B ||
        ev.button == GpiButton::START ||
        ev.button == GpiButton::SELECT) {
        // Capture the live level/XP, persist, then exit to the system menu.
        const Gen1BattleEngine::BattleParty &pp = engine_.party(0);
        if (pp.count > 0 && pp.mons[0].level > 0) {
            pentestLevel_ = pp.mons[0].level;
            pentestXp_    = slotLevelXp_[0];
        }
        pentestSaveProgress();
        inPentestBattle_ = false;
        inBattle_        = false;
        requestQuit();
    }
}

void TerminalUI::pentestMarkSeen(uint8_t dex) {
    if (dex < 1 || dex > 151) return;
    int idx = dex - 1;
    pentestDex_[idx >> 3] |= (uint8_t)(1u << (idx & 7));
}

void TerminalUI::pentestMarkBeaten(uint8_t dex) {
    if (dex < 1 || dex > 151) return;
    int idx = dex - 1;
    pentestBeaten_[idx >> 3] |= (uint8_t)(1u << (idx & 7));
    pentestDex_[idx >> 3]    |= (uint8_t)(1u << (idx & 7));  // beaten implies seen
}

void TerminalUI::pentestBuildStatus(std::vector<std::string> &out) {
    char line[96];
    // Gyms "cleared" = gym areas whose leader level the Pikachu has reached.
    int gyms = 0;
    bool counted[8] = {};
    for (int i = 0; i < KANTO_ZONE_COUNT; i++) {
        const KantoZone &z = KANTO_ZONES[i];
        if (z.gymIdx < 8 && !counted[z.gymIdx] && pentestLevel_ >= z.gymLvl) {
            counted[z.gymIdx] = true;
            gyms++;
        }
    }
    snprintf(line, sizeof(line), "Gyms cleared: %d/8", gyms);   out.push_back(line);
    snprintf(line, sizeof(line), "Pikachu:      Lv%u", (unsigned)pentestLevel_);
    out.push_back(line);
    int seen = 0, beaten = 0;
    for (int i = 0; i < 151; i++) {
        if (pentestDex_[i >> 3]    & (1u << (i & 7))) seen++;
        if (pentestBeaten_[i >> 3] & (1u << (i & 7))) beaten++;
    }
    snprintf(line, sizeof(line), "Pokedex seen:   %d/151", seen);   out.push_back(line);
    snprintf(line, sizeof(line), "Pokedex beaten: %d/151", beaten); out.push_back(line);
    out.push_back("");

    const KantoZone &zone = kantoZoneForLevel(pentestLevel_);
    snprintf(line, sizeof(line), "Area: %s", zone.name);        out.push_back(line);
    out.push_back("Wild Pokemon here:");
    std::string row;
    for (int i = 0; i < zone.wildCount; i++) {
        const char *nm = dexName(zone.wilds[i].dex);
        if (!row.empty()) row += ", ";
        row += (nm ? nm : "?");
        if ((i % 3) == 2) { out.push_back("  " + row); row.clear(); }
    }
    if (!row.empty()) out.push_back("  " + row);
}

void TerminalUI::battleEndButton(const ButtonEvent &ev) {
    if (ev.button != GpiButton::A && ev.button != GpiButton::B) return;

    // Pentest Pikachu ROM: there's no menu to return to.  A = run another wild
    // scan; B = exit the ROM back to EmulationStation.  No XP flush — the
    // pentest Pikachu is standalone and must not touch the real SAV.
    if (inPentestBattle_) {
        if (ev.button == GpiButton::A) {
            startPentestBattle();
        } else {
            const Gen1BattleEngine::BattleParty &pp = engine_.party(0);
            if (pp.count > 0 && pp.mons[0].level > 0) {
                pentestLevel_ = pp.mons[0].level;
                pentestXp_    = slotLevelXp_[0];
            }
            pentestSaveProgress();
            inPentestBattle_ = false;
            inBattle_        = false;
            requestQuit();
        }
        return;
    }

    // Gym gauntlet: chain wins into the next trainer without healing.
    if (inGymBattle_ && battleResult_ == Gen1BattleEngine::Result::P1_WIN) {
        if (pendingTrainerIdx_ >= LORD_GYM_LEADER_INDEX) {
            // Beat the leader -> award badge, mark gym cleared, return to menu
            const LordGym *g = lordGym(pendingGymIdx_);
            if (g) lordSave_.badges |= (uint8_t)(1u << g->badgeBit);
            lordSave_.gymProgress[pendingGymIdx_] = LORD_GYM_TRAINERS;
            // Record which NG+ tier this gym was last cleared at, so the gym
            // list can flag it for a rematch after the next NG+ rollover.
            if (pendingGymIdx_ < LORD_GYM_COUNT)
                lordSave_.gymTierCleared[pendingGymIdx_] = lordSave_.ngPlusTier;
            lordSave(lordSave_);
            inGymBattle_ = false;
        } else {
            // Advance to the next trainer. Engine.replaceOpponent keeps our
            // party's HP/PP/status -- that's the no-healing gauntlet rule.
            // No SAV writeback between rounds -- we treat the gauntlet as a
            // single battle sequence and persist XP only when the gym is
            // fully cleared, lost, or fled (at BATTLE_END below).
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

    // Indigo Plateau gauntlet: chain the 5 league members without healing;
    // beating the Champion (last member) triggers NG+ — mirrors the T-Deck.
    if (inE4Battle_ && battleResult_ == Gen1BattleEngine::Result::P1_WIN) {
        if (pendingE4Idx_ + 1 < LORD_E4_COUNT) {
            pendingE4Idx_++;
            Gen1Party nextE4;
            if (lordBuildE4Party(pendingE4Idx_, nextE4)) {
                engine_.replaceOpponent(nextE4);
                battleLog_.clear();
                moveSel_      = 0;
                switchMode_   = false;
                inBattle_     = true;
                battleResult_ = Gen1BattleEngine::Result::ONGOING;
                screen_       = Screen::BATTLE;
                return;  // straight into the next member
            }
            inE4Battle_ = false;  // build failed; bail to the menu fallthrough
        } else {
            // Champion defeated → NG+ bookkeeping.
            uint8_t clearedTier = lordSave_.ngPlusTier;
            lordSave_.leagueCleared = 1;
            lordSave_.e4TierCleared = clearedTier;
            lordSave_.e4Progress    = 0;            // gauntlet replayable
            if (lordSave_.ngPlusTier < 5) lordSave_.ngPlusTier++;
            lordSetCurrentNgPlusTier(lordSave_.ngPlusTier);
            // Reset every gym's trainer progress so the whole game replays at
            // the new tier.  Badges stay set; gymTierCleared gates the rematch.
            for (int i = 0; i < LORD_GYM_COUNT; i++) lordSave_.gymProgress[i] = 0;
            lordSave(lordSave_);
            inE4Battle_ = false;

            flushBattleXpToDaemon();
            inBattle_ = false;
            battleLog_.clear();
            char msg[128];
            if (clearedTier == 0)
                snprintf(msg, sizeof(msg),
                         "CHAMPION! Elite Four cleared. NG+1 unlocked!");
            else
                snprintf(msg, sizeof(msg),
                         "Champion defeated!  NG+%u -> NG+%u.",
                         (unsigned)clearedTier, (unsigned)lordSave_.ngPlusTier);
            lastEventText_ = msg;
            screen_ = Screen::DAYCARE_EVENT;
            return;
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
    inE4Battle_  = false;
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
        // The detailed per-slot summary is printed when the daemon replies
        // with SAV_WRITEBACK -- no need to announce anything here.
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
        case 0: {
            // Party: dump T-Deck-style listing into the activity feed
            // (this scrolls in the same panel as everything else instead
            // of taking over the screen).
            if (!hasParty_ || partyCount_ == 0) {
                pushActivity("> No party loaded.");
                pushActivity("  Drop a .sav in /tmp/mm-test-saves/");
                break;
            }
            uint32_t mins = (millis() - startMs_) / 60000;
            // 2×3 grid (two columns × three rows for a party of 6).  Each cell
            // is self-contained: slot, name, level, and the daycare deltas the
            // ~53-col console now has room for — +XP gained and +levels gained.
            pushActivity("[t+%um] Party (%d):", (unsigned)mins, partyCount_);
            auto fmtCell = [](char *buf, size_t n, int idx, const PartySlot &s) {
                const char *nm = s.nick[0] ? s.nick :
                                 (s.name[0] ? s.name : "(noname)");
                unsigned xp = s.xpGained > 9999 ? 9999 : (unsigned)s.xpGained;
                // Show the level transition orig→current only if it actually
                // levelled up in the daycare; otherwise just the plain level.
                if (s.savLevel && s.savLevel != s.level)
                    snprintf(buf, n, "%d.%-8.8s L%d→%d +%u XP",
                             idx + 1, nm, (int)s.savLevel, (int)s.level, xp);
                else
                    snprintf(buf, n, "%d.%-8.8s L%d +%u XP",
                             idx + 1, nm, (int)s.level, xp);
            };
            // Count display columns (UTF-8 aware) so the → arrow — 3 bytes but
            // 1 column — doesn't throw off the two-column padding below
            // (printf field widths count bytes, not columns).
            auto dispCols = [](const char *s) {
                int w = 0;
                for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
                    if ((*p & 0xC0) != 0x80) w++;
                return w;
            };
            int half = (partyCount_ + 1) / 2;
            for (int i = 0; i < half; i++) {
                int j = i + half;
                if (partySlots_[i].dex == 0) continue;
                char left[48], right[48];
                fmtCell(left, sizeof(left), i, partySlots_[i]);
                if (j < partyCount_ && partySlots_[j].dex) {
                    fmtCell(right, sizeof(right), j, partySlots_[j]);
                    int pad = 26 - dispCols(left);
                    if (pad < 1) pad = 1;
                    pushActivity("%s%*s%s", left, pad, "", right);
                } else {
                    pushActivity("%s", left);
                }
            }
            ipc_.send("{\"cmd\":\"GET_PARTY\"}");  // refresh in background
            break;
        }
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
    roguelike_   = false;
    inGymBattle_ = false;
    inE4Battle_  = false;
    localSide_   = 0;
    moveSel_     = 0;
    switchMode_  = false;
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

// Pentest Pikachu progress lives in its own tiny file (NOT the player's SAV),
// so its level/XP are remembered across ROM relaunches.
static const char *pentestSavePath() {
#ifdef __APPLE__
    return "/tmp/monstermesh/pentest.dat";
#else
    return "/var/lib/monstermesh/pentest.dat";
#endif
}
static constexpr uint32_t PENTEST_SAVE_MAGIC = 0x504B4341u;  // 'PKCA'

void TerminalUI::pentestLoadProgress() {
    FILE *f = fopen(pentestSavePath(), "rb");
    if (!f) return;  // no save yet — keep the L5 default
    uint32_t magic = 0, xp = 0; uint8_t lvl = 0;
    if (fread(&magic, sizeof(magic), 1, f) == 1 &&
        fread(&lvl,   sizeof(lvl),   1, f) == 1 &&
        fread(&xp,    sizeof(xp),    1, f) == 1 &&
        magic == PENTEST_SAVE_MAGIC && lvl >= 1 && lvl <= 100) {
        pentestLevel_ = lvl;
        pentestXp_    = xp;
        // Pokedex bitsets are optional (older saves won't have them).
        fread(pentestDex_,    sizeof(pentestDex_),    1, f);
        fread(pentestBeaten_, sizeof(pentestBeaten_), 1, f);
    }
    fclose(f);
}

void TerminalUI::pentestSaveProgress() {
    FILE *f = fopen(pentestSavePath(), "wb");
    if (!f) return;
    uint32_t magic = PENTEST_SAVE_MAGIC;
    fwrite(&magic,          sizeof(magic),          1, f);
    fwrite(&pentestLevel_,  sizeof(pentestLevel_),  1, f);
    fwrite(&pentestXp_,     sizeof(pentestXp_),     1, f);
    fwrite(pentestDex_,     sizeof(pentestDex_),    1, f);
    fwrite(pentestBeaten_,  sizeof(pentestBeaten_), 1, f);
    fclose(f);
}

// Pentest Pikachu ROM: a self-contained Pikachu-vs-zone battle that doesn't
// depend on a loaded SAV.  Pikachu starts at L5 and climbs through the Kanto
// zones, fighting the Pokemon of the area it's currently in.
void TerminalUI::startPentestBattle() {
    if (inPentestBattle_) {
        // Carry the Pikachu's level + partial XP forward from the just-ended
        // scan, then persist so progress survives a relaunch.
        const Gen1BattleEngine::BattleParty &prev = engine_.party(0);
        if (prev.count > 0 && prev.mons[0].level > 0)
            pentestLevel_ = prev.mons[0].level;
        pentestXp_ = slotLevelXp_[0];
        pentestSaveProgress();
    } else if (!pentestLoaded_) {
        // First scan this launch — load remembered progress (defaults to L5).
        pentestLoadProgress();
        pentestLoaded_ = true;
    }

    battleLog_.clear();
    roguelike_         = false;
    inGymBattle_       = false;
    inE4Battle_        = false;
    inPentestBattle_   = true;
    pentestShowStatus_ = false;
    localSide_         = 0;
    moveSel_           = 0;
    switchMode_        = false;
    inBattle_          = true;

    // Player: a fixed Lv15 Pikachu (stats derived by the engine from
    // species + level + average DVs).  The engine reads Gen1Pokemon.species as
    // the INTERNAL Gen-1 code (not national dex), so convert via dexToInternal
    // exactly like buildPlayerPartyForBattle / lordBuildGymParty do — passing
    // raw dex 25 makes the engine resolve a different (ghost) species.
    uint8_t pikaInternal = dexToInternal[25];   // Pikachu
    memset(&party_, 0, sizeof(party_));
    party_.count            = 1;
    party_.species[0]       = pikaInternal;
    party_.mons[0].species  = pikaInternal;
    party_.mons[0].level    = pentestLevel_;
    party_.mons[0].boxLevel = pentestLevel_;
    party_.mons[0].dvs[0]   = 0x88;
    party_.mons[0].dvs[1]   = 0x88;
    static const uint8_t pikaMoves[4] = { 84, 98, 39, 86 }; // ThunderShock, Quick Attack, Tail Whip, Thunder Wave
    memcpy(party_.mons[0].moves, pikaMoves, 4);
    for (int m = 0; m < 4; m++) {
        const Gen1MoveData *md = gen1Move(pikaMoves[m]);
        party_.mons[0].pp[m] = md ? md->pp : 0;
    }
    memset(party_.nicknames[0], 0x50, 11);          // 0x50 = Gen1 string term
    const char *nk = "PIKACHU";
    for (int j = 0; nk[j] && j < 10; j++)
        party_.nicknames[0][j] = (uint8_t)(0x80 + (nk[j] - 'A'));

    // Resume partial XP toward the next level; never bank it to the SAV.
    slotLevelXp_[0] = pentestXp_;
    sessionXp_[0]   = 0;

    // Opponent: drawn from the Kanto zone for the Pikachu's current level, so
    // it fights the Pokemon of the area it's progressing through (like the
    // T-Deck pentest project).  Mix: 60% zone wild, 30% the zone's gym-leader
    // ace, 10% a fully random Gen-1 mon.
    const KantoZone &zone = kantoZoneForLevel(pentestLevel_);
    snprintf(pentestZone_, sizeof(pentestZone_), "%s", zone.name);

    uint8_t wildDex, wildLv;
    int roll = rand() % 100;
    if (roll < 30 && zone.gymIdx != 255) {
        // Gym-leader ace for this area.
        const LordGym *g = lordGym(zone.gymIdx);
        const LordGymMon *ace = nullptr;
        if (g) {
            const LordGymTrainer &ldr = g->trainers[LORD_GYM_LEADER_INDEX];
            if (ldr.party && ldr.count > 0) ace = &ldr.party[ldr.count - 1];
        }
        wildDex = ace ? ace->species : 25;
        wildLv  = zone.gymLvl ? zone.gymLvl : pentestLevel_;
    } else if (roll < 90 && zone.wildCount > 0) {
        // Zone wild.
        const KantoWildMon &w = zone.wilds[rand() % zone.wildCount];
        wildDex = w.dex;
        int span = (int)w.maxLvl - (int)w.minLvl;
        wildLv = (uint8_t)(w.minLvl + (span > 0 ? rand() % (span + 1) : 0));
    } else {
        // Random Gen-1 safety net, near the Pikachu's level.
        wildDex = (uint8_t)(1 + rand() % 151);
        int wl = (int)pentestLevel_ + (rand() % 7) - 3;
        if (wl < 2) wl = 2;
        if (wl > 100) wl = 100;
        wildLv = (uint8_t)wl;
    }
    pentestMarkSeen(wildDex);                        // log to the Pokedex

    uint8_t wildMoves[4] = { 1, 33, 0, 0 };          // Pound, Tackle
    if (wildLv >= 10) wildMoves[2] = 45;             // + Growl

    uint8_t wildInternal = dexToInternal[wildDex];
    Gen1Party wild = {};
    wild.count            = 1;
    wild.species[0]       = wildInternal;
    wild.mons[0].species  = wildInternal;
    wild.mons[0].level    = wildLv;
    wild.mons[0].boxLevel = wildLv;
    memcpy(wild.mons[0].moves, wildMoves, 4);

    // Pentest flavour: a random WiFi target + a vulnerability for the on-screen
    // text.  Pure fiction — the Pi has no scanner; this is the pentest theme.
    static const char *SSIDS[] = {
        "linksys",   "NETGEAR47",  "xfinitywifi",  "ATT-WiFi-2G",
        "TP-Link_5G","HOME-A1B2",  "dlink-guest",  "CenturyLink",
        "Pixel_5599","FBI_Van_3",  "Starbucks",    "iPhone",
    };
    static const char *VULNS[] = {
        "WPS PIN brute (CVE-2011-5053)",
        "WEP key reuse / IV collision",
        "KRACK 4-way handshake replay",
        "Default admin creds (admin:admin)",
        "WPA2 PMKID hashcat -m 16800",
        "Evil-twin deauth (802.11w off)",
    };
    snprintf(pentestSsid_, sizeof(pentestSsid_), "%s", SSIDS[rand() % (int)(sizeof(SSIDS)/sizeof(SSIDS[0]))]);
    snprintf(pentestVuln_, sizeof(pentestVuln_), "%s", VULNS[rand() % (int)(sizeof(VULNS)/sizeof(VULNS[0]))]);
    pentestEndMs_ = 0;

    // Seed the (now full-height) scrolling log with the exploit narration; the
    // engine's turn messages append below these as the auto-fight plays out.
    {
        char line[96];
        snprintf(line, sizeof(line), "[%s]  PIKACHU Lv%u",
                 pentestZone_, (unsigned)pentestLevel_);
        battleLog_.push_back(line);
        snprintf(line, sizeof(line), "Scanning %s ...", pentestSsid_);
        battleLog_.push_back(line);
        snprintf(line, sizeof(line), "Vuln: %s", pentestVuln_);
        battleLog_.push_back(line);
        battleLog_.push_back("Pikachu deploys the exploit!");
    }

    battleResult_ = Gen1BattleEngine::Result::ONGOING;
    uint32_t seed = (uint32_t)(millis() ^ ((uint32_t)wildDex << 8) ^ wildLv);
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
            // Gym / E4 fights pay REGULAR (wild) XP — no trainer 1.5x bonus —
            // so you can't out-level the game by grinding the gyms.
            bool trainerBonus = !(inGymBattle_ || inE4Battle_);
            uint32_t xp = gen1XpYield(enemyDex, enemyLevel, trainerBonus);

            // Pentest Pikachu earns only 10% XP so it levels slowly across the
            // Kanto zones (matches the T-Deck pentest pacing), and the KO'd
            // species is logged to the Pokedex as "beaten".
            if (inPentestBattle_) {
                xp /= 10; if (xp < 1) xp = 1;
                pentestMarkBeaten(enemyDex);
            }

            // Gen 1: only the active mon at the time of the KO earns XP.
            // Bench members get nothing -- switch them in to level them.
            if (killerSlot < 6) {
                sessionXp_[killerSlot]   += xp;
                slotLevelXp_[killerSlot] += xp;
            }

            char line[64];
            snprintf(line, sizeof(line), "%s gained %u EXP!",
                     pp.mons[killerSlot].nickname, (unsigned)xp);
            battleLog_.push_back(line);

            // Gym / E4 gauntlets: DON'T level up mid-run.  The level should
            // change once, after the whole gym/league is cleared — not after
            // every trainer.  XP still accumulates in sessionXp_ above and is
            // flushed to the daemon at the end, which recomputes the SAV level
            // (and the SAV_WRITEBACK summary shows the net level change).  For
            // one-off battles (Fight / roguelike / pentest) we still level up
            // mid-battle as normal.
            bool deferLevels = (inGymBattle_ || inE4Battle_);

            // Medium-fast level-up: XP needed to gain ONE level from L to
            // L+1 = (L+1)^3 - L^3.  slotLevelXp_ accumulates until it
            // crosses that delta, then we bump the engine's BattlePoke
            // level, scale stats linearly, and add the maxHp delta to
            // current HP (Gen 1 heal-on-level).
            Gen1BattleEngine::BattlePoke &mon = pp.mons[killerSlot];
            while (!deferLevels && mon.level < 100 && killerSlot < 6) {
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
    // Pentest Pikachu: the fight is automatic, so manual move/switch input is
    // ignored.  Start or Select exits the ROM back to the MonsterMesh system.
    if (inPentestBattle_) {
        if (ev.button == GpiButton::START || ev.button == GpiButton::SELECT) {
            // Capture the latest level/XP from the live battle, then persist
            // before exiting so progress isn't lost mid-scan.
            const Gen1BattleEngine::BattleParty &pp = engine_.party(0);
            if (pp.count > 0 && pp.mons[0].level > 0) {
                pentestLevel_ = pp.mons[0].level;
                pentestXp_    = slotLevelXp_[0];
            }
            pentestSaveProgress();
            inPentestBattle_ = false;
            inBattle_        = false;
            requestQuit();
        }
        return;
    }

    const Gen1BattleEngine::BattleParty &pp = engine_.party(localSide_);

    // Game Boy 2x2 move grid:  move0 move1
    //                          move2 move3
    // Left/Right swap within a row, Up/Down swap rows. Switch list (party
    // of 6) is also a 2x3 grid.
    auto moveExists = [&](int slot) -> bool {
        return slot >= 0 && slot < 4 && pp.mons[pp.active].moves[slot] != 0;
    };
    switch (ev.button) {
        case GpiButton::UP:
            if (switchMode_) {
                if (switchSel_ >= 2) switchSel_ -= 2;
            } else {
                if (moveSel_ >= 2 && moveExists(moveSel_ - 2)) moveSel_ -= 2;
            }
            break;
        case GpiButton::DOWN:
            if (switchMode_) {
                if (switchSel_ + 2 < (int)pp.count) switchSel_ += 2;
            } else {
                if (moveSel_ < 2 && moveExists(moveSel_ + 2)) moveSel_ += 2;
            }
            break;
        case GpiButton::LEFT:
            if (switchMode_) {
                if (switchSel_ & 1) switchSel_--;
            } else {
                if ((moveSel_ & 1) && moveExists(moveSel_ - 1)) moveSel_--;
            }
            break;
        case GpiButton::RIGHT:
            if (switchMode_) {
                if (!(switchSel_ & 1) && switchSel_ + 1 < (int)pp.count) switchSel_++;
            } else {
                if (!(moveSel_ & 1) && moveExists(moveSel_ + 1)) moveSel_++;
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
        if (menuRow >= menuRows_ - 2) break;
    }
    // (no on-screen key legend — physical GPI buttons)
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
        if (!ok) {
            std::string reason = jsonGetStr(msg, "reason");
            pushActivity("> SAV write FAILED (%s)",
                         reason.empty() ? "?" : reason.c_str());
        } else if (!applied) {
            pushActivity("> No XP to save (none earned this fight)");
        } else {
            pushActivity("=== Progress saved to .sav ===");
            // Walk the slots[...] array and print one line per Pokemon.
            size_t pos = msg.find("\"slots\":[");
            if (pos != std::string::npos) {
                pos += 9;
                while (pos < msg.size() && msg[pos] != ']') {
                    pos = msg.find('{', pos);
                    if (pos == std::string::npos) break;
                    size_t end = msg.find('}', pos);
                    if (end == std::string::npos) break;
                    std::string sl = msg.substr(pos, end - pos + 1);

                    std::string nick = jsonGetStr(sl, "nick");
                    int oldL = jsonGetInt(sl, "old_level", 0);
                    int newL = jsonGetInt(sl, "new_level", 0);
                    int xp   = jsonGetInt(sl, "xp", 0);
                    if (newL > oldL)
                        pushActivity("  %-10s L%d -> L%d  (+%d XP)",
                                     nick.c_str(), oldL, newL, xp);
                    else
                        pushActivity("  %-10s L%d        (+%d XP)",
                                     nick.c_str(), oldL, xp);
                    pos = end + 1;
                }
            }
        }
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
        std::string gn = jsonGetStr(msg, "game_name");
        strncpy(localShortName_,   sn.c_str(), sizeof(localShortName_)   - 1);
        localShortName_[sizeof(localShortName_)     - 1] = '\0';
        strncpy(localTrainerName_, gn.c_str(), sizeof(localTrainerName_) - 1);
        localTrainerName_[sizeof(localTrainerName_) - 1] = '\0';
        pushActivity("> Radio: 0x%08X %s / %s", nid, sn.c_str(), gn.c_str());
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

    // On first party load, dump T-Deck-style listing into the activity feed.
    // Two columns to keep vertical footprint short on the GPI's 24-row LCD.
    if (first) {
        uint32_t mins = (millis() - startMs_) / 60000;
        pushActivity("[t+%um] Party (%d):", (unsigned)mins, count);
        int half = (count + 1) / 2;
        for (int i = 0; i < half; i++) {
            int j = i + half;
            const PartySlot &s1 = partySlots_[i];
            const char *n1 = s1.nick[0] ? s1.nick :
                             (s1.name[0] ? s1.name : "(noname)");
            if (j < count) {
                const PartySlot &s2 = partySlots_[j];
                const char *n2 = s2.nick[0] ? s2.nick :
                                 (s2.name[0] ? s2.name : "(noname)");
                pushActivity("%d.%-9s Lv%2d  %d.%-9s Lv%2d",
                             i + 1, n1, (int)s1.level,
                             j + 1, n2, (int)s2.level);
            } else {
                pushActivity("%d.%-9s Lv%2d",
                             i + 1, n1, (int)s1.level);
            }
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

    // Snapshot the previous batch so we can preserve firstSeenMs for any
    // nodeId we've already shown.  The new-neighbor highlight then only
    // fires on genuinely new arrivals, not on every periodic NEIGHBORS rx.
    NeighborEntry prev[MAX_NEIGHBORS_DISPLAY];
    int prevCount = neighborDisplayCount_;
    for (int i = 0; i < prevCount; i++) prev[i] = neighbors_[i];

    size_t pos = msg.find("\"list\":[");
    if (pos == std::string::npos) return;
    pos += 8;

    uint64_t now = millis();
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
        n.shortName[sizeof(n.shortName) - 1] = 0;
        strncpy(n.gameName,  gn.c_str(),   sizeof(n.gameName)  - 1);
        n.gameName[sizeof(n.gameName) - 1] = 0;
        strncpy(n.lead,      nick.c_str(), sizeof(n.lead)      - 1);
        n.lead[sizeof(n.lead) - 1] = 0;
        n.partyCount = pc;
        n.leadLevel  = lv;

        // Match against the previous batch by nodeId (preferred) or short
        // name fallback — preserve firstSeenMs so the highlight expires.
        n.firstSeenMs = 0;
        for (int j = 0; j < prevCount; j++) {
            if ((nid != 0 && prev[j].nodeId == nid) ||
                (nid == 0 && strncmp(prev[j].shortName, n.shortName,
                                     sizeof(n.shortName)) == 0)) {
                n.firstSeenMs = prev[j].firstSeenMs;
                break;
            }
        }
        bool isNew = (n.firstSeenMs == 0);
        if (isNew) n.firstSeenMs = now ? now : 1;  // 0 means "no entry"

        if (isNew) {
            pushActivity("> NEW NEIGHBOR: %s (%s) %s Lv%d", n.shortName,
                         n.gameName, n.lead[0] ? n.lead : "(?)", n.leadLevel);
        } else {
            pushActivity("> Neighbor: %s (%s) %s Lv%d", n.shortName,
                         n.gameName, n.lead[0] ? n.lead : "(?)", n.leadLevel);
        }
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
    inE4Battle_   = false;
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
    // Publish the saved NG+ tier so gym / E4 party builders scale correctly.
    lordSetCurrentNgPlusTier(lordSave_.ngPlusTier);
}

void TerminalUI::renderGymSelect()
{
    werase(winInfo_);
    werase(winMenu_);

    // Title + badge/NG+ count on ONE row so the short GPI info panel still has
    // room for all 8 gyms PLUS the league row beneath them.
    int badges = __builtin_popcount(lordSave_.badges);
    char title[64];
    if (lordSave_.ngPlusTier > 0)
        snprintf(title, sizeof(title), "Legend of Charizard  %d/8  NG+%u",
                 badges, (unsigned)lordSave_.ngPlusTier);
    else
        snprintf(title, sizeof(title), "Legend of Charizard  %d/8 badges", badges);
    wattron(winInfo_, A_BOLD);
    mvwprintw(winInfo_, 0, 1, "%s", title);
    wattroff(winInfo_, A_BOLD);

    int infoRows = getmaxy(winInfo_);
    uint8_t tier = lordSave_.ngPlusTier;
    for (int i = 0; i < LORD_GYM_COUNT && (1 + i) < infoRows; i++) {
        const LordGym *g = lordGym((uint8_t)i);
        if (!g) continue;

        bool cleared = (lordSave_.badges >> g->badgeBit) & 1;
        // In NG+, a gym is "owed" again until re-cleared at the current tier.
        bool owedAtTier = cleared && (lordSave_.gymTierCleared[i] < tier);
        bool sel     = (i == gymSel_);

        if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));

        char badge_mark = cleared ? '*' : ' ';
        const char *status = !cleared    ? "5-round gauntlet"
                           : owedAtTier  ? "NG+ rematch"
                                         : "Cleared";
        mvwprintw(winInfo_, 1 + i, 0,
                  " %c%d. %-10s %-8s  %-12s  %s",
                  badge_mark, i + 1,
                  g->city, g->badgeName,
                  g->leaderName, status);

        if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
    }

    // ── 9th row: Indigo Plateau (Elite Four + Champion) ──────────────────────
    if ((1 + LEAGUE_ROW) < infoRows) {
        bool unlocked = (badges >= LORD_GYM_COUNT);
        bool leagueDone = lordSave_.leagueCleared &&
                          lordSave_.e4TierCleared >= tier;
        bool sel = (gymSel_ == LEAGUE_ROW);
        if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
        const char *status = !unlocked  ? "Need 8 badges"
                           : leagueDone ? "Cleared"
                                        : "Elite Four + Champion";
        mvwprintw(winInfo_, 1 + LEAGUE_ROW, 0,
                  " %c9. %-10s %-8s  %-12s  %s",
                  unlocked ? '>' : 'x', "Indigo Plt", "League",
                  "Elite Four", status);
        if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
    }

    // (no on-screen key legend — physical GPI buttons)
}

void TerminalUI::gymSelectButton(const ButtonEvent &ev)
{
    switch (ev.button) {
        case GpiButton::UP:
            gymSel_ = (gymSel_ + GYM_ROWS - 1) % GYM_ROWS;
            break;
        case GpiButton::DOWN:
            gymSel_ = (gymSel_ + 1) % GYM_ROWS;
            break;
        case GpiButton::A:
            if (gymSel_ == LEAGUE_ROW) {
                // Indigo Plateau: gated on all 8 badges.  Always starts at
                // member 0 (Lorelei); the battle-end handler chains 0->4.
                if (__builtin_popcount(lordSave_.badges) >= LORD_GYM_COUNT) {
                    startE4Battle(0);
                } else {
                    lastEventText_ = "Clear all 8 gyms first!";
                    screen_ = Screen::DAYCARE_EVENT;
                }
            } else {
                // Can't replay a gym already cleared at the current tier — that
                // would let you farm XP.  (An NG+ rollover re-opens every gym as
                // a one-time rematch; those are still allowed.)
                const LordGym *g = lordGym((uint8_t)gymSel_);
                bool cleared = g && ((lordSave_.badges >> g->badgeBit) & 1);
                bool owedAtTier = cleared &&
                    (lordSave_.gymTierCleared[gymSel_] < lordSave_.ngPlusTier);
                if (cleared && !owedAtTier) {
                    lastEventText_ = "Gym already cleared!";
                    screen_ = Screen::DAYCARE_EVENT;
                } else {
                    // Gauntlet: always start at the first trainer.  No mid-gym
                    // resume.  Battle-end handler chains us through 0->4 on wins.
                    startGymBattle((uint8_t)gymSel_, 0);
                }
            }
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
    inE4Battle_        = false;

    uint32_t seed = (uint32_t)(millis() ^ ((uint32_t)gymIdx << 8) ^ trainerIdx);
    engine_.start(party_, gymParty, seed);
    screen_ = Screen::BATTLE;
}

void TerminalUI::startE4Battle(uint8_t e4Idx)
{
    if (!hasParty_ || partyCount_ == 0) {
        lastEventText_ = "No party! Load a .sav first.";
        screen_ = Screen::DAYCARE_EVENT;
        return;
    }

    buildPlayerPartyForBattle();

    Gen1Party e4Party;
    if (!lordBuildE4Party(e4Idx, e4Party)) {
        lastEventText_ = "Could not build league party.";
        screen_ = Screen::DAYCARE_EVENT;
        return;
    }

    battleLog_.clear();
    roguelike_  = false;
    localSide_  = 0;
    moveSel_    = 0;
    switchMode_ = false;
    inBattle_   = true;

    // E4 context (mutually exclusive with the gym gauntlet).
    inGymBattle_  = false;
    inE4Battle_   = true;
    pendingE4Idx_ = e4Idx;

    uint32_t seed = (uint32_t)(millis() ^ ((uint32_t)0xE4 << 8) ^ e4Idx);
    engine_.start(party_, e4Party, seed);
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

// ── SDL2 BattleWindow state sync ─────────────────────────────────────────────

void TerminalUI::syncBattleWindow() {
    BattleWindow::State s;
    // Copy local player identity so the YOU box header reads
    // "<short> / <trainer>" instead of just "YOU".
    snprintf(s.localShort,   sizeof(s.localShort),   "%s", localShortName_);
    snprintf(s.localTrainer, sizeof(s.localTrainer), "%s", localTrainerName_);
    const Gen1BattleEngine::BattleParty &pp = engine_.party(localSide_);
    const Gen1BattleEngine::BattleParty &ep = engine_.party(1 - localSide_);
    const Gen1BattleEngine::BattlePoke  &pm = pp.mons[pp.active];
    const Gen1BattleEngine::BattlePoke  &em = ep.mons[ep.active];

    s.foe.species = em.species;
    s.foe.level   = em.level;
    s.foe.hp      = em.hp;
    s.foe.maxHp   = em.maxHp;
    snprintf(s.foe.nickname, sizeof(s.foe.nickname), "%s", em.nickname);

    s.you.species = pm.species;
    s.you.level   = pm.level;
    s.you.hp      = pm.hp;
    s.you.maxHp   = pm.maxHp;
    snprintf(s.you.nickname, sizeof(s.you.nickname), "%s", pm.nickname);

    for (int i = 0; i < 4; i++) {
        uint8_t mv = pm.moves[i];
        s.moves[i].slotUsed = (mv != 0);
        if (mv == 0) {
            snprintf(s.moves[i].name, sizeof(s.moves[i].name), "---");
            s.moves[i].pp = s.moves[i].maxPp = 0;
            continue;
        }
        snprintf(s.moves[i].name, sizeof(s.moves[i].name), "%s", moveName(mv));
        s.moves[i].pp    = pm.pp[i];
        const Gen1MoveData *md = gen1Move(mv);
        s.moves[i].maxPp = md ? md->pp : pm.pp[i];
    }
    s.selectedMove = moveSel_;
    s.mode = switchMode_ ? BattleWindow::Mode::SWITCH
                         : BattleWindow::Mode::FIGHT;

    // EXP bar: progress of the active mon toward its next level (medium-fast).
    {
        uint32_t L     = pm.level;
        uint32_t delta = (L + 1) * (L + 1) * (L + 1) - L * L * L;
        uint32_t have  = (pp.active < 6) ? slotLevelXp_[pp.active] : 0;
        if (have > delta) have = delta;
        s.expPermille = delta ? (int)(have * 1000 / delta) : 0;
    }

    // Switch list (party members)
    s.switchCount = pp.count > 6 ? 6 : pp.count;
    for (int i = 0; i < s.switchCount; i++) {
        const Gen1BattleEngine::BattlePoke &m = pp.mons[i];
        snprintf(s.switchSlots[i].nickname, sizeof(s.switchSlots[i].nickname),
                 "%s", m.nickname);
        s.switchSlots[i].level   = m.level;
        s.switchSlots[i].hp      = m.hp;
        s.switchSlots[i].maxHp   = m.maxHp;
        s.switchSlots[i].active  = (i == (int)pp.active);
        s.switchSlots[i].fainted = (m.hp == 0);
    }
    s.selectedSwitch = switchSel_;

    // Feed the SDL log enough lines to fill the box — the renderer caps to
    // however many actually fit.  Pentest's full-height log shows ~10 lines, so
    // hand it a generous slice; normal battles keep the compact 4.
    int logCap = inPentestBattle_ ? 24 : BATTLE_LOG_LINES;
    int n = (int)battleLog_.size();
    int start = n > logCap ? n - logCap : 0;
    for (int i = start; i < n; i++) s.log.push_back(battleLog_[i]);

    // Optional gym / league header
    if (inGymBattle_) {
        const LordGym *g = lordGym(pendingGymIdx_);
        if (g) {
            int round = pendingTrainerIdx_ + 1;
            const char *who = (pendingTrainerIdx_ >= LORD_GYM_LEADER_INDEX)
                                ? g->leaderName
                                : g->trainers[pendingTrainerIdx_].name;
            snprintf(s.header, sizeof(s.header),
                     "%s Gym  Round %d/5  vs %s", g->city, round, who);
        }
    } else if (inE4Battle_) {
        const LordE4Member *m = lordE4Member(pendingE4Idx_);
        if (m)
            snprintf(s.header, sizeof(s.header),
                     "Indigo Plateau  %d/%d  %s %s",
                     pendingE4Idx_ + 1, LORD_E4_COUNT, m->title, m->name);
    } else if (inPentestBattle_) {
        snprintf(s.header, sizeof(s.header), "PENTEST PIKACHU");
        s.pentest = true;
        // WiFi network name goes in the foe's corner tag (replacing "FOE");
        // the name row keeps the actual species so it still reads as a battle.
        snprintf(s.foeTag, sizeof(s.foeTag), "%s", pentestSsid_);
        snprintf(s.foe.nickname, sizeof(s.foe.nickname), "%s", dexName(em.species));
        // Status overlay (toggled with A): gyms / current area / Pokedex.
        s.showStatus = pentestShowStatus_;
        if (pentestShowStatus_) pentestBuildStatus(s.statusLines);
    }

    // End overlay
    if (screen_ == Screen::BATTLE_END) {
        if (battleResult_ == Gen1BattleEngine::Result::P1_WIN) {
            s.endResult = (localSide_ == 0) ? BattleWindow::EndResult::WIN
                                            : BattleWindow::EndResult::LOSE;
        } else if (battleResult_ == Gen1BattleEngine::Result::P2_WIN) {
            s.endResult = (localSide_ == 0) ? BattleWindow::EndResult::LOSE
                                            : BattleWindow::EndResult::WIN;
        } else if (battleResult_ == Gen1BattleEngine::Result::DRAW) {
            s.endResult = BattleWindow::EndResult::DRAW;
        }
    }

    battleWindow_.setState(s);
}

void TerminalUI::syncPvpBattleWindow() {
    BattleWindow::State s;
    snprintf(s.localShort,   sizeof(s.localShort),   "%s", localShortName_);
    snprintf(s.localTrainer, sizeof(s.localTrainer), "%s", localTrainerName_);
    // Player's lead from cached partySlots_ (daemon-driven PvP doesn't expose
    // the full engine to the terminal).
    if (partyCount_ > 0) {
        const PartySlot &lead = partySlots_[0];
        s.you.species = lead.dex;
        s.you.level   = lead.level;
        s.you.hp      = pvpMyHp_;
        s.you.maxHp   = pvpMyMaxHp_ ? pvpMyMaxHp_ : 1;
        snprintf(s.you.nickname, sizeof(s.you.nickname), "%s",
                 lead.nick[0] ? lead.nick : (lead.name[0] ? lead.name : "?"));
        for (int i = 0; i < 4; i++) {
            uint8_t mv = lead.moves[i];
            s.moves[i].slotUsed = (mv != 0);
            if (mv == 0) {
                snprintf(s.moves[i].name, sizeof(s.moves[i].name), "---");
                s.moves[i].pp = s.moves[i].maxPp = 0;
                continue;
            }
            snprintf(s.moves[i].name, sizeof(s.moves[i].name), "%s", moveName(mv));
            s.moves[i].pp    = pvpMyPp_[i];
            const Gen1MoveData *md = gen1Move(mv);
            s.moves[i].maxPp = md ? md->pp : pvpMyPp_[i];
        }
    }
    // Foe: PvP doesn't ship the enemy dex over IPC right now — fall back to
    // a sane placeholder (Mewtwo) so the layout still renders.  Replace once
    // the daemon adds the enemy species to its UPDATE payload.
    s.foe.species = 150;
    s.foe.level   = s.you.level;  // best guess
    s.foe.hp      = pvpEnemyHp_;
    s.foe.maxHp   = pvpEnemyMaxHp_ ? pvpEnemyMaxHp_ : 1;
    snprintf(s.foe.nickname, sizeof(s.foe.nickname), "%s",
             pvpEnemyName_[0] ? pvpEnemyName_ : "ENEMY");

    s.selectedMove = pvpMoveSel_;
    s.mode = BattleWindow::Mode::FIGHT;

    int n = (int)pvpLog_.size();
    int start = n > BATTLE_LOG_LINES ? n - BATTLE_LOG_LINES : 0;
    for (int i = start; i < n; i++) s.log.push_back(pvpLog_[i]);

    snprintf(s.header, sizeof(s.header), "PvP  Turn %u", (unsigned)pvpTurn_);

    if (screen_ == Screen::PVP_BATTLE_END) {
        if      (pvpResult_ == 1) s.endResult = BattleWindow::EndResult::WIN;
        else if (pvpResult_ == 2) s.endResult = BattleWindow::EndResult::LOSE;
        else if (pvpResult_ == 3) s.endResult = BattleWindow::EndResult::DRAW;
    }

    battleWindow_.setState(s);
}
