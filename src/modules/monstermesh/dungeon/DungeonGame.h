#pragma once
#include "configuration.h"
#include <freertos/semphr.h>
#include "../BattlePacket.h"
#include "../MeshtasticTransport.h"
#include "DungeonModule.h"

// ── DungeonGame ───────────────────────────────────────────────────────────────
// Co-op dungeon crawler — one T-Deck acts as host (game server), others are
// guests. Host runs game logic and broadcasts DUNGEON_STATE / DUNGEON_MSG.
// Guests send DUNGEON_CMD and receive state updates.
//
// Thread safety: handlePacket() is called from Core 0 (radio task).
//                tick() and handleLocalCommand() are called from Core 1.
//                All state access is protected by mtx_.

// Dungeon command verbs (sent inside DUNGEON_CMD payload)
enum class DungeonVerb : uint8_t {
    JOIN = 0, PARTY, ATTACK, CAST, SWITCH, ITEM,
    ANSWER, WORDLE, HACK, FLEE, REST, HOST_START,
};

// ── Payload layouts (all big-endian multi-byte) ───────────────────────────────

// DUNGEON_BEACON / DUNGEON_JOIN  [14 bytes]:
//   [0-3]  chipId   [4-13] name (ASCII, null-padded to 10)
static constexpr uint8_t DUNGEON_BEACON_SIZE = 14;

// DUNGEON_JOIN_ACK  [7 bytes]:
//   [0-3] targetChipId  [4] accepted  [5] slot  [6] partySize
static constexpr uint8_t DUNGEON_JOIN_ACK_SIZE = 7;

// DUNGEON_CMD  [5 + arg bytes]:
//   [0-3] senderChipId  [4] verb  [5..] arg (ASCII, null-terminated)
static constexpr uint8_t DUNGEON_CMD_HDR_SIZE = 5;

// DUNGEON_STATE  [compact party snapshot]:
//   [0] phase  [1] depth  [2] partySize
//   per slot (up to 5) * 10 bytes:
//     [0-3] chipId  [4-5] curHp  [6-7] maxHp  [8] activeSlot  [9] aliveFlags
static constexpr uint8_t DUNGEON_STATE_SLOT_SIZE = 10;
static constexpr uint8_t DUNGEON_STATE_HDR_SIZE  = 3;

// DUNGEON_MSG  [up to 191 bytes]:
//   [0..] null-terminated ASCII text
//   [last] category nibble packed in high bits of first byte:
//   actually just plain ASCII — category encoded in first char prefix

// DUNGEON_PROMPT  [type + data]:
//   [0] prompt type: 0=trivia 1=wordle 2=hack
//   [1..] prompt data (ASCII, null-terminated)
static constexpr uint8_t DUNGEON_PROMPT_TRIVIA = 0;
static constexpr uint8_t DUNGEON_PROMPT_WORDLE = 1;
static constexpr uint8_t DUNGEON_PROMPT_HACK   = 2;

// ── Message log ───────────────────────────────────────────────────────────────
static constexpr uint8_t  DLOG_LINES   = 8;
static constexpr uint8_t  DLOG_LEN     = 60;

struct DungeonLog {
    char lines[DLOG_LINES][DLOG_LEN];
    uint8_t head = 0;
    uint8_t count = 0;

    void push(const char *msg) {
        strncpy(lines[head], msg, DLOG_LEN - 1);
        lines[head][DLOG_LEN - 1] = '\0';
        head = (head + 1) % DLOG_LINES;
        if (count < DLOG_LINES) count++;
    }

    // Oldest-first iteration: index 0 = oldest
    const char* get(uint8_t i) const {
        if (i >= count) return "";
        uint8_t idx = (DLOG_LINES + head - count + i) % DLOG_LINES;
        return lines[idx];
    }
};

// ── Test encounter (Phase 1 hardcoded) ───────────────────────────────────────
struct TestEnemy {
    const char* name     = "Charmander";
    const char* className = "Fighter";
    uint8_t     classLv  = 3;
    uint16_t    hp       = 45;
    uint16_t    maxHp    = 45;
    uint8_t     atkDmg   = 8;   // damage per enemy turn
};

// ── DungeonGame ───────────────────────────────────────────────────────────────
class DungeonGame {
public:
    explicit DungeonGame(MeshtasticTransport &transport);

    void begin();

    // Core 0 — called from BattleShim::handlePacket()
    void handlePacket(const uint8_t *buf, size_t len);

    // Core 1 — called from main/emu loop
    void tick(uint32_t now);

    // Core 1 — called from CommandBar dispatch in main loop
    // verb: dungeon command string ("host","join","attack","party",etc.)
    // arg: remainder of command string (move name, word, etc.)
    void handleLocalCommand(const char *verb, const char *arg);

    // ── Accessors (safe after taking lock externally) ─────────────────────────
    bool           isActive()  const { return active_; }
    bool           isHost()    const { return isHost_; }
    RunPhase       phase()     const { return run_.phase; }
    uint8_t        partySize() const { return run_.partySize; }
    uint8_t        floorDepth()const { return run_.currentFloor.depth; }
    const DungeonLog& log()    const { return log_; }
    const DungeonRun& run()    const { return run_; }

    // Overlay calls this to check if a new state has arrived since last render
    bool consumeDirty() {
        if (dirty_) { dirty_ = false; return true; }
        return false;
    }

    SemaphoreHandle_t mutex() const { return mtx_; }

private:
    MeshtasticTransport &transport_;
    SemaphoreHandle_t mtx_      = nullptr;
    bool              active_   = false;   // dungeon mode on/off
    bool              isHost_   = false;   // true = this device runs game logic
    uint32_t          hostId_   = 0;       // chipId of host (0 = I am host)
    uint8_t           seq_      = 0;

    DungeonRun  run_;
    DungeonLog  log_;
    TestEnemy   testEnemy_;  // Phase 1 test encounter
    bool        dirty_  = false;
    uint32_t    lastBeaconMs_ = 0;
    static constexpr uint32_t BEACON_INTERVAL_MS = 30000;

    // ── Packet senders ────────────────────────────────────────────────────────
    void sendBeacon();
    void sendJoinRequest();
    void sendJoinAck(uint32_t targetId, bool accepted, uint8_t slot);
    void sendState();
    void sendMsg(const char *msg);
    void sendPrompt(uint8_t promptType, const char *data);
    void forwardCmd(DungeonVerb verb, const char *arg);

    // ── Host: command processing ──────────────────────────────────────────────
    void hostProcessCmd(uint32_t fromId, DungeonVerb verb, const char *arg);
    void hostStartRun();
    void hostCmdAttack(uint32_t fromId, const char *moveName);
    // Trainer casts a spell: consumes a spell slot; spellName determines type+potency.
    // Replaces the Pokemon attack for this turn. Only valid when isSpellcaster().
    void hostCmdCast(uint32_t fromId, const char *spellName);
    void hostCmdSwitch(uint32_t fromId, uint8_t slot);
    void hostCmdItem(uint32_t fromId, const char *itemName);
    void hostCmdAnswer(uint32_t fromId, const char *answer);
    void hostCmdWordle(uint32_t fromId, const char *guess);
    void hostCmdFlee(uint32_t fromId);
    void hostCmdRest(uint32_t fromId);

    // ── Packet handlers ───────────────────────────────────────────────────────
    void handleBeacon(const BattlePacket &pkt, uint8_t payloadLen);
    void handleJoin(const BattlePacket &pkt, uint8_t payloadLen);
    void handleJoinAck(const BattlePacket &pkt, uint8_t payloadLen);
    void handleCmd(const BattlePacket &pkt, uint8_t payloadLen);
    void handleState(const BattlePacket &pkt, uint8_t payloadLen);
    void handleMsg(const BattlePacket &pkt, uint8_t payloadLen);
    void handlePrompt(const BattlePacket &pkt, uint8_t payloadLen);

    // ── Helpers ───────────────────────────────────────────────────────────────
    BattlePacket buildPacket(PktType type, const uint8_t *payload, uint8_t payloadLen);
    void         sendPkt(const BattlePacket &pkt, uint8_t payloadLen);
    uint32_t     myId() const { return transport_.nodeId(); }
    DungeonTrainer* findTrainer(uint32_t chipId);
    void         logMsg(const char *msg);
    void         markDirty() { dirty_ = true; }
};
