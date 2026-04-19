#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "BattlePacket.h"

class MeshtasticTransport;
class MonsterMeshEmulator;

// ── PokemonLinkProxy ──────────────────────────────────────────────────────────
// Protocol-aware proxy for the Pokemon Gen 1 Cable Club link protocol over LoRa.
//
// Problem: Pokemon's Cable Club has a ~2-second game-level timeout that fires
// before LoRa can complete the multi-byte handshake. Every raw serial byte
// forwarded over LoRa would time the game out.
//
// Solution: This proxy intercepts every serial byte and responds INSTANTLY from
// a local state machine. Only complete, phase-boundary data is sent over LoRa.
//
// Architecture (always runs as SLAVE — GB drives the clock):
//
//   Phase 1 — Role negotiation (0x01/0x02/0x60): local, no LoRa
//   Phase 2 — Menu selection (0xD0-0xDF): local, no LoRa
//   Trade Centre (0xD4):
//     1. Read local party data from WRAM (contiguous 415-byte block)
//     2. Build patch list (records 0xFE→0xFF replacements)
//     3. Send TRADE_READY + 3 trade-block fragments + 2 patch-list fragments
//     4. Stall peanut_gb on first preamble byte until remote data arrives
//     5. Feed remote's pre-buffered data during the GB's data exchange
//   Battle (0xD5): fall through to txQ/rxQ dumb relay (unchanged)
//
// Integration:
//   MonsterMeshBattleShim holds one PokemonLinkProxy.
//   BattleShim::onSerialTx → proxy.onGbTx()
//   BattleShim::onSerialRx → proxy.getResponse()
//   BattleShim::handlePacket → proxy.onRemotePacket() for TRADE_* types

class PokemonLinkProxy {
public:
    PokemonLinkProxy(MeshtasticTransport &transport, MonsterMeshEmulator &emulator);

    void begin();
    void reset();

    // ── Serial interface (called by MonsterMeshBattleShim) ───────────────────
    // GB sent a byte over the link cable; we compute our response synchronously.
    void onGbTx(uint8_t byte);
    // peanut_gb calls this each frame until we return true (or stall timer fires).
    // Returns false → stall; returns true + fills out → transfer complete.
    bool getResponse(uint8_t &out);

    // ── Network interface ────────────────────────────────────────────────────
    // Called by BattleShim::handlePacket() for TRADE_* packet types.
    void onRemotePacket(PktType type, const uint8_t *payload, uint8_t payloadLen);

    // ── Battle relay queues (borrowed from BattleShim for BATTLE mode) ───────
    void setQueues(QueueHandle_t txQ, QueueHandle_t rxQ) {
        txQ_ = txQ; rxQ_ = rxQ;
    }
    void setSessionId(uint16_t sid) { sessionId_ = sid; }

    // ── State ────────────────────────────────────────────────────────────────
    enum class Phase : uint8_t {
        DISCONNECTED,        // no serial activity yet
        NEGOTIATION,         // role negotiation (0x01 MASTER / 0x02 SLAVE / 0x60)
        MENU,                // Cable Club main menu (0xD0-0xDF bytes)
        TRADE_INIT,          // 0xD4 seen; exchanging party data over LoRa; GB stalled
        TRADE_PREAMBLE,      // 10× 0xFD preamble (local echo, no LoRa)
        TRADE_RANDOM,        // 10 random bytes + 9× 0xFD (local echo)
        TRADE_DATA,          // 415-byte party data: feed remote block to GB
        TRADE_DATA_END,      // 3 bytes (DF FE 15) + 6× 0xFD (local echo)
        TRADE_PATCH_HEADER,  // 7× 0x00 (local echo)
        TRADE_PATCH_DATA,    // 200-byte patch list: feed remote patches to GB
        TRADE_SELECT,        // pokemon selection byte → LoRa → stall for remote
        TRADE_CONFIRM,       // accept/reject byte → LoRa → stall for remote
        TRADE_DONE,          // trade complete; return to MENU
        BATTLE,              // Colosseum selected (0xD5): dumb txQ/rxQ relay
    };
    Phase phase() const { return phase_; }

private:
    MeshtasticTransport &transport_;
    MonsterMeshEmulator  &emulator_;

    Phase    phase_    = Phase::DISCONNECTED;
    uint16_t counter_  = 0;    // byte counter within current phase
    uint16_t sessionId_ = 0;
    uint8_t  seq_      = 0;    // proxy's own LoRa sequence counter
    uint8_t  lastGbByte_ = 0;  // saved for stall-then-respond pattern

    // Response pending for next getResponse() call
    bool    responsePending_ = false;
    uint8_t responseByte_    = 0xFF;
    bool    gbIsMaster_      = true;   // true if GB sent 0x01 first

    // Battle relay queues (borrowed from BattleShim)
    QueueHandle_t txQ_ = nullptr;
    QueueHandle_t rxQ_ = nullptr;

    // ── Trade data buffers ───────────────────────────────────────────────────
    // Trade block: 415-byte Gen 1 party structure (trainer name + party + OT + nicknames)
    // Spans WRAM 0xD158-0xD2F6 (contiguous in Pokemon Red/Blue).
    static constexpr uint16_t TRADE_BLOCK_SIZE = 415;
    // Patch list: 200 bytes (always padded to this size, 2 LoRa fragments)
    static constexpr uint16_t PATCH_LIST_SIZE  = 200;
    // Data bytes per LoRa fragment (payload - 2 header bytes)
    static constexpr uint8_t  FRAG_DATA_SIZE   = BATTLELINK_MAX_PAYLOAD - 2;  // 194

    uint8_t localTradeBlock_[TRADE_BLOCK_SIZE];
    uint8_t remoteTradeBlock_[TRADE_BLOCK_SIZE];
    uint8_t localPatchList_[PATCH_LIST_SIZE];
    uint8_t remotePatchList_[PATCH_LIST_SIZE];

    // Fragment reception tracking (bitmask per fragment index)
    uint8_t tradeFragMask_ = 0;  // 3 frags → bits 0-2
    uint8_t patchFragMask_ = 0;  // 2 frags → bits 0-1
    bool    remoteTradeReady_ = false;  // all 3 trade block frags received
    bool    remotePatchReady_ = false;  // all 2 patch list frags received

    // Position trackers for the data exchange phases
    uint16_t remoteDataIdx_  = 0;  // next byte to feed from remoteTradeBlock_
    uint16_t patchRemoteIdx_ = 0;  // next byte to feed from remotePatchList_

    // Async selection / confirmation from remote
    bool    remoteSelectReady_  = false;
    uint8_t remoteSelect_       = 0;
    bool    remoteConfirmReady_ = false;
    uint8_t remoteConfirm_      = 0;

    // ── Helpers ──────────────────────────────────────────────────────────────
    void transitionTo(Phase p);
    void checkUnstall();           // called from onRemotePacket when frags complete

    void readLocalTradeBlock();    // memcpy 415 bytes from WRAM
    void buildAndPatchLocalBlock();// scan for 0xFE, build patch list, replace with 0xFF
    void applyRemotePatchList();   // restore 0xFE in remoteTradeBlock_

    void sendTradeReady();
    void sendTradeBlockFragments();
    void sendPatchListFragments();
    void sendPacket(PktType type, const uint8_t *payload, uint8_t payloadLen);
};
