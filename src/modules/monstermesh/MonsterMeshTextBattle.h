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
    // Server-authoritative PvP role. LEGACY = the old lockstep dual-engine
    // path (Mode::NETWORKED + sendHash/HASH abort). SERVER = initiator runs
    // the only engine and ships UPDATE packets. CLIENT = receiver renders
    // state from UPDATE packets; runs no engine.
    enum class Role  : uint8_t { LEGACY, SERVER, CLIENT };
    enum class Phase : uint8_t {
        IDLE,           // not in a battle
        WAIT_ACTION,    // waiting for player input
        WAIT_REMOTE,    // submitted; waiting for opponent's action
        WAIT_SWITCH,    // showing switch menu
        WAIT_FLEE,      // F pressed; "Flee? K=yes L=no" overlay
        ANIMATING,      // turn just resolved, scrolling messages
        FINISHED,       // result_ != ONGOING; press any key to exit
        WAIT_CHALLENGE_OVERLAY,  // server-auth CLIENT: "X challenges you! K=accept L=decline"
        WAIT_PEER_READY, // legacy initiator: holding until receiver sends TEXT_BATTLE_READY
    };

    explicit MonsterMeshTextBattle(MeshtasticTransport &transport)
      : transport_(transport) {}

    // Must be called once at init so incoming CHALLENGEs can be filtered to
    // only those addressed to this node.
    void setMyNodeNum(uint32_t n) { myNodeNum_ = n; }

    // ── Lifecycle ───────────────────────────────────────────────────────────
    // ── Server-authoritative PvP entry point ──────────────────────────────
    // Initiator's only call. Stages our party, generates a session id, and
    // broadcasts CHALLENGE. Battle proper starts when ACCEPT arrives (in
    // handlePacket → onServerAuthAcceptPkt). Returns immediately.
    //
    // `myName` is the trainer name shown in the opponent's "X challenges
    // you!" overlay (truncated to TB_MAX_NAME_LEN).
    void startServerAuthAsInitiator(uint32_t remoteId,
                                    const Gen1Party &myParty,
                                    const char *myName);

    // Pre-stage our party + trainer name so the CLIENT path can respond to
    // an inbound CHALLENGE without an additional round-trip. Module calls
    // this whenever the loaded SAV's party changes. SAFE to call any time;
    // overwrites the previous staging.
    void setMyTbParty(const Gen1Party &p, const char *name) {
        pendingMyParty_ = p;
        haveWireParty_  = false;   // wire form (re)derived from this at send time
        myTbName_[0] = '\0';
        if (name && *name) {
            snprintf(myTbName_, sizeof(myTbName_), "%.*s",
                     (int)TB_MAX_NAME_LEN, name);
        }
    }

    // Cross-gen staging: stage a ready-made neutral WireParty (from a Gen 2/3
    // save via CrossGenSavReader) instead of a Gen1Party. Takes precedence
    // over setMyTbParty until the next setMyTbParty call.
    void setMyTbWireParty(const Gen1BattleEngine::WireParty &p, const char *name) {
        wireMy_        = p;
        haveWireParty_ = true;
        myTbName_[0] = '\0';
        if (name && *name) {
            snprintf(myTbName_, sizeof(myTbName_), "%.*s",
                     (int)TB_MAX_NAME_LEN, name);
        }
    }

    // Role of the current battle (LEGACY by default; SERVER/CLIENT only for
    // server-authoritative path).
    Role role() const { return role_; }

    // SERVER role only: true between CHALLENGE send and ACCEPT receive.
    // Module uses this to gate the screen takeover so we don't blank the
    // user's terminal panel while the challenge is still in flight.
    bool awaitingAccept() const { return awaitingAccept_; }

    // CLIENT role only: true while waiting for a FULL_STATE resync.
    bool clientNeedsFullState() const { return clientNeedsFullState_; }

    // Timestamp (millis) of the last received battle packet; 0 if none yet.
    uint32_t lastRecvMs() const { return lastRecvMs_; }

    // Networked initiator. `myParty` is our current save's party; `oppParty`
    // is the peer's party (reconstructed from their daycare beacon — both
    // sides build the same party locally from the same beacon data so we
    // don't need to push it over the wire). `remoteId` is the peer node.
    // We pick the seed and broadcast START. If oppParty.count == 0 the
    // engine falls back to a mirror-match against myParty so the battle
    // still starts (matches pre-restart behavior).
    // If existingSeed != 0, use it and skip the internal sendStart (caller
    // already broadcast TEXT_BATTLE_START with the same seed). Otherwise
    // a seed is generated and the engine emits its own start packet.
    // existingSession non-zero: set engine session_ to that value (must
    // match the session_id the caller used in the START packet so the
    // receiver's ACTION/HASH/FORFEIT packets pass our session check).
    void startNetworkedAsInitiator(uint32_t remoteId, const Gen1Party &myParty,
                                   const Gen1Party &oppParty,
                                   uint32_t existingSeed = 0,
                                   uint16_t existingSession = 0);

    // Networked receiver. Called by handlePacket() on TEXT_BATTLE_START.
    // existingSession is the session_id captured from the START packet —
    // must be passed so our outgoing ACTION packets match the initiator's
    // session check.
    void startNetworkedAsReceiver(uint32_t remoteId, const Gen1Party &myParty,
                                  uint32_t rngSeed,
                                  const Gen1Party &oppParty,
                                  uint16_t existingSession = 0);

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
    // True if the local player won the last battle.
    bool playerWon() const { return playerWon_; }
    // Short tag used for the opponent panel header (trainer name or "FOE").
    const char *oppTag() const { return oppTag_[0] ? oppTag_ : "FOE"; }
    void setOppTag(const char *tag) {
        strncpy(oppTag_, tag ? tag : "", sizeof(oppTag_) - 1);
        oppTag_[sizeof(oppTag_) - 1] = '\0';
    }
    // Call after startNetworkedAsInitiator when the peer has already proved
    // liveness (e.g. server-auth: CLIENT sent ACCEPT before engine started).
    // Clears WAIT_PEER_READY so the first move is not silently dropped.
    void markPeerReady() {
        peerReady_ = true;
        if (phase_ == Phase::WAIT_PEER_READY)
            phase_ = Phase::WAIT_ACTION;
    }

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
    void setDirty()   { dirty_ = true; }

    // ── LVGL render accessors ──────────────────────────────────────────
    // For the upcoming LVGL-widget battle screen (replacing the lgfx-
    // direct render). Module reads engine/log state via these getters
    // and pushes to LVGL widgets, so LVGL stays in charge of the screen
    // and no Meshtastic-UI bleed-through is possible.
    const Gen1BattleEngine &engine() const { return engine_; }
    uint8_t cursorIdx() const { return cursor_; }
    uint8_t switchCursor() const { return switchCursor_; }
    Phase   currentPhase() const { return phase_; }
    const char *peerName() const { return peerTbName_; }
    const char *myName()   const { return myTbName_; }
    uint32_t remoteId() const { return remoteId_; }
    // Copy up to maxLines most-recent log lines into `out` (newline-
    // separated). Returns number of lines written.
    uint8_t getRecentLog(char *out, size_t outCap, uint8_t maxLines) const;

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
    // One-turn history of our last submitted action, used to catch up a
    // peer who's stuck waiting on us. If peer ACTION lands with a turn
    // we've already passed, we re-broadcast our prevSent* for THAT turn
    // (not the current one) so peer can advance. Without this, asymmetric
    // packet loss locks one side at "Waiting for opponent..." until the
    // 60s forfeit timer fires.
    uint8_t  prevSentAction_ = 0xFF;
    uint8_t  prevSentIndex_  = 0;
    uint16_t prevSentTurn_   = 0;

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

    // One-time result-based XP bonus for mesh (NETWORKED/PvP) battles so both
    // sides earn regardless of which device ran the engine. base =
    // opponentAvgLevel * (won ? 6 : 2); added to each surviving Gen-1 party
    // slot's pendingXpPerSlot_ so it flows through the existing consumePendingXp
    // → creditBattleXpPerSlot → SD-sav writeback path. Fires once per battle
    // (resultXpAwarded_), and only for NETWORKED mode.
    void awardMeshResultXp();
    bool resultXpAwarded_ = false;
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

    uint32_t myNodeNum_ = 0;   // set via setMyNodeNum(); used to filter CHALLENGEs

    // Timeouts
    uint32_t lastRecvMs_ = 0;
    // Peer-ready handshake: initiator waits for any packet from the remote
    // peer before unblocking move submission. Without this gate, sending a
    // move at engine.turn()=0 while the receiver was still loading its
    // battle station made the receiver miss the START race and the engines
    // desynced from turn 0.
    bool     peerReady_          = false;
    bool     playerWon_          = false;
    char     oppTag_[8]          = {};    // trainer name shown in foe header
    uint32_t peerReadyTimeoutMs_ = 0;   // fallback: unblock WAIT_PEER_READY after 10s
    static constexpr uint32_t REMOTE_TIMEOUT_MS    = 60000;  // 60s w/o packet → forfeit
    static constexpr uint32_t SCROLL_INTERVAL_MS   = 600;    // text reveal cadence
    static constexpr uint32_t RESEND_INTERVAL_MS   = 4000;   // re-broadcast our action
    static constexpr uint32_t PEER_READY_TIMEOUT_MS = 10000; // fallback if no READY arrives

    // Local→engine helpers
    void appendLog(const char *line);
    static void engineLogCb(const char *line, void *ctx);

    void sendStart(uint32_t rngSeed, const Gen1Party &myParty);
    void sendAction(uint8_t actionType, uint8_t index);
    void sendForfeit();
    void sendHash();
    void sendReady();

    // After both sides submitted, run executeTurn and prep next phase.
    void resolveTurn();

    // ── Server-authoritative PvP wire handlers ────────────────────────────
    //
    // role_ governs whether the server-auth code paths run instead of the
    // legacy lockstep ones. SERVER/CLIENT each touch only their own helpers.
    Role     role_ = Role::LEGACY;
    char     myTbName_[TB_MAX_NAME_LEN + 1] = {};
    char     peerTbName_[TB_MAX_NAME_LEN + 1] = {};
    Gen1Party pendingMyParty_      = {};   // staged for CHALLENGE / ACCEPT (Gen 1 path)
    // Protocol V2 (neutral cross-gen) parties. wireMy_ is built from
    // pendingMyParty_ at send time unless a Gen 2/3 save staged it directly
    // (haveWireParty_). wirePeer_ is the opponent's party from the wire.
    Gen1BattleEngine::WireParty wireMy_   = {};
    Gen1BattleEngine::WireParty wirePeer_ = {};
    bool    haveWireParty_ = false;
    uint8_t sessionGen_    = 3;            // gen byte from CHALLENGE (client side)

    // SERVER: CHALLENGE retransmit until ACCEPT arrives.
    uint32_t lastChallengeMs_ = 0;
    uint8_t  challengeTries_  = 0;
    bool     awaitingAccept_  = false;

    // SERVER: UPDATE retransmit until client's next ACTION_V2 acks the seq.
    uint8_t  updateSeq_         = 0;     // monotonic across UPDATEs (header.seq)
    bool     unackedUpdate_     = false; // set on send, cleared on matching ACK
    bool     pendingKeyExit_    = false; // user pressed key on FINISHED but unackedUpdate_ still set
    uint32_t finishedAtMs_      = 0;    // server: millis() when phase went FINISHED
    uint32_t lastUpdateSendMs_  = 0;
    uint8_t  lastUpdateBuf_[BATTLELINK_MAX_PKT] = {};
    size_t   lastUpdateLen_     = 0;
    // Lines added to log_[] since the last UPDATE shipped them out.
    // Lets the server render its own log text (we no longer wipe log_
    // on send — that was leaving the server's screen blank).
    uint8_t  unshippedLogLines_ = 0;

    // CLIENT: ACTION_V2 retransmit + last seq we acked (echoed back as
    // lastAckedSeq in every ACTION_V2 we emit).
    uint8_t  clientActionType_       = 0xFF;
    uint8_t  clientActionIndex_      = 0;
    uint8_t  clientActionTurn_       = 0;
    uint32_t lastClientActionSendMs_ = 0;
    uint8_t  lastAppliedUpdateSeq_   = 0;
    bool     clientNeedsFullState_   = false;

    // CLIENT: between ACCEPT-tx and first UPDATE-rx, retransmit ACCEPT on
    // the same 4 s cadence the server uses for CHALLENGE. Cleared on the
    // first applied UPDATE. Prevents the deadlock where the server's last
    // CHALLENGE retransmit lost contact with our ACCEPT.
    bool     awaitingFirstUpdate_    = false;
    uint32_t lastAcceptSendMs_       = 0;
    uint32_t awaitingUpdateSinceMs_  = 0; // when awaitingFirstUpdate_ was set
    // UPDATE buffered while still in WAIT_CHALLENGE_OVERLAY (before Y).
    // Applied immediately after engine_.start() in clientAuthSendAccept.
    uint8_t  preAcceptUpdateBuf_[BATTLELINK_MAX_PKT] = {};
    size_t   preAcceptUpdateLen_     = 0;

    // CLIENT: current turn tracker. We never call engine_.executeTurn, so
    // engine_.turn() stays at 0 forever on the client. The server reports
    // the current turn in every UPDATE; we mirror it here so ACTION_V2
    // packets carry the right turn number for the server's
    // "this is for the open turn" check.
    uint8_t  clientTurn_             = 0;

    // SERVER helpers.
    void serverAuthSendChallenge();
    void serverAuthOnAcceptPkt(uint32_t fromId,
                               const uint8_t *buf, size_t len);
    void serverAuthOnActionV2Pkt(uint32_t fromId,
                                 const uint8_t *buf, size_t len);
    void serverAuthOnStateRequestPkt(uint32_t fromId,
                                     const uint8_t *buf, size_t len);
    void serverAuthSendUpdate();
    void serverAuthSendFullState();
    void serverAuthRetransmit(uint32_t nowMs);

    // CLIENT helpers (implemented in P3).
    void clientAuthOnChallengePkt(uint32_t fromId,
                                  const uint8_t *buf, size_t len);
    void clientAuthOnUpdatePkt(const uint8_t *buf, size_t len);
    void clientAuthOnFullStatePkt(const uint8_t *buf, size_t len);
    void clientAuthSendAccept(bool accepted);
    void clientAuthSendActionV2(uint8_t actionType, uint8_t index);
    void clientAuthSendStateRequest();
    void clientAuthRetransmit(uint32_t nowMs);

    // Canonical client-visible board buffer (single source of truth for
    // hash + FULL_STATE body). Layout matches the FULL_STATE wire format:
    //   [0..1] turn (BE)  [2] result  per side {active, count,
    //   count×(hp BE, status)}  then clientActivePP[4].
    // Returns bytes written (≤ ~64).
    size_t packClientStateFromEngine(uint8_t out[]);

    // Auto-replace a fainted active mon at the start of each input phase.
    void handleFaints();

    // Render helpers
    void drawHpPanel(lgfx::LGFX_Device *g, uint8_t side, int y);
    void drawMoveMenu(lgfx::LGFX_Device *g);
    void drawSwitchMenu(lgfx::LGFX_Device *g);
    void drawLog(lgfx::LGFX_Device *g);
    void drawHeader(lgfx::LGFX_Device *g);
};
