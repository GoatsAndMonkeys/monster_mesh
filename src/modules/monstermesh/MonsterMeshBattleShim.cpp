#include "MonsterMeshBattleShim.h"
#include "MonsterMeshLobby.h"
#include <string.h>

MonsterMeshBattleShim::MonsterMeshBattleShim(MeshtasticTransport &transport)
    : transport_(transport) {}

MonsterMeshBattleShim::~MonsterMeshBattleShim() {
    if (txQ_) vQueueDelete(txQ_);
    if (rxQ_) vQueueDelete(rxQ_);
}

bool MonsterMeshBattleShim::begin() {
    txQ_ = xQueueCreate(QUEUE_DEPTH, sizeof(uint8_t));
    rxQ_ = xQueueCreate(QUEUE_DEPTH, sizeof(uint8_t));
    if (!txQ_ || !rxQ_) {
        Serial.println("[SHIM] queue alloc failed");
        return false;
    }
    lastBatchMs_ = millis();
    Serial.printf("[SHIM] started  nodeId=0x%08X\n", (unsigned)transport_.nodeId());
    return true;
}

// ── ISerialLink ─────────────────────────────────────────────────────────────

void MonsterMeshBattleShim::onSerialTx(uint8_t byte) {
    if (state_ == State::IDLE) {
        state_ = State::ADVERTISING;
        lastRequestMs_ = 0;
        Serial.println("[SHIM] game wants link -> ADVERTISING");
    }
    if (state_ == State::IN_BATTLE) {
        xQueueSend(txQ_, &byte, 0);
    }
}

bool MonsterMeshBattleShim::onSerialRx(uint8_t &out) {
    if (state_ != State::IN_BATTLE) return false;
    return xQueueReceive(rxQ_, &out, 0) == pdTRUE;
}

// ── pairWith() ──────────────────────────────────────────────────────────────

void MonsterMeshBattleShim::pairWith(uint32_t remoteNodeId) {
    remoteId_ = remoteNodeId;
    uint32_t myId = transport_.nodeId();
    isMaster_  = (myId > remoteId_);
    sessionId_ = (uint16_t)(myId ^ remoteId_);

    xQueueReset(txQ_);
    xQueueReset(rxQ_);
    seq_ = 0;
    lastPacketMs_ = millis();

    state_ = State::CONNECTED;
    Serial.printf("[SHIM] paired with 0x%08X  role=%s  sid=0x%04X\n",
                  (unsigned)remoteId_,
                  isMaster_ ? "MASTER" : "SLAVE",
                  sessionId_);
}

void MonsterMeshBattleShim::cancel() {
    if (state_ != State::IDLE && state_ != State::DONE) {
        sendCancel();
    }
    state_ = State::IDLE;
    xQueueReset(txQ_);
    xQueueReset(rxQ_);
    sessionId_ = 0;
    remoteId_  = 0;
    Serial.println("[SHIM] cancelled");
}

// ── tick() — called from emulator task each frame ───────────────────────────

void MonsterMeshBattleShim::tick() {
    uint32_t now = millis();

    // Process any received packets
    processIncoming();

    switch (state_) {
        case State::ADVERTISING:
            if (now - lastRequestMs_ >= BATTLE_REQUEST_INTERVAL_MS) {
                sendRequest();
                lastRequestMs_ = now;
            }
            break;

        case State::CONNECTED:
            if (uxQueueMessagesWaiting(txQ_) > 0) {
                state_ = State::IN_BATTLE;
                Serial.println("[SHIM] serial relay -> IN_BATTLE");
            }
            break;

        case State::IN_BATTLE:
            if (now - lastBatchMs_ >= SERIAL_BATCH_MS) {
                flushTxBatch();
                lastBatchMs_ = now;
            }
            if (lastPacketMs_ && (now - lastPacketMs_ > BATTLE_TIMEOUT_MS)) {
                Serial.println("[SHIM] timeout");
                cancel();
            }
            break;

        default:
            break;
    }

    // Auto-reset DONE -> IDLE
    if (state_ == State::DONE && doneAtMs_ &&
        (now - doneAtMs_ > DONE_LINGER_MS)) {
        cancel();
        Serial.println("[SHIM] auto-reset -> IDLE");
    }
}

// ── processIncoming() — drain transport rx queue ────────────────────────────

void MonsterMeshBattleShim::processIncoming() {
    while (transport_.available()) {
        uint8_t buf[237];
        size_t len = 0;
        if (transport_.receive(buf, len, sizeof(buf))) {
            handlePacket(buf, len);
            // Note: lastPacketMs_ is updated inside handlePacket() only for
            // session-relevant types (BATTLE_REQUEST, BATTLE_ACCEPT, SERIAL_DATA).
            // Lobby beacons do NOT reset the battle timeout.
        }
    }
}

// ── Packet builders ─────────────────────────────────────────────────────────

BattlePacket MonsterMeshBattleShim::buildPacket(PktType type,
                                              const uint8_t *payload,
                                              uint8_t payloadLen) {
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)type;
    pkt.setSessionId(sessionId_);
    pkt.seq = seq_++;
    if (payload && payloadLen > 0) {
        memcpy(pkt.payload, payload, payloadLen);
    }
    return pkt;
}

void MonsterMeshBattleShim::sendRequest() {
    uint32_t id = transport_.nodeId();
    uint8_t pl[4] = {
        (uint8_t)(id >> 24), (uint8_t)(id >> 16),
        (uint8_t)(id >>  8), (uint8_t)(id)
    };
    BattlePacket pkt = buildPacket(PktType::BATTLE_REQUEST, pl, 4);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 4);
}

void MonsterMeshBattleShim::sendAccept() {
    uint32_t id = transport_.nodeId();
    uint8_t pl[4] = {
        (uint8_t)(id >> 24), (uint8_t)(id >> 16),
        (uint8_t)(id >>  8), (uint8_t)(id)
    };
    BattlePacket pkt = buildPacket(PktType::BATTLE_ACCEPT, pl, 4);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 4);
}

void MonsterMeshBattleShim::sendCancel() {
    BattlePacket pkt = buildPacket(PktType::BATTLE_CANCEL);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE);
}

void MonsterMeshBattleShim::sendPong() {
    uint8_t ts = (uint8_t)(millis() & 0xFF);
    BattlePacket pkt = buildPacket(PktType::PONG, &ts, 1);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 1);
}

void MonsterMeshBattleShim::flushTxBatch() {
    uint8_t data[SERIAL_DATA_MAX];
    uint8_t count = 0;
    uint8_t b;

    while (count < SERIAL_DATA_MAX &&
           xQueueReceive(txQ_, &b, 0) == pdTRUE) {
        data[count++] = b;
    }
    if (count == 0) return;

    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::SERIAL_DATA;
    pkt.setSessionId(sessionId_);
    pkt.seq = seq_++;
    pkt.payload[0] = count;
    memcpy(pkt.payload + 1, data, count);

    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 1 + count);
}

// ── handlePacket() ──────────────────────────────────────────────────────────

void MonsterMeshBattleShim::handlePacket(const uint8_t *buf, size_t len) {
    if (len < BATTLELINK_HDR_SIZE) return;

    const BattlePacket &pkt = *reinterpret_cast<const BattlePacket *>(buf);
    auto type = static_cast<PktType>(pkt.type);
    uint8_t payloadLen = (uint8_t)(len - BATTLELINK_HDR_SIZE);

    switch (type) {
        case PktType::BATTLE_REQUEST: {
            if (state_ != State::ADVERTISING && state_ != State::IDLE) break;
            if (payloadLen < 4) break;

            remoteId_ = ((uint32_t)pkt.payload[0] << 24) |
                        ((uint32_t)pkt.payload[1] << 16) |
                        ((uint32_t)pkt.payload[2] <<  8) |
                        pkt.payload[3];

            uint32_t myId = transport_.nodeId();
            if (remoteId_ == myId) break;

            isMaster_  = (myId > remoteId_);
            sessionId_ = (uint16_t)(myId ^ remoteId_);

            sendAccept();
            state_ = State::CONNECTED;
            lastPacketMs_ = millis();
            break;
        }

        case PktType::BATTLE_ACCEPT: {
            if (state_ != State::ADVERTISING) break;
            if (payloadLen < 4) break;

            remoteId_ = ((uint32_t)pkt.payload[0] << 24) |
                        ((uint32_t)pkt.payload[1] << 16) |
                        ((uint32_t)pkt.payload[2] <<  8) |
                        pkt.payload[3];

            uint32_t myId = transport_.nodeId();
            isMaster_  = (myId > remoteId_);
            sessionId_ = (uint16_t)(myId ^ remoteId_);

            state_ = State::CONNECTED;
            lastPacketMs_ = millis();
            break;
        }

        case PktType::SERIAL_DATA: {
            if (state_ != State::IN_BATTLE && state_ != State::CONNECTED) break;
            if (payloadLen < 1) break;
            if (pkt.sessionId() != sessionId_) break;

            handleSerialData(pkt, payloadLen);
            lastPacketMs_ = millis();
            break;
        }

        case PktType::BATTLE_CANCEL:
            if (pkt.sessionId() != sessionId_ && sessionId_ != 0) break;
            Serial.println("[SHIM] <- CANCEL");
            state_ = State::DONE;
            doneAtMs_ = millis();
            break;

        case PktType::PING:
            sendPong();
            break;

        // Lobby packets — forward
        case PktType::LOBBY_BEACON:
        case PktType::LOBBY_CHALLENGE:
        case PktType::LOBBY_ACCEPT:
        case PktType::LOBBY_REJECT:
            if (lobby_) {
                // Forward to lobby — lobby has its own handlePacket
                extern void monstermeshLobbyHandlePacket(MonsterMeshLobby *lobby,
                                                       const uint8_t *buf, size_t len);
                monstermeshLobbyHandlePacket(lobby_, buf, len);
            }
            break;

        default:
            break;
    }
}

void MonsterMeshBattleShim::handleSerialData(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < 1) return;
    uint8_t count = pkt.payload[0];
    if (count > payloadLen - 1) count = payloadLen - 1;

    if (state_ == State::CONNECTED) {
        state_ = State::IN_BATTLE;
        Serial.println("[SHIM] first SERIAL_DATA -> IN_BATTLE");
    }

    for (uint8_t i = 0; i < count; i++) {
        uint8_t b = pkt.payload[1 + i];
        if (xQueueSend(rxQ_, &b, 0) != pdTRUE) break;
    }
}
