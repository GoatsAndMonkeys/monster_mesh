#pragma once
#include <Arduino.h>

// ── MonsterMesh BattleLink wire protocol ────────────────────────────────────────
// Fits inside Meshtastic's ~237-byte decoded payload.
// All multi-byte integers are big-endian.

static constexpr uint8_t BATTLELINK_MAX_PKT           = 200;
static constexpr uint8_t BATTLELINK_HDR_SIZE          = 4;
static constexpr uint8_t BATTLELINK_MAX_PAYLOAD       = BATTLELINK_MAX_PKT - BATTLELINK_HDR_SIZE;

enum class PktType : uint8_t {
    BATTLE_REQUEST  = 0x10,
    BATTLE_ACCEPT   = 0x11,
    BATTLE_REJECT   = 0x12,
    BATTLE_CANCEL   = 0x13,
    SERIAL_DATA     = 0x20,
    PING            = 0x30,
    PONG            = 0x31,
    LOBBY_BEACON    = 0x40,   // broadcast: name + party + ELO
    LOBBY_CHALLENGE = 0x41,   // broadcast w/ target: "I challenge you"   payload[0-3]=senderChipId, payload[4-7]=targetChipId
    LOBBY_ACCEPT    = 0x42,   // broadcast w/ target: "Challenge accepted" payload[0-3]=senderChipId, payload[4-7]=targetChipId
    LOBBY_REJECT    = 0x43,   // broadcast w/ target: "Challenge declined" payload[0-3]=senderChipId, payload[4-7]=targetChipId
};

#pragma pack(push, 1)
struct BattlePacket {
    uint8_t  type;
    uint8_t  sessionHi;
    uint8_t  sessionLo;
    uint8_t  seq;
    uint8_t  payload[BATTLELINK_MAX_PAYLOAD];

    uint16_t sessionId() const {
        return ((uint16_t)sessionHi << 8) | sessionLo;
    }
    void setSessionId(uint16_t id) {
        sessionHi = (id >> 8) & 0xFF;
        sessionLo = id & 0xFF;
    }
};
#pragma pack(pop)
static_assert(sizeof(BattlePacket) == BATTLELINK_MAX_PKT,
              "BattlePacket must be exactly 200 bytes");

static constexpr uint8_t SERIAL_DATA_MAX    = BATTLELINK_MAX_PAYLOAD - 1;
static constexpr uint32_t SERIAL_BATCH_MS = 50;
static constexpr uint32_t BATTLE_REQUEST_INTERVAL_MS = 3000;
static constexpr uint32_t BATTLE_TIMEOUT_MS = 30000;
