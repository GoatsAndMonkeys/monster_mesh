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
    // Minimal-format party for mesh PvP — fits in a single packet (109 B
    // payload + header). Drops names, current HP / PP / status (always full
    // / zero at battle start), redundant species list. Per-mon = 18 bytes:
    //   species(1) level(1) dvs[2] hpExp[2] atkExp[2] defExp[2] spdExp[2]
    //   spcExp[2] moves[4]
    // Header = 1 byte count. Total = 1 + 6*18 = 109 B.
    TEXT_BATTLE_PARTY_MIN = 0x65,

    // BBS gym discovery (Phase C-1) — runs over PRIVATE_APP so it does NOT
    // appear in mesh chat. Never use TEXT_MESSAGE_APP for these.
    BBS_PING  = 0x70,  // broadcast probe; payload empty
    BBS_REPLY = 0x71,  // unicast back to prober; payload:
                        //   u8 nameLen | name[] | u8 badgeLen | badge[]
                        //   u8 leaderLen | leader[] | u8 rosterSize

    // BBS gym fight (Phase C-2 — send-party-once model).  Replaces the
    // per-turn TEXT_BATTLE_* lockstep for gym vs CPU. Way fewer packets:
    //   1. T-Deck → gym  : BBS_FIGHT_REQUEST   (no payload)
    //   2. gym    → T-Deck: TEXT_BATTLE_PARTY × N chunks (gym's Gen1Party)
    //   3. T-Deck runs the battle locally as player vs CPU
    //   4. T-Deck → gym  : BBS_FIGHT_RESULT   (outcome + challenger name)
    BBS_FIGHT_REQUEST = 0x72,
    BBS_FIGHT_RESULT  = 0x73,  // payload:
                                //   u8 outcome (0 = challenger lost, 1 = won)
                                //   u8 nameLen | name[]   (challenger short name)

    // BBS gym ladder (Phase C-3) — bulk dump.  Replaces the 5-round per-trainer
    // BBS_FIGHT_REQUEST flow with two upfront packets so the challenger can
    // run the entire 5-trainer ladder locally with zero mid-run LoRa traffic.
    //   1. T-Deck → gym : BBS_LADDER_REQUEST  (no payload)
    //   2. gym → T-Deck : BBS_LADDER_NAMES    (5 trainer names)
    //   3. gym → T-Deck : BBS_LADDER_PARTIES  (5 minimal parties: dex+level+moves)
    //   4. T-Deck runs all 5 fights locally, healing party between rounds
    //   5. T-Deck → gym : BBS_FIGHT_RESULT only AFTER beating the leader
    //                       (or on early loss, with a payload byte saying which
    //                       trainer index was reached, if you care to track)
    //
    // Wire format (NAMES, payload[]):
    //   u8 trainerCount (=5)
    //   for each trainer: u8 nameLen | char name[nameLen]   (max 16 chars each)
    //
    // Wire format (PARTIES, payload[]):
    //   u8 trainerCount (=5)
    //   for each trainer:
    //     u8 monCount (0..6)
    //     for each mon: u8 dex | u8 level | u8 move0 | u8 move1 | u8 move2 | u8 move3
    //
    // Stats (HP/atk/def/spd/spc/types) are derived on the T-Deck from
    // showdown_gen1_basestats.h + level using the same formula as
    // gen1MinimalStats(). PP defaults to canonical max from the move table.
    BBS_LADDER_REQUEST = 0x74,
    BBS_LADDER_NAMES   = 0x75,
    BBS_LADDER_PARTIES = 0x76,

    // Dungeons and MonstersMesh co-op roguelike crawler
    DUNGEON_BEACON   = 0x80,  // host broadcasts presence
    DUNGEON_JOIN     = 0x81,  // guest requests to join
    DUNGEON_JOIN_ACK = 0x82,  // host acknowledges join
    DUNGEON_CMD      = 0x83,  // guest sends command to host
    DUNGEON_STATE    = 0x84,  // host broadcasts party state
    DUNGEON_MSG      = 0x85,  // host broadcasts text message
    DUNGEON_PROMPT   = 0x86,  // host sends trivia/wordle/hack prompt
};

// Text-battle action types (TEXT_BATTLE_ACTION.payload[2])
enum class TextBattleAction : uint8_t {
    USE_MOVE = 0,   // index = move slot 0-3
    SWITCH   = 1,   // index = party slot 0-5
};

// How often (in turns) each side broadcasts a state hash so we can detect
// desync. 1 = every turn — slightly more bandwidth, but catches single-turn
// drift instead of waiting up to 5 turns for the next window (and risking
// the silent-stall case where the hash packet arrives turn-mismatched and
// gets dropped, so nothing ever compares).
static constexpr uint8_t TEXT_BATTLE_HASH_INTERVAL = 1;

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
