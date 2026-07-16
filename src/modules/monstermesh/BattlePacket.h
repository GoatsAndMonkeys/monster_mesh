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

    // ── Server-authoritative PvP protocol (replaces 0x60..0x65 on the PvP
    // path; the old TEXT_BATTLE_* enums remain for LOCAL_ROGUELIKE).
    //
    // Initiator = SERVER (the only side running Gen1BattleEngine).
    // Receiver  = CLIENT (renders state shipped by the server, no engine).
    //
    // Side convention in UPDATE / FULL_STATE payloads:
    //   wire side 0 = CLIENT's party (the receiver's perspective: "me")
    //   wire side 1 = SERVER's party (the receiver's perspective: "enemy")
    // Server maps engine-P1 (client) → wire 0 and engine-P0 (self) → wire 1
    // so both sides hash identical bytes.
    //
    // Every server→client packet on this path carries `header.seq` as a
    // monotonic UPDATE counter; the client echoes the last applied seq in
    // its next ACTION (piggyback ACK). No standalone ACK packet.

    // UPDATE: server → client. After each executeTurn the server ships only
    // the fields that changed plus a 3-byte board hash. Layout:
    //   [0]=turn  [1..2]=flags (uint16 BE)  [3..5]=boardHash24 (FNV1a low24)
    //   conditional sections appended in flag order — see TbUpdateFlag enum
    //   for full layout per bit.
    TEXT_BATTLE_UPDATE = 0x66,

    // ACTION: client → server. Fixed 4-byte payload:
    //   [0]=turn  [1]=actionType  [2]=index  [3]=lastAckedSeq
    // actionType: 0=move (index = move slot 0..3), 1=switch (party 0..5),
    // 2=flee.
    TEXT_BATTLE_ACTION_V2 = 0x67,

    // CHALLENGE: server → client. ~127-byte payload:
    //   [0]=gen  [1]=nameLen (≤12)  [2..]=name  then 109-byte partyMin.
    // rngSeed is NOT transmitted (server-authoritative — client never runs
    // the engine, so seed is private to the server).
    TEXT_BATTLE_CHALLENGE = 0x68,

    // ACCEPT: client → server. ~127-byte payload:
    //   [0]=accepted (1=accept, 0=decline)
    //   [1]=nameLen (≤12)  [2..]=name  then 109-byte partyMin.
    TEXT_BATTLE_ACCEPT = 0x69,

    // STATE_REQUEST: client → server. Sent when the client's recomputed
    // boardHash24 disagrees with the server's UPDATE.boardHash24.
    // 1-byte payload: [0]=lastAppliedSeq.
    TEXT_BATTLE_STATE_REQUEST = 0x6A,

    // FULL_STATE: server → client. Authoritative snapshot of the client-
    // visible board (== the input buffer to boardHash24). Layout:
    //   [0..1]=turn (BE)  [2]=result
    //   per side s in {0,1}: [activeSlot:1][count:1] then
    //                        count × [hp:2 BE][status:1]
    //   then clientActivePP[4]
    // maxHp is NOT shipped — it is static; client has it from the
    // CHALLENGE / ACCEPT party.
    TEXT_BATTLE_FULL_STATE = 0x6B,

    // READY: receiver → initiator. Zero-payload ping sent when the receiver
    // enters the battle station, letting the initiator know the peer's UI is
    // up. Initiator holds its render in WAIT_PEER_READY until this arrives.
    TEXT_BATTLE_READY = 0x6C,

    // Battle protocol V2: CHALLENGE/ACCEPT carrying the neutral cross-gen
    // WireParty blob (139 B, see WirePartyCodec.h) instead of the Gen-1-only
    // 109-B partyMin. New types (not new layouts under the old types) so a
    // V1 device drops V2 packets instead of parsing garbage stats.
    TEXT_BATTLE_CHALLENGE_V2 = 0x6D,
    TEXT_BATTLE_ACCEPT_V2    = 0x6E,

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

// Text-battle action types (TEXT_BATTLE_ACTION.payload[2] for the legacy
// LOCAL/lockstep path; TEXT_BATTLE_ACTION_V2.payload[1] for server-auth).
enum class TextBattleAction : uint8_t {
    USE_MOVE = 0,   // index = move slot 0-3
    SWITCH   = 1,   // index = party slot 0-5
    FLEE     = 2,   // index unused; server-auth only
};

// How often (in turns) each side broadcasts a state hash so we can detect
// desync. 1 = every turn — slightly more bandwidth, but catches single-turn
// drift instead of waiting up to 5 turns for the next window (and risking
// the silent-stall case where the hash packet arrives turn-mismatched and
// gets dropped, so nothing ever compares).
// Server-auth path does not use this — boardHash24 is carried inline on
// every UPDATE.
static constexpr uint8_t TEXT_BATTLE_HASH_INTERVAL = 1;

// ── Server-auth PvP wire constants ──────────────────────────────────────────
// Max bytes for the trainer name embedded in CHALLENGE/ACCEPT payload.
static constexpr uint8_t  TB_MAX_NAME_LEN     = 12;
// Static party blob size (matches packPartyMin output in MonsterMeshModule.cpp).
static constexpr uint8_t  TB_PARTY_MIN_BYTES  = 109;
// Fixed-size payload widths (excludes the 4-byte BattlePacket header).
static constexpr uint8_t  TB_ACTION_BYTES         = 4;
static constexpr uint8_t  TB_STATE_REQUEST_BYTES  = 1;
// Soft upper bound for log section in UPDATE (lines × bytes).
static constexpr uint8_t  TB_UPDATE_MAX_LOG_LINES = 6;
// Server retransmit/timeout cadences (ms).
// Server retransmits CHALLENGE every 8 s until ACCEPT or 15 tries
// (= 120 s budget). Measured MQTT round-trip in congested conditions
// is ~28 s (14 s each way); the old 6 × 5 s = 30 s budget left no
// margin and Red routinely cancelled before Blue's ACCEPT got back,
// so the first ACCEPT to actually arrive hit awaitingAccept_=false
// and was silently dropped.
static constexpr uint32_t TB_CHALLENGE_RESEND_MS  = 8000;
static constexpr uint32_t TB_CHALLENGE_MAX_TRIES  = 15;
// P2.39d: bumped from 6s → 15s/11s to clear airtime contention. Both
// sides retx-ing every 6s on the MonsterMesh channel triggered
// "Ch. util >25% Skip send" on the server + "MQTT queue is full",
// and ACTION_V2 packets weren't getting through. Slower retx with
// per-side stagger (server 15s, client 11s) gives each radio real
// RX windows between TX bursts.
static constexpr uint32_t TB_UPDATE_RESEND_MS     = 15000;
static constexpr uint32_t TB_ACTION_RESEND_MS     = 11000;
// 60 s no-traffic timeout once a battle is live — gives the user time
// to consult the move menu without auto-forfeiting between turns.
static constexpr uint32_t TB_NO_TRAFFIC_TIMEOUT_MS = 60000;

// UPDATE flag bits (TEXT_BATTLE_UPDATE payload[1..2] — BIG-ENDIAN uint16).
// Order in this enum doubles as the order of conditional sections in the
// packed payload — keep additions append-only.
enum TbUpdateFlag : uint16_t {
    TB_UPD_HP                = 1u << 0,
    TB_UPD_PP                = 1u << 1,
    TB_UPD_SWITCH            = 1u << 2,
    TB_UPD_STATUS            = 1u << 3,
    TB_UPD_RESULT            = 1u << 4,
    TB_UPD_LOG               = 1u << 5,
    TB_UPD_NEED_PLAYER_SWITCH = 1u << 6,
    // CLIENT's full party state — HP+status+PP for every slot + per-stat
    // boost stages. Lets the switch menu show accurate HP / status / PP
    // for bench mons (would otherwise be stuck at the values frozen in
    // at ACCEPT). Server's bench is hidden per Gen-1 information rules.
    // Section layout when set:
    //   [count:1]
    //   for each slot 0..count-1:
    //     [hp:2 BE][status:1][pp[0..3]:4]
    //   [atkBoost:i1][defBoost:i1][spdBoost:i1][spcBoost:i1]
    //   [accBoost:i1][evaBoost:i1]
    // Worst case (6 mons): 1 + 6*7 + 6 = 49 bytes.
    TB_UPD_BENCH             = 1u << 7,
    // Battle FX — per-active-mon counters + per-side field effects.
    // Visible Gen-1 UX: substitute bar, sleep / confuse / trap counters,
    // disabled move slot, must-recharge / thrashing, reflect / light
    // screen / mist screens with turn counts.
    // Section layout when set:
    //   per side s in {0=client, 1=server} (14 bytes):
    //     [sleepTurns:1][confuseTurns:1]
    //     [substituteHp:2 BE]
    //     [monFlags:1]   // bit0=mustRecharge bit1=flinched bit2=thrashing
    //                    //  bit3=rageActive  bit4=transformed bit5=charging
    //     [fieldFlags:1] // bit0=reflect bit1=lightScreen bit2=mist bit3=focused
    //     [reflectTurns:1][lightScreenTurns:1]
    //   client-side only (3 bytes):
    //     [disabledSlot:1][disabledTurns:1][trapTurns:1]
    // Total = 2×7 + 3 = 17 bytes.
    TB_UPD_FX                = 1u << 8,
};

// Client-POV result codes (TEXT_BATTLE_UPDATE result byte / FULL_STATE [2]).
enum TbClientResult : uint8_t {
    TB_RESULT_ONGOING  = 0,
    TB_RESULT_YOU_WIN  = 1,
    TB_RESULT_YOU_LOSE = 2,
    TB_RESULT_DRAW     = 3,
    TB_RESULT_FLED     = 4,
};

// ── Fixed-size pack/unpack helpers (header-only, inline) ────────────────────
//
// Variable-size packets (CHALLENGE, ACCEPT, UPDATE, FULL_STATE) build their
// payload directly in the caller because they need access to packPartyMin()
// and the engine's BattleParty.  These two are tiny and constant-shape:

// Pack a TEXT_BATTLE_ACTION_V2 payload. `out` must have at least
// TB_ACTION_BYTES bytes available.
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

// Pack a TEXT_BATTLE_STATE_REQUEST payload.
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

// 24-bit FNV-1a over the client-visible board buffer. Server and client
// both compute this over byte-identical buffers (FULL_STATE payload format).
// Returns the low 24 bits; caller stores in 3 contiguous bytes.
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

// Write only the fixed four-byte wire header. Use this for short packets whose
// backing buffer is smaller than BattlePacket: casting such a buffer to the
// full 200-byte type is C++ object-bounds undefined behavior even when the
// code touches header fields only.
static inline void writeBattlePacketHeader(uint8_t *buffer, PktType type,
                                           uint16_t sessionId, uint8_t seq = 0)
{
    buffer[0] = static_cast<uint8_t>(type);
    buffer[1] = static_cast<uint8_t>(sessionId >> 8);
    buffer[2] = static_cast<uint8_t>(sessionId);
    buffer[3] = seq;
}

static constexpr uint8_t SERIAL_DATA_MAX    = BATTLELINK_MAX_PAYLOAD - 1;
static constexpr uint32_t SERIAL_BATCH_MS = 500;  // LoRa can't keep up with 50ms — 2 pkt/s max
static constexpr uint32_t BATTLE_REQUEST_INTERVAL_MS = 10000; // 10s — don't flood router TX queue
static constexpr uint32_t BATTLE_TIMEOUT_MS = 90000;          // 90s — give game time to navigate
