#pragma once
#include <Arduino.h>
#include "BattlePacket.h"
#include "RadioTransport.h"

// ── TournamentCoordinator ────────────────────────────────────────────────────
// Manages single-elimination and double-elimination brackets with late
// registration support.  Runs on any T-Deck or a dedicated Mesh Gym node.
// Packet types 0x50–0x58.

class TournamentCoordinator {
public:
    static constexpr uint8_t  MAX_PLAYERS  = 32;
    static constexpr uint8_t  MAX_MATCHES  = 62;   // double-elim worst case for 32
    static constexpr uint32_t ANNOUNCE_INTERVAL_MS = 30000;  // re-broadcast every 30s
    static constexpr uint32_t RESULT_TIMEOUT_MS    = 300000; // 5 min to report result
    static constexpr uint8_t  PLAYER_NAME_MAX     = 16;

    enum class TournamentType : uint8_t {
        SINGLE_ELIM = 0,
        DOUBLE_ELIM = 1,
    };

    enum class Phase : uint8_t {
        IDLE,           // no tournament
        REGISTRATION,   // accepting registrations
        IN_PROGRESS,    // matches being played
        COMPLETE,       // winner decided
    };

    struct Player {
        uint32_t chipId  = 0;
        char     name[11] = {};
        uint16_t elo     = 0;
        uint8_t  seed    = 0;
        uint8_t  losses  = 0;       // double-elim: eliminated when losses >= 2
        bool     eliminated = false;
    };

    struct Match {
        uint8_t  matchId     = 0;
        uint8_t  round       = 0;
        bool     isLosersBracket = false;
        uint8_t  seed1       = 0xFF; // 0xFF = BYE
        uint8_t  seed2       = 0xFF;
        uint32_t chipId1     = 0;
        uint32_t chipId2     = 0;
        uint8_t  result      = 0;    // 0=pending, 1=p1 won, 2=p2 won, 3=timeout
        uint32_t assignedMs  = 0;
    };

    explicit TournamentCoordinator(RadioTransport &transport)
        : transport_(transport) {}

    // ── Create a new tournament ──────────────────────────────────────────────
    void create(TournamentType type, uint8_t maxPlayers, const char *name,
                bool lateRegistration = true) {
        type_        = type;
        phase_       = Phase::REGISTRATION;
        maxPlayers_  = (maxPlayers > MAX_PLAYERS) ? MAX_PLAYERS : maxPlayers;
        lateReg_     = lateRegistration;
        playerCount_ = 0;
        matchCount_  = 0;
        currentRound_ = 0;
        tournamentId_ = (uint16_t)(esp_random() & 0xFFFF);

        memset(name_, 0, sizeof(name_));
        strncpy(name_, name, PLAYER_NAME_MAX - 1);

        // Register coordinator as first player
        registerLocal();

        Serial.printf("[TOURN] created: \"%s\" id=%u type=%s max=%u\n",
                      name_, tournamentId_,
                      type_ == TournamentType::DOUBLE_ELIM ? "double" : "single",
                      maxPlayers_);
    }

    // ── Packet handler (called from radio task or BattleShim) ────────────────
    void handlePacket(const uint8_t *buf, size_t len) {
        if (len < BATTLELINK_HDR_SIZE + 2) return;
        const BattlePacket &pkt = *reinterpret_cast<const BattlePacket *>(buf);
        auto type = static_cast<PktType>(pkt.type);
        uint8_t payloadLen = (uint8_t)(len - BATTLELINK_HDR_SIZE);

        switch (type) {
            case PktType::TOURNAMENT_REGISTER:
                handleRegister(pkt, payloadLen);
                break;
            case PktType::TOURNAMENT_RESULT:
                handleResult(pkt, payloadLen);
                break;
            default: break;
        }
    }

    // ── Frame tick (call every frame or ~1Hz) ────────────────────────────────
    void tick(uint32_t now) {
        if (phase_ == Phase::IDLE) return;

        // Periodic announce
        if (phase_ == Phase::REGISTRATION || phase_ == Phase::IN_PROGRESS) {
            if (now - lastAnnounceMs_ >= ANNOUNCE_INTERVAL_MS) {
                broadcastAnnounce();
                lastAnnounceMs_ = now;
            }
        }

        // Check for match timeouts
        if (phase_ == Phase::IN_PROGRESS) {
            checkMatchTimeouts(now);
        }
    }

    // ── Manual start (coordinator presses a button) ──────────────────────────
    void startTournament() {
        if (phase_ != Phase::REGISTRATION || playerCount_ < 2) return;
        seedPlayers();
        generateBracket();
        phase_ = Phase::IN_PROGRESS;
        currentRound_ = 0;
        assignCurrentRound();
        broadcastBracket();
        Serial.printf("[TOURN] started with %u players\n", playerCount_);
    }

    // ── Accessors ────────────────────────────────────────────────────────────
    Phase           phase()         const { return phase_; }
    uint16_t        tournamentId()  const { return tournamentId_; }
    uint8_t         playerCount()   const { return playerCount_; }
    uint8_t         matchCount()    const { return matchCount_; }
    uint8_t         currentRound()  const { return currentRound_; }
    const char     *name()          const { return name_; }
    TournamentType  tournamentType() const { return type_; }
    bool            lateRegOpen()   const { return lateReg_; }

    const Player &player(uint8_t i) const { return players_[i]; }
    const Match  &match(uint8_t i)  const { return matches_[i]; }

    // Find winner (valid only when phase_ == COMPLETE)
    const Player *winner() const {
        if (phase_ != Phase::COMPLETE) return nullptr;
        for (uint8_t i = 0; i < playerCount_; i++) {
            if (!players_[i].eliminated) return &players_[i];
        }
        return nullptr;
    }

private:
    RadioTransport &transport_;

    uint16_t       tournamentId_  = 0;
    TournamentType type_          = TournamentType::SINGLE_ELIM;
    Phase          phase_         = Phase::IDLE;
    uint8_t        maxPlayers_    = 16;
    bool           lateReg_       = true;
    char           name_[PLAYER_NAME_MAX] = {};

    Player         players_[MAX_PLAYERS];
    uint8_t        playerCount_   = 0;

    Match          matches_[MAX_MATCHES];
    uint8_t        matchCount_    = 0;
    uint8_t        currentRound_  = 0;

    uint32_t       lastAnnounceMs_ = 0;

    // ── Registration ─────────────────────────────────────────────────────────

    void registerLocal() {
        // Register the coordinator's own node as first player
        Player &p = players_[playerCount_++];
        p.chipId = transport_.nodeId();
        strncpy(p.name, "HOST", 10);
        p.elo = 1200;
        p.seed = 0;
        p.losses = 0;
        p.eliminated = false;
    }

    void handleRegister(const BattlePacket &pkt, uint8_t payloadLen) {
        // TOURNAMENT_REGISTER payload: tournamentId[2], chipId[4], elo[2], name[10]
        if (payloadLen < 18) return;

        uint16_t tid = ((uint16_t)pkt.payload[0] << 8) | pkt.payload[1];
        if (tid != tournamentId_) return;

        // Only accept in REGISTRATION or early rounds with late-reg
        if (phase_ != Phase::REGISTRATION &&
            !(phase_ == Phase::IN_PROGRESS && lateReg_ && currentRound_ <= 1)) {
            sendRegisterAck(pkt, 2); // 2 = closed
            return;
        }

        uint32_t chipId = ((uint32_t)pkt.payload[2] << 24) |
                          ((uint32_t)pkt.payload[3] << 16) |
                          ((uint32_t)pkt.payload[4] <<  8) |
                          pkt.payload[5];

        // Check duplicate
        for (uint8_t i = 0; i < playerCount_; i++) {
            if (players_[i].chipId == chipId) {
                sendRegisterAck(pkt, 0); // already registered, still OK
                return;
            }
        }

        if (playerCount_ >= maxPlayers_) {
            sendRegisterAck(pkt, 1); // 1 = full
            return;
        }

        Player &p = players_[playerCount_];
        p.chipId = chipId;
        p.elo = ((uint16_t)pkt.payload[6] << 8) | pkt.payload[7];
        memcpy(p.name, &pkt.payload[8], 10);
        p.name[10] = '\0';
        p.seed = playerCount_;
        p.losses = 0;
        p.eliminated = false;
        playerCount_++;

        sendRegisterAck(pkt, 0); // 0 = OK
        Serial.printf("[TOURN] registered: %s (ELO=%u)\n", p.name, p.elo);
    }

    void sendRegisterAck(const BattlePacket &origPkt, uint8_t status) {
        // TOURNAMENT_REGISTER_ACK: tournamentId[2], chipId[4], seed[1], status[1]
        BattlePacket ack;
        memset(&ack, 0, sizeof(ack));
        ack.type = (uint8_t)PktType::TOURNAMENT_REGISTER_ACK;
        memcpy(ack.payload, origPkt.payload, 6); // tid + chipId
        ack.payload[6] = (playerCount_ > 0) ? playerCount_ - 1 : 0; // seed
        ack.payload[7] = status;
        transport_.send((uint8_t *)&ack, BATTLELINK_HDR_SIZE + 8);
    }

    // ── Seeding (sort by ELO descending) ─────────────────────────────────────

    void seedPlayers() {
        // Simple insertion sort by ELO (descending)
        for (uint8_t i = 1; i < playerCount_; i++) {
            Player tmp = players_[i];
            int8_t j = i - 1;
            while (j >= 0 && players_[j].elo < tmp.elo) {
                players_[j + 1] = players_[j];
                j--;
            }
            players_[j + 1] = tmp;
        }
        // Assign seeds
        for (uint8_t i = 0; i < playerCount_; i++) {
            players_[i].seed = i;
        }
    }

    // ── Bracket generation ───────────────────────────────────────────────────

    void generateBracket() {
        matchCount_ = 0;

        // Round up to next power of 2 for bracket size
        uint8_t bracketSize = 1;
        while (bracketSize < playerCount_) bracketSize <<= 1;

        // Winners bracket first round
        uint8_t round0Matches = bracketSize / 2;
        for (uint8_t i = 0; i < round0Matches; i++) {
            Match &m = matches_[matchCount_];
            m.matchId = matchCount_;
            m.round = 0;
            m.isLosersBracket = false;
            m.seed1 = i;
            m.seed2 = bracketSize - 1 - i;
            m.result = 0;

            // If seed2 >= playerCount_, it's a BYE
            if (m.seed2 >= playerCount_) {
                m.chipId1 = players_[m.seed1].chipId;
                m.chipId2 = 0;
                m.result = 1; // auto-win for seed1
            } else {
                m.chipId1 = players_[m.seed1].chipId;
                m.chipId2 = players_[m.seed2].chipId;
            }
            matchCount_++;
        }

        // For double elimination, losers bracket matches will be generated
        // dynamically as winners bracket results come in.
    }

    void assignCurrentRound() {
        uint32_t now = millis();
        for (uint8_t i = 0; i < matchCount_; i++) {
            Match &m = matches_[i];
            if (m.round == currentRound_ && m.result == 0 &&
                m.chipId1 != 0 && m.chipId2 != 0) {
                m.assignedMs = now;
                sendMatchAssign(m);
            }
        }
    }

    void sendMatchAssign(const Match &m) {
        // TOURNAMENT_MATCH_ASSIGN: tournamentId[2], matchId[1], chipId1[4], chipId2[4]
        BattlePacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = (uint8_t)PktType::TOURNAMENT_MATCH_ASSIGN;
        pkt.payload[0] = (uint8_t)(tournamentId_ >> 8);
        pkt.payload[1] = (uint8_t)(tournamentId_);
        pkt.payload[2] = m.matchId;
        pkt.payload[3] = (uint8_t)(m.chipId1 >> 24);
        pkt.payload[4] = (uint8_t)(m.chipId1 >> 16);
        pkt.payload[5] = (uint8_t)(m.chipId1 >>  8);
        pkt.payload[6] = (uint8_t)(m.chipId1);
        pkt.payload[7] = (uint8_t)(m.chipId2 >> 24);
        pkt.payload[8] = (uint8_t)(m.chipId2 >> 16);
        pkt.payload[9] = (uint8_t)(m.chipId2 >>  8);
        pkt.payload[10] = (uint8_t)(m.chipId2);
        transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 11);
    }

    // ── Result handling ──────────────────────────────────────────────────────

    void handleResult(const BattlePacket &pkt, uint8_t payloadLen) {
        // TOURNAMENT_RESULT: tournamentId[2], matchId[1], winnerChipId[4], loserChipId[4]
        if (payloadLen < 11) return;

        uint16_t tid = ((uint16_t)pkt.payload[0] << 8) | pkt.payload[1];
        if (tid != tournamentId_) return;

        uint8_t matchId = pkt.payload[2];
        if (matchId >= matchCount_) return;

        uint32_t winnerChip = ((uint32_t)pkt.payload[3] << 24) |
                              ((uint32_t)pkt.payload[4] << 16) |
                              ((uint32_t)pkt.payload[5] <<  8) |
                              pkt.payload[6];

        Match &m = matches_[matchId];
        if (m.result != 0) return; // already resolved

        // Determine result
        if (winnerChip == m.chipId1) {
            m.result = 1;
        } else if (winnerChip == m.chipId2) {
            m.result = 2;
        } else {
            return; // invalid reporter
        }

        applyResult(m);
        sendResultAck(matchId);

        // Check if round is complete
        if (isRoundComplete()) {
            advanceRound();
        }
    }

    void applyResult(const Match &m) {
        uint32_t loserChip = (m.result == 1) ? m.chipId2 : m.chipId1;

        for (uint8_t i = 0; i < playerCount_; i++) {
            if (players_[i].chipId == loserChip) {
                players_[i].losses++;
                if (type_ == TournamentType::SINGLE_ELIM ||
                    players_[i].losses >= 2) {
                    players_[i].eliminated = true;
                }
                break;
            }
        }

        Serial.printf("[TOURN] match %u resolved: winner=0x%08X\n",
                      m.matchId,
                      (m.result == 1) ? (unsigned)m.chipId1 : (unsigned)m.chipId2);
    }

    void sendResultAck(uint8_t matchId) {
        BattlePacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = (uint8_t)PktType::TOURNAMENT_RESULT_ACK;
        pkt.payload[0] = (uint8_t)(tournamentId_ >> 8);
        pkt.payload[1] = (uint8_t)(tournamentId_);
        pkt.payload[2] = matchId;
        pkt.payload[3] = 1; // confirmed
        transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 4);
    }

    bool isRoundComplete() {
        for (uint8_t i = 0; i < matchCount_; i++) {
            if (matches_[i].round == currentRound_ && matches_[i].result == 0) {
                return false;
            }
        }
        return true;
    }

    void advanceRound() {
        // Count remaining players
        uint8_t alive = 0;
        Player *lastAlive = nullptr;
        for (uint8_t i = 0; i < playerCount_; i++) {
            if (!players_[i].eliminated) {
                alive++;
                lastAlive = &players_[i];
            }
        }

        if (alive <= 1) {
            phase_ = Phase::COMPLETE;
            broadcastComplete();
            Serial.printf("[TOURN] complete! winner=%s\n",
                          lastAlive ? lastAlive->name : "none");
            return;
        }

        // Generate next round matches from winners of current round
        currentRound_++;
        uint8_t prevRoundStart = 0;
        uint8_t prevRoundEnd = matchCount_;

        // Collect winners from previous round
        uint8_t winnerCount = 0;
        uint32_t roundWinners[MAX_PLAYERS / 2];
        for (uint8_t i = prevRoundStart; i < prevRoundEnd; i++) {
            Match &m = matches_[i];
            if (m.round != currentRound_ - 1) continue;
            if (m.result == 1) roundWinners[winnerCount++] = m.chipId1;
            else if (m.result == 2) roundWinners[winnerCount++] = m.chipId2;
        }

        // Create next round matches
        for (uint8_t i = 0; i + 1 < winnerCount; i += 2) {
            if (matchCount_ >= MAX_MATCHES) break;
            Match &m = matches_[matchCount_];
            m.matchId = matchCount_;
            m.round = currentRound_;
            m.isLosersBracket = false;
            m.chipId1 = roundWinners[i];
            m.chipId2 = roundWinners[i + 1];
            m.result = 0;

            // Find seeds
            for (uint8_t j = 0; j < playerCount_; j++) {
                if (players_[j].chipId == m.chipId1) m.seed1 = players_[j].seed;
                if (players_[j].chipId == m.chipId2) m.seed2 = players_[j].seed;
            }
            matchCount_++;
        }

        // Odd winner gets a BYE
        if (winnerCount % 2 == 1 && matchCount_ < MAX_MATCHES) {
            Match &m = matches_[matchCount_];
            m.matchId = matchCount_;
            m.round = currentRound_;
            m.chipId1 = roundWinners[winnerCount - 1];
            m.chipId2 = 0;
            m.result = 1; // auto-win
            matchCount_++;
        }

        assignCurrentRound();
        broadcastBracket();
    }

    void checkMatchTimeouts(uint32_t now) {
        for (uint8_t i = 0; i < matchCount_; i++) {
            Match &m = matches_[i];
            if (m.result == 0 && m.assignedMs > 0 &&
                (now - m.assignedMs) > RESULT_TIMEOUT_MS) {
                m.result = 3; // timeout — both forfeit in single-elim
                Serial.printf("[TOURN] match %u timed out\n", m.matchId);
                // Mark both as having a loss
                for (uint8_t j = 0; j < playerCount_; j++) {
                    if (players_[j].chipId == m.chipId1 ||
                        players_[j].chipId == m.chipId2) {
                        players_[j].losses++;
                        if (type_ == TournamentType::SINGLE_ELIM ||
                            players_[j].losses >= 2) {
                            players_[j].eliminated = true;
                        }
                    }
                }
                if (isRoundComplete()) advanceRound();
            }
        }
    }

    // ── Broadcast helpers ────────────────────────────────────────────────────

    void broadcastAnnounce() {
        // TOURNAMENT_ANNOUNCE: tournamentId[2], type[1], maxPlayers[1], name[16], status[1], lateRegOpen[1]
        BattlePacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = (uint8_t)PktType::TOURNAMENT_ANNOUNCE;
        pkt.payload[0] = (uint8_t)(tournamentId_ >> 8);
        pkt.payload[1] = (uint8_t)(tournamentId_);
        pkt.payload[2] = (uint8_t)type_;
        pkt.payload[3] = maxPlayers_;
        memcpy(&pkt.payload[4], name_, PLAYER_NAME_MAX);
        pkt.payload[20] = (uint8_t)phase_;
        pkt.payload[21] = lateReg_ ? 1 : 0;
        pkt.payload[22] = playerCount_;
        transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 23);
    }

    void broadcastBracket() {
        // TOURNAMENT_BRACKET: tournamentId[2], round[1], matchCount[1],
        //   N x {seed1[1], seed2[1], result[1]}
        BattlePacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = (uint8_t)PktType::TOURNAMENT_BRACKET;
        pkt.payload[0] = (uint8_t)(tournamentId_ >> 8);
        pkt.payload[1] = (uint8_t)(tournamentId_);
        pkt.payload[2] = currentRound_;
        pkt.payload[3] = matchCount_;

        uint8_t offset = 4;
        for (uint8_t i = 0; i < matchCount_ && offset + 3 <= BATTLELINK_MAX_PAYLOAD; i++) {
            pkt.payload[offset++] = matches_[i].seed1;
            pkt.payload[offset++] = matches_[i].seed2;
            pkt.payload[offset++] = matches_[i].result;
        }
        transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + offset);
    }

    void broadcastComplete() {
        // TOURNAMENT_COMPLETE: tournamentId[2], winnerChipId[4], winnerName[10]
        const Player *w = winner();
        BattlePacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = (uint8_t)PktType::TOURNAMENT_COMPLETE;
        pkt.payload[0] = (uint8_t)(tournamentId_ >> 8);
        pkt.payload[1] = (uint8_t)(tournamentId_);
        if (w) {
            pkt.payload[2] = (uint8_t)(w->chipId >> 24);
            pkt.payload[3] = (uint8_t)(w->chipId >> 16);
            pkt.payload[4] = (uint8_t)(w->chipId >>  8);
            pkt.payload[5] = (uint8_t)(w->chipId);
            memcpy(&pkt.payload[6], w->name, 10);
        }
        transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 16);
    }
};
