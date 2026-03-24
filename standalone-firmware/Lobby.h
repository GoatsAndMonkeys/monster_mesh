#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/semphr.h>
#include "BattlePacket.h"
#include "RadioTransport.h"
#include "Gen1Species.h"
#include "PokemonData.h"

// Forward declarations
class BattleShim;
class EmulatorApp;
class AlertDriver;

// ── Beacon preferences bitfield ──────────────────────────────────────────────
static constexpr uint8_t PREF_WANTS_BATTLE      = (1 << 0);
static constexpr uint8_t PREF_WANTS_TRADE       = (1 << 1);
static constexpr uint8_t PREF_ALLOW_LV100       = (1 << 2);
static constexpr uint8_t PREF_CASUAL_ONLY       = (1 << 3);
static constexpr uint8_t PREF_TOURNAMENT_READY  = (1 << 4);

// ── Peer entry (one row in the lobby table) ─────────────────────────────────
struct PeerEntry {
    uint32_t chipId      = 0;
    char     name[11]    = {};      // ASCII trainer name
    char     leadName[11]= {};      // ASCII lead Pokémon name
    uint8_t  leadSpecies = 0;       // internal species ID
    uint8_t  leadLevel   = 0;
    uint8_t  partyCount  = 0;
    uint16_t elo         = 0;
    uint32_t lastSeenMs  = 0;       // millis() of last beacon
    int16_t  rssi        = 0;       // signal strength

    // ── Extended beacon fields (v1+) ──────────────────────────────────────
    RomVersion romVersion   = RomVersion::UNKNOWN;
    uint8_t    preferences  = PREF_WANTS_BATTLE;
    uint8_t    partyMaxLevel = 0;
    uint8_t    partyMinLevel = 0;
    uint8_t    hasLv100     = 0;       // count of Lv100 mons
    uint8_t    partySpecies[6] = {};   // all 6 species IDs
    uint8_t    partyLevels[6]  = {};   // level of each party member
    uint16_t   totalBattles = 0;
    uint8_t    beaconVersion = 0;      // 0 = legacy, 1 = extended
};

// ── Player stats (persisted to LittleFS) ────────────────────────────────────
struct PlayerStats {
    uint16_t elo    = 1200;
    uint16_t wins   = 0;
    uint16_t losses = 0;
    uint16_t draws  = 0;
};

// ── Lobby ───────────────────────────────────────────────────────────────────
// Peer-to-peer matchmaking built into the client firmware.
// Beacons on first open + every BEACON_INTERVAL_MS (120 s).
// Maintains a table of up to MAX_PEERS nearby players.
// ELO rating: K=32, floor=100, default=1200.

class Lobby {
public:
    static constexpr uint8_t  MAX_PEERS            = 8;
    static constexpr uint32_t BEACON_INTERVAL_MS   = 120000;  // 2 minutes
    static constexpr uint32_t PEER_TIMEOUT_MS      = 300000;  // 5 minutes
    static constexpr uint16_t ELO_DEFAULT          = 1200;
    static constexpr uint16_t ELO_FLOOR            = 100;
    static constexpr uint8_t  ELO_K                = 32;
    static constexpr const char *STATS_PATH        = "/stats.dat";

    enum class State : uint8_t {
        CLOSED,         // lobby UI not open
        BROWSING,       // viewing peer list
        CHALLENGING,    // sent challenge, waiting for response
        INCOMING,       // received challenge, showing accept/reject prompt
        PAIRED,         // accepted — handing off to BattleShim
    };

    Lobby(RadioTransport &transport, EmulatorApp &emu);

    void setShim(BattleShim *shim) { shim_ = shim; }
    void setAlertDriver(AlertDriver *alert) { alert_ = alert; }

    // Called from radio task (Core 0) when a lobby packet arrives
    void handlePacket(const uint8_t *buf, size_t len);

    // Called every frame from emuTask (Core 1) — drives beacons + timeouts
    void tick(uint32_t now);

    // Open / close the lobby UI
    void open();
    void close();

    // UI navigation (called from LobbyOverlay when lobby captures keys)
    void navigateUp();
    void navigateDown();
    void selectPeer();       // send challenge or accept incoming
    void rejectIncoming();   // reject incoming challenge

    // ── Accessors ───────────────────────────────────────────────────────────
    State   state()      const { return state_; }
    bool    isOpen()     const { return state_ != State::CLOSED; }
    uint8_t peerCount()  const { return peerCount_; }
    uint8_t cursor()     const { return cursor_; }
    const PeerEntry &peer(uint8_t i) const { return peers_[i]; }
    const PlayerStats &stats() const { return stats_; }

    // ── ELO update (call after battle result is known) ──────────────────────
    // won: true = local player won, false = lost
    void recordResult(bool won, uint16_t opponentElo);

    // ── Stats persistence ───────────────────────────────────────────────────
    void loadStats();
    void saveStats();

private:
    RadioTransport &transport_;
    EmulatorApp    &emu_;
    BattleShim     *shim_  = nullptr;
    AlertDriver    *alert_ = nullptr;

    volatile State  state_      = State::CLOSED;
    PeerEntry       peers_[MAX_PEERS];
    uint8_t         peerCount_  = 0;
    uint8_t         cursor_     = 0;
    PlayerStats     stats_;

    uint8_t         preferences_     = PREF_WANTS_BATTLE;  // local player prefs
    uint32_t        lastBeaconMs_    = 0;
    bool            beaconSent_      = false;  // first beacon sent this session?

    // Challenge state
    uint32_t        challengeTarget_ = 0;   // chipId we challenged
    uint32_t        challengeFrom_   = 0;   // chipId that challenged us
    uint32_t        challengeMs_     = 0;   // when challenge was sent/received
    static constexpr uint32_t CHALLENGE_TIMEOUT_MS = 30000;

    // ── Cross-core mutex ────────────────────────────────────────────────────
    // handlePacket() is called from Core 0 (BattleShim radio task).
    // tick() and navigation methods run on Core 1 (emuTask).
    // This mutex protects all shared mutable state.
    SemaphoreHandle_t mutex_ = nullptr;
    static constexpr TickType_t MUTEX_TIMEOUT = pdMS_TO_TICKS(10);

    // ── Internal helpers ────────────────────────────────────────────────────
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

    // ELO math
    static float expectedScore(uint16_t self, uint16_t opponent);
    static uint16_t clampElo(int32_t raw);
};
