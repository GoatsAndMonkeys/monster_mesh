// SPDX-License-Identifier: MIT
//
// MonsterMeshTextBattle — turn-based Gen 1 battle UI + LoRa wire glue.
//
// Two modes:
//   - NETWORKED: both peers run Gen1BattleEngine with the same RNG seed and
//     exchange only TEXT_BATTLE_ACTION packets. State never crosses the wire.
//   - LOCAL:     single-player vs CPU (used by the roguelike crawler).
//
// Render path uses LovyanGFX directly (matching the lobby/emulator pattern in
// MonsterMeshModule). Keyboard input: 1-4 = use moves, S = switch menu,
// F = forfeit, ESC = exit.

#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "Gen1BattleEngine.h"
#include "BattlePacket.h"
#include "MeshtasticTransport.h"

class MonsterMeshTextBattle {
public:
    static constexpr uint8_t LOG_LINES = 6;
    static constexpr uint8_t LOG_WIDTH = 40;     // chars per line
    static constexpr uint16_t SCREEN_W = 320;
    static constexpr uint16_t SCREEN_H = 240;

    enum class Mode  : uint8_t { OFF, NETWORKED, LOCAL_ROGUELIKE };
    enum class Phase : uint8_t {
        IDLE,           // not in a battle
        WAIT_ACTION,    // waiting for player input
        WAIT_REMOTE,    // submitted; waiting for opponent's action
        WAIT_SWITCH,    // showing switch menu
        WAIT_FLEE,      // F pressed; "Flee? K=yes L=no" overlay
        ANIMATING,      // turn just resolved, scrolling messages
        FINISHED,       // result_ != ONGOING; press any key to exit
    };

    explicit MonsterMeshTextBattle(MeshtasticTransport &transport)
      : transport_(transport) {}

    // ── Lifecycle ───────────────────────────────────────────────────────────
    // Networked initiator. `myParty` is our current save's party; `remoteId`
    // is the peer node. We pick the seed and broadcast START.
    void startNetworkedAsInitiator(uint32_t remoteId, const Gen1Party &myParty);

    // Networked receiver. Called by handlePacket() on TEXT_BATTLE_START.
    void startNetworkedAsReceiver(uint32_t remoteId, const Gen1Party &myParty,
                                  uint32_t rngSeed);

    // Local roguelike battle. CPU runs side 1. trainerTags[2] are the short
    // names prepended to each pokemon's nickname so messages like
    // "MMRD-MEW used Tackle" make it obvious whose pokemon is acting in a
    // mirror match. Either tag may be empty for no prefix.
    void startLocal(const Gen1Party &myParty, const Gen1Party &cpuParty,
                    const char *ourTag = "", const char *theirTag = "");

    // Gym gauntlet: swap in a fresh opponent without healing our side.
    // Phase resets to WAIT_ACTION; engine keeps player HP/PP/status. Used
    // when a gym battle continues to the next trainer after a win.
    void nextOpponent(const Gen1Party &cpu, const char *theirTag);

    // Restore the player party to full HP/PP/status. Used between MMG gym
    // ladder fights — Pokemon Center semantics.
    void healPlayer();

    // Override the "Press any key to exit." prompt shown on the result
    // screen. Module sets this to "Press any key for next gym member."
    // when a gym-ladder fight ended in a win and another trainer is on
    // deck. Empty string falls back to the default exit text.
    void setEndPrompt(const char *txt) {
        if (!txt) txt = "";
        if (strncmp(endPromptOverride_, txt, sizeof(endPromptOverride_)) == 0) return;
        snprintf(endPromptOverride_, sizeof(endPromptOverride_), "%s", txt);
        dirty_ = true;
    }

    // Override the header line ("Roguelike T0" / "LoRa Battle T0"). Empty
    // string clears back to the auto-text. Module sets this for gym fights
    // so the user sees "Pewter Gym — Brock 5/5" instead of "Roguelike T..."
    void setHeader(const char *txt) {
        if (!txt) txt = "";
        if (strncmp(headerOverride_, txt, sizeof(headerOverride_)) == 0) return;
        snprintf(headerOverride_, sizeof(headerOverride_), "%s", txt);
        dirty_ = true;
    }

    void exit();

    bool isActive() const { return mode_ != Mode::OFF; }
    Mode mode()     const { return mode_; }
    Phase phase()   const { return phase_; }
    Gen1BattleEngine::Result engineResult() const { return engine_.result(); }

    // ── Input ───────────────────────────────────────────────────────────────
    void handleKey(uint8_t c);

    // ── Wire ────────────────────────────────────────────────────────────────
    // Returns true if it consumed the packet (caller should not process further).
    bool handlePacket(uint32_t fromId, const uint8_t *buf, size_t len);

    // ── Pump ────────────────────────────────────────────────────────────────
    void tick(uint32_t nowMs);

    // ── Render ──────────────────────────────────────────────────────────────
    void render(lgfx::LGFX_Device *gfx);

    // Was the screen dirtied since last render? (caller batches with spiLock.)
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

private:
    MeshtasticTransport &transport_;
    Gen1BattleEngine     engine_;

    Mode  mode_  = Mode::OFF;
    Phase phase_ = Phase::IDLE;

    uint32_t remoteId_ = 0;
    uint16_t session_  = 0;
    uint8_t  cursor_   = 0;          // selected move/party slot
    uint8_t  switchCursor_ = 0;
    bool     pendingRemoteAction_ = false;
    uint8_t  lastSentAction_ = 0xFF; // for retry
    uint8_t  lastSentIndex_  = 0;
    uint16_t lastSentTurn_   = 0;
    uint32_t lastSendMs_     = 0;
    uint8_t  fleeAttempts_   = 0;

    // ── Per-faint XP tracking ────────────────────────────────────────────
    // participantMask_ marks player slots that have been active during the
    // current opponent's turn on the field. When that opponent faints, XP
    // is split among the participants (mirrors Gen 1 EXP-share semantics
    // without an Exp.All item). lastEnemyHp_ snapshots side-1 HPs at the
    // top of resolveTurn so the wrapper can detect transitions to 0 after
    // executeTurn.
    uint8_t  participantMask_ = 0x01;
    uint8_t  lastPlayerActive_ = 0;
    uint8_t  lastEnemyActive_  = 0;
    uint16_t lastEnemyHp_[6]   = {};
    bool     isTrainerBattle_  = true;
    uint8_t  pendingXpDrops_   = 0;  // number of opponents fainted

    // Per-slot pending XP (drained by the module each runOnce tick into
    // creditBattleXpPerSlot, which writes exp directly to the saved
    // party member at that slot — no further splitting). Real Gen 1
    // semantics: only participants get XP, and they get the full per-
    // participant share calculated at faint time.
    uint32_t pendingXpPerSlot_[6] = {};

    // Per-slot in-battle XP for level-up gating. Each slot's counter
    // accumulates XP from defeated opponents; when it crosses the
    // (l+1)^3 - l^3 threshold the BattlePoke's level bumps and stats
    // scale up linearly, adding the HP delta to current HP as a
    // temporary heal. Resets to 0 at startLocal/startNetworked.
    uint32_t slotXpAccum_[6] = {};

    // Apply a +1 level bump to engine_.party(0).mons[slot]: scale
    // maxHp/atk/def/spd/spc linearly with newLevel/oldLevel, add the
    // maxHp delta to current hp (heal-on-level), bump level.
    void inBattleLevelUp(uint8_t slot);
public:
    // Drained each module tick. `out` is filled with per-slot XP earned
    // since the last drain; the LVGL thread credits each slot directly
    // to the saved party (no further splitting). Returns true if any
    // slot had a non-zero balance.
    bool consumePendingXp(uint32_t out[6]) {
        bool any = false;
        for (uint8_t i = 0; i < 6; ++i) {
            out[i] = pendingXpPerSlot_[i];
            if (out[i]) any = true;
            pendingXpPerSlot_[i] = 0;
        }
        pendingXpDrops_ = 0;
        return any;
    }
private:

    // Scrolling text log — circular buffer.
    char    log_[LOG_LINES][LOG_WIDTH + 1] = {};
    uint8_t logHead_ = 0;        // next write slot
    uint8_t logFill_ = 0;        // number of valid lines
    uint32_t scrollMs_ = 0;      // millis at which we can show the next line
    uint8_t  scrollPending_ = 0; // lines queued but not yet shown

    bool dirty_ = true;
    char headerOverride_[40] = {};
    char endPromptOverride_[48] = {};

    // Timeouts
    uint32_t lastRecvMs_ = 0;
    static constexpr uint32_t REMOTE_TIMEOUT_MS = 60000;   // 60s w/o packet → forfeit
    static constexpr uint32_t SCROLL_INTERVAL_MS = 600;    // text reveal cadence
    static constexpr uint32_t RESEND_INTERVAL_MS = 4000;   // re-broadcast our action

    // Local→engine helpers
    void appendLog(const char *line);
    static void engineLogCb(const char *line, void *ctx);

    void sendStart(uint32_t rngSeed, const Gen1Party &myParty);
    void sendAction(uint8_t actionType, uint8_t index);
    void sendForfeit();
    void sendHash();

    // After both sides submitted, run executeTurn and prep next phase.
    void resolveTurn();

    // Auto-replace a fainted active mon at the start of each input phase.
    void handleFaints();

    // Render helpers
    void drawHpPanel(lgfx::LGFX_Device *g, uint8_t side, int y);
    void drawMoveMenu(lgfx::LGFX_Device *g);
    void drawSwitchMenu(lgfx::LGFX_Device *g);
    void drawLog(lgfx::LGFX_Device *g);
    void drawHeader(lgfx::LGFX_Device *g);
};
