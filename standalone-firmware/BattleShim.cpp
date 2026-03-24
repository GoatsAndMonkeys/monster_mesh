#include "BattleShim.h"
#include "Lobby.h"
#include "TournamentCoordinator.h"
#include "TournamentClient.h"
#include <string.h>

// ── Constructor / Destructor ──────────────────────────────────────────────────

BattleShim::BattleShim(RadioTransport &transport)
    : transport_(transport) {}

BattleShim::~BattleShim() {
    if (radioTaskHandle_) vTaskDelete(radioTaskHandle_);
    if (txQ_) vQueueDelete(txQ_);
    if (rxQ_) vQueueDelete(rxQ_);
}

// ── begin() ───────────────────────────────────────────────────────────────────

bool BattleShim::begin() {
    txQ_ = xQueueCreate(QUEUE_DEPTH, sizeof(uint8_t));
    rxQ_ = xQueueCreate(QUEUE_DEPTH, sizeof(uint8_t));
    if (!txQ_ || !rxQ_) {
        Serial.println("[SHIM] queue alloc failed");
        return false;
    }

    xTaskCreatePinnedToCore(
        radioTaskEntry, "radio",
        8192,   // 8KB stack (RadioLib + our code)
        this,
        2,      // priority — above kb (1), below emulator (5)
        &radioTaskHandle_,
        0       // Core 0 (PRO_CPU)
    );

    Serial.printf("[SHIM] started  nodeId=0x%08X\n", (unsigned)transport_.nodeId());
    return true;
}

// ── ISerialLink ───────────────────────────────────────────────────────────────

void BattleShim::onSerialTx(uint8_t byte) {
    // Called from Core 1 (emulator task).
    if (state_ == State::IDLE) {
        // Game is initiating a link session — start advertising.
        state_ = State::ADVERTISING;
        lastRequestMs_ = 0;  // force immediate REQUEST on next radio tick
        Serial.println("[SHIM] game wants link → ADVERTISING");
    }
    if (state_ == State::IN_BATTLE) {
        // Non-blocking; if queue is full, drop the byte (very rare).
        xQueueSend(txQ_, &byte, 0);
    }
}

bool BattleShim::onSerialRx(uint8_t &out) {
    // Called from Core 1 (emulator task).
    // Return false → emulator returns GB_SERIAL_RX_NO_CONNECTION → game retries
    // next frame. No busywait — just tick forward.
    if (state_ != State::IN_BATTLE) return false;

    return xQueueReceive(rxQ_, &out, 0) == pdTRUE;
}

// ── pairWith() — called by Lobby on accepted challenge ──────────────────────

void BattleShim::pairWith(uint32_t remoteChipId) {
    remoteId_ = remoteChipId;
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

// ── cancel() ─────────────────────────────────────────────────────────────────

void BattleShim::cancel() {
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

// ── Radio task entry ──────────────────────────────────────────────────────────

void BattleShim::radioTaskEntry(void *pv) {
    static_cast<BattleShim *>(pv)->radioTaskLoop();
}

void BattleShim::radioTaskLoop() {
    uint32_t lastBatchMs = millis();

    for (;;) {
        uint32_t now = millis();

        // ── 1. Receive any incoming LoRa packet ──────────────────────────────
        if (transport_.available()) {
            uint8_t buf[BATTLELINK_MAX_PKT];
            size_t  len = 0;
            if (transport_.receive(buf, len, sizeof(buf))) {
                handlePacket(buf, len);
                lastPacketMs_ = millis();
            }
        }

        // ── 2. State machine ─────────────────────────────────────────────────
        switch (state_) {
            case State::ADVERTISING:
                if (now - lastRequestMs_ >= BATTLE_REQUEST_INTERVAL_MS) {
                    sendRequest();
                    lastRequestMs_ = now;
                }
                break;

            case State::CONNECTED:
                // Transition to serial relay as soon as game queues the first
                // byte (tx queue non-empty means the game is in link mode and
                // the LoRa session is ready).
                if (uxQueueMessagesWaiting(txQ_) > 0) {
                    state_ = State::IN_BATTLE;
                    Serial.println("[SHIM] serial relay → IN_BATTLE");
                }
                break;

            case State::IN_BATTLE:
                // Flush tx batch every SERIAL_BATCH_MS
                if (now - lastBatchMs >= SERIAL_BATCH_MS) {
                    flushTxBatch();
                    lastBatchMs = now;
                }
                // Session timeout
                if (lastPacketMs_ && (now - lastPacketMs_ > BATTLE_TIMEOUT_MS)) {
                    Serial.println("[SHIM] timeout — disconnecting");
                    cancel();
                }
                break;

            default:
                break;
        }

        // ── Auto-reset: DONE → IDLE after DONE_LINGER_MS ────────────────
        if (state_ == State::DONE && doneAtMs_ &&
            (now - doneAtMs_ > DONE_LINGER_MS)) {
            cancel();  // resets queues, session, state → IDLE
            Serial.println("[SHIM] auto-reset → IDLE");
        }

        vTaskDelay(pdMS_TO_TICKS(10));  // 10 ms polling granularity
    }
}

// ── Packet builders ───────────────────────────────────────────────────────────

BattlePacket BattleShim::buildPacket(PktType type,
                                      const uint8_t *payload,
                                      uint8_t payloadLen) {
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)type;
    pkt.setSessionId(sessionId_);
    pkt.seq  = seq_++;
    if (payload && payloadLen > 0) {
        memcpy(pkt.payload, payload, payloadLen);
    }
    return pkt;
}

void BattleShim::sendRequest() {
    uint32_t id = transport_.nodeId();
    uint8_t  pl[4] = {
        (uint8_t)(id >> 24), (uint8_t)(id >> 16),
        (uint8_t)(id >>  8), (uint8_t)(id)
    };
    BattlePacket pkt = buildPacket(PktType::BATTLE_REQUEST, pl, 4);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 4);
    Serial.println("[SHIM] → BATTLE_REQUEST");
}

void BattleShim::sendAccept() {
    uint32_t id = transport_.nodeId();
    uint8_t  pl[4] = {
        (uint8_t)(id >> 24), (uint8_t)(id >> 16),
        (uint8_t)(id >>  8), (uint8_t)(id)
    };
    BattlePacket pkt = buildPacket(PktType::BATTLE_ACCEPT, pl, 4);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 4);
    Serial.println("[SHIM] → BATTLE_ACCEPT");
}

void BattleShim::sendCancel() {
    BattlePacket pkt = buildPacket(PktType::BATTLE_CANCEL);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE);
}

void BattleShim::sendPong() {
    uint8_t ts = (uint8_t)(millis() & 0xFF);
    BattlePacket pkt = buildPacket(PktType::PONG, &ts, 1);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 1);
}

// ── flushTxBatch() ────────────────────────────────────────────────────────────

void BattleShim::flushTxBatch() {
    uint8_t data[SERIAL_DATA_MAX];
    uint8_t count = 0;
    uint8_t b;

    while (count < SERIAL_DATA_MAX &&
           xQueueReceive(txQ_, &b, 0) == pdTRUE) {
        data[count++] = b;
    }
    if (count == 0) return;

    // Build SERIAL_DATA packet: payload[0]=count, payload[1..]=bytes
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::SERIAL_DATA;
    pkt.setSessionId(sessionId_);
    pkt.seq   = seq_++;
    pkt.payload[0] = count;
    memcpy(pkt.payload + 1, data, count);

    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 1 + count);
}

// ── handlePacket() ────────────────────────────────────────────────────────────

void BattleShim::handlePacket(const uint8_t *buf, size_t len) {
    if (len < BATTLELINK_HDR_SIZE) return;

    const BattlePacket &pkt = *reinterpret_cast<const BattlePacket *>(buf);
    auto type = static_cast<PktType>(pkt.type);
    uint8_t payloadLen = (uint8_t)(len - BATTLELINK_HDR_SIZE);

    switch (type) {
        case PktType::BATTLE_REQUEST: {
            if (state_ != State::ADVERTISING && state_ != State::IDLE) break;
            if (payloadLen < 4) break;

            // Extract remote chip ID
            remoteId_ = ((uint32_t)pkt.payload[0] << 24) |
                        ((uint32_t)pkt.payload[1] << 16) |
                        ((uint32_t)pkt.payload[2] <<  8) |
                        pkt.payload[3];

            uint32_t myId = transport_.nodeId();
            if (remoteId_ == myId) break;  // same device; ignore

            // Determine role: higher chip ID is MASTER (drives serial clock)
            isMaster_  = (myId > remoteId_);
            sessionId_ = (uint16_t)(myId ^ remoteId_);  // deterministic session ID

            Serial.printf("[SHIM] ← REQUEST  remoteId=0x%08X  role=%s  sid=0x%04X\n",
                          (unsigned)remoteId_,
                          isMaster_ ? "MASTER" : "SLAVE",
                          sessionId_);

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

            Serial.printf("[SHIM] ← ACCEPT   remoteId=0x%08X  role=%s  sid=0x%04X\n",
                          (unsigned)remoteId_,
                          isMaster_ ? "MASTER" : "SLAVE",
                          sessionId_);

            state_ = State::CONNECTED;
            lastPacketMs_ = millis();
            break;
        }

        case PktType::SERIAL_DATA: {
            if (state_ != State::IN_BATTLE && state_ != State::CONNECTED) break;
            if (payloadLen < 1) break;
            if (pkt.sessionId() != sessionId_) break;  // wrong session

            handleSerialData(pkt, payloadLen);
            lastPacketMs_ = millis();
            break;
        }

        case PktType::BATTLE_CANCEL:
            if (pkt.sessionId() != sessionId_ && sessionId_ != 0) break;
            Serial.println("[SHIM] ← CANCEL — opponent disconnected");
            state_ = State::DONE;
            doneAtMs_ = millis();
            break;

        case PktType::PING:
            sendPong();
            break;

        // ── Lobby packets — forwarded to Lobby if present ───────────────────
        case PktType::LOBBY_BEACON:
        case PktType::LOBBY_CHALLENGE:
        case PktType::LOBBY_ACCEPT:
        case PktType::LOBBY_REJECT:
            if (lobby_) lobby_->handlePacket(buf, len);
            break;

        // ── Tournament packets — forwarded to coordinator and/or client ──────
        case PktType::TOURNAMENT_REGISTER:
        case PktType::TOURNAMENT_RESULT:
            // These are directed at the coordinator
            if (tournamentCoord_) tournamentCoord_->handlePacket(buf, len);
            break;
        case PktType::TOURNAMENT_ANNOUNCE:
        case PktType::TOURNAMENT_REGISTER_ACK:
        case PktType::TOURNAMENT_BRACKET:
        case PktType::TOURNAMENT_MATCH_ASSIGN:
        case PktType::TOURNAMENT_RESULT_ACK:
        case PktType::TOURNAMENT_STATUS:
        case PktType::TOURNAMENT_COMPLETE:
            // These are directed at tournament clients
            if (tournamentClient_) tournamentClient_->handlePacket(buf, len);
            break;

        default:
            break;
    }
}

// ── handleSerialData() ────────────────────────────────────────────────────────

void BattleShim::handleSerialData(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < 1) return;
    uint8_t count = pkt.payload[0];

    // Clamp to what's actually in the packet
    if (count > payloadLen - 1) count = payloadLen - 1;

    // Transition out of CONNECTED on first data received (just in case the
    // remote triggers IN_BATTLE before us)
    if (state_ == State::CONNECTED) {
        state_ = State::IN_BATTLE;
        Serial.println("[SHIM] first SERIAL_DATA received → IN_BATTLE");
    }

    for (uint8_t i = 0; i < count; i++) {
        uint8_t b = pkt.payload[1 + i];
        // Non-blocking put; if queue full, oldest bytes are dropped
        if (xQueueSend(rxQ_, &b, 0) != pdTRUE) {
            Serial.println("[SHIM] rxQ full — byte dropped");
            break;
        }
    }
}
