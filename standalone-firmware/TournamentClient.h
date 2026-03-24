#pragma once
#include <Arduino.h>
#include "BattlePacket.h"
#include "RadioTransport.h"

// ── TournamentClient ─────────────────────────────────────────────────────────
// Player-side tournament participation. Discovers nearby tournaments, handles
// registration, receives match assignments, and reports results.

class TournamentClient {
public:
    static constexpr uint8_t MAX_DISCOVERED = 4;

    enum class ClientState : uint8_t {
        IDLE,           // not in a tournament
        DISCOVERED,     // heard announce(s), showing available tournaments
        REGISTERED,     // sent register, awaiting ACK
        WAITING,        // in tournament, waiting for match assignment
        MATCHED,        // assigned a match — need to battle
        REPORTING,      // battle done, reporting result
    };

    struct DiscoveredTournament {
        uint16_t id            = 0;
        uint8_t  type          = 0;    // TournamentType
        uint8_t  maxPlayers    = 0;
        uint8_t  currentPlayers = 0;
        char     name[17]      = {};
        uint8_t  phase         = 0;    // Phase enum
        bool     lateReg       = false;
        uint32_t lastSeenMs    = 0;
    };

    explicit TournamentClient(RadioTransport &transport)
        : transport_(transport) {}

    // ── Packet handler ───────────────────────────────────────────────────────
    void handlePacket(const uint8_t *buf, size_t len) {
        if (len < BATTLELINK_HDR_SIZE + 2) return;
        const BattlePacket &pkt = *reinterpret_cast<const BattlePacket *>(buf);
        auto type = static_cast<PktType>(pkt.type);
        uint8_t payloadLen = (uint8_t)(len - BATTLELINK_HDR_SIZE);

        switch (type) {
            case PktType::TOURNAMENT_ANNOUNCE:
                handleAnnounce(pkt, payloadLen);
                break;
            case PktType::TOURNAMENT_REGISTER_ACK:
                handleRegisterAck(pkt, payloadLen);
                break;
            case PktType::TOURNAMENT_BRACKET:
                handleBracket(pkt, payloadLen);
                break;
            case PktType::TOURNAMENT_MATCH_ASSIGN:
                handleMatchAssign(pkt, payloadLen);
                break;
            case PktType::TOURNAMENT_RESULT_ACK:
                handleResultAck(pkt, payloadLen);
                break;
            case PktType::TOURNAMENT_COMPLETE:
                handleComplete(pkt, payloadLen);
                break;
            case PktType::TOURNAMENT_STATUS:
                // informational, update display
                break;
            default: break;
        }
    }

    // ── Register for a discovered tournament ─────────────────────────────────
    void registerFor(uint8_t discoveredIdx, const char *trainerName, uint16_t elo) {
        if (discoveredIdx >= discoveredCount_) return;
        auto &t = discovered_[discoveredIdx];

        activeTournamentId_ = t.id;
        state_ = ClientState::REGISTERED;
        registerMs_ = millis();

        // TOURNAMENT_REGISTER: tournamentId[2], chipId[4], elo[2], name[10]
        BattlePacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = (uint8_t)PktType::TOURNAMENT_REGISTER;
        pkt.payload[0] = (uint8_t)(t.id >> 8);
        pkt.payload[1] = (uint8_t)(t.id);

        uint32_t id = transport_.nodeId();
        pkt.payload[2] = (uint8_t)(id >> 24);
        pkt.payload[3] = (uint8_t)(id >> 16);
        pkt.payload[4] = (uint8_t)(id >>  8);
        pkt.payload[5] = (uint8_t)(id);
        pkt.payload[6] = (uint8_t)(elo >> 8);
        pkt.payload[7] = (uint8_t)(elo);

        // Name (10 chars max)
        memset(&pkt.payload[8], 0, 10);
        strncpy((char *)&pkt.payload[8], trainerName, 10);

        transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 18);
        Serial.printf("[TCLIENT] registering for tournament %u\n", t.id);
    }

    // ── Report match result ──────────────────────────────────────────────────
    void reportResult(bool won) {
        if (state_ != ClientState::MATCHED) return;

        uint32_t myId = transport_.nodeId();
        uint32_t winnerChip = won ? myId : opponentChipId_;
        uint32_t loserChip  = won ? opponentChipId_ : myId;

        // TOURNAMENT_RESULT: tournamentId[2], matchId[1], winnerChipId[4], loserChipId[4]
        BattlePacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = (uint8_t)PktType::TOURNAMENT_RESULT;
        pkt.payload[0] = (uint8_t)(activeTournamentId_ >> 8);
        pkt.payload[1] = (uint8_t)(activeTournamentId_);
        pkt.payload[2] = currentMatchId_;
        pkt.payload[3] = (uint8_t)(winnerChip >> 24);
        pkt.payload[4] = (uint8_t)(winnerChip >> 16);
        pkt.payload[5] = (uint8_t)(winnerChip >>  8);
        pkt.payload[6] = (uint8_t)(winnerChip);
        pkt.payload[7] = (uint8_t)(loserChip >> 24);
        pkt.payload[8] = (uint8_t)(loserChip >> 16);
        pkt.payload[9] = (uint8_t)(loserChip >>  8);
        pkt.payload[10] = (uint8_t)(loserChip);

        transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 11);
        state_ = ClientState::REPORTING;
        Serial.printf("[TCLIENT] reported result: %s\n", won ? "WIN" : "LOSS");
    }

    // ── Accessors ────────────────────────────────────────────────────────────
    ClientState state()            const { return state_; }
    uint8_t     discoveredCount()  const { return discoveredCount_; }
    const DiscoveredTournament &discovered(uint8_t i) const { return discovered_[i]; }
    uint16_t    activeTournamentId() const { return activeTournamentId_; }
    uint8_t     currentMatchId()   const { return currentMatchId_; }
    uint32_t    opponentChipId()   const { return opponentChipId_; }
    uint8_t     mySeed()           const { return mySeed_; }

    // Bracket data (raw from coordinator)
    uint8_t     bracketRound()     const { return bracketRound_; }
    uint8_t     bracketMatchCount() const { return bracketMatchCount_; }

    struct BracketEntry { uint8_t seed1, seed2, result; };
    static constexpr uint8_t MAX_BRACKET_ENTRIES = 62;
    const BracketEntry &bracketEntry(uint8_t i) const { return bracketEntries_[i]; }

    // Winner info (set when TOURNAMENT_COMPLETE received)
    const char *winnerName()    const { return winnerName_; }
    bool        isComplete()    const { return state_ == ClientState::IDLE && winnerName_[0] != '\0'; }

private:
    RadioTransport &transport_;

    ClientState    state_               = ClientState::IDLE;
    uint16_t       activeTournamentId_  = 0;
    uint8_t        mySeed_              = 0;
    uint8_t        currentMatchId_      = 0;
    uint32_t       opponentChipId_      = 0;
    uint32_t       registerMs_          = 0;

    // Discovered tournaments (from ANNOUNCE broadcasts)
    DiscoveredTournament discovered_[MAX_DISCOVERED];
    uint8_t              discoveredCount_ = 0;

    // Bracket cache
    uint8_t      bracketRound_      = 0;
    uint8_t      bracketMatchCount_ = 0;
    BracketEntry bracketEntries_[MAX_BRACKET_ENTRIES] = {};

    // Winner
    char         winnerName_[11] = {};

    // ── Handlers ─────────────────────────────────────────────────────────────

    void handleAnnounce(const BattlePacket &pkt, uint8_t payloadLen) {
        if (payloadLen < 23) return;
        uint16_t tid = ((uint16_t)pkt.payload[0] << 8) | pkt.payload[1];

        // Update or add to discovered list
        DiscoveredTournament *slot = nullptr;
        for (uint8_t i = 0; i < discoveredCount_; i++) {
            if (discovered_[i].id == tid) { slot = &discovered_[i]; break; }
        }
        if (!slot && discoveredCount_ < MAX_DISCOVERED) {
            slot = &discovered_[discoveredCount_++];
        }
        if (!slot) {
            // Evict oldest
            uint32_t oldest = UINT32_MAX;
            uint8_t oldIdx = 0;
            for (uint8_t i = 0; i < MAX_DISCOVERED; i++) {
                if (discovered_[i].lastSeenMs < oldest) {
                    oldest = discovered_[i].lastSeenMs;
                    oldIdx = i;
                }
            }
            slot = &discovered_[oldIdx];
        }

        slot->id             = tid;
        slot->type           = pkt.payload[2];
        slot->maxPlayers     = pkt.payload[3];
        memset(slot->name, 0, sizeof(slot->name));
        memcpy(slot->name, &pkt.payload[4], 16);
        slot->phase          = pkt.payload[20];
        slot->lateReg        = pkt.payload[21] != 0;
        slot->currentPlayers = pkt.payload[22];
        slot->lastSeenMs     = millis();

        if (state_ == ClientState::IDLE && discoveredCount_ > 0) {
            state_ = ClientState::DISCOVERED;
        }
    }

    void handleRegisterAck(const BattlePacket &pkt, uint8_t payloadLen) {
        if (payloadLen < 8) return;
        uint16_t tid = ((uint16_t)pkt.payload[0] << 8) | pkt.payload[1];
        if (tid != activeTournamentId_) return;

        uint8_t status = pkt.payload[7];
        if (status == 0) {
            mySeed_ = pkt.payload[6];
            state_ = ClientState::WAITING;
            Serial.printf("[TCLIENT] registered OK, seed=%u\n", mySeed_);
        } else {
            Serial.printf("[TCLIENT] registration failed: status=%u\n", status);
            state_ = ClientState::DISCOVERED;
        }
    }

    void handleBracket(const BattlePacket &pkt, uint8_t payloadLen) {
        if (payloadLen < 4) return;
        uint16_t tid = ((uint16_t)pkt.payload[0] << 8) | pkt.payload[1];
        if (tid != activeTournamentId_ && state_ > ClientState::DISCOVERED) return;

        bracketRound_      = pkt.payload[2];
        bracketMatchCount_ = pkt.payload[3];

        uint8_t offset = 4;
        for (uint8_t i = 0; i < bracketMatchCount_ && offset + 3 <= payloadLen; i++) {
            bracketEntries_[i].seed1  = pkt.payload[offset++];
            bracketEntries_[i].seed2  = pkt.payload[offset++];
            bracketEntries_[i].result = pkt.payload[offset++];
        }
    }

    void handleMatchAssign(const BattlePacket &pkt, uint8_t payloadLen) {
        if (payloadLen < 11) return;
        uint16_t tid = ((uint16_t)pkt.payload[0] << 8) | pkt.payload[1];
        if (tid != activeTournamentId_) return;

        uint32_t chip1 = ((uint32_t)pkt.payload[3] << 24) |
                         ((uint32_t)pkt.payload[4] << 16) |
                         ((uint32_t)pkt.payload[5] <<  8) |
                         pkt.payload[6];
        uint32_t chip2 = ((uint32_t)pkt.payload[7] << 24) |
                         ((uint32_t)pkt.payload[8] << 16) |
                         ((uint32_t)pkt.payload[9] <<  8) |
                         pkt.payload[10];

        uint32_t myId = transport_.nodeId();
        if (chip1 != myId && chip2 != myId) return; // not our match

        currentMatchId_ = pkt.payload[2];
        opponentChipId_ = (chip1 == myId) ? chip2 : chip1;
        state_ = ClientState::MATCHED;
        Serial.printf("[TCLIENT] match assigned: vs 0x%08X\n", (unsigned)opponentChipId_);
    }

    void handleResultAck(const BattlePacket &pkt, uint8_t payloadLen) {
        if (payloadLen < 4) return;
        uint16_t tid = ((uint16_t)pkt.payload[0] << 8) | pkt.payload[1];
        if (tid != activeTournamentId_) return;

        if (state_ == ClientState::REPORTING) {
            state_ = ClientState::WAITING; // back to waiting for next match
            Serial.println("[TCLIENT] result confirmed, waiting for next round");
        }
    }

    void handleComplete(const BattlePacket &pkt, uint8_t payloadLen) {
        if (payloadLen < 16) return;
        uint16_t tid = ((uint16_t)pkt.payload[0] << 8) | pkt.payload[1];
        if (tid != activeTournamentId_ && state_ <= ClientState::DISCOVERED) return;

        memset(winnerName_, 0, sizeof(winnerName_));
        memcpy(winnerName_, &pkt.payload[6], 10);
        state_ = ClientState::IDLE;
        Serial.printf("[TCLIENT] tournament complete! winner=%s\n", winnerName_);
    }
};
