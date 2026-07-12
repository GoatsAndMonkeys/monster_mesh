// TerminalUI.cpp - MonsterMesh Pi menu-driven ncurses UI
// Navigation: D-pad moves within/between tabs. A=select, B=back, Start=menu, Select=help.
// No text input - every action is reached through the MESH / LOCAL / SYSTEM menu tree.

#include "TerminalUI.h"
#include "SpriteRender.h"
#include "Gen2SpriteCache.h"              // VAR_* colour skin enum (rare-catch rolls)
#include "../shared/BattlePacket.h"
#include "../battle/WirePartyCodec.h"      // protocol-V2 neutral cross-gen party
#include "../battle/showdown_gen1_moves.h"
#include "../shared/DaycareSavPatcher.h"   // dexToInternal[] table
#include "../shared/Gen1BaseExp.h"         // gen1XpYield()
#include "../shared/LordE4.h"              // Indigo Plateau rosters
#include "../shared/LordLogic.h"           // NG+ tier state + scaling
#include "../shared/KantoZones.h"          // Pentest Pikachu zone encounters
#include "../shared/Gen1Learnsets.h"       // per-species level-up moves (swapped battler)
#include "../shared/Gen1Evolution.h"       // caught mons evolve as their trip level climbs
#include "../shared/BreedingApp.h"         // roster + Mendelian breeding (Breed tab)
#include "../shared/PentestCatch.h"        // deterministic wild genotype from BSSID
#include <ncurses.h>
#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <algorithm>

// ── Local helpers ─────────────────────────────────────────────────────────────

// Bill's PC skin-category tabs. A mon matches every category its skin belongs to
// (a Dark-Pink shows in both Pink and Dark), so counts can overlap — it's a
// filter view, not a partition.
static const char *const kBoxTabNames[] = { "All", "Shy", "Pnk", "Rnw", "Drk", "Ddk", "Reg" };
static constexpr int kBoxTabCount = 7;
static bool boxTabMatch(breeding::Skin s, int tab) {
    using namespace breeding;
    switch (tab) {
        case 0: return true;                                                   // All
        case 1: return s == SKIN_SHINY || s == SKIN_DARK_SHINY || s == SKIN_BLACKOUT_SHINY;
        case 2: return s == SKIN_PINK  || s == SKIN_DARK_PINK  || s == SKIN_BLACKOUT_PINK;
        case 3: return s == SKIN_RAINBOW || s == SKIN_DARK_RAINBOW || s == SKIN_BLACKOUT_RAINBOW;
        case 4: return s == SKIN_DARK || s == SKIN_DARK_SHINY || s == SKIN_DARK_PINK || s == SKIN_DARK_RAINBOW;
        case 5: return s == SKIN_BLACKOUT || s == SKIN_BLACKOUT_SHINY || s == SKIN_BLACKOUT_PINK || s == SKIN_BLACKOUT_RAINBOW;
        case 6: return s == SKIN_REGULAR;
    }
    return false;
}

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
                case 'y': case 'Y':
                    ev.button = GpiButton::Y;      handleButton(ev); break;
                case 'x': case 'X':
                    ev.button = GpiButton::X;      handleButton(ev); break;
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
        // Also tick while in standby so it keeps scanning for a vulnerable AP.
        if (inPentestBattle_ || pentestStandby_) pentestAutoTick();

        render();
        usleep(16666);
    }
}

// Auto-battle driver for the Pentest Pikachu ROM.  Every ~900 ms it lets the
// engine's CPU picker choose a move for BOTH sides and runs the turn, so the
// whole fight is hands-off.  Stops as soon as the battle ends (runTurnWithXp
// flips screen_ to BATTLE_END).
void TerminalUI::pentestAutoTick() {
    // Freeze the auto-fight / standby rescan while the status overlay is up.
    if (pentestShowStatus_) return;
    uint64_t now = millis();

    // Standby: no active battle — Pikachu is waiting for a vulnerable WiFi
    // network.  Rescan periodically; the moment a vulnerable AP is in range,
    // drop out of standby and start the fight.
    if (pentestStandby_) {
        if (now - pentestScanMs_ >= PENTEST_STANDBY_SCAN_MS) {
            pentestScanMs_ = now;
            pentestScanNetworks();
            if (!pentestScanAvailable_ || !pentestNets_.empty()) {
                pentestStandby_ = false;
                startPentestBattle();      // picks the target + builds the fight
            }
        }
        return;
    }

    // Battle over: there's no "YOU WIN" screen for the pentest ROM — you can
    // read the result straight off the frozen battle frame.  Keep that frame
    // on screen for a couple of seconds, then chain into the next scan.
    if (battleResult_ != Gen1BattleEngine::Result::ONGOING) {
        pentestBossMode_ = false;   // boss key: disengage when battle ends
        if (pentestEndMs_ == 0) {
            pentestEndMs_ = now;                         // begin the linger
            // Tally this battle's outcome exactly once (player is P1).
            if (!pentestTallied_) {
                if (battleResult_ == Gen1BattleEngine::Result::P1_WIN) {
                    pentestWins_++;
                    // Beat a gym leader? Mark the badge so the next leader (and
                    // its zones) unlock — drives progression like the T-Deck.
                    if (pentestBattleGym_ < 8)
                        pentestGymBeaten_ |= (uint8_t)(1u << pentestBattleGym_);
                } else if (battleResult_ == Gen1BattleEngine::Result::P2_WIN) {
                    pentestLosses_++;
                }
                pentestTallied_ = true;
                pentestSaveProgress();
            }
        }
        screen_ = Screen::BATTLE;                       // suppress BATTLE_END
        if (now - pentestEndMs_ >= PENTEST_END_MS) {
            if (battleResult_ == Gen1BattleEngine::Result::P2_WIN)
                pentestRematch_ = true;  // lost: retry the same network
            startPentestBattle();
        }
        return;
    }

    // Boss mode: player is fighting manually — skip the auto-pick tick.
    if (pentestBossMode_) return;

    // Ongoing: auto-pick a move for both sides on a steady beat.
    if (!inBattle_ || screen_ != Screen::BATTLE) return;
    if (now - lastPentestTurnMs_ < PENTEST_TURN_MS) return;
    lastPentestTurnMs_ = now;

    // Rare-colour foe: throw Poke Balls instead of attacking once it's weakened.
    if (pentestTryCatchTick()) return;

    switchMode_ = false;
    uint8_t pa, pi, ca, ci;
    engine_.cpuPickAction(localSide_,     pa, pi);   // auto-pick our move
    engine_.submitAction(localSide_,      pa, pi);
    engine_.cpuPickAction(1 - localSide_, ca, ci);   // foe's move
    engine_.submitAction(1 - localSide_,  ca, ci);
    runTurnWithXp();

    // runTurnWithXp() flips screen_ to BATTLE_END on the finishing blow; undo it
    // so the win screen never shows.  DON'T start the linger here — leave
    // pentestEndMs_ at 0 so the battle-over block at the top runs the W/L tally
    // (it keys off pentestEndMs_ == 0) on the next tick, then begins the linger.
    if (battleResult_ != Gen1BattleEngine::Result::ONGOING)
        screen_ = Screen::BATTLE;       // suppress the BATTLE_END flash
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
    // The Breed screen has no use for the MESH/LOCAL/SYSTEM tab bar — give it
    // (nearly) the whole panel so the mon list + rooms + odds have room.
    bool breedFull = (screen_ == Screen::BREEDING);
    int want = breedFull ? 1 : (battle ? MENU_ROWS_BATTLE : MENU_ROWS_DEFAULT);
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
    // Exception: SIGINT tool screens use ncurses; the SDL window is closed while
    // they're visible and reopens when B returns to Screen::BATTLE.
    if (pentestMode_) {
        // X / screen-off: close SDL, blank ncurses to black, turn off backlight.
        if (pentestScreenOff_) {
            if (battleWindow_.isOpen()) battleWindow_.close();
            if (winStatus_) { wbkgd(winStatus_, A_NORMAL); werase(winStatus_); wnoutrefresh(winStatus_); }
            if (winInfo_)   { wbkgd(winInfo_,   A_NORMAL); werase(winInfo_);   wnoutrefresh(winInfo_);   }
            if (winMenu_)   { wbkgd(winMenu_,   A_NORMAL); werase(winMenu_);   wnoutrefresh(winMenu_);   }
            wbkgd(stdscr, A_NORMAL); werase(stdscr); wnoutrefresh(stdscr);
            doupdate();
            return;
        }

        bool isSigint = (screen_ == Screen::SIGINT_SCANNER  ||
                         screen_ == Screen::SIGINT_PROBES   ||
                         screen_ == Screen::SIGINT_DEAUTHS  ||
                         screen_ == Screen::SIGINT_CAPTURES);
        if (isSigint) {
            if (battleWindow_.isOpen()) battleWindow_.close();
            // fall through to standard ncurses render path
        } else {
            if (!battleWindow_.isOpen()) battleWindow_.open();
            if (battleWindow_.isOpen()) {
                syncBattleWindow();
                battleWindow_.render();
            }
            return;
        }
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
        case Screen::MOVE_LEARN:    renderMoveLearn();                       break;
        case Screen::GYM_SELECT:    renderGymSelect();                       break;
        case Screen::BREEDING:      renderBreeding();                        break;
        case Screen::PVP_BATTLE:    renderPvpBattle();                       break;
        case Screen::PVP_BATTLE_END:renderPvpBattleEnd();                    break;
        case Screen::HELP:            renderHelp();                            break;
        case Screen::CONFIRM_QUIT:  renderConfirmQuit();                     break;
        case Screen::CHALLENGE:     renderChallenge();                       break;
        case Screen::SIGINT_SCANNER:  renderSigintScanner();                 break;
        case Screen::SIGINT_PROBES:   renderSigintProbes();                  break;
        case Screen::SIGINT_DEAUTHS:  renderSigintDeauths();                 break;
        case Screen::SIGINT_CAPTURES: renderSigintCaptures();                break;
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

    // Items start at row 3; the menu window is menuRows_ tall, leaving
    // menuRows_-4 usable item rows (4 at the default 8). The MESH/LOCAL tabs
    // top out at 3-4 items so no scrolling is needed.
    const char **items = tabItems(activeTab_);
    int count = tabItemCount(activeTab_);
    int maxRows = menuRows_ - 4;
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
        // Helper: format elapsed seconds as "Xs", "Xm", or "Xh"
        auto fmtAgo = [](uint32_t nowMs32, uint32_t seenMs, char *out, size_t len) {
            if (seenMs == 0) { snprintf(out, len, "?"); return; }
            uint32_t sec = nowMs32 >= seenMs ? (nowMs32 - seenMs) / 1000 : 0;
            if (sec < 60)        snprintf(out, len, "%us", (unsigned)sec);
            else if (sec < 3600) snprintf(out, len, "%um", (unsigned)(sec / 60));
            else                 snprintf(out, len, "%uh", (unsigned)(sec / 3600));
        };
        uint32_t now32 = (uint32_t)(now & 0xFFFFFFFF);
        for (int i = 0; i < neighborDisplayCount_ && i * 2 < infoRows - 1; i++) {
            const NeighborEntry &n = neighbors_[i];
            bool sel = (i == neighborSel_);
            bool fresh = (n.firstSeenMs != 0 &&
                          now - n.firstSeenMs < NEW_NEIGHBOR_HIGHLIGHT_MS);
            if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
            const char *marker = fresh ? "*" : " ";
            mvwprintw(winInfo_, i * 2, 0, "%s", marker);
            if (fresh) wattron(winInfo_, COLOR_PAIR(4) | A_BOLD);
            else       wattron(winInfo_, A_BOLD);
            mvwprintw(winInfo_, i * 2, 1, " %s", n.shortName);
            if (fresh) wattroff(winInfo_, COLOR_PAIR(4));
            wattroff(winInfo_, A_BOLD);
            char ago[8];
            fmtAgo(now32, n.lastSeenMs, ago, sizeof(ago));
            mvwprintw(winInfo_, i * 2, 2 + (int)strlen(n.shortName),
                      " (%s) %s%s", n.gameName, ago, sel ? " <" : "  ");
            if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
            if (n.partyCount > 0)
                mvwprintw(winInfo_, i * 2 + 1, 3, "%s Lv%d  (%d in party)",
                          n.lead, n.leadLevel, n.partyCount);
        }
    }

    // ── Battle action menu (opens on A over the highlighted neighbor) ─────────
    if (neighborAction_ >= 0 && neighborSel_ < neighborDisplayCount_) {
        const NeighborEntry &n = neighbors_[neighborSel_];
        static const char *const kActs[NEIGHBOR_ACTION_COUNT] = {
            "MonsterMesh Battle", "Gauntlet", "Cancel"
        };
        static const char *const kActDesc[NEIGHBOR_ACTION_COUNT] = {
            "live PvP", "async fight", ""
        };
        mvwprintw(winMenu_, 0, 1, "Battle %s:", n.shortName);
        for (int a = 0; a < NEIGHBOR_ACTION_COUNT; a++) {
            bool asel = (a == neighborAction_);
            if (asel) wattron(winMenu_, A_REVERSE | COLOR_PAIR(3));
            mvwprintw(winMenu_, 1 + a, 1, " %s%s%s ",
                      kActs[a], kActDesc[a][0] ? " - " : "", kActDesc[a]);
            if (asel) wattroff(winMenu_, A_REVERSE | COLOR_PAIR(3));
        }
        mvwprintw(winMenu_, 1 + NEIGHBOR_ACTION_COUNT, 1, "[A] Select  [B] Cancel");
    } else if (neighborDisplayCount_ > 0) {
        // Browsing hint.
        mvwprintw(winMenu_, 0, 1, "[A] Battle   [B] Back");
    }
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

// ── Move-learn chooser ──────────────────────────────────────────────────────────
// Fired when a daycare level-up teaches a move to a Pokemon that already knows
// four.  The list is rendered in the (taller) info panel; the weakest current
// move is pre-highlighted so the player can just press A.  Options 0-3 forget
// that move; option 4 declines.

int TerminalUI::weakestMoveIndex(const PendingLearn &pl) const {
    int best = 0, bestScore = 1000;
    for (int i = 0; i < 4; i++) {
        const Gen1MoveData *m = pl.curMoves[i] ? gen1Move(pl.curMoves[i]) : nullptr;
        int score = m ? m->power : 0;
        // A 0-power status move (Thunder Wave, Sleep Powder, ...) is often more
        // useful than a weak attack, so give it a nominal score rather than
        // always auto-dropping it.  The player can still override.
        if (score == 0) score = 35;
        if (pl.curMoves[i] == 0) score = -1;  // an empty slot is always weakest
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

void TerminalUI::advanceLearnQueue() {
    if (!learnQueue_.empty()) learnQueue_.erase(learnQueue_.begin());
    if (learnQueue_.empty()) {
        screen_ = Screen::MENU;
    } else {
        learnCursor_ = weakestMoveIndex(learnQueue_.front());
    }
}

void TerminalUI::renderMoveLearn() {
    werase(winInfo_);
    werase(winMenu_);
    if (learnQueue_.empty()) { screen_ = Screen::MENU; return; }

    const PendingLearn &pl = learnQueue_.front();
    const Gen1MoveData *nm = gen1Move(pl.newMove);

    mvwprintw(winInfo_, 0, 1, "=== Move Learning ===");
    wattron(winInfo_, A_BOLD);
    mvwprintw(winInfo_, 2, 2, "%s wants to learn %s (PWR %u)!",
              pl.nick, moveName(pl.newMove), nm ? (unsigned)nm->power : 0);
    wattroff(winInfo_, A_BOLD);
    mvwprintw(winInfo_, 4, 2, "Forget which move?");

    for (int i = 0; i < 5; i++) {
        int row = 6 + i;
        const char *cursor = (i == learnCursor_) ? ">" : " ";
        if (i == learnCursor_) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
        if (i < 4) {
            const Gen1MoveData *cm = pl.curMoves[i] ? gen1Move(pl.curMoves[i]) : nullptr;
            if (pl.curMoves[i])
                mvwprintw(winInfo_, row, 2, "%s %-13s PWR %u",
                          cursor, moveName(pl.curMoves[i]),
                          cm ? (unsigned)cm->power : 0);
            else
                mvwprintw(winInfo_, row, 2, "%s (empty slot)", cursor);
        } else {
            mvwprintw(winInfo_, row, 2, "%s Don't learn %s", cursor,
                      moveName(pl.newMove));
        }
        if (i == learnCursor_) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
    }

    int remaining = (int)learnQueue_.size() - 1;
    if (remaining > 0)
        mvwprintw(winInfo_, 12, 2, "(%d more move%s to decide)",
                  remaining, remaining == 1 ? "" : "s");

    mvwprintw(winMenu_, 1, 1, "[Up/Down] Pick  [A] Confirm  [B] Skip");
}

void TerminalUI::moveLearnButton(const ButtonEvent &ev) {
    if (learnQueue_.empty()) { screen_ = Screen::MENU; return; }

    switch (ev.button) {
        case GpiButton::UP:
            learnCursor_ = (learnCursor_ + 5 - 1) % 5;
            break;
        case GpiButton::DOWN:
            learnCursor_ = (learnCursor_ + 1) % 5;
            break;
        case GpiButton::A: {
            PendingLearn pl = learnQueue_.front();
            int forget = learnCursor_;   // 4 == don't learn (daemon skips >=4)
            char cmd[96];
            snprintf(cmd, sizeof(cmd),
                "{\"cmd\":\"LEARN_MOVE\",\"slot\":%u,\"forget\":%d,\"move\":%u}",
                (unsigned)pl.slot, forget, (unsigned)pl.newMove);
            ipc_.send(cmd);
            if (forget < 4) {
                pushActivity("  %s learned %s (forgot %s)", pl.nick,
                             moveName(pl.newMove), moveName(pl.curMoves[forget]));
                // Keep later decisions for the SAME slot accurate: the move we
                // just replaced is now the new move.
                for (auto &q : learnQueue_)
                    if (q.slot == pl.slot) q.curMoves[forget] = pl.newMove;
            } else {
                pushActivity("  %s did not learn %s", pl.nick,
                             moveName(pl.newMove));
            }
            advanceLearnQueue();
            break;
        }
        case GpiButton::B:
            pushActivity("  %s did not learn %s", learnQueue_.front().nick,
                         moveName(learnQueue_.front().newMove));
            advanceLearnQueue();
            break;
        default:
            break;
    }
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
            const char *who = (pendingTrainerIdx_ >= lordGymLeaderIndex(g))
                                ? g->leaderName
                                : g->trainers[pendingTrainerIdx_].name;
            snprintf(hdr, sizeof(hdr),
                     "%s Gym  Round %d/%u  vs %s",
                     g->city, round, (unsigned)g->trainerCount, who);
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

    // START+SELECT = quit from anywhere (non-pentest modes)
    if (!pentestMode_ &&
        (ev.button == GpiButton::START || ev.button == GpiButton::SELECT)) {
        static uint64_t firstMs = 0;
        static bool     firstWasStart = false;
        uint64_t now = millis();
        if (firstMs == 0 || now - firstMs > 1000) {
            firstMs       = now;
            firstWasStart = (ev.button == GpiButton::START);
        } else {
            bool otherHeld = firstWasStart ? (ev.button == GpiButton::SELECT)
                                           : (ev.button == GpiButton::START);
            if (otherHeld) { requestQuit(); return; }
        }
    }
    // Select always toggles help (from any screen except battle)
    if (ev.button == GpiButton::SELECT && screen_ != Screen::BATTLE) {
        screen_ = (screen_ == Screen::HELP) ? Screen::MENU : Screen::HELP;
        return;
    }
    // Start always returns to menu — except on the Breeding screen, where START
    // is the "open the breeder-rooms menu" button (handled in breedingButton).
    if (ev.button == GpiButton::START && screen_ != Screen::BATTLE &&
        screen_ != Screen::BREEDING) {
        screen_ = Screen::MENU;
        return;
    }

    // NOTE: the player's on-screen colour is FIXED to the skin the active mon
    // was caught or bred with (state_.you.variant, derived from its genotype).
    // The old L/R "cycle skin" test-hook has been removed so coloration can no
    // longer be changed by hand.

    switch (screen_) {
        case Screen::MENU:          menuButton(ev);         break;
        case Screen::PARTY:         partyButton(ev);        break;
        case Screen::NEIGHBORS:     neighborsButton(ev);    break;
        case Screen::DAYCARE_EVENT: daycareEventButton(ev); break;
        case Screen::BATTLE:        battleButton(ev);       break;
        case Screen::BATTLE_END:    battleEndButton(ev);    break;
        case Screen::MOVE_LEARN:    moveLearnButton(ev);    break;
        case Screen::GYM_SELECT:    gymSelectButton(ev);    break;
        case Screen::BREEDING:      breedingButton(ev);     break;
        case Screen::PVP_BATTLE:    pvpBattleButton(ev);    break;
        case Screen::PVP_BATTLE_END:pvpBattleEndButton(ev); break;
        case Screen::HELP:            helpButton(ev);              break;
        case Screen::CONFIRM_QUIT:  confirmQuitButton(ev);        break;
        case Screen::CHALLENGE:     challengeButton(ev);          break;
        case Screen::SIGINT_SCANNER:  sigintScannerButton(ev);    break;
        case Screen::SIGINT_PROBES:   sigintProbesButton(ev);     break;
        case Screen::SIGINT_DEAUTHS:  sigintDeauthsButton(ev);    break;
        case Screen::SIGINT_CAPTURES: sigintCapturesButton(ev);   break;
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
        case GpiButton::Y:
            startLocalBattle();
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

// MonsterMesh Battle — live synchronous PvP. Send a real challenge over the
// mesh (Pi as server) and wait for the peer to accept. Same challenge/accept
// flow + branding as the T-Deck's MonsterMesh Battle.
void TerminalUI::startMmbChallenge(const NeighborEntry &n) {
    pvpServerMode_         = true;
    pvpPendingMyAction_    = 0xFF;
    pvpPendingOppReceived_ = false;
    char cmd[64];
    snprintf(cmd, sizeof(cmd),
             "{\"cmd\":\"SEND_CHALLENGE\",\"node_id\":%u}", (unsigned)n.nodeId);
    ipc_.send(cmd);
    pushActivity("> MonsterMesh Battle: challenge sent to %s...", n.shortName);
}

// Gauntlet — asynchronous fight. Ask the daemon for the neighbor's cached party
// (from their beacon), run a local battle against that snapshot with no need
// for the peer to be online, then ANNOUNCE_RESULT back to the mesh as chat.
void TerminalUI::startGauntletFight(const NeighborEntry &n) {
    asyncFightActive_ = true;
    asyncFightNodeId_ = n.nodeId;
    snprintf(asyncFightTrainer_, sizeof(asyncFightTrainer_),
             "%s-%s", n.shortName, n.gameName);
    char cmd[80];
    snprintf(cmd, sizeof(cmd),
             "{\"cmd\":\"GET_NEIGHBOR_PARTY\",\"node_id\":%u}", (unsigned)n.nodeId);
    ipc_.send(cmd);
    pushActivity("> Gauntlet vs %s...", asyncFightTrainer_);
}

void TerminalUI::neighborsButton(const ButtonEvent &ev) {
    // ── Battle action menu open on the highlighted neighbor ───────────────────
    if (neighborAction_ >= 0) {
        switch (ev.button) {
            case GpiButton::UP:
                neighborAction_ = (neighborAction_ + NEIGHBOR_ACTION_COUNT - 1) % NEIGHBOR_ACTION_COUNT;
                break;
            case GpiButton::DOWN:
                neighborAction_ = (neighborAction_ + 1) % NEIGHBOR_ACTION_COUNT;
                break;
            case GpiButton::A: {
                int action = neighborAction_;
                neighborAction_ = -1;                       // close the menu
                if (action == 2 || neighborSel_ >= neighborDisplayCount_) break;  // Cancel
                const NeighborEntry &n = neighbors_[neighborSel_];
                if (n.nodeId == 0) { pushActivity("> Can't battle: missing node ID"); break; }
                if (action == 0) startMmbChallenge(n);      // MonsterMesh Battle (live PvP)
                else             startGauntletFight(n);     // Gauntlet (async)
                break;
            }
            case GpiButton::B:
                neighborAction_ = -1;                       // cancel, stay in the list
                break;
            default: break;
        }
        return;
    }

    // ── Browsing the neighbor list ────────────────────────────────────────────
    switch (ev.button) {
        case GpiButton::UP:
            if (neighborDisplayCount_ > 0)
                neighborSel_ = (neighborSel_ + neighborDisplayCount_ - 1) % neighborDisplayCount_;
            break;
        case GpiButton::DOWN:
            if (neighborDisplayCount_ > 0)
                neighborSel_ = (neighborSel_ + 1) % neighborDisplayCount_;
            break;
        case GpiButton::A:
            // Open the battle action menu on the highlighted neighbor.
            if (neighborSel_ < neighborDisplayCount_ && neighbors_[neighborSel_].nodeId != 0)
                neighborAction_ = 0;
            else if (neighborDisplayCount_ > 0)
                pushActivity("> Can't battle: missing node ID");
            break;
        case GpiButton::Y:
            // Power-user shortcut straight to a live MonsterMesh Battle.
            if (neighborSel_ < neighborDisplayCount_ && neighbors_[neighborSel_].nodeId != 0)
                startMmbChallenge(neighbors_[neighborSel_]);
            break;
        case GpiButton::B:
            screen_ = Screen::MENU;
            neighborSel_ = 0;
            neighborAction_ = -1;
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
                pvpXpAwarded_ = false;    // re-arm the once-per-battle XP award
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
    if (inPentestBattle_ || pentestStandby_) { pentestButton(ev); return; }
    if (inBattle_) {
        battleHandleButton(ev);
    } else {
        battleEndButton(ev);
    }
}

void TerminalUI::pentestButton(const ButtonEvent &ev) {
    // ── Status overlay open: 6-option menu ───────────────────────────────────
    //   0: Back
    //   1: AP Scanner  → SIGINT_SCANNER (ncurses, B returns to BATTLE)
    //   2: Probe Sniffer → SIGINT_PROBES
    //   3: Deauth Log  → SIGINT_DEAUTHS
    //   4: Captures    → SIGINT_CAPTURES
    //   5: Reset Pikachu → confirm step (nOpts shrinks to 2)

    // ── START is the universal "menu" button: it opens/closes the status &
    //    SIGINT overlay. START+SELECT (either order, within 1 s) still saves &
    //    exits pentest (the emulator-style hotkey). SELECT alone just arms the
    //    combo. Handled here, ahead of everything, so START works from any view.
    if (ev.pressed && (ev.button == GpiButton::START || ev.button == GpiButton::SELECT)) {
        uint64_t now = millis();
        bool combo = false;
        if (pentestExitPressMs_ != 0 && now - pentestExitPressMs_ < 1000)
            combo = pentestExitStartFirst_ ? (ev.button == GpiButton::SELECT)
                                           : (ev.button == GpiButton::START);
        if (combo) {
            const Gen1BattleEngine::BattleParty &pp = engine_.party(0);
            if (pp.count > 0 && pp.mons[0].level > 0) {
                pentestLevel_ = pp.mons[0].level;
                pentestXp_    = slotLevelXp_[0];
            }
            pentestSaveProgress();
            inPentestBattle_ = false;
            inBattle_        = false;
            requestQuit();
            return;
        }
        pentestExitPressMs_    = now;
        pentestExitStartFirst_ = (ev.button == GpiButton::START);
        // START alone toggles the overlay (never while manually boss-fighting).
        if (ev.button == GpiButton::START && !pentestBossMode_) {
            pentestShowStatus_ = !pentestShowStatus_;
            pentestSubView_    = 0;
            pentestStatusSel_  = 0;
            pentestMenuPage_   = 0;
            if (pentestShowStatus_) pentestConfirmReset_ = false;
        }
        return;   // SELECT alone: swallow, armed for the exit combo.
    }
    if (ev.pressed) pentestExitPressMs_ = 0;   // other button breaks the combo

    if (pentestShowStatus_) {
        // ── Status detail sub-page (subView 1): B returns to menu ────────────
        if (pentestSubView_ == 1) {
            if (ev.button == GpiButton::B && ev.pressed) {
                pentestSubView_   = 0;
                pentestMenuPage_  = 0;
                pentestStatusSel_ = 1;   // leave cursor on Status
            }
            return;
        }

        // ── Bill's PC browser (subView 6): L/R = tab, U/D = scan, A = menu ────
        if (pentestSubView_ == 6) {
            if (!ev.pressed) return;
            const auto &box = breedApp_.roster();
            // Build the current tab's filtered index list (for scan + actions).
            std::vector<int> filt;
            for (int i = 0; i < (int)box.size(); ++i)
                if (boxTabMatch(breeding::skinOf(box[i].geno), pentestBoxTab_)) filt.push_back(i);
            int fn = (int)filt.size();

            // Action menu open (A opened it):
            //   0=Set Active   1=Breed (put in room)   2=Dedup   3=Cancel
            if (pentestBoxAction_ >= 0) {
                switch (ev.button) {
                    case GpiButton::UP:   pentestBoxAction_ = (pentestBoxAction_ + 3) % 4; return;
                    case GpiButton::DOWN: pentestBoxAction_ = (pentestBoxAction_ + 1) % 4; return;
                    case GpiButton::A:
                        if (pentestBoxAction_ == 0 && fn > 0) {          // Set Active
                            const breeding::BreedMon &m = box[filt[pentestBoxSel_ % fn]];
                            // Independent journeys: swap battler AND its trip.
                            // pentestActivateMon banks the outgoing mon's live
                            // progress, sets dex/variant/id (coloration lock via
                            // skinOf preserved), and loads this mon's own level/
                            // badges/W-L into the live set (seeding from its catch
                            // level on first activation).
                            uint8_t mdex = m.dex;              // m may dangle after activate
                            pentestActivateMon(m.id);
                            pentestSaveProgress();
                            breedMsg_ = std::string(dexName(mdex)) + " is now your battler!";
                        } else if (pentestBoxAction_ == 1 && fn > 0) {   // Breed
                            pentestBoxBreedPick(box[filt[pentestBoxSel_ % fn]]);
                        } else if (pentestBoxAction_ == 2) {             // Dedup
                            pentestDedupBox();
                            breedMsg_ = "Deduped: one per species+colour.";
                        }
                        pentestBoxAction_ = -1;
                        return;
                    case GpiButton::B: pentestBoxAction_ = -1; return;
                    default: return;
                }
            }

            switch (ev.button) {
                case GpiButton::LEFT:
                    pentestBoxTab_ = (pentestBoxTab_ + kBoxTabCount - 1) % kBoxTabCount;
                    pentestBoxSel_ = 0;
                    return;
                case GpiButton::RIGHT:
                    pentestBoxTab_ = (pentestBoxTab_ + 1) % kBoxTabCount;
                    pentestBoxSel_ = 0;
                    return;
                case GpiButton::UP:
                    if (fn) pentestBoxSel_ = (pentestBoxSel_ + fn - 1) % fn;
                    return;
                case GpiButton::DOWN:
                    if (fn) pentestBoxSel_ = (pentestBoxSel_ + 1) % fn;
                    return;
                case GpiButton::A:
                    pentestBoxAction_ = 0;   // open the action menu
                    return;
                case GpiButton::B:
                    pentestSubView_   = 0;
                    pentestMenuPage_  = 0;
                    pentestStatusSel_ = 2;   // leave cursor on Bill's PC
                    return;
                default: return;
            }
        }

        // ── SIGINT sub-views (2=scanner 3=probes 4=deauths 5=captures) ────────
        if (pentestSubView_ >= 2 && pentestSubView_ <= 5) {
            if (!ev.pressed) return;
            // Common: B = back to the Security submenu (these tools live there).
            if (ev.button == GpiButton::B) {
                int prevView      = pentestSubView_;   // 2..5
                pentestSubView_   = 0;
                pentestMenuPage_  = 1;                 // Security page
                pentestStatusSel_ = prevView - 1;      // subView 2..5 → item 1..4
                return;
            }
            // Scroll-enabled sub-views
            int *sel = nullptr, *scroll = nullptr;
            int  listSz = 0;
            if (pentestSubView_ == 2) { sel = &sigintNetSel_;    scroll = &sigintNetScroll_;    listSz = (int)sigintNets_.size();    }
            if (pentestSubView_ == 3) { sel = &sigintProbeSel_;  scroll = &sigintProbeScroll_;  listSz = (int)sigintProbes_.size();  }
            if (pentestSubView_ == 4) { sel = &sigintDeauthSel_; scroll = &sigintDeauthScroll_; listSz = (int)sigintDeauths_.size(); }
            if (pentestSubView_ == 5) { sel = &sigintCapSel_;    scroll = &sigintCapScroll_;    listSz = (int)sigintCaps_.size();    }
            if (sel) {
                constexpr int vis = 7;
                switch (ev.button) {
                    case GpiButton::UP:
                        if (*sel > 0) { (*sel)--; if (*sel < *scroll) (*scroll)--; }
                        return;
                    case GpiButton::DOWN:
                        if (*sel < listSz - 1) { (*sel)++; if (*sel >= *scroll + vis) (*scroll)++; }
                        return;
                    case GpiButton::A: case GpiButton::SELECT:
                        // AP scanner: A = start/refresh capture; others = refresh data
                        if (pentestSubView_ == 2 && (int)sigintNets_.size() > *sel)
                            sigintStartCapture(sigintNets_[*sel]);
                        else if (pentestSubView_ == 2) { sigintLoadAllNets(); *sel = *scroll = 0; }
                        else if (pentestSubView_ == 3) { sigintLoadProbes();  *sel = *scroll = 0; }
                        else if (pentestSubView_ == 4) { sigintLoadDeauths(); *sel = *scroll = 0; }
                        else if (pentestSubView_ == 5) { sigintLoadCapFiles();*sel = *scroll = 0; }
                        return;
                    default: return;
                }
            }
            return;
        }

        // ── Confirm-reset sub-step ────────────────────────────────────────────
        if (pentestConfirmReset_) {
            switch (ev.button) {
                case GpiButton::UP: case GpiButton::DOWN:
                case GpiButton::LEFT: case GpiButton::RIGHT:
                    pentestStatusSel_ = 1 - pentestStatusSel_;
                    return;
                case GpiButton::A:
                    if (pentestStatusSel_ == 0) pentestResetProgress();
                    else { pentestConfirmReset_ = false; pentestStatusSel_ = 4; }
                    return;
                case GpiButton::B:
                    if (ev.pressed) { pentestConfirmReset_ = false; pentestStatusSel_ = 4; }
                    return;
                default: return;
            }
        }

        // ── Main in-log menu (page 0) / Security submenu (page 1) ─────────────
        //   Main:     0 Back  1 Status  2 Bill's PC  3 Security  4 Reset Pikachu
        //   Security: 0 Back  1 AP Scanner  2 Probe Sniffer  3 Deauth Log  4 Captures
        const int nOpts = 5;
        switch (ev.button) {
            case GpiButton::UP:   case GpiButton::LEFT:
                pentestStatusSel_ = (pentestStatusSel_ + nOpts - 1) % nOpts;
                return;
            case GpiButton::DOWN: case GpiButton::RIGHT:
                pentestStatusSel_ = (pentestStatusSel_ + 1) % nOpts;
                return;
            case GpiButton::A:
                if (pentestMenuPage_ == 1) {
                    // ── Security submenu ──────────────────────────────────────
                    switch (pentestStatusSel_) {
                        case 0:  // Back → main menu, cursor on Security
                            pentestMenuPage_  = 0;
                            pentestStatusSel_ = 3;
                            break;
                        case 1:  // AP Scanner
                            sigintLoadAllNets();
                            sigintNetSel_ = sigintNetScroll_ = 0;
                            pentestSubView_ = 2;
                            break;
                        case 2:  // Probe Sniffer
                            sigintLoadProbes();
                            sigintProbeSel_ = sigintProbeScroll_ = 0;
                            pentestSubView_ = 3;
                            break;
                        case 3:  // Deauth Log
                            sigintLoadDeauths();
                            sigintDeauthSel_ = sigintDeauthScroll_ = 0;
                            pentestSubView_ = 4;
                            break;
                        case 4:  // Captures
                            sigintLoadCapFiles();
                            sigintCapSel_ = sigintCapScroll_ = 0;
                            pentestSubView_ = 5;
                            break;
                    }
                    return;
                }
                // ── Main menu ─────────────────────────────────────────────────
                switch (pentestStatusSel_) {
                    case 0:  // Back
                        pentestShowStatus_ = false;
                        pentestStatusSel_  = 0;
                        break;
                    case 1:  // Status
                        pentestSubView_ = 1;
                        break;
                    case 2:  // Bill's PC — caught-mon browser
                        pentestBoxSel_  = 0;
                        pentestSubView_ = 6;
                        break;
                    case 3:  // Security — open the WiFi/pentest submenu
                        pentestMenuPage_  = 1;
                        pentestStatusSel_ = 0;
                        break;
                    case 4:  // Reset Pikachu
                        pentestConfirmReset_ = true;
                        pentestStatusSel_    = 0;
                        break;
                }
                return;
            case GpiButton::B:
                if (ev.pressed) {
                    if (pentestMenuPage_ == 1) {
                        // Leave Security → back to main menu, cursor on Security.
                        pentestMenuPage_  = 0;
                        pentestStatusSel_ = 3;
                    } else {
                        pentestShowStatus_ = false;
                        pentestStatusSel_  = 0;
                        pentestSubView_    = 0;
                    }
                }
                return;
            default:
                return;
        }
    } else if (ev.button == GpiButton::A) {
        if (pentestBossMode_) {
            battleHandleButton(ev);
        } else {
            pentestShowStatus_   = true;
            pentestSubView_      = 0;
            pentestStatusSel_    = 0;
            pentestMenuPage_     = 0;
            pentestConfirmReset_ = false;
        }
        return;
    }

    // R (shoulder) — toggle screen off / back on.
    if (ev.button == GpiButton::X && ev.pressed) {
        pentestScreenOff_ = !pentestScreenOff_;
        if (!pentestScreenOff_) {
            // Restore backlight when turning screen back on.
#ifndef __APPLE__
            (void)system("sudo -n bash -c 'for f in /sys/class/backlight/*/brightness; do echo 255 > \"$f\"; done' 2>/dev/null || true");
#endif
        } else {
            // Kill backlight when screen goes off.
#ifndef __APPLE__
            (void)system("sudo -n bash -c 'for f in /sys/class/backlight/*/brightness; do echo 0 > \"$f\"; done' 2>/dev/null || true");
#endif
        }
        return;
    }

    // Y — boss key: toggle boss mode in battle, OR start a random interactive
    // battle from the standby screen (hides the pentest session in plain sight).
    if (ev.button == GpiButton::Y && ev.pressed) {
        if (pentestStandby_) {
            // From standby: force-start a random Kanto battle without waiting
            // for a real network scan (pentestRematch_ bypasses pentestPickTarget).
            pentestRematch_ = true;
            startPentestBattle();
            pentestBossMode_ = true;
            return;
        }
        if (inPentestBattle_ && inBattle_ &&
            battleResult_ == Gen1BattleEngine::Result::ONGOING) {
            pentestBossMode_ = !pentestBossMode_;
            // When entering boss mode suppress the status overlay.
            if (pentestBossMode_) {
                pentestShowStatus_   = false;
                pentestConfirmReset_ = false;
            }
        }
        return;
    }

    // Boss mode: delegate directional/A/B inputs to the interactive battle handler.
    if (pentestBossMode_) {
        switch (ev.button) {
            case GpiButton::UP:
            case GpiButton::DOWN:
            case GpiButton::LEFT:
            case GpiButton::RIGHT:
            case GpiButton::A:
                battleHandleButton(ev);
                return;
            case GpiButton::B:
                // B exits boss mode but does NOT quit pentest.
                if (ev.pressed) pentestBossMode_ = false;
                return;
            default:
                break;
        }
    }

    // (START / SELECT — menu toggle and the START+SELECT exit combo — are handled
    //  at the top of this function, ahead of every other view.)
}

// Wipe all pentest progress back to a fresh Level-5 Pikachu, kick a fresh
// wpa_cli scan, and enter standby so the scan has time to complete before
// pentestScanNetworks() reads the results.  This ensures all currently visible
// networks appear after reset — not just whatever was in the stale cache.
void TerminalUI::pentestResetProgress() {
    pentestLevel_  = 5;
    pentestXp_     = 0;
    memset(pentestDex_,    0, sizeof(pentestDex_));
    memset(pentestBeaten_, 0, sizeof(pentestBeaten_));
    pentestUsedSsid_  = 0;
    pentestWins_      = 0;
    pentestLosses_    = 0;
    pentestGymBeaten_ = 0;
    pentestActiveDex_     = 0;   // back to the default Pikachu…
    pentestActiveVariant_ = 0;   // …in its Regular caught colour
    pentestActiveTritan_  = false;
    pentestActiveId_      = 0;
    pentestSaveProgress();

    pentestShowStatus_   = false;
    pentestConfirmReset_ = false;
    pentestStatusSel_    = 0;
    pentestRematch_      = false;
    pentestDoneSsids_.clear();   // forget every network cracked so far...
    pentestNets_.clear();        // ...so they're all fair game again.
#ifndef __APPLE__
    // Kick a fresh scan.  wpa_cli scan is async (~2-3s); by entering standby
    // the first standby tick (PENTEST_STANDBY_SCAN_MS = 6s later) reads fully
    // updated results rather than the stale pre-reset cache.
    (void)system("sudo -n wpa_cli -i wlan0 scan >/dev/null 2>&1");
#endif
    inPentestBattle_ = false;
    pentestEnterStandby();   // waits for fresh scan before picking a target
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

// Build the in-log menu (or status detail sub-page) that appears in the battle
// message box when the player presses A.  Uses menuMode so each line owns its
// own ">" selector arrow — drawMessageLog skips the global "> " prefix.
void TerminalUI::pentestBuildMenuLog(std::vector<std::string> &out) {
    char ln[64];

    // ── Status detail (subView 1) ─────────────────────────────────────────────
    if (pentestSubView_ == 1) {
        pentestBuildStatus(out);
        out.push_back("  B: back");
        return;
    }

    // ── AP Scanner (subView 2) ────────────────────────────────────────────────
    if (pentestSubView_ == 2) {
        if (sigintNets_.empty()) {
            out.push_back("  No vulnerable APs found.");
        } else {
            int vis = 7, scroll = sigintNetScroll_;
            for (int i = scroll; i < (int)sigintNets_.size() && i < scroll + vis; i++) {
                const auto &n = sigintNets_[i];
                // "SEL RSSI :xx:yy:zz SSID"
                static const char *tT[] = {"    ","OPEN","UNCO","RARE"};
                snprintf(ln, sizeof(ln), "%s %-4s %3d %-8s %.18s",
                         i == sigintNetSel_ ? ">" : " ",
                         tT[n.tier < 4 ? n.tier : 0],
                         n.rssi,
                         n.bssid[0] ? (n.bssid + (int)strlen(n.bssid) - 8) : "?",
                         n.ssid[0] ? n.ssid : "(hidden)");
                out.push_back(ln);
            }
        }
        if (sigintCapActive_)
            snprintf(ln, sizeof(ln), "  [CAPTURING %.20s]", sigintCapSSID_);
        else
            snprintf(ln, sizeof(ln), "  A:capture  SEL:rescan  B:back");
        out.push_back(ln);
        return;
    }

    // ── Probe Sniffer (subView 3) ─────────────────────────────────────────────
    if (pentestSubView_ == 3) {
        if (sigintProbes_.empty()) {
            out.push_back("  No probes captured.");
        } else {
            int vis = 7, scroll = sigintProbeScroll_;
            for (int i = scroll; i < (int)sigintProbes_.size() && i < scroll + vis; i++) {
                const auto &p = sigintProbes_[i];
                snprintf(ln, sizeof(ln), "%s %2d %-8s %.22s",
                         i == sigintProbeSel_ ? ">" : " ",
                         p.count,
                         p.mac[0] ? (p.mac + (int)strlen(p.mac) - 8) : "?",
                         p.ssid[0] ? p.ssid : "(hidden)");
                out.push_back(ln);
            }
        }
        out.push_back("  A/SEL:rescan  B:back");
        return;
    }

    // ── Deauth Log (subView 4) ────────────────────────────────────────────────
    if (pentestSubView_ == 4) {
        if (sigintDeauths_.empty()) {
            out.push_back("  No deauth events.");
        } else {
            int vis = 7, scroll = sigintDeauthScroll_;
            for (int i = scroll; i < (int)sigintDeauths_.size() && i < scroll + vis; i++) {
                snprintf(ln, sizeof(ln), "%s %.46s",
                         i == sigintDeauthSel_ ? ">" : " ",
                         sigintDeauths_[i].line);
                out.push_back(ln);
            }
        }
        out.push_back("  A:refresh  B:back");
        return;
    }

    // ── Captures (subView 5) ──────────────────────────────────────────────────
    if (pentestSubView_ == 5) {
        if (sigintCaps_.empty()) {
            out.push_back("  No captures yet.");
            out.push_back("  Use AP Scanner to start.");
        } else {
            int vis = 7, scroll = sigintCapScroll_;
            for (int i = scroll; i < (int)sigintCaps_.size() && i < scroll + vis; i++) {
                const auto &c = sigintCaps_[i];
                long kb = c.sizeBytes / 1024;
                snprintf(ln, sizeof(ln), "%s %-32s %ldK",
                         i == sigintCapSel_ ? ">" : " ",
                         c.name, kb);
                out.push_back(ln);
            }
        }
        out.push_back("  B:back");
        return;
    }

    // ── Confirm-reset sub-step ────────────────────────────────────────────────
    if (pentestConfirmReset_) {
        out.push_back("  RESET all progress?");
        out.push_back(pentestStatusSel_ == 0 ? "> Yes, RESET ALL" : "  Yes, RESET ALL");
        out.push_back(pentestStatusSel_ == 1 ? "> Cancel"         : "  Cancel");
        return;
    }

    // ── Main menu (subView 0) ─────────────────────────────────────────────────
    // Breeding is what most players are here for, so Bill's PC sits near the top
    // and the WiFi/pentest tools are tucked into a "Security" submenu (page 1).
    static const char *mainItems[] = {
        "Back", "Status", "Bill's PC", "Security", "Reset Pikachu",
    };
    static const char *secItems[] = {
        "Back", "AP Scanner", "Probe Sniffer", "Deauth Log", "Captures",
    };
    const bool sec = (pentestMenuPage_ == 1);
    const char **items = sec ? secItems : mainItems;
    const int nItems   = 5;
    if (sec) out.push_back("-- Security --");
    for (int i = 0; i < nItems; i++) {
        snprintf(ln, sizeof(ln), "%s %s",
                 (i == pentestStatusSel_) ? ">" : " ", items[i]);
        out.push_back(ln);
    }
}

void TerminalUI::pentestBuildStatus(std::vector<std::string> &out) {
    char line[96];
    // Standby banner: Pikachu is idle, waiting for a vulnerable WiFi network.
    if (pentestStandby_) {
        out.push_back("== STANDBY ==");
        out.push_back("Waiting for a vulnerable network...");
        snprintf(line, sizeof(line), "APs in range: %d   Caught: %d",
                 pentestNetsSeen_, (int)breedApp_.size());
        out.push_back(line);
        out.push_back("");
    }
    // Gyms cleared = leaders actually defeated (drives zone unlock, like the
    // T-Deck), not levels reached.
    int gyms = __builtin_popcount(pentestGymBeaten_);
    snprintf(line, sizeof(line), "Gyms cleared: %d/8", gyms);   out.push_back(line);
    const char *mon = (pentestLevel_ >= 30) ? "Raichu " : "Pikachu";  // evolves at L30
    snprintf(line, sizeof(line), "%s:      Lv%u", mon, (unsigned)pentestLevel_);
    out.push_back(line);
    // Independent journeys: show THIS mon's current Kanto location (the highest
    // zone unlocked by its badges that its level can reach). Zone is derived, so
    // it already reflects the active individual's own level + badges.
    {
        const KantoZone *zone = &KANTO_ZONES[0];
        for (uint8_t i = 0; i < KANTO_ZONE_COUNT; ++i)
            if (KANTO_ZONES[i].unlockedAfter <= (uint8_t)gyms &&
                KANTO_ZONES[i].minPikaLvl <= pentestLevel_)
                zone = &KANTO_ZONES[i];
        snprintf(line, sizeof(line), "Location:    %s", zone->name);
        out.push_back(line);
    }
    unsigned w = pentestWins_, l = pentestLosses_, tot = w + l;
    unsigned pct = tot ? (unsigned)((w * 100 + tot / 2) / tot) : 0;
    snprintf(line, sizeof(line), "Record: %u W - %u L  (%u%%)", w, l, pct);
    out.push_back(line);
    int seen = 0, beaten = 0;
    for (int i = 0; i < 151; i++) {
        if (pentestDex_[i >> 3]    & (1u << (i & 7))) seen++;
        if (pentestBeaten_[i >> 3] & (1u << (i & 7))) beaten++;
    }
    snprintf(line, sizeof(line), "Pokedex seen:   %d/151", seen);   out.push_back(line);
    snprintf(line, sizeof(line), "Pokedex beaten: %d/151", beaten); out.push_back(line);
    out.push_back("");

    // ── Bill's PC — the mons you've CAUGHT (full genotype, breedable) ──────────
    // Loaded from the box file at pentest init + appended live on each catch.
    if (!breedLoaded_) { pentestLoadBox(); breedLoaded_ = true; }
    const auto &box = breedApp_.roster();
    snprintf(line, sizeof(line), "Bill's PC (caught): %d", (int)box.size());
    out.push_back(line);
    if (box.empty()) {
        out.push_back("  (none yet - catch a rare colour!)");
    } else {
        // Newest first, cap the list so the status view stays readable.
        int shown = 0;
        for (int i = (int)box.size() - 1; i >= 0 && shown < 12; --i, ++shown) {
            const breeding::BreedMon &m = box[i];
            snprintf(line, sizeof(line), "  L%-3d %-10.10s %-8.8s %s%s%s",
                     m.level, m.nick[0] ? m.nick : dexName(m.dex),
                     breeding::skinName(breeding::skinOf(m.geno)),
                     m.geno.female ? "\xE2\x99\x80" : "\xE2\x99\x82",
                     breeding::isSterile(m.geno) ? " bb" : "",
                     breeding::isTritan(m.geno)
                         ? (m.geno.tritan == 2 ? " TT" : " Tn") : "");
            out.push_back(line);
        }
        if ((int)box.size() > 12) {
            snprintf(line, sizeof(line), "  ...and %d more", (int)box.size() - 12);
            out.push_back(line);
        }
    }
    out.push_back("");

    // "Pokemon in current gym" = the next leader you have to beat (Brock until
    // his badge is yours, then Misty, ...).  After all 8, show the last as the
    // repeatable boss.
    uint8_t gIdx = (uint8_t)gyms;
    if (gIdx >= LORD_GYM_COUNT) gIdx = LORD_GYM_COUNT - 1;
    const LordGym *g = lordGym(gIdx);
    if (g) {
        if (gyms >= LORD_GYM_COUNT)
            snprintf(line, sizeof(line), "All badges! Rematch: %s", g->leaderName);
        else
            snprintf(line, sizeof(line), "Next gym: %s (%s)", g->city, g->leaderName);
        out.push_back(line);
        out.push_back("Leader's team:");
        const LordGymTrainer &ldr = g->trainers[lordGymLeaderIndex(g)];
        std::string row;
        for (int i = 0; i < ldr.count; i++) {
            const char *nm = dexName(ldr.party[i].species);
            char cell[24];
            snprintf(cell, sizeof(cell), "%s L%u", nm ? nm : "?",
                     (unsigned)ldr.party[i].level);
            if (!row.empty()) row += ", ";
            row += cell;
            if ((i % 2) == 1) { out.push_back("  " + row); row.clear(); }
        }
        if (!row.empty()) out.push_back("  " + row);
    }

}

// Down-convert the blood-test UTF-8 (♀ ♂ — “ ”) to plain ASCII so the SDL 5×7
// bitmap font (ASCII-only) renders the box view cleanly.
static std::string pentestAsciiSanitize(const std::string &in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) { out += (char)c; ++i; continue; }
        // 3-byte sequences we care about.
        if (i + 2 < in.size() && c == 0xE2) {
            unsigned char b1 = (unsigned char)in[i+1], b2 = (unsigned char)in[i+2];
            if (b1 == 0x99 && b2 == 0x80)      { out += "(F)"; i += 3; continue; } // ♀
            if (b1 == 0x99 && b2 == 0x82)      { out += "(M)"; i += 3; continue; } // ♂
            if (b1 == 0x80 && b2 == 0x94)      { out += "-";   i += 3; continue; } // — em dash
            if (b1 == 0x80 && (b2 == 0x9C || b2 == 0x9D)) { out += '"'; i += 3; continue; } // “ ”
        }
        // Unknown multibyte: skip its continuation bytes, emit '?'.
        out += '?'; ++i;
        while (i < in.size() && ((unsigned char)in[i] & 0xC0) == 0x80) ++i;
    }
    return out;
}

// Fill `s` for the Bill's PC caught-mon browser (sub-view 6). Front+back sprites
// come from boxSpecies/boxVariant; the blood test goes into s.log (ASCII). The
// selected mon is pentestBoxSel_ (wrapped by the input handler).
bool TerminalUI::pentestSyncBoxView(BattleWindow::State &s) {
    if (!(pentestShowStatus_ && pentestSubView_ == 6)) return false;
    if (!breedLoaded_) { pentestLoadBox(); breedLoaded_ = true; }
    const auto &box = breedApp_.roster();
    int n = (int)box.size();

    s.boxView  = true;
    s.menuMode = false;
    s.log.clear();

    if (n == 0) {
        s.boxSpecies = 0;
        snprintf(s.header, sizeof(s.header), "Bill's PC  (empty)");
        return true;
    }

    // Per-tab counts + the tab-bar header (current tab bracketed).
    int cnt[kBoxTabCount] = {0};
    for (const auto &mm : box) {
        breeding::Skin s2 = breeding::skinOf(mm.geno);
        for (int t = 0; t < kBoxTabCount; ++t) if (boxTabMatch(s2, t)) cnt[t]++;
    }
    if (pentestBoxTab_ < 0) pentestBoxTab_ = kBoxTabCount - 1;
    if (pentestBoxTab_ >= kBoxTabCount) pentestBoxTab_ = 0;
    // Tab chips are drawn by BattleWindow from these fields.
    s.boxTabCur = (uint8_t)pentestBoxTab_;
    for (int t = 0; t < kBoxTabCount; ++t) s.boxTabCnt[t] = (uint16_t)cnt[t];
    s.header[0] = '\0';

    // Filtered list for the current tab (LEFT/RIGHT switch tabs, U/D scan within).
    std::vector<int> filt;
    for (int i = 0; i < n; ++i)
        if (boxTabMatch(breeding::skinOf(box[i].geno), pentestBoxTab_)) filt.push_back(i);
    int fn = (int)filt.size();
    if (fn == 0) { s.boxSpecies = 0; return true; }   // this category is empty
    if (pentestBoxSel_ < 0)  pentestBoxSel_ = fn - 1;
    if (pentestBoxSel_ >= fn) pentestBoxSel_ = 0;

    const breeding::BreedMon &m = box[filt[pentestBoxSel_]];
    const breeding::Genotype &g = m.geno;
    breeding::Skin sk = breeding::skinOf(g);
    s.boxSpecies = m.dex;
    s.boxVariant = (uint8_t)(int)sk;   // Skin 0..11 maps 1:1 to VAR_ (incl. Blackout)
    s.boxTritan  = breeding::isTritan(g);   // render as a tritanope sees it + badge
    s.boxAction   = (int8_t)pentestBoxAction_;
    s.boxIsActive = (pentestActiveDex_ != 0 && m.dex == pentestActiveDex_);
    // Breeding pick indicator (1st breeder chosen, waiting for the mate).
    s.boxBreedPending = (pentestBreedPickId_ != 0);
    s.boxBreedIsThis  = (pentestBreedPickId_ != 0 && m.id == pentestBreedPickId_);
    if (pentestBreedPickId_ != 0) {
        const breeding::BreedMon *bp = breedApp_.findById(pentestBreedPickId_);
        snprintf(s.boxBreedName, sizeof(s.boxBreedName), "%s",
                 bp ? (bp->nick[0] ? bp->nick : breeding::BreedingApp::dexName(bp->dex))
                    : "?");
    } else {
        s.boxBreedName[0] = '\0';
    }

    // Status box (battle FOE-box) fields: name, level, skin.
    s.foe.species = m.dex;
    s.foe.level   = m.level;
    snprintf(s.foe.nickname, sizeof(s.foe.nickname), "%s",
             m.nick[0] ? m.nick : breeding::BreedingApp::dexName(m.dex));
    snprintf(s.foeTag, sizeof(s.foeTag), "%s", breeding::skinName(sk));

    // ── Genome code line + two-column word list (cosmetic | markers) ──────────
    auto AP = [](uint8_t d, const char *a, const char *b, const char *c) {
        return d == 0 ? a : (d == 1 ? b : c);
    };
    char title[80];
    snprintf(title, sizeof(title), "%s  Lv%d  %s %s",
             m.nick[0] ? m.nick : breeding::BreedingApp::dexName(m.dex),
             m.level, breeding::skinName(sk), g.female ? "(F)" : "(M)");
    s.log.push_back(title);
    char gl[96];
    snprintf(gl, sizeof(gl), "Genome  %s %s %s | %s %s %s",
             AP(g.rainbow, "RR", "Rr", "rr"), AP(g.shiny, "SS", "Ss", "ss"),
             AP(g.dark, "dd", "Dd", "DD"),    AP(g.sterile, "BB", "Bb", "bb"),
             AP(g.cantFight, "FF", "Ff", "ff"), AP(g.noHatch, "HH", "Hh", "hh"));
    s.log.push_back(gl);

    // Per-locus meaning words. Rainbow/Pink display on females only; a male
    // carrier is shown as "hidden".
    const char *rW = g.rainbow == 0 ? "--"
                   : (g.female ? (g.rainbow == 2 ? "Rainbow" : "Pink")
                               : (g.rainbow == 2 ? "rr hidden" : "Rr hidden"));
    const char *sW = g.shiny == 2 ? (g.rainbow == 0 ? "SHINY" : "masked")
                                  : (g.shiny == 1 ? "carrier" : "--");
    const char *dW = g.dark == 2 ? "Blackout" : (g.dark == 1 ? "Dark" : "--");
    const char *bW = g.sterile == 2 ? "STERILE" : (g.sterile == 1 ? "carrier" : "ok");
    const char *fW = g.cantFight == 2 ? "CANT FIGHT" : (g.cantFight == 1 ? "carrier" : "ok");
    const char *hW = g.noHatch == 2 ? "NO HATCH" : (g.noHatch == 1 ? "carrier" : "ok");

    auto twoCol = [&](const char *ll, const char *lv, const char *lw,
                      const char *rl, const char *rv, const char *rw) {
        char left[40], row[96];
        snprintf(left, sizeof(left), "%-8s%s %s", ll, lv, lw);
        snprintf(row, sizeof(row), "%-24s%-8s%s %s", left, rl, rv, rw);
        s.log.push_back(row);
    };
    s.log.push_back("COSMETIC (looks)      MARKERS (health)");
    twoCol("Rainbow", AP(g.rainbow, "RR", "Rr", "rr"),  rW, "Sterile", AP(g.sterile, "BB", "Bb", "bb"),  bW);
    twoCol("Shiny",   AP(g.shiny, "SS", "Ss", "ss"),    sW, "CantFgt", AP(g.cantFight, "FF", "Ff", "ff"), fW);
    twoCol("Dark",    AP(g.dark, "dd", "Dd", "DD"),     dW, "NoHatch", AP(g.noHatch, "HH", "Hh", "hh"),  hW);
    // Tritan (autosomal-dominant colour blindness) — orthogonal to the skins, so
    // it gets its own line and is only shown when the mon actually carries it.
    if (breeding::isTritan(g)) {
        char tl[64];
        snprintf(tl, sizeof(tl), "Tritan   %s  (sees like a tritanope)",
                 AP(g.tritan, "nn", "Tn", "TT"));
        s.log.push_back(tl);
    }
    return true;
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
        const LordGym *g = lordGym(pendingGymIdx_);
        if (g && pendingTrainerIdx_ >= lordGymLeaderIndex(g)) {
            // Beat the leader -> award badge, mark gym cleared, return to menu
            lordSave_.badges |= (uint8_t)(1u << g->badgeBit);
            lordSave_.gymProgress[pendingGymIdx_] = g->trainerCount;
            // Record which NG+ tier this gym was last cleared at, so the gym
            // list can flag it for a rematch after the next NG+ rollover.
            if (pendingGymIdx_ < LORD_GYM_COUNT)
                lordSave_.gymTierCleared[pendingGymIdx_] = lordSave_.ngPlusTier;
            lordSave(lordSave_);
            inGymBattle_ = false;
        } else {
            // Advance to the next trainer.  Like the real games — where you can
            // walk out to the Poke Center between battles — the party is FULLY
            // HEALED (HP, PP, status, stat stages) before each gym trainer, so
            // each fight starts fresh instead of a no-heal gauntlet.
            // XP keeps accumulating in slotLevelXp_; no SAV writeback until the
            // gym is fully cleared, lost, or fled (at BATTLE_END below).
            pendingTrainerIdx_++;
            Gen1Party nextGymParty;
            if (lordBuildGymParty(pendingGymIdx_, pendingTrainerIdx_, nextGymParty)) {
                Gen1BattleEngine::BattleParty &php = engine_.party(localSide_);
                for (uint8_t i = 0; i < php.count; i++) {
                    Gen1BattleEngine::BattlePoke &m = php.mons[i];
                    m.hp           = m.maxHp;
                    m.status       = 0;          // ST_NONE
                    m.sleepTurns   = 0;
                    m.confuseTurns = 0;
                    m.toxicCounter = 0;
                    m.atkBoost = m.defBoost = m.spdBoost = m.spcBoost = 0;
                    m.accBoost = m.evaBoost = 0;
                    for (int k = 0; k < 4; k++) {
                        const Gen1MoveData *md = gen1Move(m.moves[k]);
                        if (md && m.moves[k]) m.pp[k] = md->pp;
                    }
                }
                engine_.replaceOpponent(nextGymParty);
                resetBattleParticipants();
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
                resetBattleParticipants();
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

    inGymBattle_   = false;
    inE4Battle_    = false;
    inBattle_      = false;
    pvpServerMode_ = false;
    battleLog_.clear();
    screen_        = Screen::MENU;
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

// Award result-based XP after a mesh (PvP) battle so BOTH sides earn,
// independent of which device ran the engine.  In server-auth PvP only the
// engine-running side accrues sessionXp_ via runTurnWithXp(); the client role
// (this Pi accepted a T-Deck's challenge) never runs turns, so without this it
// would earn nothing.  Formula: base = opponentAvgLevel * (won ? 6 : 2),
// clamped >=1, added to each surviving Gen-1 party slot's sessionXp_, then
// flushed to the daemon for the normal SAV writeback.  Fires once per battle.
void TerminalUI::awardPvpResultXp() {
    if (pvpXpAwarded_) return;
    pvpXpAwarded_ = true;

    const bool won = (pvpResult_ == 1);   // 1=win, 2=lose, 3=draw

    // Opponent average level.  The client role doesn't receive the foe's
    // per-mon levels over the wire, so use our own party's average level as a
    // proxy (teams are level-matched in practice).
    int sum = 0, n = 0;
    for (int i = 0; i < partyCount_ && i < 6; i++) {
        uint8_t lv = partySlots_[i].level;
        if (lv) { sum += lv; n++; }
    }
    uint8_t oppLv = n ? (uint8_t)(sum / n) : 10;

    uint32_t base = (uint32_t)oppLv * (won ? 6u : 2u);
    if (base < 1) base = 1;

    bool any = false;
    for (int i = 0; i < partyCount_ && i < 6; i++) {
        uint8_t dex = partySlots_[i].dex;
        if (dex < 1 || dex > 151) continue;   // Gen-1 species only
        sessionXp_[i] += base;
        any = true;
    }
    if (any) flushBattleXpToDaemon();
}

// ── Menu actions ──────────────────────────────────────────────────────────────

void TerminalUI::activateItem(int item) {
    switch (activeTab_) {
        case 0: activateMeshItem(item);   break;
        case 1: activateLocalItem(item);  break;
        case 2: activateSystemItem(item); break;
        case 3: activateBreedItem(item);  break;
    }
}

void TerminalUI::activateMeshItem(int item) {
    switch (item) {
        case 0: { // HollaBack (HB!) — broadcasts our party beacon AND, over
                  // MQTT, asks peers to reply so we get a live roster back.
                  // (The daemon downgrades to a plain beacon on a real radio.)
            ipc_.send("{\"cmd\":\"HOLLABACK\"}");
            pushActivity("HB!");
            // Stream freshly-arriving responses as HB lines for a short window
            // (see parseNeighbors); first, echo who we already know about.
            hbUntilMs_ = millis() + 15000;
            for (int i = 0; i < neighborDisplayCount_; i++) {
                const NeighborEntry &n = neighbors_[i];
                pushActivity("HB %s/%s Lv%d %s",
                             n.shortName[0] ? n.shortName : "?",
                             n.lead[0] ? n.lead : "?",
                             n.leadLevel, n.partyCount ? "Kanto" : "-");
            }
            break;
        }
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
        case 3: // Breed — Bill's PC box
            if (!breedLoaded_) { pentestLoadBox(); breedLoaded_ = true; }
            breedCursorA_ = 0;
            breedCursorB_ = -1;
            breedScroll_  = 0;
            breedMsg_.clear();
            screen_ = Screen::BREEDING;
            break;
    }
}

void TerminalUI::activateSystemItem(int item) {
    switch (item) {
        case 0: screen_ = Screen::HELP; break;
        case 1: screen_ = Screen::CONFIRM_QUIT; break;
    }
}

void TerminalUI::activateBreedItem(int item) {
    if (item == 0) {                       // Breeder Rooms
        if (!breedLoaded_) { pentestLoadBox(); breedLoaded_ = true; }
        breedCursorA_ = 0;
        breedCursorB_ = -1;
        breedScroll_  = 0;
        breedMsg_.clear();
        screen_ = Screen::BREEDING;
    }
}

int TerminalUI::tabItemCount(int tab) const {
    switch (tab) {
        case 0: return MESH_COUNT;
        case 1: return LOCAL_COUNT;
        case 2: return SYSTEM_COUNT;
        case 3: return BREED_COUNT;
        default: return 0;
    }
}

const char **TerminalUI::tabItems(int tab) const {
    switch (tab) {
        case 0: return (const char **)MESH_ITEMS;
        case 1: return (const char **)LOCAL_ITEMS;
        case 2: return (const char **)SYSTEM_ITEMS;
        case 3: return (const char **)BREED_ITEMS;
        default: return nullptr;
    }
}

const char **TerminalUI::tabDescs(int tab) const {
    switch (tab) {
        case 0: return (const char **)MESH_DESC;
        case 1: return (const char **)LOCAL_DESC;
        case 2: return (const char **)SYSTEM_DESC;
        case 3: return (const char **)BREED_DESC;
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

    // CPU party: a RANDOM team — different species, movesets and (slightly)
    // levels every fight, so no two CPU battles feel the same.  Drawn from a
    // varied Gen-1 pool with sensible moves, scaled to your party's level.
    static const uint8_t CPU_POOL[][5] = {
        // species, move1, move2, move3, move4
        {  4,  10, 45,  52,  0 },  // Charmander  Scratch / Growl / Ember
        {  7,  33, 39,  55,  0 },  // Squirtle    Tackle / Tail Whip / Water Gun
        {  1,  33, 45,  22,  0 },  // Bulbasaur   Tackle / Growl / Vine Whip
        { 25,  84, 45,  98,  0 },  // Pikachu     ThunderShock / Growl / Quick Attack
        { 52,  10, 45,  44,  0 },  // Meowth      Scratch / Growl / Bite
        {  6,  52, 17,  10,  0 },  // Charizard   Ember / Wing Attack / Scratch
        { 16,  33, 45,  16,  0 },  // Pidgey      Tackle / Growl / Gust
        { 19,  33, 39,  98,  0 },  // Rattata     Tackle / Tail Whip / Quick Attack
        { 41,  33, 141, 0,   0 },  // Zubat       Tackle / Leech Life
        { 74,  33, 88,  0,   0 },  // Geodude     Tackle / Rock Throw
        { 92,  93, 45,  0,   0 },  // Gastly      Confusion / Growl
        { 54,  55, 10,  0,   0 },  // Psyduck     Water Gun / Scratch
        { 66,   2, 33,  0,   0 },  // Machop      Karate Chop / Tackle
        { 60, 145, 33,  0,   0 },  // Poliwag     Bubble / Tackle
        { 23,  33, 45,  44,  0 },  // Ekans       Tackle / Growl / Bite
        { 27,  10, 33,  88,  0 },  // Sandshrew   Scratch / Tackle / Rock Throw
    };
    const int POOL_N = (int)(sizeof(CPU_POOL) / sizeof(CPU_POOL[0]));
    Gen1Party cpu = {};
    int cpuCount = (int)party_.count;
    if (cpuCount > 6) cpuCount = 6;
    cpu.count = (uint8_t)cpuCount;
    int prevIdx = -1;
    for (int i = 0; i < cpuCount; i++) {
        int idx = rand() % POOL_N;
        if (idx == prevIdx) idx = (idx + 1) % POOL_N;   // avoid back-to-back dupes
        prevIdx = idx;
        uint8_t mon = CPU_POOL[idx][0];                 // national dex
        // The engine reads Gen1Pokemon.species as the INTERNAL Gen-1 code, not
        // national dex — pass the raw dex and it resolves a blank/no-stat
        // "ghost" that yields no XP.  Convert exactly like the gym/pentest paths.
        uint8_t internal = dexToInternal[mon];
        int lv = (int)avgLv + (rand() % 5) - 2;         // small per-mon variance
        if (lv < 2)   lv = 2;
        if (lv > 100) lv = 100;
        cpu.species[i]       = internal;
        cpu.mons[i].species  = internal;
        cpu.mons[i].level    = (uint8_t)lv;
        cpu.mons[i].boxLevel = (uint8_t)lv;
        memcpy(cpu.mons[i].moves, CPU_POOL[idx] + 1, 4);
    }

    uint32_t seed = (uint32_t)(millis() ^ (uint32_t)(uintptr_t)this);
    engine_.start(party_, cpu, seed, battleGen_);
    resetBattleParticipants();
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
    engine_.start(party_, wild, seed, battleGen_);
    resetBattleParticipants();
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

// ── Independent journeys: active-mon <-> live-set mirroring ───────────────────
// The live pentest* globals are the ACTIVE mon's working set. These two helpers
// move that set to/from the active mon's persistent "home": the starter fields
// (id 0) or the matching BreedMon's trip* fields (a Bill's PC catch/bred mon).
static inline uint16_t clamp16(uint32_t v) { return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v; }

void TerminalUI::pentestStoreLiveToActive() {
    if (pentestActiveId_ == 0) {
        starterLevel_  = pentestLevel_;
        starterXp_     = pentestXp_;
        starterGym_    = pentestGymBeaten_;
        starterWins_   = clamp16(pentestWins_);
        starterLosses_ = clamp16(pentestLosses_);
    } else if (breeding::BreedMon *m = breedApp_.findById(pentestActiveId_)) {
        m->tripLevel     = pentestLevel_;
        m->tripXp        = pentestXp_;
        m->tripGymBeaten = pentestGymBeaten_;
        m->tripWins      = clamp16(pentestWins_);
        m->tripLosses    = clamp16(pentestLosses_);
    }
    // If the active id is a box mon we can't find (box not loaded yet), do
    // nothing — the live set will be re-synced once the box is loaded.
}

void TerminalUI::pentestLoadActiveToLive() {
    if (pentestActiveId_ == 0) {
        pentestActiveTritan_ = false;     // default Pikachu — no tritan gene
        if (starterLevel_ == 0) {         // uninitialized → fresh L5 starter run
            starterLevel_ = 5; starterXp_ = 0; starterGym_ = 0;
            starterWins_  = 0; starterLosses_ = 0;
        }
        pentestLevel_     = starterLevel_;
        pentestXp_        = starterXp_;
        pentestGymBeaten_ = (uint8_t)starterGym_;
        pentestWins_      = starterWins_;
        pentestLosses_    = starterLosses_;
    } else if (breeding::BreedMon *m = breedApp_.findById(pentestActiveId_)) {
        // tritan isn't in the save file — derive it from the (now-loaded) box mon.
        pentestActiveTritan_ = breeding::isTritan(m->geno);
        if (m->tripLevel == 0) {          // first activation → seed from catch level
            m->tripLevel     = (m->level < 5) ? 5 : m->level;
            m->tripXp        = 0;
            m->tripGymBeaten = 0;
            m->tripWins      = 0;
            m->tripLosses    = 0;
        }
        pentestLevel_     = m->tripLevel;
        pentestXp_        = m->tripXp;
        pentestGymBeaten_ = (uint8_t)m->tripGymBeaten;
        pentestWins_      = m->tripWins;
        pentestLosses_    = m->tripLosses;
    }
    // Unknown box id (box not loaded) → leave the live set as-is.
}

// Swap the active battler to `newId`, banking the current mon's trip and loading
// the new mon's. Called from Bill's PC "Set Active" and once at startup to sync
// the live set to the persisted active mon.
void TerminalUI::pentestActivateMon(uint32_t newId) {
    pentestStoreLiveToActive();                 // bank the outgoing mon's progress
    if (newId == 0) {
        pentestActiveId_      = 0;
        pentestActiveDex_     = 0;
        pentestActiveVariant_ = 0;              // default Regular Pikachu skin
        pentestActiveTritan_  = false;
    } else if (breeding::BreedMon *m = breedApp_.findById(newId)) {
        pentestActiveId_      = m->id;
        pentestActiveDex_     = m->dex;
        // Preserve Feature-1's coloration lock: skin follows the individual.
        pentestActiveVariant_ = (uint8_t)(int)breeding::skinOf(m->geno);
        pentestActiveTritan_  = breeding::isTritan(m->geno);   // tritan follows too
    } else {
        return;                                 // unknown id — keep current mon
    }
    pentestLoadActiveToLive();                  // bring the incoming mon's progress live
}

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
        // Used-SSID mask is optional too (saves older than this feature).
        fread(&pentestUsedSsid_, sizeof(pentestUsedSsid_), 1, f);
        // W/L tally is optional as well (older saves won't have it).
        fread(&pentestWins_,   sizeof(pentestWins_),   1, f);
        fread(&pentestLosses_, sizeof(pentestLosses_), 1, f);
        // Gym-beaten bitset (optional — older saves predate gym progression).
        fread(&pentestGymBeaten_, sizeof(pentestGymBeaten_), 1, f);
        // Active battler dex (optional — 0/absent = default Pikachu).
        pentestActiveDex_ = 0;
        fread(&pentestActiveDex_, sizeof(pentestActiveDex_), 1, f);
        // Active battler's caught/bred colour + individual id (optional tail —
        // older saves predate the coloration lock; default 0 = Regular Pikachu).
        pentestActiveVariant_ = 0;
        pentestActiveId_      = 0;
        fread(&pentestActiveVariant_, sizeof(pentestActiveVariant_), 1, f);
        fread(&pentestActiveId_,      sizeof(pentestActiveId_),      1, f);
        // ── Independent journeys: the DEFAULT Pikachu's own trip (optional tail).
        // Older saves predate this block; detect its absence and MIGRATE by
        // seeding the starter's trip from the (formerly global) progress above,
        // so the player's existing Pikachu journey becomes the starter's.
        starterLevel_ = 0;   // sentinel: 0 => not present in file
        bool haveStarter =
            fread(&starterLevel_,  sizeof(starterLevel_),  1, f) == 1 &&
            fread(&starterXp_,     sizeof(starterXp_),     1, f) == 1 &&
            fread(&starterGym_,    sizeof(starterGym_),    1, f) == 1 &&
            fread(&starterWins_,   sizeof(starterWins_),   1, f) == 1 &&
            fread(&starterLosses_, sizeof(starterLosses_), 1, f) == 1;
        if (!haveStarter || starterLevel_ == 0) {
            starterLevel_  = pentestLevel_;
            starterXp_     = pentestXp_;
            starterGym_    = pentestGymBeaten_;
            starterWins_   = clamp16(pentestWins_);
            starterLosses_ = clamp16(pentestLosses_);
        }
    }
    fclose(f);
}

void TerminalUI::pentestSaveProgress() {
    // Independent journeys: bank the live set into the ACTIVE mon's home first,
    // so the on-disk pentest.dat starter block / box trip record stay in sync
    // with the globals we're about to write (level-ups, badges, W/L all flow
    // through here). Persist the active box mon's trip too — but only once the
    // box is loaded, or we'd rewrite an empty roster over the real box file.
    pentestStoreLiveToActive();
    if (breedLoaded_ && pentestActiveId_ != 0) pentestRewriteBox();

    FILE *f = fopen(pentestSavePath(), "wb");
    if (!f) return;
    uint32_t magic = PENTEST_SAVE_MAGIC;
    fwrite(&magic,          sizeof(magic),          1, f);
    fwrite(&pentestLevel_,  sizeof(pentestLevel_),  1, f);
    fwrite(&pentestXp_,     sizeof(pentestXp_),     1, f);
    fwrite(pentestDex_,     sizeof(pentestDex_),    1, f);
    fwrite(pentestBeaten_,  sizeof(pentestBeaten_), 1, f);
    fwrite(&pentestUsedSsid_, sizeof(pentestUsedSsid_), 1, f);
    fwrite(&pentestWins_,   sizeof(pentestWins_),   1, f);
    fwrite(&pentestLosses_, sizeof(pentestLosses_), 1, f);
    fwrite(&pentestGymBeaten_, sizeof(pentestGymBeaten_), 1, f);
    fwrite(&pentestActiveDex_, sizeof(pentestActiveDex_), 1, f);
    fwrite(&pentestActiveVariant_, sizeof(pentestActiveVariant_), 1, f);
    fwrite(&pentestActiveId_,      sizeof(pentestActiveId_),      1, f);
    // Independent journeys: the default Pikachu's own trip (optional tail).
    fwrite(&starterLevel_,  sizeof(starterLevel_),  1, f);
    fwrite(&starterXp_,     sizeof(starterXp_),     1, f);
    fwrite(&starterGym_,    sizeof(starterGym_),    1, f);
    fwrite(&starterWins_,   sizeof(starterWins_),   1, f);
    fwrite(&starterLosses_, sizeof(starterLosses_), 1, f);
    fclose(f);
}

// Canonical Gen-1 Pikachu learnset (level -> move id), sorted by level.  As the
// pentest Pikachu climbs, its active 4 moves are the most-recently-learned ones
// (oldest drops off), and at L30 it evolves into Raichu — mirroring the T-Deck
// Pentest Pikachu.  Raichu learns nothing more after evolving in Gen 1.
namespace {
struct PikaLearn { uint8_t level; uint8_t move; };
static const PikaLearn kPikaLearnset[] = {
    { 1,  84},  // Thunder Shock
    { 1,  45},  // Growl
    { 6,  39},  // Tail Whip
    { 8,  86},  // Thunder Wave
    {11,  98},  // Quick Attack
    {15, 104},  // Double Team
    {20,  21},  // Slam
    {26,  85},  // Thunderbolt
    {33,  97},  // Agility
    {41,  87},  // Thunder
};
static constexpr uint8_t PIKACHU_EVOLVE_LEVEL = 30;  // Pikachu -> Raichu

// Fill out[4] with the 4 most-recently-learned moves at `level` (oldest drops
// when a 5th is learned), backfilled with Thunder Shock so there's always an
// attacking move in slot 0.
static void pikaMovesForLevel(uint8_t level, uint8_t out[4]) {
    uint8_t window[4] = {0, 0, 0, 0};
    int n = 0;
    for (const PikaLearn &e : kPikaLearnset) {
        if (e.level > level) break;             // table is level-sorted
        if (n < 4) {
            window[n++] = e.move;
        } else {                                // slide window, drop oldest
            window[0] = window[1];
            window[1] = window[2];
            window[2] = window[3];
            window[3] = e.move;
        }
    }
    for (int i = 0; i < 4; i++) out[i] = window[i] ? window[i] : 84;
}
}  // namespace

// Map a wpa_cli "flags" field to a real-world WiFi weakness.  Returns false
// (secure) or true with `out` set to a flavour description of the weakness.
// Priority order matches the firmware's BeaconClassifier tier table:
//   WEP  = RARE (barely seen in the wild anymore)
//   WPS  = UNCOMMON (still shipped on many home routers)
//   TKIP = COMMON  (WPA1/WPA2-TKIP, RC4-based)
//   Open = COMMON  (no encryption at all — HaleHound addition)
static bool pentestVulnForFlags(const char *flags, std::string &out) {
    std::string f = flags ? flags : "";
    auto has = [&](const char *s) { return f.find(s) != std::string::npos; };
    // Every network in range is a wild encounter — the "vulnerability" is just
    // flavour text on the mon you meet there. Pick the most interesting real
    // attack the flags allow, most-severe first.
    if (has("WEP"))  { out = "WEP IV collision / key reuse";        return true; }
    if (has("WPS"))  { out = "WPS PIN attack (Pixie Dust)";         return true; }
    if (has("TKIP")) { out = "WPA-TKIP (BEAST/RC4 brute)";          return true; }
    if (!has("WPA") && !has("RSN")) {            // no WPA/WPA2/WPA3 => open AP
        out = "Open network - cleartext sniffing";
        return true;
    }
    if (has("SAE"))  { out = "WPA3 SAE downgrade (Dragonblood)";    return true; }
    out = "WPA2 PMKID hashcat -m 16800";         // WPA2-PSK — still an encounter
    return true;
}

// Scan nearby WiFi via wpa_cli and refresh pentestNets_ with the VULNERABLE
// networks currently in range that haven't been cracked yet this session.
// Sets pentestScanAvailable_ false when no scanner is reachable (off-device or
// no WiFi) so the caller can fall back to the fictional demo list.
// HaleHound additions vs original: TKIP networks, hidden SSIDs (SSID IE len=0
// with encryption present), and BLE advertisements via pentestBleScan().
void TerminalUI::pentestScanNetworks() {
    pentestNets_.clear();
    pentestNetsSeen_      = 0;
    pentestScanAvailable_ = false;
#ifndef __APPLE__
    // Kick a THROTTLED active scan so the cache holds every AP in range — not
    // just the one we're associated to. wpa_supplicant does NOT background-scan
    // while associated, so without this the game only ever sees the connected
    // network. The scan is async (results land within a couple seconds and are
    // read on the next call) and throttled to ~10 s so it barely disturbs the
    // association. This is a warwalking game — finding networks is the point.
    uint64_t now = millis();
    if (now - pentestLastScanKickMs_ > 10000) {
        pentestLastScanKickMs_ = now;
        int rc = system("sudo -n wpa_cli -i wlan0 scan >/dev/null 2>&1");
        (void)rc;
    }
    // Read the supplicant's cached scan results (freshly repopulated above).
    FILE *f = popen("sudo -n wpa_cli -i wlan0 scan_results 2>/dev/null", "r");
    if (!f) { pentestBleScan(); return; }
    char buf[256];
    bool header = true;
    while (fgets(buf, sizeof(buf), f)) {
        if (header) { header = false; pentestScanAvailable_ = true; continue; }
        char *save  = nullptr;
        char *bssid = strtok_r(buf,     "\t", &save);            // bssid (kept for hidden SSIDs)
        (void)strtok_r(nullptr, "\t", &save);                    // frequency
        (void)strtok_r(nullptr, "\t", &save);                    // signal
        char *flags = strtok_r(nullptr, "\t", &save);            // flags
        char *ssid  = strtok_r(nullptr, "\t\r\n", &save);        // ssid (may be empty/hidden)
        pentestNetsSeen_++;
        std::string vuln;
        if (!pentestVulnForFlags(flags ? flags : "", vuln)) continue;  // secure — skip
        // Build display name: real SSID or "(hidden-XX:XX:XX)" from the BSSID
        // tail (last 8 chars) so hidden APs are distinguishable in the log.
        // Skip open hidden APs — no SSID + no encryption is just noise.
        char displaySsid[40];
        if (!ssid || !*ssid) {
            if (vuln.find("Open") != std::string::npos) continue;
            const char *b = bssid ? bssid : "";
            int blen = (int)strlen(b);
            snprintf(displaySsid, sizeof(displaySsid), "(hidden-%s)",
                     blen >= 8 ? b + blen - 8 : "??:??:??");
        } else {
            snprintf(displaySsid, sizeof(displaySsid), "%.39s", ssid);
        }
        // Only de-dup WITHIN the current in-range list (the same AP can show up
        // twice per scan). Networks are NOT retired across encounters anymore —
        // every nearby network stays a repeatable catch attempt.
        bool skip = false;
        for (auto &n : pentestNets_) if (n.ssid == displaySsid) { skip = true; break; }
        if (skip) continue;
        pentestNets_.push_back({ displaySsid, vuln, bssid ? bssid : "" });
    }
    pclose(f);
    // Supplement WiFi results with BLE advertisement detections.  Non-fatal
    // if Bluetooth is absent or the sudoers entry for hcitool isn't set up.
    pentestBleScan();
#endif
}

// BLE advertisement scan using hcidump (BlueZ).  Detects:
//   - Flood: >100 raw HCI adv-report lines in 3 seconds (Flipper/spammer)
//   - Apple: manufacturer-specific data with company ID 0x004C (little-endian
//     bytes "4c 00" in the hcidump hex dump)
// Appends any detections directly to pentestNets_.  Requires a sudoers
// NOPASSWD entry for hcidump (same pattern as the wpa_cli entries).
void TerminalUI::pentestBleScan() {
#ifndef __APPLE__
    static constexpr int BLE_FLOOD_THRESH  = 100; // adv-report lines in 3s
    static constexpr int APPLE_PROX_THRESH =   5; // hits to flag UNCOMMON-equiv
    FILE *f = popen("sudo -n timeout 3 hcidump -i hci0 -X 2>/dev/null", "r");
    if (!f) return;
    int advLines = 0, appleHits = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '>') advLines++;            // each HCI event starts with ">"
        // Apple company ID 0x004C in little-endian MFR data = "4c 00"
        if (strstr(line, "4c 00") || strstr(line, "4C 00")) appleHits++;
    }
    pclose(f);
    // BLE detections have no BSSID — leave it empty so pentestPickTarget()
    // synthesises a stable seed from the label instead.
    if (advLines >= BLE_FLOOD_THRESH)
        pentestNets_.push_back({ "BLE_Flood",
            "BLE advertisement flood (Flipper/spammer)", "" });
    if (appleHits >= APPLE_PROX_THRESH)
        pentestNets_.push_back({ "BLE_AppleProx",
            "Apple BLE proximity spam (AirDrop/Handoff)", "" });
    else if (appleHits >= 1)
        pentestNets_.push_back({ "BLE_AppleiBeacon",
            "Apple BLE adv detected (iBeacon/device nearby)", "" });
#endif
}

// ── SIGINT tools ──────────────────────────────────────────────────────────────
// AP Scanner, Probe Sniffer, Deauth Log, Captures — reachable from both the
// Pentest Pikachu status overlay (press A) and the normal mmterm SYSTEM tab.

const char *TerminalUI::sigintCaptureDir() {
#ifdef __APPLE__
    return "/tmp/monstermesh/captures";
#else
    return "/var/lib/monstermesh/captures";
#endif
}

// Tier thresholds shared by sigintLoadAllNets and renderSigintScanner.
// Returns: 3=RARE(WEP) 2=UNCOMMON(WPS) 1=COMMON(open/TKIP/hidden) 0=secure.
static uint8_t sigintTierForFlags(const char *flags) {
    if (!flags || !*flags) return 0;
    if (strstr(flags, "WEP"))  return 3;
    if (strstr(flags, "WPS"))  return 2;
    if (strstr(flags, "TKIP")) return 1;
    if (!strstr(flags, "WPA") && !strstr(flags, "RSN")) return 1;  // open
    return 0;
}

static const char *sigintTierTag(uint8_t tier) {
    switch (tier) {
        case 3: return "RARE";
        case 2: return "UNCO";
        case 1: return "OPEN";
        default: return "    ";
    }
}

// Load ALL nearby APs from wpa_cli scan_results (not just vulnerable ones).
// Sorted strongest RSSI first so juicy targets float to the top.
void TerminalUI::sigintLoadAllNets() {
    sigintNets_.clear();
#ifndef __APPLE__
    FILE *f = popen("sudo -n wpa_cli -i wlan0 scan_results 2>/dev/null", "r");
    if (!f) return;
    char buf[256];
    bool header = true;
    while (fgets(buf, sizeof(buf), f)) {
        if (header) { header = false; continue; }
        char *save  = nullptr;
        char *bssid = strtok_r(buf,     "\t", &save);
        (void)strtok_r(nullptr, "\t", &save);            // freq
        char *rssi  = strtok_r(nullptr, "\t", &save);
        char *flags = strtok_r(nullptr, "\t", &save);
        char *ssid  = strtok_r(nullptr, "\t\r\n", &save);
        if (!bssid) continue;
        SigintNet n = {};
        snprintf(n.bssid, sizeof(n.bssid), "%.17s", bssid);
        n.rssi = rssi ? atoi(rssi) : -100;
        snprintf(n.flags, sizeof(n.flags), "%.63s", flags ? flags : "");
        if (ssid && *ssid)
            snprintf(n.ssid, sizeof(n.ssid), "%.39s", ssid);
        else {
            int blen = (int)strlen(n.bssid);
            snprintf(n.ssid, sizeof(n.ssid), "(hidden-%s)",
                     blen >= 8 ? n.bssid + blen - 8 : "??:??:??");
        }
        n.tier = sigintTierForFlags(n.flags);
        sigintNets_.push_back(n);
    }
    pclose(f);
    // Sort: vulnerable first (tier desc), then by RSSI desc.
    std::sort(sigintNets_.begin(), sigintNets_.end(),
              [](const SigintNet &a, const SigintNet &b) {
                  if (a.tier != b.tier) return a.tier > b.tier;
                  return a.rssi > b.rssi;
              });
#endif
}

// Run a 5-second passive probe-request sniff via tcpdump.  Extracts unique
// source MACs + target SSIDs (from parenthesised SSID in tcpdump output).
// Blocks for up to 5 seconds — acceptable since the user just pressed Rescan.
void TerminalUI::sigintLoadProbes() {
    sigintProbes_.clear();
#ifndef __APPLE__
    FILE *f = popen(
        "sudo -n timeout 5 tcpdump -i wlan0 -l -e type mgt subtype probe-req 2>/dev/null",
        "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // tcpdump -e probe-req line format (varies by version):
        //   "HH:MM:SS.us  SRCMAC (oui X) > BCAST, Probe Request (SSID)"
        // Extract source MAC: first 17-char token after timestamp.
        char *src = nullptr;
        // Skip timestamp (field ends at first space after the dot in usecs)
        char *p = strchr(line, ' ');
        if (p) p = p + 1;
        if (p && strlen(p) >= 17) src = p;
        if (!src) continue;
        char mac[18] = {};
        int colons = 0;
        for (int i = 0; i < 17 && src[i]; i++) {
            if (src[i] == ':') colons++;
            mac[i] = src[i];
        }
        mac[17] = '\0';
        if (colons < 5) continue;  // not a valid MAC

        // Extract SSID from "(SSID)" at end of line; "()" = broadcast probe.
        char ssid[40] = {};
        const char *lp = strrchr(line, '(');
        const char *rp = strrchr(line, ')');
        if (lp && rp && rp > lp + 1) {
            int len = (int)(rp - lp - 1);
            if (len >= (int)sizeof(ssid)) len = (int)sizeof(ssid) - 1;
            strncpy(ssid, lp + 1, len);
            ssid[len] = '\0';
        }

        // Merge duplicate (mac, ssid) pairs.
        bool found = false;
        for (auto &pr : sigintProbes_) {
            if (strcmp(pr.mac, mac) == 0 && strcmp(pr.ssid, ssid) == 0) {
                pr.count++;
                found = true;
                break;
            }
        }
        if (!found) {
            SigintProbe pr = {};
            strncpy(pr.mac,  mac,  sizeof(pr.mac)  - 1);
            strncpy(pr.ssid, ssid, sizeof(pr.ssid) - 1);
            pr.count = 1;
            sigintProbes_.push_back(pr);
        }
    }
    pclose(f);
#endif
}

// Pull deauth / disconnect events from wpa_supplicant's ring log.
// On Pi this catches when the AP deauth'd US — useful for detecting
// deauth-attack campaigns targeting our own connection.
void TerminalUI::sigintLoadDeauths() {
    sigintDeauths_.clear();
#ifndef __APPLE__
    // wpa_cli log outputs the supplicant ring buffer (most recent entries).
    // Filter for deauth/disassoc/disconnect keywords.
    FILE *f = popen(
        "sudo -n wpa_cli -i wlan0 log 2>/dev/null | "
        "grep -i 'deauth\\|disassoc\\|disconnect\\|Disconnected\\|CTRL-EVENT'",
        "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        int n = (int)strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;
        SigintDeauth d = {};
        strncpy(d.line, line, sizeof(d.line) - 1);
        sigintDeauths_.push_back(d);
        if (sigintDeauths_.size() >= 80) break;  // cap to avoid unbounded growth
    }
    pclose(f);
#endif
}

// List .pcap files in the capture directory.
void TerminalUI::sigintLoadCapFiles() {
    sigintCaps_.clear();
    const char *dir = sigintCaptureDir();
#ifndef __APPLE__
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strstr(ent->d_name, ".pcap") == nullptr) continue;
        SigintCap cap = {};
        snprintf(cap.name, sizeof(cap.name), "%.79s", ent->d_name);
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st = {};
        if (stat(path, &st) == 0) cap.sizeBytes = (long)st.st_size;
        sigintCaps_.push_back(cap);
    }
    closedir(d);
    // Sort alphabetically (newest timestamp names sort last).
    std::sort(sigintCaps_.begin(), sigintCaps_.end(),
              [](const SigintCap &a, const SigintCap &b) {
                  return strcmp(a.name, b.name) < 0;
              });
#else
    (void)dir;
#endif
}

// Start a background tcpdump capturing EAPOL frames (which includes the
// 4-way handshake WPA key exchange + PMKID in Message 1) to a .pcap file.
// hcxtools / hashcat can extract the PMKID and handshake from the pcap
// offline: hcxpcapngtool -o out.22000 cap_SSID.pcap
void TerminalUI::sigintStartCapture(const SigintNet &net) {
    const char *dir = sigintCaptureDir();
    // Sanitise SSID for use as a filename component (keep alnum + dash/underscore).
    char safeSsid[40] = {};
    const char *s = net.ssid;
    int j = 0;
    for (int i = 0; s[i] && j < 30; i++) {
        char c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            safeSsid[j++] = c;
        else if (c == ' ')
            safeSsid[j++] = '_';
    }
    if (j == 0) snprintf(safeSsid, sizeof(safeSsid), "unknown");

    char cmd[512];
    // mkdir -p first; tcpdump -c 500 stops after 500 EAPOL frames (~a few
    // complete handshakes), then the background job exits naturally.
    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s && "
             "sudo -n tcpdump -i wlan0 -w %s/cap_%.30s_$(date +%%s).pcap "
             "ether proto 0x888e -c 500 2>/dev/null &",
             dir, dir, safeSsid);
    system(cmd);

    sigintCapActive_ = true;
    snprintf(sigintCapSSID_, sizeof(sigintCapSSID_), "%.39s", net.ssid);
}

// ── Render: AP Scanner ────────────────────────────────────────────────────────
void TerminalUI::renderSigintScanner() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);
    int infoCols = getmaxx(winInfo_);

    // Header
    char hdr[80];
    if (sigintCapActive_)
        snprintf(hdr, sizeof(hdr), " AP SCANNER [%d APs] [cap: %.16s]",
                 (int)sigintNets_.size(), sigintCapSSID_);
    else
        snprintf(hdr, sizeof(hdr), " AP SCANNER [%d APs]  [A]=Capture  [B]=Back",
                 (int)sigintNets_.size());
    wattron(winInfo_, A_REVERSE | COLOR_PAIR(4));
    mvwprintw(winInfo_, 0, 0, "%-*.*s", infoCols, infoCols, hdr);
    wattroff(winInfo_, A_REVERSE | COLOR_PAIR(4));

    if (sigintNets_.empty()) {
        mvwprintw(winInfo_, 2, 2, "No APs found (wpa_cli unavailable or no scan results)");
        mvwprintw(winInfo_, 3, 2, "Run:  sudo wpa_cli -i wlan0 scan");
        mvwprintw(winMenu_, 1, 1, "[A]=Rescan  [B]=Back");
        return;
    }

    // Column header
    wattron(winInfo_, A_BOLD);
    mvwprintw(winInfo_, 1, 1, "%-4s %4s  %-18s %s",
              "TIER", "RSSI", "BSSID", "SSID");
    wattroff(winInfo_, A_BOLD);
    mvwhline(winInfo_, 2, 1, ACS_HLINE, infoCols - 2);

    int listRows = infoRows - 3;
    int total    = (int)sigintNets_.size();
    if (sigintNetSel_ < sigintNetScroll_) sigintNetScroll_ = sigintNetSel_;
    if (sigintNetSel_ >= sigintNetScroll_ + listRows)
        sigintNetScroll_ = sigintNetSel_ - listRows + 1;

    for (int i = 0; i < listRows; i++) {
        int idx = sigintNetScroll_ + i;
        if (idx >= total) break;
        const SigintNet &n = sigintNets_[idx];
        bool sel = (idx == sigintNetSel_);

        // Pick color based on tier
        int cp = 5;  // default (secure)
        if (n.tier == 3) cp = 3;        // RARE  → pair 3 (red-ish / A_REVERSE)
        else if (n.tier == 2) cp = 2;   // UNCO  → pair 2 (yellow-ish)
        else if (n.tier == 1) cp = 1;   // OPEN  → pair 1 (green)

        if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(cp));
        else     wattron(winInfo_, COLOR_PAIR(cp));

        mvwprintw(winInfo_, 3 + i, 1, "%-4s %4d %-18s %s",
                  sigintTierTag(n.tier), n.rssi, n.bssid,
                  n.ssid[0] ? n.ssid : "(hidden)");

        if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(cp));
        else     wattroff(winInfo_, COLOR_PAIR(cp));
    }

    // Scroll hint
    if (total > listRows) {
        mvwprintw(winInfo_, infoRows - 1, infoCols - 12, "[%d/%d]",
                  sigintNetSel_ + 1, total);
    }

    // Menu bar shows flags of selected network
    if (sigintNetSel_ < total) {
        const SigintNet &sel = sigintNets_[sigintNetSel_];
        mvwprintw(winMenu_, 1, 1, "Flags: %-40s", sel.flags);
    }
}

void TerminalUI::sigintScannerButton(const ButtonEvent &ev) {
    int total = (int)sigintNets_.size();
    switch (ev.button) {
        case GpiButton::UP:
            if (sigintNetSel_ > 0) sigintNetSel_--;
            break;
        case GpiButton::DOWN:
            if (sigintNetSel_ < total - 1) sigintNetSel_++;
            break;
        case GpiButton::A:
            if (total > 0)
                sigintStartCapture(sigintNets_[sigintNetSel_]);
            else {
                sigintLoadAllNets();
                sigintNetSel_ = 0;
            }
            break;
        case GpiButton::B:
            screen_ = pentestMode_ ? Screen::BATTLE : Screen::MENU;
            break;
        case GpiButton::SELECT:
            // Rescan on SELECT
            sigintLoadAllNets();
            sigintNetSel_ = 0;
            break;
        default: break;
    }
}

// ── Render: Probe Sniffer ─────────────────────────────────────────────────────
void TerminalUI::renderSigintProbes() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);
    int infoCols = getmaxx(winInfo_);

    wattron(winInfo_, A_REVERSE | COLOR_PAIR(4));
    char hdr[80];
    snprintf(hdr, sizeof(hdr), " PROBE SNIFFER [%d devices]  [A]=Rescan  [B]=Back",
             (int)sigintProbes_.size());
    mvwprintw(winInfo_, 0, 0, "%-*.*s", infoCols, infoCols, hdr);
    wattroff(winInfo_, A_REVERSE | COLOR_PAIR(4));

    if (sigintProbes_.empty()) {
        mvwprintw(winInfo_, 2, 2, "No probe requests captured.");
        mvwprintw(winInfo_, 3, 2, "[A] to run a 5-second scan.");
        return;
    }

    wattron(winInfo_, A_BOLD);
    mvwprintw(winInfo_, 1, 1, "%-3s %-17s  %s", "CNT", "SOURCE MAC", "PROBED SSID");
    wattroff(winInfo_, A_BOLD);
    mvwhline(winInfo_, 2, 1, ACS_HLINE, infoCols - 2);

    int listRows = infoRows - 3;
    int total    = (int)sigintProbes_.size();
    if (sigintProbeSel_ < sigintProbeScroll_) sigintProbeScroll_ = sigintProbeSel_;
    if (sigintProbeSel_ >= sigintProbeScroll_ + listRows)
        sigintProbeScroll_ = sigintProbeSel_ - listRows + 1;

    for (int i = 0; i < listRows; i++) {
        int idx = sigintProbeScroll_ + i;
        if (idx >= total) break;
        const SigintProbe &p = sigintProbes_[idx];
        bool sel = (idx == sigintProbeSel_);
        if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
        const char *ssidLabel = p.ssid[0] ? p.ssid : "<broadcast>";
        mvwprintw(winInfo_, 3 + i, 1, "%3d %-17s  %s",
                  p.count, p.mac, ssidLabel);
        if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
    }
    if (total > listRows)
        mvwprintw(winInfo_, infoRows - 1, infoCols - 12, "[%d/%d]",
                  sigintProbeSel_ + 1, total);
}

void TerminalUI::sigintProbesButton(const ButtonEvent &ev) {
    int total = (int)sigintProbes_.size();
    switch (ev.button) {
        case GpiButton::UP:   if (sigintProbeSel_ > 0) sigintProbeSel_--; break;
        case GpiButton::DOWN: if (sigintProbeSel_ < total - 1) sigintProbeSel_++; break;
        case GpiButton::A:
        case GpiButton::SELECT:
            sigintLoadProbes();
            sigintProbeSel_ = sigintProbeScroll_ = 0;
            break;
        case GpiButton::B:
            screen_ = pentestMode_ ? Screen::BATTLE : Screen::MENU;
            break;
        default: break;
    }
}

// ── Render: Deauth Log ────────────────────────────────────────────────────────
void TerminalUI::renderSigintDeauths() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);
    int infoCols = getmaxx(winInfo_);

    wattron(winInfo_, A_REVERSE | COLOR_PAIR(4));
    char hdr[80];
    snprintf(hdr, sizeof(hdr), " DEAUTH LOG [%d events]  [A]=Refresh  [B]=Back",
             (int)sigintDeauths_.size());
    mvwprintw(winInfo_, 0, 0, "%-*.*s", infoCols, infoCols, hdr);
    wattroff(winInfo_, A_REVERSE | COLOR_PAIR(4));

    if (sigintDeauths_.empty()) {
        mvwprintw(winInfo_, 2, 2, "No deauth / disconnect events in wpa_supplicant log.");
        mvwprintw(winInfo_, 3, 2, "Events appear here when deauth attacks are detected.");
        return;
    }

    int listRows = infoRows - 1;
    int total    = (int)sigintDeauths_.size();
    if (sigintDeauthSel_ < sigintDeauthScroll_) sigintDeauthScroll_ = sigintDeauthSel_;
    if (sigintDeauthSel_ >= sigintDeauthScroll_ + listRows)
        sigintDeauthScroll_ = sigintDeauthSel_ - listRows + 1;

    for (int i = 0; i < listRows; i++) {
        int idx = sigintDeauthScroll_ + i;
        if (idx >= total) break;
        bool sel = (idx == sigintDeauthSel_);
        if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
        // Truncate to window width
        char truncated[128];
        snprintf(truncated, sizeof(truncated), "%-*.*s",
                 infoCols - 2, infoCols - 2, sigintDeauths_[idx].line);
        mvwprintw(winInfo_, 1 + i, 1, "%s", truncated);
        if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
    }
    if (total > listRows)
        mvwprintw(winInfo_, infoRows - 1, infoCols - 12, "[%d/%d]",
                  sigintDeauthSel_ + 1, total);
}

void TerminalUI::sigintDeauthsButton(const ButtonEvent &ev) {
    int total = (int)sigintDeauths_.size();
    switch (ev.button) {
        case GpiButton::UP:   if (sigintDeauthSel_ > 0) sigintDeauthSel_--; break;
        case GpiButton::DOWN: if (sigintDeauthSel_ < total - 1) sigintDeauthSel_++; break;
        case GpiButton::A:
        case GpiButton::SELECT:
            sigintLoadDeauths();
            sigintDeauthSel_ = sigintDeauthScroll_ = 0;
            break;
        case GpiButton::B:
            screen_ = pentestMode_ ? Screen::BATTLE : Screen::MENU;
            break;
        default: break;
    }
}

// ── Render: Captures ─────────────────────────────────────────────────────────
void TerminalUI::renderSigintCaptures() {
    werase(winInfo_);
    werase(winMenu_);

    int infoRows = getmaxy(winInfo_);
    int infoCols = getmaxx(winInfo_);

    wattron(winInfo_, A_REVERSE | COLOR_PAIR(4));
    char hdr[80];
    snprintf(hdr, sizeof(hdr), " CAPTURES  [%s]", sigintCaptureDir());
    mvwprintw(winInfo_, 0, 0, "%-*.*s", infoCols, infoCols, hdr);
    wattroff(winInfo_, A_REVERSE | COLOR_PAIR(4));

    if (sigintCaps_.empty()) {
        mvwprintw(winInfo_, 2, 2, "No captures yet.");
        mvwprintw(winInfo_, 3, 2, "Go to AP Scanner and press [A] on a target.");
        mvwprintw(winInfo_, 4, 2, "Offline crack with:");
        mvwprintw(winInfo_, 5, 2, "  hcxpcapngtool -o out.22000 cap_SSID.pcap");
        mvwprintw(winInfo_, 6, 2, "  hashcat -m 22000 out.22000 wordlist.txt");
        mvwprintw(winMenu_, 1, 1, "[A]=Refresh  [B]=Back");
        return;
    }

    wattron(winInfo_, A_BOLD);
    mvwprintw(winInfo_, 1, 1, "%-50s  %s", "FILE", "SIZE");
    wattroff(winInfo_, A_BOLD);
    mvwhline(winInfo_, 2, 1, ACS_HLINE, infoCols - 2);

    int listRows = infoRows - 3;
    int total    = (int)sigintCaps_.size();
    if (sigintCapSel_ < sigintCapScroll_) sigintCapScroll_ = sigintCapSel_;
    if (sigintCapSel_ >= sigintCapScroll_ + listRows)
        sigintCapScroll_ = sigintCapSel_ - listRows + 1;

    for (int i = 0; i < listRows; i++) {
        int idx = sigintCapScroll_ + i;
        if (idx >= total) break;
        const SigintCap &c = sigintCaps_[idx];
        bool sel = (idx == sigintCapSel_);
        if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
        char sizeStr[16];
        if (c.sizeBytes >= 1024 * 1024)
            snprintf(sizeStr, sizeof(sizeStr), "%.1f MB", c.sizeBytes / 1048576.0);
        else if (c.sizeBytes >= 1024)
            snprintf(sizeStr, sizeof(sizeStr), "%.1f KB", c.sizeBytes / 1024.0);
        else
            snprintf(sizeStr, sizeof(sizeStr), "%ld B", c.sizeBytes);
        mvwprintw(winInfo_, 3 + i, 1, "%-50.50s  %s", c.name, sizeStr);
        if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
    }
    if (total > listRows)
        mvwprintw(winInfo_, infoRows - 1, infoCols - 12, "[%d/%d]",
                  sigintCapSel_ + 1, total);

    mvwprintw(winMenu_, 1, 1, "[A]=Refresh  [B]=Back");
}

void TerminalUI::sigintCapturesButton(const ButtonEvent &ev) {
    int total = (int)sigintCaps_.size();
    switch (ev.button) {
        case GpiButton::UP:   if (sigintCapSel_ > 0) sigintCapSel_--; break;
        case GpiButton::DOWN: if (sigintCapSel_ < total - 1) sigintCapSel_++; break;
        case GpiButton::A:
        case GpiButton::SELECT:
            sigintLoadCapFiles();
            sigintCapSel_ = sigintCapScroll_ = 0;
            break;
        case GpiButton::B:
            screen_ = pentestMode_ ? Screen::BATTLE : Screen::MENU;
            break;
        default: break;
    }
}

// ── End SIGINT tools ──────────────────────────────────────────────────────────

// Choose the next WiFi target.  Returns true with pentestSsid_/pentestVuln_ set,
// or false when a working scanner found NO vulnerable network in range (→ the
// caller drops to standby).  With no scanner at all, falls back to the fictional
// demo list so the ROM still plays off-device.
bool TerminalUI::pentestPickTarget() {
    // Refresh the in-range list every pick so encounters keep coming from ALL
    // nearby networks — revisiting an AP is another CATCH ATTEMPT (deterministic
    // species per BSSID), not a one-and-done. (Reads cached wpa_cli results, no
    // active scan.)
    pentestScanNetworks();

    if (pentestScanAvailable_) {
        if (pentestNets_.empty()) return false;          // nothing vulnerable nearby
        int pick = rand() % (int)pentestNets_.size();
        snprintf(pentestSsid_, sizeof(pentestSsid_), "%s", pentestNets_[pick].ssid.c_str());
        snprintf(pentestVuln_, sizeof(pentestVuln_), "%s", pentestNets_[pick].vuln.c_str());
        // Seed the colour roll with the real MAC when we have one; otherwise
        // fall back to the SSID so the network still maps to a stable colour.
        snprintf(pentestBssid_, sizeof(pentestBssid_), "%s",
                 pentestNets_[pick].bssid.empty() ? pentestNets_[pick].ssid.c_str()
                                                  : pentestNets_[pick].bssid.c_str());
        // NOTE: intentionally do NOT retire the network — it stays a repeatable
        // encounter so you can keep trying for a catch / farm its fixed mon.
        return true;
    }

    // ── No scanner: fictional demo list (no-repeat via a bitmask) ──
    // Keep to ≤16 entries so pentestUsedSsid_ (uint16_t bitmask) doesn't overflow.
    static const char *SSIDS[] = {
        "linksys",       "NETGEAR47",      "xfinitywifi",    "ATT-WiFi-2G",
        "TP-Link_5G",    "HOME-A1B2",      "dlink-guest",    "CenturyLink",
        "Pixel_5599",    "FBI_Van_3",      "Starbucks",      "iPhone",
        "(hidden-ac:3f)","BLE_Flood_01",   "BLE_AppleProx",
    };
    static const char *VULNS[] = {
        "WPS PIN brute (CVE-2011-5053)",
        "WEP key reuse / IV collision",
        "KRACK 4-way handshake replay",
        "Default admin creds (admin:admin)",
        "WPA2 PMKID hashcat -m 16800",
        "Evil-twin deauth (802.11w off)",
        "WPA-TKIP (BEAST/RC4 brute)",
        "Hidden SSID (probe-request sniff)",
        "Deauth flood - 802.11 mgmt attack",
        "BLE advertisement flood (Flipper/spammer)",
        "Apple BLE proximity spam (SourApple/Handoff)",
    };
    const int NUM_SSID = (int)(sizeof(SSIDS)/sizeof(SSIDS[0]));
    // Repeatable encounters: pick any demo network each time (no no-repeat mask),
    // so the same fixed-mon networks keep coming up for more catch attempts.
    int pick = rand() % NUM_SSID;
    snprintf(pentestSsid_, sizeof(pentestSsid_), "%s", SSIDS[pick]);
    snprintf(pentestVuln_, sizeof(pentestVuln_), "%s",
             VULNS[rand() % (int)(sizeof(VULNS)/sizeof(VULNS[0]))]);
    // Demo mode has no real MAC — synthesise a stable pseudo-BSSID from the
    // SSID so each demo network still maps deterministically to a colour.
    {
        uint32_t h = 2166136261u;
        for (const char *s = SSIDS[pick]; *s; ++s) { h ^= (uint8_t)*s; h *= 16777619u; }
        snprintf(pentestBssid_, sizeof(pentestBssid_),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 (h >> 0) & 0xFF, (h >> 8) & 0xFF, (h >> 16) & 0xFF,
                 (h >> 24) & 0xFF, (h >> 4) & 0xFF, (h >> 12) & 0xFF);
    }
    return true;
}

// Enter standby: no battle, just wait + scan for a vulnerable network.  The SDL
// window renders the status/standby screen (driven from syncBattleWindow).
void TerminalUI::pentestEnterStandby() {
    pentestStandby_  = true;
    inPentestBattle_ = false;
    inBattle_        = false;
    pentestScanMs_   = millis();
    screen_          = Screen::BATTLE;     // keeps the SDL window up for the overlay
}

// Pentest Pikachu ROM: a self-contained Pikachu-vs-zone battle that doesn't
// depend on a loaded SAV.  Pikachu starts at L5 and climbs through the Kanto
// zones, fighting the Pokemon of the area it's currently in.
// ── Rare-colour roll ─────────────────────────────────────────────────────────
// Colour roll out of 16384: Shiny 4 (1/4096), Pink 3, Rainbow 1; Dark is a
// separate 1/1024 draw that stacks with the colour (Dark-Shiny / Pink / Rainbow).
// Parse an AP MAC string ("AA:BB:CC:DD:EE:FF") into 6 raw bytes so the genotype
// roll hashes the SAME 6 bytes the firmware / mmpoke do — keeping a network's
// mon+skin identical across the ESP32, mmpoke, and this terminal.
static void parseMacBytes(const char *s, uint8_t out[6]) {
    for (int i = 0; i < 6; ++i) out[i] = 0;
    if (!s) return;
    int n = 0;
    for (size_t i = 0; s[i] && n < 6; ++i) {
        if (s[i] == ':') continue;
        auto hx = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
            if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
            return 0;
        };
        if (!s[i + 1]) break;
        out[n++] = (uint8_t)((hx(s[i]) << 4) | hx(s[i + 1]));
        ++i;
    }
}

// Roll the wild foe's FULL cosmetic + defect genotype deterministically from the
// AP MAC + species (canonical PentestCatch model — matches firmware & mmpoke).
// Stashes it in pentestFoeGeno_ so a catch keeps the whole genotype for breeding,
// and returns the Gen2SpriteCache VAR_* used to tint the sprite (Blackout skins,
// which have no dedicated sprite yet, fold onto their Dark equivalents).
int TerminalUI::rollRareColor(const char *bssid, uint8_t species) {
    uint8_t mac[6];
    parseMacBytes(bssid, mac);
    breeding::Genotype g = pentest::rollWildGenotype(mac, species);

#ifdef PENTEST_RARE_TEST
    // TEST ONLY: force a visible cosmetic far more often so the catch flow is
    // observable. Still deterministic per (MAC, species).
    if (breeding::skinOf(g) == breeding::SKIN_REGULAR) {
        uint32_t h = pentest::geneHash(mac, species, 0xEE);
        if ((h & 3u) != 0u) { g.female = 1; g.rainbow = 1; }   // ~75% forced ♀ Pink
    }
#endif

    pentestFoeGeno_ = g;
    // breeding::Skin 0..11 maps 1:1 to Gen2SpriteCache VAR_* (incl. Blackout).
    return (int)breeding::skinOf(g);
}

static const char *rareColorName(int v) {
    using namespace Gen2SpriteCache;
    switch (v) {
        case VAR_SHINY:        return "Shiny";
        case VAR_PINK:         return "Pink";
        case VAR_RAINBOW:      return "Rainbow";
        case VAR_DARK:         return "Dark";
        case VAR_DARK_SHINY:   return "Dark Shiny";
        case VAR_DARK_PINK:    return "Dark Pink";
        case VAR_DARK_RAINBOW: return "Dark Rainbow";
        default:              return "";
    }
}

static const char *pentestRarePath() {
#ifdef __APPLE__
    return "/tmp/monstermesh/rare_catches.dat";
#else
    return "/var/lib/monstermesh/rare_catches.dat";
#endif
}

// Persist one caught rare (dex, colour variant, level) by appending to a file.
void TerminalUI::pentestSaveRareCatch(uint8_t dex, uint8_t variant, uint8_t level) {
    FILE *f = fopen(pentestRarePath(), "ab");
    if (!f) return;
    uint8_t rec[3] = { dex, variant, level };
    fwrite(rec, 1, sizeof(rec), f);
    fclose(f);
}

// ── Breeding box (Bill's PC) — full-genotype catches for the Breed tab ─────────
// Stored as the canonical 22-byte CaughtMon wire record so it is byte-compatible
// with the firmware transfer + BreedingApp::importCaughtMonBlob(): dex, level,
// geno[7] (rainbow,shiny,dark,sterile,cantFight,noHatch,female), caughtSec u32,
// provenance (0=Wild), nick[8]. Same file feeds the future LoRa social transfer.
static const char *pentestBoxPath() {
#ifdef __APPLE__
    return "/tmp/monstermesh/breeding_box.dat";
#else
    return "/var/lib/monstermesh/breeding_box.dat";
#endif
}

// Serialize one caught mon to the versioned 34-byte box record (format v2): the
// canonical 22-byte CaughtMon wire prefix (still byte-compatible with the
// firmware transfer / importCaughtMonBlob) followed by the 11-byte per-individual
// trip tail (tripLevel, tripXp u32, tripGymBeaten u16, tripWins u16, tripLosses
// u16) and finally the 1-byte deck-only tritan genotype, all little-endian.
static void pentestSerializeMon(const breeding::BreedMon &m,
                                uint8_t rec[breeding::BREEDBOX_REC_SIZE]) {
    memset(rec, 0, breeding::BREEDBOX_REC_SIZE);
    rec[0] = m.dex;
    rec[1] = m.level;
    rec[2] = m.geno.rainbow;   rec[3] = m.geno.shiny;    rec[4] = m.geno.dark;
    rec[5] = m.geno.sterile;   rec[6] = m.geno.cantFight; rec[7] = m.geno.noHatch;
    rec[8] = m.geno.female;
    // rec[9..12] caughtSec — leave 0 (clock not needed for the roster)
    // provenance byte: 0 = Wild catch; else the Fn generation number (bred).
    rec[13] = (m.prov == breeding::PROV_WILD) ? 0
                                              : (m.provGen ? m.provGen : 1);
    memcpy(rec + 14, m.nick, 8);
    // ── Trip tail (offset 22) ─────────────────────────────────────────────────
    uint8_t *t = rec + 22;
    t[0]  = m.tripLevel;
    t[1]  = (uint8_t)(m.tripXp & 0xFF);
    t[2]  = (uint8_t)((m.tripXp >> 8)  & 0xFF);
    t[3]  = (uint8_t)((m.tripXp >> 16) & 0xFF);
    t[4]  = (uint8_t)((m.tripXp >> 24) & 0xFF);
    t[5]  = (uint8_t)(m.tripGymBeaten & 0xFF);
    t[6]  = (uint8_t)((m.tripGymBeaten >> 8) & 0xFF);
    t[7]  = (uint8_t)(m.tripWins & 0xFF);
    t[8]  = (uint8_t)((m.tripWins >> 8) & 0xFF);
    t[9]  = (uint8_t)(m.tripLosses & 0xFF);
    t[10] = (uint8_t)((m.tripLosses >> 8) & 0xFF);
    // ── Deck-only tritan gene (offset 22 + trip size = 33) ─────────────────────
    // NOT part of the 22-byte firmware-compatible prefix — box format v2 only.
    rec[22 + breeding::BREEDBOX_TRIP_SIZE] = m.geno.tritan;
}

// Write the 5-byte box header (magic + version) to an empty file.
static void pentestWriteBoxHeader(FILE *f) {
    uint32_t magic = breeding::BREEDBOX_MAGIC;
    uint8_t  ver   = breeding::BREEDBOX_VERSION;
    fwrite(&magic, 4, 1, f);
    fwrite(&ver,   1, 1, f);
}

void TerminalUI::pentestAppendBox(const breeding::BreedMon &m) {
    FILE *f = fopen(pentestBoxPath(), "ab");
    if (!f) return;
    // A brand-new file needs the versioned header before its first record.
    // (Existing files are already normalized to the versioned format by
    // pentestLoadBox(), so appends here always land after a valid header.)
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) pentestWriteBoxHeader(f);
    uint8_t rec[breeding::BREEDBOX_REC_SIZE];
    pentestSerializeMon(m, rec);
    fwrite(rec, 1, sizeof(rec), f);
    fclose(f);
}

// Persist the 3 breeder rooms as a same-device binary blob (magic + version +
// per-build room size guard so a layout change discards cleanly).
static const char *pentestRoomsPath() {
#ifdef __APPLE__
    return "/tmp/monstermesh/breeder_rooms.dat";
#else
    return "/var/lib/monstermesh/breeder_rooms.dat";
#endif
}
void TerminalUI::pentestSaveRooms() {
    FILE *f = fopen(pentestRoomsPath(), "wb");
    if (!f) return;
    uint32_t magic = 0x424D5252;                    // "RRMB"
    uint8_t  ver   = 1;
    uint16_t rsz   = (uint16_t)sizeof(breeding::BreederRoom);
    fwrite(&magic, 4, 1, f); fwrite(&ver, 1, 1, f); fwrite(&rsz, 2, 1, f);
    fwrite(breederMgr_.rawRooms(), sizeof(breeding::BreederRoom),
           breeding::NUM_BREEDER_ROOMS, f);
    fclose(f);
}
void TerminalUI::pentestLoadRooms() {
    FILE *f = fopen(pentestRoomsPath(), "rb");
    if (!f) return;
    uint32_t magic = 0; uint8_t ver = 0; uint16_t rsz = 0;
    if (fread(&magic, 4, 1, f) == 1 && fread(&ver, 1, 1, f) == 1 &&
        fread(&rsz, 2, 1, f) == 1 && magic == 0x424D5252 && ver == 1 &&
        rsz == (uint16_t)sizeof(breeding::BreederRoom)) {
        breeding::BreederRoom tmp[breeding::NUM_BREEDER_ROOMS];
        if (fread(tmp, sizeof(breeding::BreederRoom),
                  breeding::NUM_BREEDER_ROOMS, f) == breeding::NUM_BREEDER_ROOMS)
            breederMgr_.restoreRooms(tmp);
    }
    fclose(f);
}

// Overwrite the box file with exactly the current roster in the versioned
// format (header + 33-byte trip records). Used after dedup, after a live-set
// save (to persist the active mon's trip), and once at load to upgrade any
// legacy headerless file in place.
void TerminalUI::pentestRewriteBox() {
    FILE *f = fopen(pentestBoxPath(), "wb");
    if (!f) return;
    pentestWriteBoxHeader(f);
    uint8_t rec[breeding::BREEDBOX_REC_SIZE];
    for (const auto &m : breedApp_.roster()) {
        pentestSerializeMon(m, rec);
        fwrite(rec, 1, sizeof(rec), f);
    }
    fclose(f);
}

// Collapse the box to ONE mon per (SPECIES + COLORATION) — so you keep e.g. a
// Pink Jigglypuff AND a Pink Pidgey, just not duplicate copies of the same
static uint64_t breederRoomsSig(const breeding::BreederManager &m);   // defined below

// Bill's PC "Breed" action — pick two mons as a breeder pair without leaving the
// virtual-pet ROM. First pick arms pentestBreedPickId_; the second pick (a
// different mon) places the pair in a breeder room via the shared BreederManager,
// so it shows up in the MM terminal's breeder-room browser. All eligibility
// checks (sterile, cooldown, room-full, already-in-a-room) live in placePair.
void TerminalUI::pentestBoxBreedPick(const breeding::BreedMon &m) {
    if (breeding::isSterile(m.geno)) {
        breedMsg_ = std::string(dexName(m.dex)) + " is STERILE — can't breed.";
        return;
    }
    // No first pick yet (or re-picking the same mon toggles the choice off).
    if (pentestBreedPickId_ == 0 || pentestBreedPickId_ == m.id) {
        if (pentestBreedPickId_ == m.id) {
            pentestBreedPickId_ = 0;
            breedMsg_ = "Breeder pick cleared.";
        } else {
            pentestBreedPickId_ = m.id;
            breedMsg_ = std::string("Breeder 1: ") +
                        (m.nick[0] ? m.nick : dexName(m.dex)) +
                        " \xE2\x80\x94 pick a mate, then Breed.";
        }
        return;
    }
    // Second pick — resolve both mons to their current roster indices and place.
    const auto &roster = breedApp_.roster();
    int idxA = -1, idxB = -1;
    for (int i = 0; i < (int)roster.size(); ++i) {
        if (roster[i].id == pentestBreedPickId_) idxA = i;
        if (roster[i].id == m.id)                idxB = i;
    }
    if (idxA < 0) {                       // first pick vanished (deduped?) — restart
        pentestBreedPickId_ = m.id;
        breedMsg_ = "First breeder is gone. Pick again.";
        return;
    }
    std::string err;
    int room = breederMgr_.placePair(breedApp_, (size_t)idxA, (size_t)idxB,
                                     time(nullptr), err);
    if (room >= 0) {
        pentestSaveRooms();
        breederSig_ = breederRoomsSig(breederMgr_);
        breedMsg_ = "Paired! Egg at 6AM, hatches 6PM. (See Breed tab)";
    } else {
        breedMsg_ = err;
    }
    pentestBreedPickId_ = 0;
}

// species+colour — keeping the one with the fewest genetic disorders (lowest
// sterile+cantFight+noHatch dosage sum). User-triggered from the Bill's PC menu.
// Rewrites the file if anything changed (backing up the pre-dedup box once).
void TerminalUI::pentestDedupBox() {
    const auto &roster = breedApp_.roster();
    if (roster.size() <= 1) return;
    auto defectScore = [](const breeding::Genotype &g) {
        return (int)g.sterile + (int)g.cantFight + (int)g.noHatch;   // 0..6, lower=better
    };
    std::vector<breeding::BreedMon> keep;
    keep.reserve(roster.size());
    for (const auto &m : roster) {
        int sk = (int)breeding::skinOf(m.geno);
        int found = -1;
        for (size_t i = 0; i < keep.size(); ++i)
            if (keep[i].dex == m.dex && (int)breeding::skinOf(keep[i].geno) == sk) {
                found = (int)i; break;
            }
        if (found < 0) keep.push_back(m);
        else if (defectScore(m.geno) < defectScore(keep[(size_t)found].geno))
            keep[(size_t)found] = m;
    }
    if (keep.size() == roster.size()) return;   // nothing to collapse
    breedApp_.clear();
    for (auto &m : keep) breedApp_.add(m);
    // One-time safety backup of the pre-dedup box, then rewrite.
    std::string bak = std::string(pentestBoxPath()) + ".predupe";
    FILE *chk = fopen(bak.c_str(), "rb");
    if (chk) { fclose(chk); }                  // backup already exists — don't clobber
    else     { rename(pentestBoxPath(), bak.c_str()); }
    pentestRewriteBox();
}

void TerminalUI::pentestLoadBox() {
    // Idempotent: importBox appends, so guard against a second load doubling the
    // roster (one call site — the first-scan path — is unguarded by breedLoaded_).
    if (breedLoaded_) return;
    FILE *f = fopen(pentestBoxPath(), "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len > 0 && len < 1 << 20) {
        std::vector<uint8_t> buf((size_t)len);
        // importBox handles BOTH the versioned trip format and legacy headerless
        // 22-byte files (migrating the latter with default trip fields).
        if (fread(buf.data(), 1, (size_t)len, f) == (size_t)len)
            breedApp_.importBox(buf.data(), buf.size());
    }
    fclose(f);
    // Sort the box by Pokedex number so mons are always in a findable order
    // (within any colour tab they appear in dex order, not random catch order).
    auto &ros = breedApp_.mutableRoster();
    std::sort(ros.begin(), ros.end(),
              [](const breeding::BreedMon &a, const breeding::BreedMon &b) {
                  if (a.dex != b.dex) return a.dex < b.dex;
                  return (int)breeding::skinOf(a.geno) < (int)breeding::skinOf(b.geno);
              });
    // Normalize the on-disk file to the versioned format (upgrades any legacy
    // headerless box in place, and persists trip fields in sorted order).
    if (!ros.empty()) pentestRewriteBox();
    pentestLoadRooms();   // restore any occupied breeder rooms (survives relaunch)
    // Independent journeys: now that the box is loaded, sync the live set to the
    // persisted active mon (a box mon's trip lives in the roster we just loaded).
    // pentestLoadActiveToLive seeds a first-time box mon from its catch level.
    pentestLoadActiveToLive();
}

// Cheap signature of the breeder-room state — changes whenever a room is placed,
// lays an egg, hatches, or is emptied, so we only rewrite the file on change.
static uint64_t breederRoomsSig(const breeding::BreederManager &m) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < m.roomCount(); ++i) {
        const auto &r = m.room(i);
        uint64_t v = (uint64_t)r.state ^ ((uint64_t)r.idA << 8) ^ ((uint64_t)r.idB << 20)
                   ^ ((uint64_t)r.eggHatchAt << 3) ^ ((uint64_t)r.rolled << 40)
                   ^ ((uint64_t)r.pendingChild.dex << 44);
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

// Auto-tick hook: if the foe is a rare colour, Pikachu fights it down to <=60%
// HP then throws Poke Balls (skipping the attack) until it's caught. Returns
// true when it handled the tick.
bool TerminalUI::pentestTryCatchTick() {
    if (!inPentestBattle_ || pentestFoeVariant_ == Gen2SpriteCache::VAR_NORMAL) return false;
    Gen1BattleEngine::BattleParty &ep = engine_.party(1 - localSide_);
    if (ep.count == 0 || ep.active >= ep.count) return false;
    Gen1BattleEngine::BattlePoke &foe = ep.mons[ep.active];
    if (foe.hp == 0 || foe.maxHp == 0) return false;

    if (!pentestCatching_) {
        // Don't re-catch the EXACT same species+colour you already own (stops the
        // box refilling with duplicates of the same repeatable network). Collapsing
        // colours to one-of-each is a manual action in the Bill's PC menu.
        if (!breedLoaded_) { pentestLoadBox(); breedLoaded_ = true; }
        breeding::Skin foeSkin = breeding::skinOf(pentestFoeGeno_);
        for (const auto &owned : breedApp_.roster())
            if (owned.dex == pentestFoeDex_ && breeding::skinOf(owned.geno) == foeSkin)
                return false;
        // Like the real games: it has to be WORN DOWN first. Keep fighting until
        // the foe is into the red (~25% HP) before switching to Poke Balls.
        if ((uint32_t)foe.hp * 4 > (uint32_t)foe.maxHp) return false;   // >25% HP: keep fighting
        pentestCatching_ = true;
        char line[80];
        snprintf(line, sizeof(line), "A %s %s! Pikachu throws a Poke Ball!",
                 rareColorName(pentestFoeVariant_), dexName(foe.species));
        battleLog_.push_back(line);
    }

    // Gen-1-style catch odds: low even when weakened (no bait/status in the
    // auto-battle), so it takes several balls and isn't a guaranteed grab.
    // 40permille at full HP → ~600permille at 1 HP. We stop attacking once
    // catching so the foe is never KO'd — you just keep throwing.
    uint16_t p = pentest::catchPermille(foe.hp, foe.maxHp, false, false);
    bool caught = ((uint32_t)(rand() % 1000) < p);
    // Trigger the SDL Poke Ball throw/wobble/result animation for this throw.
    pentestCatchSeq_++;
    pentestCatchOutcome_ = caught ? 1 : 2;
    if (caught) {
        char line[80];
        snprintf(line, sizeof(line), "Gotcha! The %s %s was caught!",
                 rareColorName(pentestFoeVariant_), dexName(foe.species));
        battleLog_.push_back(line);
        pentestRareCaught_++;
        pentestSaveRareCatch(foe.species, (uint8_t)pentestFoeVariant_, foe.level);
        // New genetics: keep the FULL genotype so this catch can breed. Add it to
        // the live roster AND persist it to the box file (Bill's PC / Breed tab).
        breeding::BreedMon caught{};
        caught.dex   = pentestFoeDex_;
        caught.level = foe.level;
        caught.geno  = pentestFoeGeno_;
        caught.prov  = breeding::PROV_WILD;
        strncpy(caught.nick, breeding::BreedingApp::dexName(pentestFoeDex_),
                sizeof(caught.nick) - 1);
        breedApp_.add(caught);
        pentestAppendBox(caught);
        battleResult_ = Gen1BattleEngine::Result::P1_WIN;   // reuse the end/linger flow
        screen_ = Screen::BATTLE;
    } else {
        battleLog_.push_back("Aww! It broke free!");
    }
    return true;
}

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
        pentestLoadBox();          // Bill's PC — caught mons for the Breed tab
        breedLoaded_ = true;
        pentestLoaded_ = true;
    }

    // Pikachu only fights networks with a real weakness.  Pick a vulnerable
    // WiFi target (real scan via wpa_cli, or the demo list off-device); if none
    // is in range, drop to standby and keep scanning.
    // On a rematch (lost last fight) reuse the same SSID/vuln without re-scanning.
    if (pentestRematch_) {
        pentestRematch_ = false;
        // pentestSsid_ and pentestVuln_ are already set from the previous battle.
    } else if (!pentestPickTarget()) {
        pentestEnterStandby();
        return;
    }

    battleLog_.clear();
    roguelike_         = false;
    inGymBattle_       = false;
    inE4Battle_        = false;
    inPentestBattle_   = true;
    pentestStandby_    = false;
    pentestShowStatus_ = false;
    pentestTallied_    = false;
    localSide_         = 0;
    moveSel_           = 0;
    switchMode_        = false;
    inBattle_          = true;

    // Player: a fixed Lv15 Pikachu (stats derived by the engine from
    // species + level + average DVs).  The engine reads Gen1Pokemon.species as
    // the INTERNAL Gen-1 code (not national dex), so convert via dexToInternal
    // exactly like buildPlayerPartyForBattle / lordBuildGymParty do — passing
    // raw dex 25 makes the engine resolve a different (ghost) species.
    // Species + moveset track the Pikachu's level: it evolves into Raichu at
    // L30, and carries the 4 most-recently-learned moves from the Gen-1
    // learnset (recomputed each scan, so leveling up brings new moves online).
    // Active battler: default Pikachu/Raichu, OR a caught mon promoted via the
    // Bill's PC "Set as Active" menu (pentestActiveDex_ != 0).
    bool    evolved      = (pentestLevel_ >= PIKACHU_EVOLVE_LEVEL);
    // A caught battler evolves as its trip level climbs: a Weedle levelled to 68
    // fights as Beedrill (dex 15), so it uses Beedrill's species + learnset
    // instead of being a moveless Weedle. Resolved dynamically from the caught
    // base dex + current level, so evolutions come online as the mon levels.
    uint8_t battlerDex   = (pentestActiveDex_ != 0)
                             ? gen1FinalFormAtLevel(pentestActiveDex_, pentestLevel_)
                             : (uint8_t)(evolved ? 26 : 25);
    uint8_t battlerInt   = dexToInternal[battlerDex];
    uint8_t battlerMoves[4];
    char    battlerName[12];
    if (pentestActiveDex_ != 0) {
        // The 4 most-recently-learned moves at this level from the EVOLVED form's
        // learnset, just like the games. Pre-evolutions (Weedle/Caterpie/etc.)
        // have no level-up moves of their own, so fall back to the caught form
        // only if the evolved form somehow has an empty learnset.
        uint8_t all[32];
        uint8_t na = gen1MovesLearnedBetween(battlerDex, 0, pentestLevel_, all, 32);
        if (na == 0 && battlerDex != pentestActiveDex_)
            na = gen1MovesLearnedBetween(pentestActiveDex_, 0, pentestLevel_, all, 32);
        battlerMoves[0] = battlerMoves[1] = battlerMoves[2] = battlerMoves[3] = 0;
        int start = (na > 4) ? na - 4 : 0, j = 0;
        for (int i = start; i < (int)na; i++) battlerMoves[j++] = all[i];
        if (battlerMoves[0] == 0) battlerMoves[0] = 33;       // Tackle fallback (no learnset)
        snprintf(battlerName, sizeof(battlerName), "%s", dexName(battlerDex));
    } else {
        pikaMovesForLevel(pentestLevel_, battlerMoves);
        snprintf(battlerName, sizeof(battlerName), "%s", evolved ? "RAICHU" : "PIKACHU");
    }
    memset(&party_, 0, sizeof(party_));
    party_.count            = 1;
    party_.species[0]       = battlerInt;
    party_.mons[0].species  = battlerInt;
    party_.mons[0].level    = pentestLevel_;
    party_.mons[0].boxLevel = pentestLevel_;
    party_.mons[0].dvs[0]   = 0x88;
    party_.mons[0].dvs[1]   = 0x88;
    memcpy(party_.mons[0].moves, battlerMoves, 4);
    for (int m = 0; m < 4; m++) {
        const Gen1MoveData *md = gen1Move(battlerMoves[m]);
        party_.mons[0].pp[m] = md ? md->pp : 0;
    }
    memset(party_.nicknames[0], 0x50, 11);          // 0x50 = Gen1 string term
    for (int j = 0; battlerName[j] && j < 10; j++) {
        char c = battlerName[j];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);   // uppercase
        if (c >= 'A' && c <= 'Z') party_.nicknames[0][j] = (uint8_t)(0x80 + (c - 'A'));
        else                      party_.nicknames[0][j] = 0x7F;   // space for non-letters
    }

    // Resume partial XP toward the next level; never bank it to the SAV.
    slotLevelXp_[0] = pentestXp_;
    sessionXp_[0]   = 0;

    // ── Opponent selection — faithful to the T-Deck / Heltec Pentest Pikachu ──
    //   85% wild from ALL currently-unlocked zones (progression = number of gym
    //        leaders BEATEN, not Pikachu's level).
    //   15% the NEXT gym leader in sequence (Brock, then Misty, ...); after all
    //        8 are beaten it clamps to Viridian as a repeatable boss.
    // Beating a leader sets a bit in pentestGymBeaten_, unlocking the next set
    // of zones — so the wild pool grows as you earn badges, just like the games.
    uint8_t gymsBeaten = (uint8_t)__builtin_popcount(pentestGymBeaten_);

    uint8_t unlockedZones[KANTO_ZONE_COUNT];
    uint8_t nUnlocked = 0;
    for (uint8_t i = 0; i < KANTO_ZONE_COUNT; ++i)
        if (KANTO_ZONES[i].unlockedAfter <= gymsBeaten && KANTO_ZONES[i].wildCount > 0)
            unlockedZones[nUnlocked++] = i;
    if (nUnlocked == 0) unlockedZones[nUnlocked++] = 0;   // Route 1 fallback

    // Wild level: uniform across [pikaLvl/2, pikaLvl+1] (min 2, max 100) — the
    // wide-variance scaling the T-Deck uses (easy laps + occasional matched fight).
    auto scaledWildLvl = [&]() -> uint8_t {
        int lo = (int)pentestLevel_ / 2;
        int hi = (int)pentestLevel_ + 1;
        if (lo < 2) lo = 2;
        if (hi < lo + 1) hi = lo + 1;
        if (hi > 100) hi = 100;
        int lvl = lo + rand() % (hi - lo + 1);
        if (lvl < 2)   lvl = 2;
        if (lvl > 100) lvl = 100;
        return (uint8_t)lvl;
    };
    // Rarity gate (accept-per-1000): legendaries 5, pseudo-legendaries 20,
    // rares 50, everything else always.  Keeps Mewtwo/Dragonite/etc. trophies.
    auto rarityPermille = [](uint8_t dex) -> uint16_t {
        if (dex==144||dex==145||dex==146||dex==150||dex==151) return 5;
        if (dex==147||dex==148||dex==149)                     return 20;
        if (dex==113||dex==130||dex==131||dex==137||dex==142||
            dex==143||dex==134 ||dex==135||dex==136)          return 50;
        return 0;
    };

    uint8_t wildDex, wildLv;
    uint8_t wildMoves[4] = { 0, 0, 0, 0 };
    const char *gymTrainerName = nullptr;   // set for gym fights (shown in log)
    int roll = rand() % 100;
    if (roll < 85) {
        // ── Wild from an unlocked zone ──
        const KantoZone &z = KANTO_ZONES[unlockedZones[rand() % nUnlocked]];
        const KantoWildMon *wm = &z.wilds[rand() % z.wildCount];
        // Reroll up to 8 times for a common if we hit a gated rare species.
        uint16_t perm = rarityPermille(wm->dex);
        if (perm != 0 && (uint16_t)(rand() % 1000) >= perm) {
            for (int t = 0; t < 8; ++t) {
                const KantoWildMon *c = &z.wilds[rand() % z.wildCount];
                if (rarityPermille(c->dex) == 0) { wm = c; break; }
            }
        }
        // 2% rare-overlay: a slim chance any encounter dips into the global
        // legendary/pseudo pool (still gated by per-species accept rate).
        static const uint8_t RARE_POOL[] = {
            113,130,131,134,135,136,137,142,143, 147,148,149, 144,145,146,150,151,
        };
        uint8_t overlayDex = 0;
        if (rand() % 100 < 2) {
            uint8_t d = RARE_POOL[rand() % (int)(sizeof(RARE_POOL))];
            uint16_t op = rarityPermille(d); if (op == 0) op = 50;
            if ((uint16_t)(rand() % 1000) < op) overlayDex = d;
        }
        wildDex = overlayDex ? overlayDex : wm->dex;
        wildLv  = scaledWildLvl();
        wildMoves[0] = 1; wildMoves[1] = 33;          // Pound, Tackle
        if (wildLv >= 10) wildMoves[2] = 45;          // + Growl
        snprintf(pentestZone_, sizeof(pentestZone_), "%s", z.name);
        pentestBattleGym_ = 255;
    } else {
        // ── Gym fight: face ANY of the gym's trainers, not just the leader ──
        // You work through the gym's junior trainers and its leader, like the
        // real games.  The leader appears ~40% of gym encounters; junior
        // trainers fill the rest.  Only beating the LEADER clears the gym
        // (marks the badge and unlocks the next gym + its zones).
        uint8_t gIdx = gymsBeaten;
        if (gIdx >= LORD_GYM_COUNT) gIdx = LORD_GYM_COUNT - 1;
        const LordGym *g = lordGym(gIdx);
        const LordGymMon *foe = nullptr;
        bool isLeader = false;
        if (g) {
            uint8_t leaderIdx = lordGymLeaderIndex(g);
            // 40% leader, 60% a random junior trainer (or always the leader if
            // this gym somehow has no juniors).
            uint8_t tIdx = (leaderIdx == 0 || rand() % 100 < 40)
                             ? leaderIdx
                             : (uint8_t)(rand() % leaderIdx);
            const LordGymTrainer *tr = &g->trainers[tIdx];
            if (!tr->party || tr->count == 0) {                   // empty slot? use leader
                tIdx = leaderIdx;
                tr   = &g->trainers[tIdx];
            }
            isLeader       = (tIdx == leaderIdx);
            gymTrainerName = tr->name;
            if (tr->party && tr->count > 0)
                foe = &tr->party[rand() % tr->count];             // random mon of that trainer
        }
        wildDex = foe ? foe->species : 25;
        wildLv  = (foe && foe->level) ? foe->level : pentestLevel_;
        if (foe) memcpy(wildMoves, foe->moves, 4);
        else     wildMoves[0] = 84;                    // Thunder Shock fallback
        snprintf(pentestZone_, sizeof(pentestZone_), "%s Gym",
                 g ? g->city : "Kanto");
        // Only a LEADER win advances progression; junior fights are 255 (no badge).
        pentestBattleGym_ = isLeader ? gIdx : 255;
    }
    pentestMarkSeen(wildDex);                          // log to the Pokedex

    // Roll this encounter's colour skin. Rare colours (non-Regular) get caught
    // with a Poke Ball once Pikachu weakens them, not just defeated.  Seeded by
    // the target's MAC + species, so this AP always shows the same colour here.
    pentestFoeVariant_ = rollRareColor(pentestBssid_, wildDex);  // sets pentestFoeGeno_
    pentestFoeDex_     = wildDex;                                 // national dex for a catch
    pentestCatching_   = false;

    uint8_t wildInternal = dexToInternal[wildDex];
    Gen1Party wild = {};
    wild.count            = 1;
    wild.species[0]       = wildInternal;
    wild.mons[0].species  = wildInternal;
    wild.mons[0].level    = wildLv;
    wild.mons[0].boxLevel = wildLv;
    memcpy(wild.mons[0].moves, wildMoves, 4);

    // (SSID + vulnerability were chosen by pentestPickTarget() above.)
    pentestEndMs_ = 0;

    // Seed the (now full-height) scrolling log with the exploit narration; the
    // engine's turn messages append below these as the auto-fight plays out.
    {
        char line[96];
        snprintf(line, sizeof(line), "[%s]  PIKACHU Lv%u",
                 pentestZone_, (unsigned)pentestLevel_);
        battleLog_.push_back(line);
        if (gymTrainerName) {                       // gym fight: name the trainer
            snprintf(line, sizeof(line), "%s wants to battle!", gymTrainerName);
            battleLog_.push_back(line);
        }
        snprintf(line, sizeof(line), "Scanning %s ...", pentestSsid_);
        battleLog_.push_back(line);
        snprintf(line, sizeof(line), "Vuln: %s", pentestVuln_);
        battleLog_.push_back(line);
        battleLog_.push_back("Pikachu deploys the exploit!");
    }

    battleResult_ = Gen1BattleEngine::Result::ONGOING;
    uint32_t seed = (uint32_t)(millis() ^ ((uint32_t)wildDex << 8) ^ wildLv);
    engine_.start(party_, wild, seed, battleGen_);
    resetBattleParticipants();
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

    // XP sharing: mark that our currently-active mon fought the enemy that is
    // out right now.  This accumulates across turns, so if a second mon is
    // switched in against the same enemy, both are recorded and split its EXP
    // when it faints (Gen 1 behaviour).
    if (ep.active < Gen1BattleEngine::MAX_PARTY && pp.active < 6)
        enemyParticipants_[ep.active] |= (uint8_t)(1u << pp.active);

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

            // XP sharing (Gen 1): every mon that fought this enemy and is
            // still alive splits the yield evenly.  Fall back to the KO'er if
            // the participant mask is somehow empty (e.g. a KO from a status
            // effect the first turn it was sent out).  Bench mons that never
            // faced this enemy still get nothing.
            uint8_t mask = (i < Gen1BattleEngine::MAX_PARTY) ? enemyParticipants_[i] : 0;
            uint8_t sharers[6];
            uint8_t nShare = 0;
            for (uint8_t s = 0; s < 6 && s < pp.count; s++) {
                if ((mask & (1u << s)) && pp.mons[s].hp > 0) sharers[nShare++] = s;
            }
            if (nShare == 0 && killerSlot < 6) sharers[nShare++] = killerSlot;

            uint32_t share = (nShare > 0) ? (xp / nShare) : 0;
            if (share < 1) share = 1;  // never round a real KO down to zero

            // Gym / E4 gauntlets: DON'T level up mid-run.  The level should
            // change once, after the whole gym/league is cleared — not after
            // every trainer.  XP still accumulates in sessionXp_ below and is
            // flushed to the daemon at the end, which recomputes the SAV level
            // (and the SAV_WRITEBACK summary shows the net level change).  For
            // one-off battles (Fight / roguelike / pentest) we still level up
            // mid-battle as normal.
            bool deferLevels = (inGymBattle_ || inE4Battle_);

            char line[64];
            for (uint8_t si = 0; si < nShare; si++) {
                uint8_t slot = sharers[si];
                sessionXp_[slot]   += share;
                slotLevelXp_[slot] += share;

                snprintf(line, sizeof(line), "%s gained %u EXP!",
                         pp.mons[slot].nickname, (unsigned)share);
                battleLog_.push_back(line);

                // Medium-fast level-up: XP needed to gain ONE level from L to
                // L+1 = (L+1)^3 - L^3.  slotLevelXp_ accumulates until it
                // crosses that delta, then we bump the engine's BattlePoke
                // level, scale stats linearly, and add the maxHp delta to
                // current HP (Gen 1 heal-on-level).
                Gen1BattleEngine::BattlePoke &mon = pp.mons[slot];
                while (!deferLevels && mon.level < 100) {
                    uint32_t L = mon.level;
                    uint32_t levelDelta = (L + 1) * (L + 1) * (L + 1) - L * L * L;
                    if (slotLevelXp_[slot] < levelDelta) break;
                    slotLevelXp_[slot] -= levelDelta;

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

                    // Pentest Pikachu: announce any move learned at this level
                    // and the L30 evolution.  Both take effect on the NEXT
                    // scan, when the party is rebuilt from the new level (the
                    // engine's active moveset/species is fixed for this battle).
                    if (inPentestBattle_) {
                        for (const PikaLearn &e : kPikaLearnset) {
                            if (e.level == newLevel) {
                                snprintf(line, sizeof(line), "Learned %s!",
                                         moveName(e.move));
                                battleLog_.push_back(line);
                            }
                        }
                        if (newLevel == PIKACHU_EVOLVE_LEVEL)
                            battleLog_.push_back("PIKACHU evolved into RAICHU!");
                    }
                }
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
    // Pentest Pikachu auto-mode: fight runs on its own, block all move input.
    // Boss mode (pentestBossMode_) is the exception — player controls manually.
    if (inPentestBattle_ && !pentestBossMode_) {
        if (ev.button == GpiButton::START || ev.button == GpiButton::SELECT) {
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
                if (switchSel_ != (int)pp.active && pp.mons[switchSel_].hp > 0) {
                    if (pvpServerMode_) {
                        // Queue Pi's switch; wait for opponent's action
                        if (pvpPendingOppReceived_) {
                            engine_.submitAction(0, 1, (uint8_t)switchSel_);
                            engine_.submitAction(1, pvpPendingOppAction_, pvpPendingOppIndex_);
                            pvpPendingOppReceived_ = false;
                            pvpPendingMyAction_    = 0xFF;
                            runTurnWithXp();
                            sendPvpUpdate(battleResult_ != Gen1BattleEngine::Result::ONGOING);
                        } else {
                            pvpPendingMyAction_ = 1;
                            pvpPendingMyIndex_  = (uint8_t)switchSel_;
                        }
                    } else {
                        engine_.submitAction(localSide_, 1, (uint8_t)switchSel_);
                        uint8_t cpuA, cpuI;
                        engine_.cpuPickAction(1 - localSide_, cpuA, cpuI);
                        engine_.submitAction(1 - localSide_, cpuA, cpuI);
                        runTurnWithXp();
                    }
                    switchMode_ = false;
                }
            } else {
                if (pp.mons[pp.active].moves[moveSel_] != 0) {
                    if (pvpServerMode_) {
                        // Queue Pi's move; run turn immediately if opponent's already arrived
                        if (pvpPendingOppReceived_) {
                            engine_.submitAction(0, 0, (uint8_t)moveSel_);
                            engine_.submitAction(1, pvpPendingOppAction_, pvpPendingOppIndex_);
                            pvpPendingOppReceived_ = false;
                            pvpPendingMyAction_    = 0xFF;
                            runTurnWithXp();
                            sendPvpUpdate(battleResult_ != Gen1BattleEngine::Result::ONGOING);
                        } else {
                            pvpPendingMyAction_ = 0;
                            pvpPendingMyIndex_  = (uint8_t)moveSel_;
                        }
                    } else {
                        engine_.submitAction(localSide_, 0, (uint8_t)moveSel_);
                        uint8_t cpuA, cpuI;
                        engine_.cpuPickAction(1 - localSide_, cpuA, cpuI);
                        engine_.submitAction(1 - localSide_, cpuA, cpuI);
                        runTurnWithXp();
                    }
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
        // Award result-based XP once, then persist to the SAV.  The client role
        // never accrued sessionXp_ during the fight, so this is the only place
        // it earns anything from a mesh battle.
        awardPvpResultXp();
    }
}

// ── PvP server-mode (Pi challenged a T-Deck, Pi runs the battle engine) ──────

void TerminalUI::parsePvpAccept(const std::string &msg) {
    int accepted = jsonGetInt(msg, "accepted", 0);
    if (!accepted) {
        pvpServerMode_ = false;
        pushActivity("> MMB: challenge declined");
        return;
    }
    std::string trainer = jsonGetStr(msg, "trainer");
    strncpy(pvpEnemyName_, trainer.c_str(), sizeof(pvpEnemyName_) - 1);
    pvpEnemyName_[sizeof(pvpEnemyName_) - 1] = '\0';

    // Decode the foe's neutral WireParty (protocol V2, 139 B) from the JSON
    // byte array — national dex + final computed stats + uint16 moves.
    Gen1BattleEngine::WireParty foeWire = {};
    size_t pos = msg.find("\"party_min\":[");
    if (pos != std::string::npos) {
        uint8_t blob[TB_WIRE_PARTY_BYTES] = {};
        pos += 13;
        for (int i = 0; i < TB_WIRE_PARTY_BYTES; i++) {
            while (pos < msg.size() && (msg[pos] == ' ' || msg[pos] == ',')) pos++;
            if (pos >= msg.size() || msg[pos] == ']') break;
            blob[i] = (uint8_t)atoi(msg.c_str() + pos);
            while (pos < msg.size() && msg[pos] != ',' && msg[pos] != ']') pos++;
        }
        unpackWireParty(blob, foeWire);
    }

    // Build Pi's own party (populates party_ and sets inBattle_=true, screen_=BATTLE)
    buildPlayerPartyForBattle();

    // Restart engine against the real foe instead of the random CPU. Prefer
    // the daemon-pushed wire party (the exact bytes it sent in the CHALLENGE
    // blob — required for Gen 2/3 saves, which have no Gen1Party form);
    // fall back to converting our local Gen-1 party the same way the daemon
    // would, so both engines derive from identical bytes either way.
    Gen1BattleEngine::WireParty myWire = {};
    if (hasMyWireParty_) {
        myWire = myWireParty_;
    } else {
        gen1PartyToWireParty(party_, myWire, battleGen_);
    }
    uint32_t seed = (uint32_t)(millis() ^ (uint32_t)(uintptr_t)this);
    engine_.start(myWire, foeWire, seed, battleGen_);
    resetBattleParticipants();

    // Server-mode battle state
    pvpServerMode_         = true;
    pvpServerTurn_         = 0;
    pvpPendingMyAction_    = 0xFF;
    pvpPendingOppReceived_ = false;
    roguelike_   = false;
    inGymBattle_ = false;
    inE4Battle_  = false;
    moveSel_     = 0;
    switchMode_  = false;
    battleLog_.clear();
    battleResult_ = Gen1BattleEngine::Result::ONGOING;

    // Send initial UPDATE so T-Deck can show starting HP
    sendPvpUpdate(false);
    pushActivity("> MMB: %s accepted! Battle on!", pvpEnemyName_);
}

void TerminalUI::parsePvpAction(const std::string &msg) {
    if (!pvpServerMode_ || !inBattle_) return;
    uint8_t oppAction = (uint8_t)jsonGetInt(msg, "action", 0);
    uint8_t oppIndex  = (uint8_t)jsonGetInt(msg, "index",  0);

    if (pvpPendingMyAction_ != 0xFF) {
        // Pi's action is already queued — submit both and run the turn
        engine_.submitAction(0, pvpPendingMyAction_, pvpPendingMyIndex_);
        engine_.submitAction(1, oppAction, oppIndex);
        pvpPendingMyAction_    = 0xFF;
        pvpPendingOppReceived_ = false;
        runTurnWithXp();
        sendPvpUpdate(battleResult_ != Gen1BattleEngine::Result::ONGOING);
    } else {
        // Pi hasn't picked yet — store opponent's action and wait
        pvpPendingOppReceived_ = true;
        pvpPendingOppAction_   = oppAction;
        pvpPendingOppIndex_    = oppIndex;
    }
}

void TerminalUI::sendPvpUpdate(bool includeResult) {
    // Build UPDATE body in T-Deck wire format:
    //   engine_.party(0) = Pi (server), engine_.party(1) = T-Deck (client)
    //   HP sent as: client first, then server (T-Deck sees my_hp = client HP)
    uint16_t flags = (uint16_t)(TB_UPD_HP | TB_UPD_PP | TB_UPD_SWITCH | TB_UPD_STATUS);
    if (includeResult) flags |= (uint16_t)TB_UPD_RESULT;

    // Collect the most recent log lines (cap at 4 to stay within packet budget)
    int logStart = (int)battleLog_.size() - 4;
    if (logStart < 0) logStart = 0;
    int numLog = (int)battleLog_.size() - logStart;
    if (numLog > 0) flags |= (uint16_t)TB_UPD_LOG;

    const Gen1BattleEngine::BattleParty &cli = engine_.party(1);  // T-Deck
    const Gen1BattleEngine::BattleParty &srv = engine_.party(0);  // Pi
    const auto &cliMon = cli.mons[cli.active];
    const auto &srvMon = srv.mons[srv.active];

    pvpServerTurn_++;

    // BATTLELINK_MAX_PAYLOAD = 196 bytes — the maximum BattlePacket.payload[] size
    uint8_t body[BATTLELINK_MAX_PAYLOAD] = {};
    int w = 0;
    body[w++] = pvpServerTurn_;
    body[w++] = (flags >> 8) & 0xFF;
    body[w++] =  flags       & 0xFF;
    body[w++] = 0; body[w++] = 0; body[w++] = 0;  // boardHash24 (unused)

    // HP: client (T-Deck), then server (Pi)
    body[w++] = (cliMon.hp >> 8) & 0xFF;
    body[w++] =  cliMon.hp       & 0xFF;
    body[w++] = (srvMon.hp >> 8) & 0xFF;
    body[w++] =  srvMon.hp       & 0xFF;

    // PP: client (T-Deck) active mon's PP
    for (int i = 0; i < 4; i++) body[w++] = cliMon.pp[i];

    // SWITCH: client active, server active
    body[w++] = cli.active;
    body[w++] = srv.active;

    // STATUS: client, server
    body[w++] = cliMon.status;
    body[w++] = srvMon.status;

    if (includeResult) {
        body[w++] = (uint8_t)engine_.result();
    }

    if (numLog > 0) {
        // Reserve space for the line count; fill after loop so count matches what fit
        int countPos = w++;
        int actualLines = 0;
        for (int i = 0; i < numLog; i++) {
            const std::string &line = battleLog_[logStart + i];
            uint8_t ll = (uint8_t)(line.size() < 64 ? line.size() : 64);
            if (w + 1 + ll >= BATTLELINK_MAX_PAYLOAD) break;
            body[w++] = ll;
            memcpy(body + w, line.c_str(), ll);
            w += ll;
            actualLines++;
        }
        body[countPos] = (uint8_t)actualLines;
    }

    // Encode as JSON byte array and ship via daemon
    char jsonBuf[1024];
    int pos = snprintf(jsonBuf, sizeof(jsonBuf), "{\"cmd\":\"SEND_BATTLE_UPDATE\",\"data\":[");
    for (int i = 0; i < w; i++) {
        if (i > 0) pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, ",");
        pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "%u", (unsigned)body[i]);
    }
    snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "]}");
    ipc_.send(jsonBuf);
}

// ── IPC message handling ──────────────────────────────────────────────────────

void TerminalUI::onIpcMessage(const std::string &msg) {
    std::string type = jsonGetStr(msg, "type");
    if      (type == "PARTY_UPDATE")       parsePartyUpdate(msg);
    else if (type == "STATUS")             parseStatus(msg);
    else if (type == "DAYCARE_EVENT")      parseDaycareEvent(msg);
    else if (type == "CHALLENGE_RECEIVED")  parseChallenge(msg);
    else if (type == "ACHIEVEMENT")         parseAchievement(msg);
    else if (type == "BATTLE_UPDATE")       parseBattleUpdate(msg);
    else if (type == "PVP_ACCEPT_RECEIVED") parsePvpAccept(msg);
    else if (type == "PVP_ACTION_RECEIVED") parsePvpAction(msg);
    else if (type == "MY_WIRE_PARTY") {
        // Daemon's authoritative wire form of OUR party (any gen). Decode the
        // 139-byte blob; engine seeding prefers this over local conversion.
        size_t pos = msg.find("\"party_min\":[");
        if (pos != std::string::npos) {
            uint8_t blob[TB_WIRE_PARTY_BYTES] = {};
            pos += 13;
            for (int i = 0; i < TB_WIRE_PARTY_BYTES; i++) {
                while (pos < msg.size() && (msg[pos] == ' ' || msg[pos] == ',')) pos++;
                if (pos >= msg.size() || msg[pos] == ']') break;
                blob[i] = (uint8_t)atoi(msg.c_str() + pos);
                while (pos < msg.size() && msg[pos] != ',' && msg[pos] != ']') pos++;
            }
            unpackWireParty(blob, myWireParty_);
            hasMyWireParty_ = myWireParty_.count > 0;
            mySavGen_ = (uint8_t)jsonGetInt(msg, "sav_gen", 1);
        }
    }
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
            // Parse a "key":[n,n,...] int array out of a slot object.
            auto parseIntArray = [](const std::string &obj, const char *key,
                                    uint8_t *out, int maxN) -> int {
                std::string pat = std::string("\"") + key + "\":[";
                size_t a = obj.find(pat);
                if (a == std::string::npos) return 0;
                a += pat.size();
                size_t b = obj.find(']', a);
                if (b == std::string::npos) return 0;
                int n = 0;
                size_t i = a;
                while (i < b && n < maxN) {
                    while (i < b && (obj[i] == ',' || obj[i] == ' ')) i++;
                    if (i >= b) break;
                    out[n++] = (uint8_t)atoi(obj.c_str() + i);
                    while (i < b && obj[i] != ',') i++;
                }
                return n;
            };

            // Walk the slots[...] array and print one line per Pokemon.  Slot
            // objects contain nested arrays ("learned"/"pending"/"moves") but
            // those use [] not {}, so the first '}' still closes the object.
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
                    int slot = jsonGetInt(sl, "slot", -1);
                    int oldL = jsonGetInt(sl, "old_level", 0);
                    int newL = jsonGetInt(sl, "new_level", 0);
                    int xp   = jsonGetInt(sl, "xp", 0);
                    if (newL > oldL)
                        pushActivity("  %-10s L%d -> L%d  (+%d XP)",
                                     nick.c_str(), oldL, newL, xp);
                    else
                        pushActivity("  %-10s L%d        (+%d XP)",
                                     nick.c_str(), oldL, xp);

                    // Moves auto-learned into empty slots — just report them.
                    uint8_t learned[8];
                    int nLearned = parseIntArray(sl, "learned", learned, 8);
                    for (int k = 0; k < nLearned; k++)
                        pushActivity("  %s learned %s!", nick.c_str(),
                                     moveName(learned[k]));

                    // Moves that need a forget-choice — queue a chooser prompt.
                    uint8_t pending[8], curMoves[4] = {};
                    int nPending = parseIntArray(sl, "pending", pending, 8);
                    if (nPending > 0 && slot >= 0 && slot < 6) {
                        parseIntArray(sl, "moves", curMoves, 4);
                        for (int k = 0; k < nPending; k++) {
                            PendingLearn pl{};
                            pl.slot    = (uint8_t)slot;
                            pl.newMove = pending[k];
                            for (int m = 0; m < 4; m++) pl.curMoves[m] = curMoves[m];
                            strncpy(pl.nick, nick.c_str(), sizeof(pl.nick) - 1);
                            learnQueue_.push_back(pl);
                        }
                    }
                    pos = end + 1;
                }
            }

            // If any Pokemon wants to learn a move but is full, jump into the
            // chooser (weakest move pre-selected).  The daemon patches each
            // choice as we confirm it.
            if (!learnQueue_.empty()) {
                learnCursor_ = weakestMoveIndex(learnQueue_.front());
                screen_ = Screen::MOVE_LEARN;
            }
        }
    }
    else if (type == "BEACON_RESULT") {
        bool ok = jsonGetInt(msg, "ok", 0) != 0;
        int party = jsonGetInt(msg, "party", 0);
        uint32_t nid = jsonGetU32(msg, "node_id", 0);
        if (ok && party > 0)
            pushActivity("> Beacon sent (party=%d, node=0x%08X)", party, nid);
        else if (ok)
            pushActivity("> Beacon sent (no party - load a .sav)");
        else
            pushActivity("> Beacon FAILED - no serial");
    }
    else if (type == "NODE_INFO") {
        uint32_t nid = jsonGetU32(msg, "node_id", 0);
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
    challengeNodeId_ = jsonGetU32(msg, "node_id", 0);
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
        uint32_t nid     = jsonGetU32(slot, "node_id", 0);
        uint32_t lsms    = jsonGetU32(slot, "last_seen_ms", 0);

        NeighborEntry &n = neighbors_[i];
        n.nodeId     = nid;
        n.lastSeenMs = lsms;
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

        // HollaBack live stream: a fresh responder arriving inside the HB
        // window gets echoed into the activity feed as an "HB ..." line
        // (matches the T-Deck's live HollaBack response stream).
        if (isNew && now < hbUntilMs_) {
            pushActivity("HB %s/%s Lv%d %s",
                         n.shortName[0] ? n.shortName : "?",
                         n.lead[0] ? n.lead : "?",
                         n.leadLevel, n.partyCount ? "Kanto" : "-");
        }

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
    engine_.start(party_, foe, seed, battleGen_);
    resetBattleParticipants();
    screen_       = Screen::BATTLE;
}

void TerminalUI::parseStatus(const std::string &msg) {
    neighborCount_  = jsonGetInt(msg, "neighbors", 0);
    daycareActive_  = jsonGetInt(msg, "active", 0) != 0;
    std::string ev  = jsonGetStr(msg, "last_event");
    if (!ev.empty()) lastEventText_ = ev;
    lastEventXp_    = jsonGetInt(msg, "last_event_xp", 0);

    // NOTE: do NOT touch neighborDisplayCount_ here. The neighbor list is owned
    // by the structured NEIGHBORS push (parseNeighbors); the daemon keeps a
    // neighbor as long as it was heard within NEIGHBOR_TIMEOUT_MS (1 hour).
    // Wiping it on every periodic STATUS tick made neighbors vanish after ~60s.
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

// ── Breed tab (Bill's PC) ─────────────────────────────────────────────────────
// A scrollable list of every caught mon. Move the cursor with UP/DOWN; press A
// to lock the first parent, move, press A again to breed the pair. B cancels a
// pending selection, or leaves the tab. Uses the same Mendelian engine as
// mmbreed (breeding::BreedingApp) so the offspring inherits exactly per the
// genetics doc; a kept child is added to the box and persisted.
// Exact Mendelian child-outcome distribution for a pairing (per-locus
// independent). skin[] sums the 12 visible skins over the 50/50 sex coin;
// defAff/defCar are per-defect P(affected)/P(carrier).
struct ChildOdds {
    double skin[12] = {0};
    double defAff[3] = {0}, defCar[3] = {0};   // sterile, cantFight, noHatch
    // Hidden ♂ carriers: a male with a rainbow allele shows Regular but carries
    // the pink (1 dose) or rainbow (2 dose) gene — invisible until bred to a ♀.
    double malePink = 0, maleRnbw = 0;
};
static ChildOdds computeChildOdds(const breeding::Genotype &a, const breeding::Genotype &b) {
    auto locus = [](uint8_t da, uint8_t db, double out[3]) {
        double pa = da / 2.0, pb = db / 2.0;    // prob each parent passes the rare allele
        out[0] = (1 - pa) * (1 - pb);           // dosage 0 (clean)
        out[2] = pa * pb;                       // dosage 2 (homozygous rare)
        out[1] = 1.0 - out[0] - out[2];         // dosage 1 (carrier)
    };
    double R[3], S[3], D[3], B[3], F[3], H[3];
    locus(a.rainbow, b.rainbow, R);   locus(a.shiny, b.shiny, S);
    locus(a.dark, b.dark, D);         locus(a.sterile, b.sterile, B);
    locus(a.cantFight, b.cantFight, F); locus(a.noHatch, b.noHatch, H);
    ChildOdds o;
    for (int rb = 0; rb < 3; rb++)
      for (int sh = 0; sh < 3; sh++)
        for (int dk = 0; dk < 3; dk++)
          for (int fem = 0; fem < 2; fem++) {
              breeding::Genotype g{};
              g.rainbow = (uint8_t)rb; g.shiny = (uint8_t)sh; g.dark = (uint8_t)dk;
              g.female = (uint8_t)fem;
              double p = R[rb] * S[sh] * D[dk] * 0.5;
              o.skin[(int)breeding::skinOf(g)] += p;
              if (fem == 0 && rb == 1) o.malePink += p;   // hidden pink carrier
              if (fem == 0 && rb == 2) o.maleRnbw += p;   // hidden rainbow carrier
          }
    o.defAff[0] = B[2]; o.defCar[0] = B[1];
    o.defAff[1] = F[2]; o.defCar[1] = F[1];
    o.defAff[2] = H[2]; o.defCar[2] = H[1];
    return o;
}

void TerminalUI::renderBreeding()
{
    werase(winInfo_);

    // Advance the overnight cycle: eggs APPEAR at 6 AM, HATCH at 6 PM. Any mon
    // that hatches is added to the roster by tick(); persist it to the box.
    long now = time(nullptr);
    {
        breeding::Rng rng((uint64_t)now ^ ((uint64_t)breedApp_.size() << 8));
        for (auto &e : breederMgr_.tick(breedApp_, now, rng)) {
            if (e.result.status == breeding::BREED_OK)
                pentestAppendBox(e.result.child);
            breedMsg_ = e.result.message;
        }
    }
    // Persist the breeder rooms whenever their state changed (place / egg / hatch
    // / cancel), so they survive the terminal relaunching between ROM opens.
    { uint64_t sig = breederRoomsSig(breederMgr_);
      if (sig != breederSig_) { pentestSaveRooms(); breederSig_ = sig; } }

    int rows = getmaxy(winInfo_);

    // ── Rooms browser (Y toggles): view/kick pairs + see this pair's child odds ─
    if (breedRoomsView_) {
        int rc = breederMgr_.roomCount();
        if (breedRoomSel_ < 0)   breedRoomSel_ = rc - 1;
        if (breedRoomSel_ >= rc) breedRoomSel_ = 0;
        wattron(winInfo_, A_BOLD);
        mvwprintw(winInfo_, 0, 1, "BREEDER ROOMS   [START: back to mons]");
        wattroff(winInfo_, A_BOLD);
        for (int i = 0; i < rc; ++i) {
            const breeding::BreederRoom &rm = breederMgr_.room(i);
            bool sel = (i == breedRoomSel_);
            if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
            if (rm.state == breeding::ROOM_EMPTY) {
                mvwprintw(winInfo_, 2 + i, 0, "%cRoom %d: (empty)", sel ? '>' : ' ', i + 1);
            } else {
                const char *na = rm.parentA.nick[0] ? rm.parentA.nick : breeding::BreedingApp::dexName(rm.parentA.dex);
                const char *nb = rm.parentB.nick[0] ? rm.parentB.nick : breeding::BreedingApp::dexName(rm.parentB.dex);
                mvwprintw(winInfo_, 2 + i, 0, "%cRoom %d: %.7s x %.7s  %s",
                          sel ? '>' : ' ', i + 1, na, nb,
                          rm.state == breeding::ROOM_EGG ? "EGG->6PM" : "->egg 6AM");
            }
            if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
        }
        const breeding::BreederRoom &srm = breederMgr_.room(breedRoomSel_);
        int ry = 3 + rc;
        if (srm.state != breeding::ROOM_EMPTY) {
            // Full genotype letters for each parent (all six loci).
            auto parentLine = [&](const breeding::BreedMon &m) {
                char code[24]; breeding::genoLetters(m.geno, code, sizeof(code));
                const char *nm = m.nick[0] ? m.nick : breeding::BreedingApp::dexName(m.dex);
                const char *sx = m.geno.female ? "\xE2\x99\x80" : "\xE2\x99\x82";
                if (ry < rows - 1)
                    mvwprintw(winInfo_, ry++, 0, "%.9s %s %s", nm, sx, code);
            };
            parentLine(srm.parentA);
            parentLine(srm.parentB);
            ChildOdds o = computeChildOdds(srm.parentA.geno, srm.parentB.geno);
            int order[12]; for (int i = 0; i < 12; i++) order[i] = i;
            std::sort(order, order + 12, [&](int x, int y) { return o.skin[x] > o.skin[y]; });
            if (ry < rows - 1) mvwprintw(winInfo_, ry++, 0, "Child colors:");
            std::string col;
            for (int k = 0; k < 12; k++) {
                int sk = order[k]; if (o.skin[sk] < 0.005) break;
                char c[28]; snprintf(c, sizeof(c), "%s %.0f%%  ",
                                     breeding::skinName((breeding::Skin)sk), o.skin[sk] * 100);
                if ((int)(col.size() + strlen(c)) > getmaxx(winInfo_) - 1 && ry < rows - 1) {
                    mvwprintw(winInfo_, ry++, 0, "%s", col.c_str()); col.clear();
                }
                col += c;
            }
            if (!col.empty() && ry < rows - 1) mvwprintw(winInfo_, ry++, 0, "%s", col.c_str());
            if (ry < rows - 1)
                mvwprintw(winInfo_, ry++, 0, "Hidden male carrier: pink %.0f%%  rnbw %.0f%%",
                          o.malePink * 100, o.maleRnbw * 100);
            if (ry < rows - 1)
                mvwprintw(winInfo_, ry++, 0, "Disorders: Ster %.0f%% Cant %.0f%% Hatch %.0f%%",
                          o.defAff[0] * 100, o.defAff[1] * 100, o.defAff[2] * 100);
        }
        mvwprintw(winInfo_, rows - 1, 1, "%.*s", getmaxx(winInfo_) - 2,
                  srm.state != breeding::ROOM_EMPTY ? "A: kick out   U/D: room   START: mons   B: back"
                                                    : "U/D: room   START: mons   B: back");
        return;
    }

    const auto &r = breedApp_.roster();

    if (r.empty()) {
        wattron(winInfo_, A_BOLD);
        mvwprintw(winInfo_, 0, 1, "Breeder Rooms  (0)");
        wattroff(winInfo_, A_BOLD);
        mvwprintw(winInfo_, 2, 1, "No caught mons yet.");
        mvwprintw(winInfo_, 3, 1, "Catch mons in the Pentest ROM first.");
        return;
    }

    // Per-category counts + build the filtered list for the current tab.
    int cnt[kBoxTabCount] = {0};
    for (const auto &m : r) {
        breeding::Skin s2 = breeding::skinOf(m.geno);
        for (int t = 0; t < kBoxTabCount; ++t) if (boxTabMatch(s2, t)) cnt[t]++;
    }
    if (breedTab_ < 0) breedTab_ = kBoxTabCount - 1;
    if (breedTab_ >= kBoxTabCount) breedTab_ = 0;
    std::vector<int> filt;
    for (int i = 0; i < (int)r.size(); ++i)
        if (boxTabMatch(breeding::skinOf(r[i].geno), breedTab_)) filt.push_back(i);
    int fn = (int)filt.size();
    if (breedCursorA_ < 0)  breedCursorA_ = fn ? fn - 1 : 0;
    if (breedCursorA_ >= fn) breedCursorA_ = 0;

    // Row 0: title. Row 1: category tab bar (L/R filters), current highlighted.
    wattron(winInfo_, A_BOLD);
    mvwprintw(winInfo_, 0, 1, "Breeder Rooms  \xE2\x97\x84 %s %d \xE2\x96\xB6%s",
              kBoxTabNames[breedTab_], fn,
              breedApp_.breedingUnlocked() ? "" : "  [locked]");
    wattroff(winInfo_, A_BOLD);
    { int cx = 1;
      for (int t = 0; t < kBoxTabCount; ++t) {
          char chip[16]; snprintf(chip, sizeof(chip), "%s%d", kBoxTabNames[t], cnt[t]);
          if (t == breedTab_) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
          mvwprintw(winInfo_, 1, cx, " %s ", chip);
          if (t == breedTab_) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
          cx += (int)strlen(chip) + 3;
      } }

    // Reserve the bottom rows for the 3 breeder rooms + a prompt. List starts row 2.
    int roomRows = breeding::NUM_BREEDER_ROOMS + 1;
    int listRows = rows - roomRows - 3;
    if (listRows < 1) listRows = 1;
    if (breedCursorA_ < breedScroll_)             breedScroll_ = breedCursorA_;
    if (breedCursorA_ >= breedScroll_ + listRows) breedScroll_ = breedCursorA_ - listRows + 1;
    if (breedScroll_ < 0) breedScroll_ = 0;

    for (int i = 0; i < listRows && (breedScroll_ + i) < fn; ++i) {
        int fi  = breedScroll_ + i;
        int idx = filt[fi];
        const breeding::BreedMon &m = r[idx];
        bool sel    = (fi == breedCursorA_);
        bool locked = (idx == breedCursorB_);
        bool busy   = breederMgr_.inAnyRoom(m.id);
        long cd     = breeding::BreederManager::cooldownRemaining(m, now);
        char mark   = locked ? '*' : (sel ? '>' : (busy ? '~' : ' '));
        breeding::Skin sk = breeding::skinOf(m.geno);
        if (sel) wattron(winInfo_, A_REVERSE | COLOR_PAIR(3));
        mvwprintw(winInfo_, 2 + i, 0, "%cL%-3d %-9.9s %-8.8s %s%s%s",
                  mark, m.level,
                  m.nick[0] ? m.nick : breeding::BreedingApp::dexName(m.dex),
                  breeding::skinName(sk),
                  m.geno.female ? "\xE2\x99\x80" : "\xE2\x99\x82",
                  breeding::isTritan(m.geno)
                    ? (m.geno.tritan == 2 ? " TT" : " Tn") : "",
                  breeding::isSterile(m.geno) ? " bb"
                    : busy ? " ~room"
                    : cd > 0 ? " ~cd" : "");
        if (sel) wattroff(winInfo_, A_REVERSE | COLOR_PAIR(3));
    }

    int ry = 2 + listRows;
    if (breedCursorB_ >= 0 && breedCursorB_ < (int)r.size() && fn > 0) {
        // ── Cross preview: child coloration + defect odds (mate1 x cursor) ──
        const breeding::BreedMon &mA = r[breedCursorB_];
        const breeding::BreedMon &mB = r[filt[breedCursorA_]];
        ChildOdds o = computeChildOdds(mA.geno, mB.geno);
        int order[12]; for (int i = 0; i < 12; i++) order[i] = i;
        std::sort(order, order + 12, [&](int x, int y) { return o.skin[x] > o.skin[y]; });
        mvwprintw(winInfo_, ry++, 0, "Child colors (%.5s x %.5s):",
                  mA.nick[0] ? mA.nick : breeding::BreedingApp::dexName(mA.dex),
                  mB.nick[0] ? mB.nick : breeding::BreedingApp::dexName(mB.dex));
        std::string colline;
        for (int k = 0; k < 12; k++) {
            int sk = order[k];
            if (o.skin[sk] < 0.005) break;                 // hide <0.5%
            char cell[28];
            snprintf(cell, sizeof(cell), "%s %.0f%%  ",
                     breeding::skinName((breeding::Skin)sk), o.skin[sk] * 100.0);
            if ((int)(colline.size() + strlen(cell)) > getmaxx(winInfo_) - 1) {
                if (ry < rows - 1) mvwprintw(winInfo_, ry++, 0, "%s", colline.c_str());
                colline.clear();
            }
            colline += cell;
        }
        if (!colline.empty() && ry < rows - 1)
            mvwprintw(winInfo_, ry++, 0, "%s", colline.c_str());
        if (ry < rows - 1)
            mvwprintw(winInfo_, ry++, 0,
                      "Hidden male carrier: pink %.0f%%  rnbw %.0f%%",
                      o.malePink * 100.0, o.maleRnbw * 100.0);
        if (ry < rows - 1)
            mvwprintw(winInfo_, ry++, 0,
                      "Disorders: Ster %.0f%% Cant %.0f%% Hatch %.0f%% (bb/ff/hh)",
                      o.defAff[0] * 100, o.defAff[1] * 100, o.defAff[2] * 100);
    } else {
        // Breeder-room status (one line per room, with egg/hatch clock times).
        auto lines = breederMgr_.statusLines(breedApp_, now);
        for (size_t i = 0; i < lines.size() && ry < rows - 1; ++i)
            mvwprintw(winInfo_, ry++, 0, "%.*s", getmaxx(winInfo_) - 1, lines[i].c_str());
    }

    const char *bottom = !breedMsg_.empty() ? breedMsg_.c_str()
                       : (breedCursorB_ >= 0 ? "A: 2nd mate  L/R: filter  START: rooms  B: cancel"
                                             : "A: mate 1  L/R: filter  START: rooms  B: back");
    mvwprintw(winInfo_, rows - 1, 1, "%.*s", getmaxx(winInfo_) - 2, bottom);
}

void TerminalUI::breedingButton(const ButtonEvent &ev)
{
    // ── Rooms browser mode (START/Y toggles) ────────────────────────────────
    if (breedRoomsView_) {
        int rc = breederMgr_.roomCount();
        switch (ev.button) {
            case GpiButton::START:
            case GpiButton::Y:    breedRoomsView_ = false; return;
            case GpiButton::UP:   breedRoomSel_ = (breedRoomSel_ + rc - 1) % rc; return;
            case GpiButton::DOWN: breedRoomSel_ = (breedRoomSel_ + 1) % rc; return;
            case GpiButton::A:
                if (breederMgr_.room(breedRoomSel_).state != breeding::ROOM_EMPTY) {
                    breederMgr_.cancel(breedRoomSel_);
                    pentestSaveRooms();
                    breederSig_ = breederRoomsSig(breederMgr_);
                    breedMsg_ = "Kicked them out of the room.";
                }
                return;
            case GpiButton::B:    screen_ = Screen::MENU; return;
            default:              return;
        }
    }

    const auto &r = breedApp_.roster();
    long now = time(nullptr);
    // Rebuild the current tab's filtered list (roster indices).
    std::vector<int> filt;
    for (int i = 0; i < (int)r.size(); ++i)
        if (boxTabMatch(breeding::skinOf(r[i].geno), breedTab_)) filt.push_back(i);
    int fn = (int)filt.size();

    switch (ev.button) {
        case GpiButton::LEFT:
            breedTab_ = (breedTab_ + kBoxTabCount - 1) % kBoxTabCount;
            breedCursorA_ = 0; breedScroll_ = 0; breedMsg_.clear();
            break;
        case GpiButton::RIGHT:
            breedTab_ = (breedTab_ + 1) % kBoxTabCount;
            breedCursorA_ = 0; breedScroll_ = 0; breedMsg_.clear();
            break;
        case GpiButton::UP:
            if (fn) breedCursorA_ = (breedCursorA_ + fn - 1) % fn;
            breedMsg_.clear();
            break;
        case GpiButton::DOWN:
            if (fn) breedCursorA_ = (breedCursorA_ + 1) % fn;
            breedMsg_.clear();
            break;
        case GpiButton::A: {
            if (fn == 0) break;
            int rosterIdx = filt[breedCursorA_ % fn];
            uint32_t mid = r[rosterIdx].id;
            // If this mon is already in a breeder room, A takes it OUT (cancels
            // the room; the 7-day cooldown it started still applies).
            if (breederMgr_.inAnyRoom(mid)) {
                for (int s = 0; s < breederMgr_.roomCount(); ++s) {
                    const auto &rm = breederMgr_.room(s);
                    if (rm.idA == mid || rm.idB == mid) { breederMgr_.cancel(s); break; }
                }
                breedMsg_ = "Took them out of the breeder room.";
                breedCursorB_ = -1;
                break;
            }
            if (breedCursorB_ < 0) {
                breedCursorB_ = rosterIdx;            // lock mate 1 (roster index)
                breedMsg_ = "Mate 1 set \xE2\x80\x94 filter/scroll to mate 2.";
            } else {
                std::string err;
                int room = breederMgr_.placePair(breedApp_, (size_t)breedCursorB_,
                                                 (size_t)rosterIdx, now, err);
                breedMsg_ = (room >= 0) ? "Paired! Egg at 6AM, hatches 6PM." : err;
                breedCursorB_ = -1;
            }
            break;
        }
        case GpiButton::START:
        case GpiButton::Y:
            breedRoomsView_ = true; breedRoomSel_ = 0; breedMsg_.clear();
            break;
        case GpiButton::B:
            if (breedCursorB_ >= 0) { breedCursorB_ = -1; breedMsg_ = "Cleared."; }
            else                     screen_ = Screen::MENU;
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
    engine_.start(party_, gymParty, seed, battleGen_);
    resetBattleParticipants();
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
    engine_.start(party_, e4Party, seed, battleGen_);
    resetBattleParticipants();
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

// Unsigned 32-bit variant — REQUIRED for node IDs (and other uint32 fields).
// Meshtastic node IDs routinely have the high bit set (e.g. 0xF1A05E70 =
// 4054163568 > INT_MAX), so parsing them through the signed jsonGetInt/atoi
// overflows to 0x7FFFFFFF and every challenge/lookup targets the wrong node.
uint32_t TerminalUI::jsonGetU32(const std::string &j, const char *key, uint32_t def) {
    std::string search = std::string("\"") + key + "\":";
    size_t pos = j.find(search);
    if (pos == std::string::npos) return def;
    pos += search.size();
    while (pos < j.size() && j[pos] == ' ') pos++;
    if (pos >= j.size()) return def;
    return (uint32_t)strtoul(j.c_str() + pos, nullptr, 10);
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
    s.foe.status  = em.status;
    s.foe.confused = (em.confuseTurns > 0);
    // Rare-coloured foe (pentest encounters roll a colour skin).
    s.foe.variant = (inPentestBattle_ || pentestStandby_) ? (uint8_t)pentestFoeVariant_ : 0;
    // Tritan foe: use the rolled wild genotype (pentest encounters only).
    s.foe.tritan  = (inPentestBattle_ || pentestStandby_) &&
                    breeding::isTritan(pentestFoeGeno_);
    snprintf(s.foe.nickname, sizeof(s.foe.nickname), "%s", em.nickname);

    s.you.species = pm.species;
    s.you.level   = pm.level;
    s.you.hp      = pm.hp;
    s.you.maxHp   = pm.maxHp;
    s.you.status  = pm.status;
    s.you.confused = (pm.confuseTurns > 0);
    s.you.variant = pentestActiveVariant_;  // locked to caught/bred colour
    s.you.tritan  = pentestActiveTritan_;   // active mon's tritanopia gene
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
            const char *who = (pendingTrainerIdx_ >= lordGymLeaderIndex(g))
                                ? g->leaderName
                                : g->trainers[pendingTrainerIdx_].name;
            snprintf(s.header, sizeof(s.header),
                     "%s Gym  Round %d/%u  vs %s",
                     g->city, round, (unsigned)g->trainerCount, who);
        }
    } else if (inE4Battle_) {
        const LordE4Member *m = lordE4Member(pendingE4Idx_);
        if (m)
            snprintf(s.header, sizeof(s.header),
                     "Indigo Plateau  %d/%d  %s %s",
                     pendingE4Idx_ + 1, LORD_E4_COUNT, m->title, m->name);
    } else if (inPentestBattle_) {
        if (!pentestBossMode_) {
            // Normal pentest mode: header + SSID tag always visible.
            snprintf(s.header, sizeof(s.header), "PENTEST PIKACHU");
            s.pentest = true;
            s.catchSeq     = pentestCatchSeq_;      // drives the Poke Ball animation
            s.catchOutcome = pentestCatchOutcome_;
            snprintf(s.foeTag, sizeof(s.foeTag), "%s", pentestSsid_);
            snprintf(s.foe.nickname, sizeof(s.foe.nickname), "%s", dexName(em.species));
            if (pentestShowStatus_ && !pentestSyncBoxView(s)) {
                s.menuMode = true;
                s.log.clear();
                pentestBuildMenuLog(s.log);
            }
        }
        // Boss mode: no pentest header/tag/overlay — looks like a regular battle.
    } else if (pentestStandby_) {
        // Standby: Pikachu waits for a target.  Sprites + HP boxes stay up;
        // the log box shows stats or the menu when A is pressed.
        snprintf(s.header, sizeof(s.header), "PENTEST PIKACHU");
        s.pentest = true;

        bool evolved = (pentestLevel_ >= 30);
        uint8_t pdex = evolved ? 26 : 25;
        uint8_t pmv[4]; pikaMovesForLevel(pentestLevel_, pmv);
        Gen1BattleEngine::BattlePoke tmp;
        Gen1BattleEngine::initBattlePokeFromBase(tmp, pdex, pentestLevel_, pmv);
        s.you.species = pdex;
        s.you.level   = pentestLevel_;
        s.you.maxHp   = tmp.maxHp;
        s.you.hp      = tmp.maxHp;
        s.you.variant = pentestActiveVariant_;  // locked to caught/bred colour
        s.you.tritan  = pentestActiveTritan_;   // active mon's tritanopia gene
        snprintf(s.you.nickname, sizeof(s.you.nickname), "%s",
                 evolved ? "RAICHU" : "PIKACHU");

        s.foe.species = 0; s.foe.level = 0; s.foe.hp = 0; s.foe.maxHp = 0;
        snprintf(s.foeTag,       sizeof(s.foeTag),       "%s", "SCANNING");
        snprintf(s.foe.nickname, sizeof(s.foe.nickname), "%s", "(no target)");

        uint32_t L = pentestLevel_;
        uint32_t delta = (L+1)*(L+1)*(L+1) - L*L*L;
        uint32_t have  = pentestXp_; if (have > delta) have = delta;
        s.expPermille = delta ? (int)(have * 1000 / delta) : 0;

        if (pentestShowStatus_ && pentestSyncBoxView(s)) {
            // Bill's PC browser owns the frame.
        } else if (pentestShowStatus_) {
            s.menuMode = true;
            s.log.clear();
            pentestBuildMenuLog(s.log);
        } else {
            s.log.clear();
            char b[96];
            int gyms = __builtin_popcount(pentestGymBeaten_);
            int seen = 0, beaten = 0;
            for (int i = 0; i < 151; i++) {
                if (pentestDex_[i >> 3]    & (1u << (i & 7))) seen++;
                if (pentestBeaten_[i >> 3] & (1u << (i & 7))) beaten++;
            }
            s.log.push_back("Scanning for a vulnerable network...");
            s.log.push_back("");
            snprintf(b, sizeof(b), "%s Lv%u    Gyms %d/8",
                     evolved ? "RAICHU" : "PIKACHU", (unsigned)pentestLevel_, gyms);
            s.log.push_back(b);
            snprintf(b, sizeof(b), "Pokedex  seen %d / beaten %d", seen, beaten);
            s.log.push_back(b);
            snprintf(b, sizeof(b), "Record   %u W - %u L",
                     (unsigned)pentestWins_, (unsigned)pentestLosses_);
            s.log.push_back(b);
            snprintf(b, sizeof(b), "WiFi  in range %d  /  cracked %d",
                     pentestNetsSeen_, (int)pentestDoneSsids_.size());
            s.log.push_back(b);
        }
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
