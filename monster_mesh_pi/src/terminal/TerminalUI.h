#pragma once
#include "../shared/platform.h"
#include "../shared/PokemonData.h"
#include "../shared/DaycareTypes.h"
#include "../shared/IpcProtocol.h"
#include "../shared/LordSave.h"
#include "../shared/LordGyms.h"
#include "../battle/Gen1BattleEngine.h"
#include "IpcClient.h"
#include "InputHandler.h"
#include "BattleWindow.h"
#include <string>
#include <vector>
#include <ncurses.h>

// ── Screen / mode enum ────────────────────────────────────────────────────────

enum class Screen {
    MENU,           // 3-tab menu (default)
    PARTY,          // party detail view
    NEIGHBORS,      // mesh neighbor list
    DAYCARE_EVENT,  // last daycare event detail
    BATTLE,         // local Gen1 battle (vs CPU)
    BATTLE_END,     // battle result, press A to return
    GYM_SELECT,     // gym list with badge status
    PVP_BATTLE,     // networked battle vs T-Deck (state driven by daemon UPDATEs)
    PVP_BATTLE_END, // networked battle result
    HELP,           // help overlay
    CONFIRM_QUIT,   // A=quit, B=cancel
    CHALLENGE,      // incoming PvP challenge: accept/decline
};

// ── Menu layout (3 tabs) ──────────────────────────────────────────────────────

static constexpr int TAB_COUNT = 3;
static constexpr const char *TAB_NAMES[TAB_COUNT] = { "MESH", "LOCAL", "SYSTEM" };

static constexpr const char *MESH_ITEMS[]  = { "Beacon", "Neighbors", "Daycare" };
static constexpr const char *MESH_DESC[]   = {
    "Broadcast your party to the mesh",
    "See nearby trainers  [K=challenge]",
    "Last daycare event + XP",
};
static constexpr int MESH_COUNT = 3;

static constexpr const char *LOCAL_ITEMS[] = { "Party", "Fight", "Gyms" };
static constexpr const char *LOCAL_DESC[]  = {
    "View your current Pokemon party",
    "Battle a local CPU opponent",
    "Legend of Charizard gym battles",
};
static constexpr int LOCAL_COUNT = 3;

static constexpr const char *SYSTEM_ITEMS[] = { "Help", "Quit" };
static constexpr const char *SYSTEM_DESC[]  = {
    "Show button controls",
    "Return to RetroPie",
};
static constexpr int SYSTEM_COUNT = 2;

// ── TerminalUI ────────────────────────────────────────────────────────────────

class TerminalUI {
public:
    TerminalUI();
    ~TerminalUI();

    bool init();
    void run();      // blocking loop
    void shutdown();

    bool shouldQuit()  const { return shouldQuit_; }
    void requestQuit()      { shouldQuit_ = true; }

    // "Pentest Pikachu" ROM mode: when launched as a separate ROM in the
    // RetroPie MonsterMesh system, mmterm boots straight into a battle screen
    // (no menu) styled after the T114 pentest battle.  Set before run().
    void setPentestMode(bool b) { pentestMode_ = b; }

private:
    // ── Subsystems ────────────────────────────────────────────────────────────
    IpcClient    ipc_;
    InputHandler input_;
    BattleWindow battleWindow_;   // SDL2 graphical battle screen (battle-only)

    // Build a BattleWindow::State snapshot from the current local/PvP battle
    // state and push it into battleWindow_.  Opens the window on first call
    // for the active battle.  Called from renderBattle / renderPvpBattle.
    void syncBattleWindow();
    void syncPvpBattleWindow();

    // ── ncurses windows ───────────────────────────────────────────────────────
    WINDOW *winStatus_ = nullptr;  // row 0: status bar
    WINDOW *winInfo_   = nullptr;  // rows 1..(rows_-MENU_ROWS-1): info / detail panel
    WINDOW *winMenu_   = nullptr;  // bottom MENU_ROWS rows: tabbed menu
    int rows_ = 24, cols_ = 40;
    // The bottom menu region resizes per screen.  Battle screens want a
    // compact FIGHT box (5 rows) so the info panel above can fit the
    // sprites and message log.  The 3-tab menu screens need ~10 rows so
    // five item rows can fit between the tab header, separator, and hint.
    static constexpr int MENU_ROWS_BATTLE  = 5;
    static constexpr int MENU_ROWS_DEFAULT = 8;   // 3 item rows + 5 chrome rows

    // GBC DMG green-scale color slots.  These indices are written into the
    // terminal's color palette via init_color() in startup() — anything
    // numbered 16+ is safe (16-color terminals reserve 0..15).  Per-species
    // sprite palettes occupy 32+.
    static constexpr int GB_PAPER_ = 16;
    static constexpr int GB_LIGHT_ = 17;
    static constexpr int GB_DARK_  = 18;
    static constexpr int GB_INK_   = 19;
    bool gbColorsActive_ = false;   // true if init_color() succeeded
    int menuRows_ = MENU_ROWS_DEFAULT;
    static constexpr int STATUS_ROWS  = 1;
    void applyMenuRowsForScreen();   // resize winInfo_/winMenu_ as screen_ changes

    // ── Navigation state ──────────────────────────────────────────────────────
    Screen screen_     = Screen::MENU;
    int    activeTab_  = 0;   // 0=MESH 1=LOCAL 2=SYSTEM
    int    activeItem_ = 0;   // highlighted row within active tab
    bool   shouldQuit_ = false;

    // ── Cached data from daemon ───────────────────────────────────────────────
    // Party display cache - populated from PARTY_UPDATE JSON
    struct PartySlot {
        uint8_t  dex      = 0;
        uint8_t  level    = 0;
        uint8_t  savLevel = 0;
        char     nick[12] = {};
        char     name[12] = {};
        uint32_t xpGained = 0;
        uint32_t hours    = 0;
        uint8_t  moves[4] = {};
        // Real DVs + stat experience pulled from the SAV's Gen1Pokemon
        // struct. Without these the battle engine builds fake stats from
        // base + avg DVs + 0 stat exp, which makes Recover (and every
        // stat in general) feel weak for a trained team.
        uint8_t  dvs[2]   = {0x88, 0x88};
        uint16_t statExp[5] = {};  // hp, atk, def, spd, spc
    };
    PartySlot   partySlots_[6] = {};
    int         partyCount_   = 0;
    bool        hasParty_     = false;
    Gen1Party   party_        = {};   // kept for local battle engine only
    int         neighborCount_ = 0;
    bool        daycareActive_ = false;
    std::string lastEventText_;

    // Local-node identity, populated from the daemon's NODE_INFO push and
    // shown in the status bar.  shortName is the 4-char Meshtastic radio
    // name; trainerName is the SAV trainer (Gen 1 offset 0x2598).  Empty
    // strings render as "—" until the daemon supplies them.
    char        localShortName_[5]    = "";
    char        localTrainerName_[12] = "";
    int         lastEventXp_  = 0;
    int         lastEventSlot_ = 0;

    struct NeighborEntry {
        uint32_t nodeId;
        char     shortName[5];
        char     gameName[8];
        int      partyCount;
        char     lead[11];   // lead pokemon nickname
        int      leadLevel;
        uint64_t firstSeenMs;  // millis() when first appeared this session
    };
    // Highlight new neighbors for this long after first appearance.
    static constexpr uint64_t NEW_NEIGHBOR_HIGHLIGHT_MS = 30000;

    // Async-fight context: when the user challenges a neighbor we cache
    // their identity so the result message after the battle has names.
    bool       asyncFightActive_   = false;
    uint32_t   asyncFightNodeId_   = 0;
    char       asyncFightTrainer_[13] = {};
    static constexpr int MAX_NEIGHBORS_DISPLAY = 16;
    NeighborEntry neighbors_[MAX_NEIGHBORS_DISPLAY] = {};
    int           neighborDisplayCount_ = 0;
    int           neighborSel_          = 0;  // highlighted neighbor

    // XP summary per party slot (accumulated from DAYCARE_EVENT pushes
    // and battle kills).  sessionXp_ is monotonic — total XP gained this
    // session.  slotLevelXp_ resets on each in-battle level-up and tracks
    // progress toward the next one, so leveling works the same on a Lv7
    // Mewtwo as on a Lv70 one.
    uint32_t sessionXp_[6]    = {};
    uint32_t slotLevelXp_[6]  = {};

    // Activity log - scrolling feed above the menu (party loads, beacons,
    // events, achievements). Newest at end. Capped to 64 entries.
    std::vector<std::string> activityLog_;
    void pushActivity(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

    // Pending PvP challenge
    bool     hasPendingChallenge_ = false;
    uint32_t challengeNodeId_     = 0;
    char     challengerName_[13]  = {};
    int      challengeSel_        = 0;  // 0=Accept 1=Decline

    // Active PvP battle state (daemon-driven UPDATEs)
    uint16_t pvpMyHp_       = 0;
    uint16_t pvpMyMaxHp_    = 0;
    uint8_t  pvpMyPp_[4]    = {};
    uint8_t  pvpMyStatus_   = 0;
    uint16_t pvpEnemyHp_    = 0;
    uint16_t pvpEnemyMaxHp_ = 0;
    uint8_t  pvpEnemyStatus_= 0;
    uint8_t  pvpTurn_       = 0;
    uint8_t  pvpResult_     = 0;   // 0=ongoing,1=win,2=lose,3=draw
    bool     pvpNeedSwitch_ = false;
    char     pvpEnemyName_[13] = {};
    std::vector<std::string> pvpLog_;
    int      pvpMoveSel_    = 0;

    // ── Battle state ──────────────────────────────────────────────────────────
    Gen1BattleEngine engine_;
    bool        inBattle_     = false;
    uint8_t     localSide_    = 0;   // 0 = player is P0 (always for local battles)
    bool        roguelike_    = false;
    int         moveSel_      = 0;   // highlighted move slot (0-3)
    bool        switchMode_   = false;
    int         switchSel_    = 0;
    std::vector<std::string> battleLog_;
    static constexpr int BATTLE_LOG_LINES = 4;
    Gen1BattleEngine::Result battleResult_ = Gen1BattleEngine::Result::ONGOING;

    // ── Gym state ─────────────────────────────────────────────────────────────
    LordSave    lordSave_          = {};
    bool        lordLoaded_        = false;
    int         gymSel_            = 0;   // highlighted gym (0..LORD_GYM_COUNT;
                                          // index LORD_GYM_COUNT = Indigo Plateau)
    uint8_t     pendingGymIdx_     = 0;
    uint8_t     pendingTrainerIdx_ = 0;
    bool        inGymBattle_       = false;
    // Indigo Plateau (Elite Four + Champion) gauntlet state.  inE4Battle_ runs
    // the 5-member chain (no healing between); pendingE4Idx_ is 0..4.
    bool        inE4Battle_        = false;
    uint8_t     pendingE4Idx_      = 0;
    // Selectable rows on the Gyms screen = 8 gyms + the league entry.
    static constexpr int GYM_ROWS  = LORD_GYM_COUNT + 1;
    static constexpr int LEAGUE_ROW = LORD_GYM_COUNT;   // index of Indigo Plateau

    // ── Info panel scroll (for party / neighbor / event detail views) ─────────
    int infoScroll_ = 0;

    // ── Reconnect throttle ────────────────────────────────────────────────────
    uint32_t lastConnectMs_ = 0;
    static constexpr uint32_t RECONNECT_MS = 2000;

    // Wall-clock start so the activity feed can show t+Xm in session-relative
    // time instead of CLOCK_MONOTONIC since boot (which on a Mac that's been
    // up for weeks reads as "[t+59329m]").
    uint32_t startMs_ = 0;

    // ── Rendering ────────────────────────────────────────────────────────────
    void render();
    void renderStatusBar();
    void renderMenu();
    void renderInfoPanel();
    void renderParty();
    void renderNeighbors();
    void renderDaycareEvent();
    void renderHelp();
    void renderConfirmQuit();
    void renderChallenge();
    void renderBattle();
    void renderBattleEnd();
    void renderPvpBattle();
    void renderPvpBattleEnd();
    void drawHpBar(WINDOW *w, int y, int x, int width,
                   uint16_t hp, uint16_t maxHp, const char *label);
    void clearInfo();

    // Gen 1 GB-style battle screen helpers.  drawBox() paints an ncurses
    // border with an optional title chip in the top edge.  drawStatusBox()
    // fills the contents with a name + level + inline HP bar + numeric HP.
    void drawBox(WINDOW *w, int y, int x, int h, int w_, const char *title);
    void drawStatusBox(WINDOW *w, int y, int x, int box_w, bool isFoe,
                       const Gen1BattleEngine::BattlePoke &mon);

    // ── Input ────────────────────────────────────────────────────────────────
    void handleButton(const ButtonEvent &ev);
    void menuButton(const ButtonEvent &ev);
    void partyButton(const ButtonEvent &ev);
    void neighborsButton(const ButtonEvent &ev);
    void daycareEventButton(const ButtonEvent &ev);
    void challengeButton(const ButtonEvent &ev);
    void battleButton(const ButtonEvent &ev);
    void battleHandleButton(const ButtonEvent &ev);
    void battleEndButton(const ButtonEvent &ev);
    void pvpBattleButton(const ButtonEvent &ev);
    void pvpBattleEndButton(const ButtonEvent &ev);
    void helpButton(const ButtonEvent &ev);
    void confirmQuitButton(const ButtonEvent &ev);

    // ── Menu actions (called when user presses A on an item) ─────────────────
    void activateItem(int item);
    void activateMeshItem(int item);
    void activateLocalItem(int item);
    void activateSystemItem(int item);

    // Build the player's Gen1Party (`party_`) from partySlots_ using the
    // real moves the SAV file provided.  Used by both startLocalBattle()
    // and startGymBattle() so we share one source of truth.
    void buildPlayerPartyForBattle();

    // Run engine.executeTurn + autoReplaceIfFainted and award XP for any
    // enemy mons that died this turn.  Caller picks moves and submits
    // actions; this just wraps the turn-resolution + result update.
    void runTurnWithXp();

    // After a battle ends, push every slot's sessionXp_ to the daemon as
    // CREDIT_XP commands and then ask it to writeback to the SAV file.
    // Resets sessionXp_ so the next run starts from zero.
    void flushBattleXpToDaemon();
    int         tabItemCount(int tab) const;
    const char **tabItems(int tab) const;
    const char **tabDescs(int tab) const;

    // ── Battle helpers ────────────────────────────────────────────────────────
    void startLocalBattle();
    void startRoguelike();
    // Pentest Pikachu ROM: build a self-contained Pikachu-vs-wild battle and
    // jump straight to the battle screen (independent of the SAV party).
    void startPentestBattle();
    void pentestAutoTick();          // auto-battle driver (moves play themselves)
    bool pentestMode_      = false;  // launched as the pentest ROM
    bool inPentestBattle_  = false;  // currently in a pentest-styled battle
    bool pentestStarted_   = false;  // one-shot guard for the boot battle
    uint64_t lastPentestTurnMs_ = 0; // throttle for auto turns
    uint64_t pentestEndMs_      = 0; // when the current fight ended (0 = ongoing)
    // The pentest Pikachu progresses through the Kanto zones as it levels — it
    // starts at L5 and is persisted to its own file (NOT the player's SAV) so
    // its level / XP are remembered across ROM relaunches.
    uint8_t  pentestLevel_      = 5;
    uint32_t pentestXp_         = 0;  // partial XP toward the next level
    bool     pentestLoaded_     = false;
    bool     pentestShowStatus_ = false;  // status overlay toggled with A
    uint8_t  pentestDex_[19]    = {};     // 151-bit "seen" Pokedex (bitset)
    uint8_t  pentestBeaten_[19] = {};     // 151-bit "beaten" Pokedex (bitset)
    void pentestLoadProgress();       // read level/xp/dex from disk (default L5)
    void pentestSaveProgress();       // write level/xp/dex to disk
    void pentestButton(const ButtonEvent &ev);  // input while in a pentest scan
    void pentestMarkSeen(uint8_t dex);          // flag a species as encountered
    void pentestMarkBeaten(uint8_t dex);        // flag a species as defeated
    void pentestBuildStatus(std::vector<std::string> &out);  // status overlay text
    static constexpr uint64_t PENTEST_TURN_MS = 900;
    static constexpr uint64_t PENTEST_END_MS  = 2500;  // victory linger before next
    char pentestSsid_[24] = {};      // WiFi target name shown in the fight
    char pentestVuln_[40] = {};      // vulnerability detail shown in the fight
    char pentestZone_[24] = {};      // current Kanto zone/area name
    void battleSubmitPlayerAction();
    void battleRunCpuAndExecute();
    static void battleLogSinkStatic(const char *line, void *ctx);
    void        battleLogSink(const char *line);
    uint8_t     avgPartyLevel() const;

    // ── Gym helpers ───────────────────────────────────────────────────────────
    void renderGymSelect();
    void gymSelectButton(const ButtonEvent &ev);
    void startGymBattle(uint8_t gymIdx, uint8_t trainerIdx);
    // Start the Indigo Plateau gauntlet at Elite Four member `e4Idx` (0..4).
    void startE4Battle(uint8_t e4Idx);
    void loadLordSave();

    // ── IPC ──────────────────────────────────────────────────────────────────
    void onIpcMessage(const std::string &msg);
    void parsePartyUpdate(const std::string &msg);
    void parseDaycareEvent(const std::string &msg);
    void parseChallenge(const std::string &msg);
    void parseAchievement(const std::string &msg);
    void parseStatus(const std::string &msg);
    void parseNeighbors(const std::string &msg);
    void parseNeighborParty(const std::string &msg);
    void parseBattleUpdate(const std::string &msg);

    // ── Tiny JSON helpers (no library - strstr-based) ─────────────────────────
    static std::string jsonGetStr(const std::string &j, const char *key);
    static int         jsonGetInt(const std::string &j, const char *key, int def = 0);

};
