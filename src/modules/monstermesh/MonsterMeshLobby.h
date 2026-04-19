#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "BattlePacket.h"
#include "MeshtasticTransport.h"
#include "Gen1Species.h"
#include "PokemonData.h"

class MonsterMeshBattleShim;
class MonsterMeshEmulator;

struct PeerEntry {
    uint32_t chipId      = 0;
    char     name[11]    = {};
    char     leadName[11]= {};
    uint8_t  leadSpecies = 0;
    uint8_t  leadLevel   = 0;
    uint8_t  partyCount  = 0;
    uint16_t elo         = 0;
    uint32_t lastSeenMs  = 0;
    int16_t  rssi        = 0;
};

struct PlayerStats {
    uint16_t elo    = 1200;
    uint16_t wins   = 0;
    uint16_t losses = 0;
    uint16_t draws  = 0;
};

class MonsterMeshLobby {
public:
    static constexpr uint8_t  MAX_PEERS            = 8;
    static constexpr uint32_t BEACON_INTERVAL_MS   = 120000;
    static constexpr uint32_t PEER_TIMEOUT_MS      = 300000;
    static constexpr uint16_t ELO_DEFAULT          = 1200;
    static constexpr uint16_t ELO_FLOOR            = 100;
    static constexpr uint8_t  ELO_K                = 32;
    static constexpr const char *STATS_PATH        = "/stats.dat";

    enum class State : uint8_t {
        CLOSED, BROWSING, CHALLENGING, INCOMING, PAIRED,
    };

    MonsterMeshLobby(MeshtasticTransport &transport, MonsterMeshEmulator &emu);

    void setShim(MonsterMeshBattleShim *shim) { shim_ = shim; }

    void handlePacket(const uint8_t *buf, size_t len);
    void tick(uint32_t now);
    void open();
    void close();

    void navigateUp();
    void navigateDown();
    void selectPeer();
    void rejectIncoming();

    State   state()      const { return state_; }
    bool    isOpen()     const { return state_ != State::CLOSED; }
    uint8_t peerCount()  const { return peerCount_; }
    uint8_t cursor()     const { return cursor_; }
    const PeerEntry &peer(uint8_t i) const { return peers_[i]; }
    const PlayerStats &stats() const { return stats_; }

    void recordResult(bool won, uint16_t opponentElo);
    void loadStats();
    void saveStats();

private:
    MeshtasticTransport &transport_;
    MonsterMeshEmulator    &emu_;
    MonsterMeshBattleShim  *shim_ = nullptr;

    volatile State  state_      = State::CLOSED;
    PeerEntry       peers_[MAX_PEERS];
    uint8_t         peerCount_  = 0;
    uint8_t         cursor_     = 0;
    PlayerStats     stats_;

    uint32_t        lastBeaconMs_    = 0;
    bool            beaconSent_      = false;

    uint32_t        challengeTarget_ = 0;
    uint32_t        challengeFrom_   = 0;
    uint32_t        challengeMs_     = 0;
    static constexpr uint32_t CHALLENGE_TIMEOUT_MS = 30000;

    void sendBeacon();
    void sendChallenge(uint32_t targetId);
    void sendAccept(uint32_t targetId);
    void sendReject(uint32_t targetId);

    void handleBeacon(const BattlePacket &pkt, uint8_t payloadLen);
    void handleChallenge(const BattlePacket &pkt, uint8_t payloadLen);
    void handleAcceptPkt(const BattlePacket &pkt, uint8_t payloadLen);
    void handleRejectPkt(const BattlePacket &pkt, uint8_t payloadLen);

    PeerEntry *findPeer(uint32_t chipId);
    PeerEntry *addOrUpdatePeer(uint32_t chipId);
    void       expirePeers(uint32_t now);

    static float expectedScore(uint16_t self, uint16_t opponent);
    static uint16_t clampElo(int32_t raw);
};

// Bridge function so BattleShim can forward lobby packets without including this header
void monstermeshLobbyHandlePacket(MonsterMeshLobby *lobby, const uint8_t *buf, size_t len);
