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

    // Trade protocol (used by PokemonLinkProxy)
    TRADE_READY      = 0x50,
    TRADE_BLOCK_PART = 0x51,
    PATCH_LIST_PART  = 0x52,
    TRADE_SELECT     = 0x53,
    TRADE_CONFIRM    = 0x54,

    // Text-battle protocol (Gen1BattleEngine, deterministic dual-side execution).
    // Both sides share an RNG seed and only exchange inputs — no battle state.
    TEXT_BATTLE_START   = 0x60,  // payload: u32 rngSeed | u8 gen | u8 partyCount | partyHash[8]
    TEXT_BATTLE_ACTION  = 0x61,  // payload: u16 turn | u8 actionType | u8 index
    TEXT_BATTLE_FORFEIT = 0x62,  // payload: empty (session field identifies battle)
    TEXT_BATTLE_HASH    = 0x63,  // payload: u16 turn | u8 stateHash[8] (desync detection)
    TEXT_BATTLE_PARTY   = 0x64,  // payload chunk: u8 partIdx | u8 partTotal | bytes  (full party data, sent once at start)
};

// Text-battle action types (TEXT_BATTLE_ACTION.payload[2])
enum class TextBattleAction : uint8_t {
    USE_MOVE = 0,   // index = move slot 0-3
    SWITCH   = 1,   // index = party slot 0-5
};

// How often (in turns) each side broadcasts a state hash so we can detect desync.
static constexpr uint8_t TEXT_BATTLE_HASH_INTERVAL = 5;

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
static constexpr uint32_t SERIAL_BATCH_MS = 500;  // LoRa can't keep up with 50ms — 2 pkt/s max
static constexpr uint32_t BATTLE_REQUEST_INTERVAL_MS = 10000; // 10s — don't flood router TX queue
static constexpr uint32_t BATTLE_TIMEOUT_MS = 90000;          // 90s — give game time to navigate
