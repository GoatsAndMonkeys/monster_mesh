#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "ISerialLink.h"
#include "MeshtasticTransport.h"
#include "BattlePacket.h"

class MonsterMeshLobby;

// ── BattleShim (Meshtastic version) ──────────────────────────────────────────
// Same state machine as standalone BattleShim, but no dedicated radio task.
// Instead, tick() is called from the emulator task, and packets flow through
// MeshtasticTransport (queued to/from MonsterMeshModule's mesh send).

class MonsterMeshBattleShim : public ISerialLink {
public:
    explicit MonsterMeshBattleShim(MeshtasticTransport &transport);
    ~MonsterMeshBattleShim();

    bool begin();

    // ── ISerialLink ──────────────────────────────────────────────────────────
    void onSerialTx(uint8_t byte) override;
    bool onSerialRx(uint8_t &out) override;

    // Called every frame from emulator task — drives state machine + batch flush
    void tick();

    enum class State : uint8_t {
        IDLE = 0, ADVERTISING, CONNECTED, IN_BATTLE, DONE,
    };
    State    state()      const { return state_; }
    bool     isMaster()   const { return isMaster_; }
    uint16_t sessionId()  const { return sessionId_; }
    uint32_t remoteId()   const { return remoteId_; }

    void cancel();
    void pairWith(uint32_t remoteNodeId);
    void setLobby(MonsterMeshLobby *lobby) { lobby_ = lobby; }

private:
    MeshtasticTransport &transport_;
    MonsterMeshLobby       *lobby_ = nullptr;

    QueueHandle_t txQ_ = nullptr;
    QueueHandle_t rxQ_ = nullptr;
    static constexpr uint16_t QUEUE_DEPTH = 512;

    volatile State    state_          = State::IDLE;
    volatile bool     isMaster_       = false;
    volatile uint16_t sessionId_      = 0;
    volatile uint32_t remoteId_       = 0;
    uint8_t           seq_            = 0;
    // These are written by Core 0 (handleReceived/pairWith) and read by Core 1 (tick).
    // Must be volatile to prevent the Core 1 compiler from caching stale values.
    volatile uint32_t lastPacketMs_   = 0;
    volatile uint32_t lastRequestMs_  = 0;
    volatile uint32_t lastBatchMs_    = 0;
    volatile uint32_t doneAtMs_       = 0;
    static constexpr uint32_t DONE_LINGER_MS = 5000;

    void sendRequest();
    void sendAccept();
    void sendCancel();
    void sendPong();
    void flushTxBatch();
    void processIncoming();
    void handlePacket(const uint8_t *buf, size_t len);
    void handleSerialData(const BattlePacket &pkt, uint8_t payloadLen);

    BattlePacket buildPacket(PktType type, const uint8_t *payload = nullptr,
                             uint8_t payloadLen = 0);
};
