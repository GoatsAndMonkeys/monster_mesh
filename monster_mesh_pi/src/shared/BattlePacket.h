#pragma once
#include "platform.h"

// ── MonsterMesh BattleLink wire protocol ─────────────────────────────────────
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
    LOBBY_BEACON    = 0x40,
    LOBBY_CHALLENGE = 0x41,
    LOBBY_ACCEPT    = 0x42,
    LOBBY_REJECT    = 0x43,

    TRADE_READY      = 0x50,
    TRADE_BLOCK_PART = 0x51,
    PATCH_LIST_PART  = 0x52,
    TRADE_SELECT     = 0x53,
    TRADE_CONFIRM    = 0x54,

    TEXT_BATTLE_START   = 0x60,
    TEXT_BATTLE_ACTION  = 0x61,
    TEXT_BATTLE_FORFEIT = 0x62,
    TEXT_BATTLE_HASH    = 0x63,
    TEXT_BATTLE_PARTY   = 0x64,
    TEXT_BATTLE_PARTY_MIN = 0x65,

    TEXT_BATTLE_UPDATE       = 0x66,
    TEXT_BATTLE_ACTION_V2    = 0x67,
    TEXT_BATTLE_CHALLENGE    = 0x68,
    TEXT_BATTLE_ACCEPT       = 0x69,
    TEXT_BATTLE_STATE_REQUEST = 0x6A,
    TEXT_BATTLE_FULL_STATE   = 0x6B,

    // Battle protocol V2: CHALLENGE/ACCEPT carrying the neutral cross-gen
    // WireParty blob (139 B, see WirePartyCodec.h) instead of the Gen-1-only
    // 109-B partyMin. New types (not new layouts under the old types) so a
    // V1 device drops V2 packets instead of parsing garbage stats.
    TEXT_BATTLE_CHALLENGE_V2 = 0x6D,
    TEXT_BATTLE_ACCEPT_V2    = 0x6E,

    BBS_PING  = 0x70,
    BBS_REPLY = 0x71,
    BBS_FIGHT_REQUEST = 0x72,
    BBS_FIGHT_RESULT  = 0x73,
    BBS_LADDER_REQUEST = 0x74,
    BBS_LADDER_NAMES   = 0x75,
    BBS_LADDER_PARTIES = 0x76,

    DUNGEON_BEACON   = 0x80,
    DUNGEON_JOIN     = 0x81,
    DUNGEON_JOIN_ACK = 0x82,
    DUNGEON_CMD      = 0x83,
    DUNGEON_STATE    = 0x84,
    DUNGEON_MSG      = 0x85,
    DUNGEON_PROMPT   = 0x86,

    // Daycare beacon — MUST be 0x60 to match the T-Deck firmware
    // (PokemonDaycare.cpp sets beacon.type = 0x60). This collides with
    // TEXT_BATTLE_START on the wire; the receiver disambiguates by
    // checking that the payload size matches sizeof(DaycareBeacon).
    DAYCARE_BEACON   = 0x60,
};

enum class TextBattleAction : uint8_t {
    USE_MOVE = 0,
    SWITCH   = 1,
    FLEE     = 2,
};

static constexpr uint8_t TEXT_BATTLE_HASH_INTERVAL = 1;

static constexpr uint8_t  TB_MAX_NAME_LEN     = 12;
static constexpr uint8_t  TB_PARTY_MIN_BYTES  = 109;
static constexpr uint8_t  TB_ACTION_BYTES         = 4;
static constexpr uint8_t  TB_STATE_REQUEST_BYTES  = 1;
static constexpr uint8_t  TB_UPDATE_MAX_LOG_LINES = 6;
static constexpr uint32_t TB_CHALLENGE_RESEND_MS  = 8000;
static constexpr uint32_t TB_CHALLENGE_MAX_TRIES  = 15;
static constexpr uint32_t TB_UPDATE_RESEND_MS     = 6000;
static constexpr uint32_t TB_ACTION_RESEND_MS     = 6000;
static constexpr uint32_t TB_NO_TRAFFIC_TIMEOUT_MS = 60000;

enum TbUpdateFlag : uint16_t {
    TB_UPD_HP                = 1u << 0,
    TB_UPD_PP                = 1u << 1,
    TB_UPD_SWITCH            = 1u << 2,
    TB_UPD_STATUS            = 1u << 3,
    TB_UPD_RESULT            = 1u << 4,
    TB_UPD_LOG               = 1u << 5,
    TB_UPD_NEED_PLAYER_SWITCH = 1u << 6,
    TB_UPD_BENCH             = 1u << 7,
    TB_UPD_FX                = 1u << 8,
};

enum TbClientResult : uint8_t {
    TB_RESULT_ONGOING  = 0,
    TB_RESULT_YOU_WIN  = 1,
    TB_RESULT_YOU_LOSE = 2,
    TB_RESULT_DRAW     = 3,
    TB_RESULT_FLED     = 4,
};

static inline void tbPackAction(uint8_t out[TB_ACTION_BYTES],
                                uint8_t turn, uint8_t actionType,
                                uint8_t index, uint8_t lastAckedSeq)
{
    out[0] = turn;
    out[1] = actionType;
    out[2] = index;
    out[3] = lastAckedSeq;
}

static inline bool tbUnpackAction(const uint8_t *in, size_t len,
                                  uint8_t &turn, uint8_t &actionType,
                                  uint8_t &index, uint8_t &lastAckedSeq)
{
    if (len < TB_ACTION_BYTES) return false;
    turn         = in[0];
    actionType   = in[1];
    index        = in[2];
    lastAckedSeq = in[3];
    return true;
}

static inline void tbPackStateRequest(uint8_t out[TB_STATE_REQUEST_BYTES],
                                      uint8_t lastAppliedSeq)
{
    out[0] = lastAppliedSeq;
}

static inline bool tbUnpackStateRequest(const uint8_t *in, size_t len,
                                        uint8_t &lastAppliedSeq)
{
    if (len < TB_STATE_REQUEST_BYTES) return false;
    lastAppliedSeq = in[0];
    return true;
}

static inline uint32_t tbBoardHash24(const uint8_t *buf, size_t len)
{
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; ++i) {
        h ^= buf[i];
        h *= 0x01000193u;
    }
    return h & 0x00FFFFFFu;
}

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

static constexpr uint8_t  SERIAL_DATA_MAX    = BATTLELINK_MAX_PAYLOAD - 1;
static constexpr uint32_t SERIAL_BATCH_MS    = 500;
static constexpr uint32_t BATTLE_REQUEST_INTERVAL_MS = 10000;
static constexpr uint32_t BATTLE_TIMEOUT_MS  = 90000;
