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

    // Local roguelike battle. CPU runs side 1.
    void startLocal(const Gen1Party &myParty, const Gen1Party &cpuParty);

    void exit();

    bool isActive() const { return mode_ != Mode::OFF; }
    Mode mode()     const { return mode_; }
    Phase phase()   const { return phase_; }

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

    // Scrolling text log — circular buffer.
    char    log_[LOG_LINES][LOG_WIDTH + 1] = {};
    uint8_t logHead_ = 0;        // next write slot
    uint8_t logFill_ = 0;        // number of valid lines
    uint32_t scrollMs_ = 0;      // millis at which we can show the next line
    uint8_t  scrollPending_ = 0; // lines queued but not yet shown

    bool dirty_ = true;

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
