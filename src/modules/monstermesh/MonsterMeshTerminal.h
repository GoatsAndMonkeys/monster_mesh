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
    // Re-apply theme colors to all child widgets. Module calls this from
    // the runOnce theme-change branch so the terminal re-skins along
    // with the rest of the UI when the user picks a new theme.
    void applyTheme();

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

    // True only when the terminal's input field is the LVGL-focused object.
    // The module's keystroke intercept uses this to know whether to route
    // ASCII keys to onKey() (terminal foregrounded) or fall through to LVGL
    // so the user can type in chat / DM panels that were navigated to
    // without closing the terminal. Safe to call from any thread; touches
    // LVGL state but reads only.
    bool hasInputFocus() const;

    // Remove input_ from the LVGL focus group + clear LV_STATE_FOCUSED
    // WITHOUT hiding the panel or losing scrollback. Used when entering
    // ROM loader: the terminal panel stays alive, but LVGL's focus moves
    // off our input field so chat/DM textareas can claim it on return.
    void yieldFocus();

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

    // `achievements` command renderer. Same shape as DaycareStatusFn —
    // fills `buf` with newline-separated lines.
    typedef void (*DaycareAchievementsFn)(void *ctx, char *buf, size_t bufLen);
    void setDaycareAchievementsFn(DaycareAchievementsFn fn, void *ctx) {
        daycareAchFn_  = fn;
        daycareAchCtx_ = ctx;
    }

    // `beacon` command — manually fire a daycare beacon broadcast. Lets
    // the user kick a peer's NodeDB / daycare neighbor list before a
    // multiplayer match instead of waiting for the periodic beacon.
    typedef void (*BeaconFn)(void *ctx);
    void setBeaconFn(BeaconFn fn, void *ctx) {
        beaconFn_ = fn;
        beaconCtx_ = ctx;
    }

    // `forget mmb` — removes all tracked MonsterMesh-capable nodes from
    // NodeDB so they re-announce with fresh public keys on next beacon.
    typedef void (*ForgetMmbFn)(void *ctx);
    void setForgetMmbFn(ForgetMmbFn fn, void *ctx) {
        forgetMmbFn_ = fn;
        forgetMmbCtx_ = ctx;
    }


    // `mmt` (no args) lists peers we've recently heard a daycare beacon
    // from. Module fills the buffer with newline-separated lines.
    // mmbOnly=true: only include peers seen within the last hour (for mmb).
    // mmbOnly=false: all known peers (for fight / hb).
    typedef void (*MmtListFn)(void *ctx, char *buf, size_t bufLen, bool mmbOnly);
    void setMmtListFn(MmtListFn fn, void *ctx) {
        mmtListFn_  = fn;
        mmtListCtx_ = ctx;
    }


    // Hook for the `fight` command — kicks off a local CPU battle (random peer).
    typedef void (*FightFn)(void *ctx);
    void setFightFn(FightFn fn, void *ctx) {
        fightFn_ = fn;
        fightCtx_ = ctx;
    }

    // Hook for `fight N` with a specific peer — shortName is 4-char null-term.
    // Module looks up that neighbor in daycare and uses their party as the CPU.
    typedef void (*FightByNameFn)(void *ctx, const char *shortName);
    void setFightByNameFn(FightByNameFn fn, void *ctx) {
        fightByNameFn_ = fn;
        fightByNameCtx_ = ctx;
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

    // Server-authoritative PvP entry: `mmb2 <short>`. Module resolves
    // the peer's short_name and calls textBattle_.startServerAuthAsInitiator
    // — single CHALLENGE packet carries our party so the receiver loads
    // the battle screen instantly on accept.
    typedef void (*Mmb2ChallengeFn)(void *ctx, const char *peerShortName);
    void setMmb2ChallengeFn(Mmb2ChallengeFn fn, void *ctx) {
        mmb2Fn_ = fn;
        mmb2Ctx_ = ctx;
    }

    // Hook for `loc e4 fight` — module runs a 5-trainer Indigo Plateau
    // gauntlet (4 Elite Four + Champion). Member 0..4 (4 = Champion).
    typedef void (*E4FightFn)(void *ctx, uint8_t memberIdx);
    void setE4FightFn(E4FightFn fn, void *ctx) {
        e4FightFn_ = fn;
        e4FightCtx_ = ctx;
    }

    // ── BBS gym discovery + fight (Phase C — multiplayer gym gauntlet) ─────
    // Hook for `bbs` (no args): module broadcasts a `!bbs ping` text-message
    // to all-nodes. Each running gauntlet node replies unicast with a GYM:
    // line that lands in onBbsReply(). Terminal collects for ~5s then prints.
    typedef void (*BbsProbeFn)(void *ctx);
    void setBbsProbeFn(BbsProbeFn fn, void *ctx) {
        bbsProbeFn_ = fn;
        bbsProbeCtx_ = ctx;
    }

    // Hook for `dungeon <verb> [arg]`: forward to DungeonGame::handleLocalCommand.
    typedef void (*DungeonFn)(void *ctx, const char *verb, const char *arg);
    void setDungeonFn(DungeonFn fn, void *ctx) {
        dungeonFn_ = fn;
        dungeonCtx_ = ctx;
    }

    // Hook for `bbs fight N`: terminal looks up the cached gym at slot N-1
    // and asks the module to start a networked Gen1 battle vs that node.
    // (Phase C-2: not yet implemented end-to-end; module-side hook prints
    // "not yet wired" until the receiver-side handshake lands.)
    typedef void (*BbsFightFn)(void *ctx, uint32_t gymNodeNum);
    void setBbsFightFn(BbsFightFn fn, void *ctx) {
        bbsFightFn_ = fn;
        bbsFightCtx_ = ctx;
    }

    // Called by the module when a `GYM:` reply arrives (parsed from a text
    // message). Adds the gym to the discovered-cache and prints a list line.
    void onBbsReply(uint32_t fromNodeNum, const char *gymName,
                    const char *badge, const char *leader, uint8_t roster);

    // Module checks this to decide whether to peek at text DMs for `GYM:`
    // replies. True for ~10 s after the last `bbs` probe.
    bool isBbsProbing() const;

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

    // Re-grab keyboard focus on the input textarea. Called by the module
    // after a battle ends so the user can keep typing without manually
    // tapping the textarea — the battle's lgfx render path stole focus.
    void refocus();

    // Append a single line to the terminal output. Public alias for the
    // internal println, used by the module to surface async status (mmt
    // resolve results, etc.) without leaking the LVGL widget access.
    void printLine(const char *s);

    // Credit per-slot battle XP: xp[i] goes directly to party_.mons[i]
    // (no internal splitting). Levels bump via the medium-fast curve
    // (exp = level^3); a "X grew to L" line prints per level-up.
    // Re-renders the party listing.
    void creditBattleXpPerSlot(const uint32_t xp[6]);

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
    // TinyBBS-style menu state: which bare-number menu is active.
    enum class MenuCmd : uint8_t { NONE, GYM, MMG, MMB, FIGHT };
    MenuCmd  pendingMenuCmd_ = MenuCmd::NONE;

    DaycareStatusFn daycareStatusFn_ = nullptr;
    void           *daycareStatusCtx_ = nullptr;
    DaycareForceEventFn daycareForceFn_ = nullptr;
    void               *daycareForceCtx_ = nullptr;
    DaycareAchievementsFn daycareAchFn_  = nullptr;
    void                 *daycareAchCtx_ = nullptr;
    MmtListFn           mmtListFn_      = nullptr;
    void               *mmtListCtx_     = nullptr;
    BeaconFn            beaconFn_       = nullptr;
    void               *beaconCtx_      = nullptr;
    ForgetMmbFn         forgetMmbFn_    = nullptr;
    void               *forgetMmbCtx_   = nullptr;
    FightFn             fightFn_       = nullptr;
    void               *fightCtx_      = nullptr;
    FightByNameFn       fightByNameFn_ = nullptr;
    void               *fightByNameCtx_ = nullptr;
    GymFightFn          gymFightFn_    = nullptr;
    void               *gymFightCtx_   = nullptr;
    ExploreFn           exploreFn_     = nullptr;
    void               *exploreCtx_    = nullptr;
    E4FightFn           e4FightFn_     = nullptr;
    void               *e4FightCtx_    = nullptr;
    Mmb2ChallengeFn     mmb2Fn_        = nullptr;
    void               *mmb2Ctx_       = nullptr;

    // ── BBS gym discovery (Phase C) ─────────────────────────────────────────
    // Small cache of gyms heard via the last `bbs` probe. Capacity is a
    // soft cap — we just print a "...+N more" if more replies come in.
    static constexpr uint8_t MAX_DISCOVERED_GYMS = 8;
    struct DiscoveredGym {
        uint32_t nodeNum;
        char     gymName[16];
        char     badgeName[16];
        char     leader[16];
        uint8_t  rosterSize;
    };
    DiscoveredGym discoveredGyms_[MAX_DISCOVERED_GYMS] = {};
    uint8_t       discoveredCount_ = 0;
    uint32_t      bbsLastProbeMs_  = 0;

    BbsProbeFn  bbsProbeFn_  = nullptr;
    void       *bbsProbeCtx_ = nullptr;
    BbsFightFn  bbsFightFn_  = nullptr;
    void       *bbsFightCtx_ = nullptr;
    DungeonFn   dungeonFn_   = nullptr;
    void       *dungeonCtx_  = nullptr;
};
