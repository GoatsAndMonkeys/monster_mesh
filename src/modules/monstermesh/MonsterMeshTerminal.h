// SPDX-License-Identifier: MIT
//
// MonsterMeshTerminal — minimal LVGL text terminal.
//
// First pass: command parser + scrolling output. Battle/SAV/network commands
// will land in subsequent iterations. The class owns its LVGL panel; the
// module just toggles open()/close() and forwards keypresses.

#pragma once

#include <Arduino.h>
#include "PokemonData.h"
#include "LordSave.h"

// Forward-declare LVGL types so the header doesn't need lvgl.h.
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

class MonsterMeshTerminal {
  public:
    MonsterMeshTerminal() = default;

    // Build the LVGL hierarchy under `parent`. Idempotent — second call
    // unhides the existing panel.
    void open(lv_obj_t *parent);
    // Hide the panel (does not destroy LVGL objects).
    void close();

    bool isOpen() const { return open_; }

    // Forward a typed character. 0x0D / '\n' submits the line. 0x08 deletes.
    void onKey(uint8_t key);

    // LV_EVENT_READY (Enter pressed in the textarea, e.g. via virtual keyboard
    // or hardware keyboard) hands us the full line in one call.
    void onSubmit(const char *line);

    // Push the player's party (read from emulator WRAM by the module on
    // terminal entry). Decoded nicknames live in party_.nicknames in Gen1
    // charset; we ASCII-decode them in the `party` command.
    void setParty(const Gen1Party &p);
    bool hasParty() const { return partyLoaded_; }
    const Gen1Party &getParty() const { return party_; }

    // Callback used to fill `buf` with the current daycare status (multiline,
    // newline-separated). Invoked when the user types `daycare` in the
    // terminal. Set by the module so the terminal stays decoupled from
    // PokemonDaycare's headers.
    typedef void (*DaycareStatusFn)(void *ctx, char *buf, size_t bufLen);
    void setDaycareStatusFn(DaycareStatusFn fn, void *ctx) {
        daycareStatusFn_ = fn;
        daycareStatusCtx_ = ctx;
    }

    // Optional force-event hook for the `daycare event` sub-command. Skipped
    // silently if not wired.
    typedef void (*DaycareForceEventFn)(void *ctx);
    void setDaycareForceEventFn(DaycareForceEventFn fn, void *ctx) {
        daycareForceFn_ = fn;
        daycareForceCtx_ = ctx;
    }

    // Hook for the `fight` command — kicks off a local CPU battle.
    typedef void (*FightFn)(void *ctx);
    void setFightFn(FightFn fn, void *ctx) {
        fightFn_ = fn;
        fightCtx_ = ctx;
    }

    // Hook for `gym fight <N>` — gymIdx is 0..7, trainerIdx is the per-gym
    // progress slot (0..4; 4 is the leader). Terminal supplies it from
    // lord_.gymProgress so the module doesn't need to know LordSave.
    typedef void (*GymFightFn)(void *ctx, uint8_t gymIdx, uint8_t trainerIdx);
    void setGymFightFn(GymFightFn fn, void *ctx) {
        gymFightFn_ = fn;
        gymFightCtx_ = ctx;
    }

    // Hook for `loc explore` — module spawns a wild CPU battle against a
    // route encounter pool selected by routeIdx (0..7).
    typedef void (*ExploreFn)(void *ctx, uint8_t routeIdx);
    void setExploreFn(ExploreFn fn, void *ctx) {
        exploreFn_ = fn;
        exploreCtx_ = ctx;
    }

    // Called by the module when an explore wild battle ends so the terminal
    // can update LordRunStats + persist + print a flavor line.
    void onExploreBattleEnded(uint8_t routeIdx, bool playerWon, uint8_t lvl);

    // Hook for `loc e4 fight` — module runs a 5-trainer Indigo Plateau
    // gauntlet (4 Elite Four + Champion). Member 0..4 (4 = Champion).
    typedef void (*E4FightFn)(void *ctx, uint8_t memberIdx);
    void setE4FightFn(E4FightFn fn, void *ctx) {
        e4FightFn_ = fn;
        e4FightCtx_ = ctx;
    }

    // Called by the module when the E4 gauntlet ends. On full clear sets
    // leagueCleared=1 in LordSave (NG+ unlock gate).
    void onE4BattleEnded(uint8_t memberIdx, bool playerWon);

    // Called by the module when a gym battle ends — terminal owns LordSave
    // so it advances gymProgress / awards the badge / persists.
    void onGymBattleEnded(uint8_t gymIdx, uint8_t trainerIdx, bool playerWon);

    // Called by the module when the deferred SAV-load finishes on the LoRa
    // thread. Wipes the existing scrollback and reprints the party block so
    // the user sees the freshly-loaded data without having to type "party".
    void refreshParty();

  private:
    void println(const char *s);
    void prompt();
    void executeLine(const char *line);
    void clearOutput();
    void showParty();  // print party listing or "no party" hint

    lv_obj_t *panel_   = nullptr;
    lv_obj_t *output_  = nullptr;  // scrolling lv_obj container of labels
    lv_obj_t *input_   = nullptr;  // single-line lv_textarea
    bool      open_    = false;

    // Pending input buffer (we accumulate keys and execute on enter).
    char inbuf_[128] = {};
    uint8_t inlen_   = 0;

    Gen1Party party_ = {};
    bool partyLoaded_ = false;

    // Legend of Charizard — persistent badge / explore-run / news state.
    // Loaded on first open() per session, saved on any badge change. The
    // Lord* class names are legacy ("Legend of the Red Dragon" ancestor);
    // user-facing strings say "Legend of Charizard".
    LordSave lord_ = {};
    bool     lordLoaded_ = false;
    // -1 = no pending rematch confirmation. Otherwise the gym index whose
    // confirmation prompt is awaiting a yes/no on the next typed line.
    int8_t   pendingRematchGym_ = -1;

    DaycareStatusFn daycareStatusFn_ = nullptr;
    void           *daycareStatusCtx_ = nullptr;
    DaycareForceEventFn daycareForceFn_ = nullptr;
    void               *daycareForceCtx_ = nullptr;
    FightFn             fightFn_       = nullptr;
    void               *fightCtx_      = nullptr;
    GymFightFn          gymFightFn_    = nullptr;
    void               *gymFightCtx_   = nullptr;
    ExploreFn           exploreFn_     = nullptr;
    void               *exploreCtx_    = nullptr;
    E4FightFn           e4FightFn_     = nullptr;
    void               *e4FightCtx_    = nullptr;
};
