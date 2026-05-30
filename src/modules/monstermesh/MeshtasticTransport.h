#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ── MeshtasticTransport ──────────────────────────────────────────────────────
// Replaces SX1262Transport. Instead of direct radio access, packets go through
// Meshtastic's mesh network on channel 1 (MonsterMesh Center) using PRIVATE_APP
// portnum (256).
//
// Sending: MonsterMeshModule calls sendToMesh() on our behalf.
// Receiving: MonsterMeshModule::handleReceived() pushes packets into rxQueue_.
// nodeId: uses Meshtastic's node number.

class MeshtasticTransport {
public:
    MeshtasticTransport() {}
    ~MeshtasticTransport() {
        if (rxQueue_) vQueueDelete(rxQueue_);
    }

    bool begin() {
        rxQueue_ = xQueueCreate(RX_QUEUE_DEPTH, sizeof(RxPacket));
        return rxQueue_ != nullptr;
    }

    // Called by MonsterMeshModule when a PRIVATE_APP packet arrives on our channel
    void pushReceivedPacket(const uint8_t *data, size_t len, int16_t rssi) {
        if (!rxQueue_ || len == 0 || len > MAX_PKT) return;
        RxPacket pkt;
        memcpy(pkt.data, data, len);
        pkt.len = len;
        pkt.rssi = rssi;
        xQueueSend(rxQueue_, &pkt, 0);  // non-blocking
    }

    bool available() {
        return rxQueue_ && uxQueueMessagesWaiting(rxQueue_) > 0;
    }

    bool receive(uint8_t *buf, size_t &len, size_t bufLen) {
        if (!rxQueue_) return false;
        RxPacket pkt;
        if (xQueueReceive(rxQueue_, &pkt, 0) != pdTRUE) return false;
        size_t copyLen = (pkt.len < bufLen) ? pkt.len : bufLen;
        memcpy(buf, pkt.data, copyLen);
        len = copyLen;
        lastRssi_ = pkt.rssi;
        return true;
    }

    int16_t lastRssi() const { return lastRssi_; }

    // Set by MonsterMeshModule after it knows our Meshtastic node number
    void setNodeId(uint32_t id) { nodeId_ = id; }
    uint32_t nodeId() const { return nodeId_; }

    // Send buffer — MonsterMeshModule polls this and calls sendToMesh()
    // Returns true if queued successfully. Module will drain it on its thread.
    bool queueSend(const uint8_t *data, size_t len) {
        if (!txQueue_) {
            txQueue_ = xQueueCreate(TX_QUEUE_DEPTH, sizeof(TxPacket));
            if (!txQueue_) return false;
        }
        if (len > MAX_PKT) return false;
        TxPacket pkt;
        memcpy(pkt.data, data, len);
        pkt.len = len;
        return xQueueSend(txQueue_, &pkt, pdMS_TO_TICKS(50)) == pdTRUE;
    }

    bool hasPendingSend() {
        return txQueue_ && uxQueueMessagesWaiting(txQueue_) > 0;
    }

    bool dequeueSend(uint8_t *buf, size_t &len, size_t bufLen) {
        if (!txQueue_) return false;
        TxPacket pkt;
        if (xQueueReceive(txQueue_, &pkt, 0) != pdTRUE) return false;
        size_t copyLen = (pkt.len < bufLen) ? pkt.len : bufLen;
        memcpy(buf, pkt.data, copyLen);
        len = copyLen;
        return true;
    }

    // Drop everything currently queued for send. Called when stale
    // CHALLENGE retransmits would otherwise delay a fresh UPDATE — e.g.
    // ACCEPT just arrived and we've got several CHALLENGE packets piled
    // up in the queue draining at ~116 B/s.
    void dropPendingSends() {
        if (txQueue_) xQueueReset(txQueue_);
    }

    // Legacy RadioTransport interface for BattleShim/Lobby compatibility
    bool send(const uint8_t *data, size_t len) {
        return queueSend(data, len);
    }

private:
    static constexpr size_t MAX_PKT = 237;  // Meshtastic payload limit
    static constexpr uint8_t RX_QUEUE_DEPTH = 16;
    static constexpr uint8_t TX_QUEUE_DEPTH = 16;

    struct RxPacket {
        uint8_t data[MAX_PKT];
        size_t  len;
        int16_t rssi;
    };

    struct TxPacket {
        uint8_t data[MAX_PKT];
        size_t  len;
    };

    QueueHandle_t rxQueue_ = nullptr;
    QueueHandle_t txQueue_ = nullptr;
    int16_t       lastRssi_ = 0;
    uint32_t      nodeId_   = 0;
};
