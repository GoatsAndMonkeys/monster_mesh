#pragma once
#include "../shared/platform.h"
#include "../shared/DaycareTypes.h"
#include "../shared/BattlePacket.h"
#include "../battle/WirePartyCodec.h"   // WireParty + TB_WIRE_PARTY_BYTES (protocol V2)
#include "../shared/IpcProtocol.h"
#include "PokemonDaycare.h"
#include "MeshSerial.h"
#include "SaveWatcher.h"
#include "IpcServer.h"
#include <string>

// MonsterMesh traffic goes on the "MonsterMesh" channel, which is the
// secondary channel (index 1) on every node that runs ensureMonsterMeshChannel()
// or has been provisioned by tools/setup_mm_channel.py.
static constexpr uint8_t MONSTERMESH_CHANNEL = 1;

class MonsterMeshDaemon {
public:
    MonsterMeshDaemon();
    ~MonsterMeshDaemon();

    // Initialize all subsystems. Returns true on success.
    bool init(const char *serialPort, const char *saveDir);

    // Set path to mesh_relay.py before init() — daemon uses subprocess instead of raw serial
    void setRelayScript(const char *path) {
        strncpy(relayScript_, path, sizeof(relayScript_) - 1);
    }

    // Main loop iteration. Call in a loop.
    void tick();

    // Signal handler: set this true to stop
    static volatile bool shouldStop;

private:
    PokemonDaycare daycare_;
    MeshSerial     mesh_;
    SaveWatcher    watcher_;
    IpcServer      ipc_;

    // Config
    char serialPort_[64];
    char saveDir_[256];
    char relayScript_[256] = {};

    // Local node info
    char shortName_[5];
    char gameName_[8];

    // Serial reconnect timer
    uint32_t lastSerialRetryMs_ = 0;
    static constexpr uint32_t SERIAL_RETRY_INTERVAL_MS = 5000;

    // Queued incoming PvP challenge (one at a time)
    bool     hasPendingChallenge_ = false;
    uint32_t challengeNodeId_     = 0;
    char     challengerName_[13]  = {};
    uint16_t challengeSessionId_  = 0;       // session ID from the CHALLENGE packet
    // Server's party (received in CHALLENGE, V2 WireParty blob)
    uint8_t  challengePartyMin_[TB_WIRE_PARTY_BYTES] = {};
    bool     hasChallengeParty_   = false;

    // Active PvP battle state (CLIENT role — T-Deck is server)
    bool     pvpActive_         = false;
    uint32_t pvpPeerNodeId_     = 0;
    uint16_t pvpSessionId_      = 0;
    uint8_t  pvpLastAppliedSeq_ = 0;
    uint8_t  pvpTurn_           = 0;

    // PvP SERVER role — Pi initiated the challenge, runs the engine
    bool     pvpServerMode_     = false;
    bool     pvpAwaitingAccept_ = false;
    uint8_t  pvpUpdateSeq_      = 0;

    // Pack a Gen1Party into 109-byte partyMin wire format
    void buildOurWireParty(Gen1BattleEngine::WireParty &out);

    // Send a proper TEXT_BATTLE_ACCEPT or DECLINE to the peer
    void sendBattleAccept(uint32_t peerNodeId, uint16_t sessionId, bool accepted);

    // Send a TEXT_BATTLE_CHALLENGE to a target node (server role)
    void sendBattleChallenge(uint32_t targetNodeId);

    // Callbacks wired to daycare
    void onDaycareEvent(const DaycareEvent &evt);
    void onBeaconSend(const DaycareBeacon &beacon);
    void onDmSend(uint32_t destNodeId, const char *msg);
    void onBroadcast(const char *msg);

    // Callbacks from mesh serial
    void onMeshPacket(const MeshPacketIn &pkt);
    void onNodeInfo(uint32_t nodeId, const char *shortName);

    // IPC message handler
    void onIpcMessage(const std::string &msg);

    // IPC push helpers
    void pushPartyUpdate();
    void pushStatus();
    void pushAchievement(const char *name, const char *desc);
    void pushDaycareEvent(const DaycareEvent &evt);
    void pushNeighbors();

    // Build JSON party array from current daycare state
    std::string buildPartyJson();

    // Serial reconnect
    void tryReconnectSerial();

    uint32_t lastTickMs_ = 0;
};
