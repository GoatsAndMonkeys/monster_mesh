#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "ISerialLink.h"
#include "RadioTransport.h"
#include "BattlePacket.h"

class Lobby;
class TournamentCoordinator;
class TournamentClient;

// ── BattleShim ────────────────────────────────────────────────────────────────
// Implements ISerialLink. Intercepts Peanut-GB serial callbacks and relays
// the byte stream to a remote T-Deck via LoRa.
//
// Architecture:
//   Core 1 (emulator task) → onSerialTx() → txQ_ → radioTask → LoRa Tx
//   LoRa Rx → radioTask → rxQ_ → onSerialRx() → Core 1 (emulator task)
//
// Batching strategy (Option A + batching from link_cable_protocol.md §10):
//   TX bytes accumulate in txQ_ and are flushed every SERIAL_BATCH_MS.
//   This limits LoRa overhead while keeping per-turn latency to ~1–3 RTTs
//   regardless of how many serial sync points the ROM uses per turn.
//
// Connection flow:
//   1. When game begins sending serial bytes → ADVERTISING
//   2. Both sides broadcast BATTLE_REQUEST packets
//   3. First REQUEST received → send BATTLE_ACCEPT, enter CONNECTED
//   4. On receiving BATTLE_ACCEPT from remote → enter IN_BATTLE (serial relay)
//   5. Higher chip ID device acts as serial MASTER (feeds 0x01 in handshake)
//   6. All subsequent serial bytes relayed transparently

class BattleShim : public ISerialLink {
public:
    explicit BattleShim(RadioTransport &transport);
    ~BattleShim();

    // Call once after RadioTransport::begin(). Starts the radio task on Core 0.
    bool begin();

    // ── ISerialLink ──────────────────────────────────────────────────────────
    void onSerialTx(uint8_t byte) override;
    bool onSerialRx(uint8_t &out) override;

    // ── Status accessors (safe to call from any task) ────────────────────────
    enum class State : uint8_t {
        IDLE       = 0,  // no link activity
        ADVERTISING,     // game wants to link; broadcasting REQUEST
        CONNECTED,       // LoRa session established; waiting for first serial byte
        IN_BATTLE,       // active serial relay
        DONE,            // battle ended or cancelled
    };
    State   state()        const { return state_; }
    bool    isMaster()     const { return isMaster_; }
    uint16_t sessionId()   const { return sessionId_; }

    // Call to cleanly disconnect (sends BATTLE_CANCEL and resets state).
    void cancel();

    // Called by Lobby when a challenge is accepted.
    // Sets up the session directly (skips ADVERTISING) and enters CONNECTED.
    void pairWith(uint32_t remoteChipId);

    // Set lobby reference so radio task can forward lobby packets.
    void setLobby(Lobby *lobby) { lobby_ = lobby; }

    // Set tournament references so radio task can forward tournament packets.
    void setTournamentCoord(TournamentCoordinator *coord) { tournamentCoord_ = coord; }
    void setTournamentClient(TournamentClient *client) { tournamentClient_ = client; }

private:
    RadioTransport         &transport_;
    Lobby                  *lobby_            = nullptr;
    TournamentCoordinator  *tournamentCoord_  = nullptr;
    TournamentClient       *tournamentClient_ = nullptr;

    // ── FreeRTOS queues (thread-safe between Core 0 and Core 1) ─────────────
    QueueHandle_t txQ_ = nullptr;   // Core 1 writes, Core 0 reads
    QueueHandle_t rxQ_ = nullptr;   // Core 0 writes, Core 1 reads
    static constexpr uint16_t QUEUE_DEPTH = 512;

    // ── Session state ────────────────────────────────────────────────────────
    volatile State   state_     = State::IDLE;
    bool             isMaster_  = false;
    uint16_t         sessionId_ = 0;
    uint32_t         remoteId_  = 0;  // remote node's chip ID
    uint8_t          seq_       = 0;  // wrapping packet sequence counter
    uint32_t         lastPacketMs_ = 0;

    // Advertising / request retry
    uint32_t lastRequestMs_ = 0;

    // Auto-reset timer: after entering DONE, wait 5 s then cancel() → IDLE
    uint32_t doneAtMs_ = 0;
    static constexpr uint32_t DONE_LINGER_MS = 5000;

    // ── Radio task ────────────────────────────────────────────────────────────
    TaskHandle_t radioTaskHandle_ = nullptr;
    static void  radioTaskEntry(void *pv);
    void         radioTaskLoop();

    // ── Packet helpers ───────────────────────────────────────────────────────
    void sendRequest();
    void sendAccept();
    void sendCancel();
    void sendPong();
    void flushTxBatch();  // drain txQ_ and send as SERIAL_DATA packet
    void handlePacket(const uint8_t *buf, size_t len);
    void handleSerialData(const BattlePacket &pkt, uint8_t payloadLen);

    BattlePacket buildPacket(PktType type, const uint8_t *payload = nullptr,
                             uint8_t payloadLen = 0);
};
