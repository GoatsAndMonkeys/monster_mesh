#pragma once
#include "../shared/platform.h"
#include "../shared/BattlePacket.h"
#include "../shared/DaycareTypes.h"
#include <string>
#include <functional>

// Meshtastic serial framing:
// [0x94][0xC3][len_hi][len_lo][protobuf bytes...]
// len is big-endian 16-bit, protobuf is a ToRadio or FromRadio message

// Minimal Meshtastic portnum for MonsterMesh (PRIVATE_APP range)
static constexpr uint32_t PORTNUM_PRIVATE_APP = 256;

struct MeshPacketIn {
    uint32_t fromNode;      // sender node ID
    uint32_t toNode;        // destination node ID (0 = broadcast)
    uint32_t channel;       // channel index
    uint8_t  payload[240];  // raw payload bytes
    uint16_t payloadLen;
};

class MeshSerial {
public:
    // Callback types
    using PacketCallback  = std::function<void(const MeshPacketIn &pkt)>;
    using NodeInfoCallback = std::function<void(uint32_t nodeId, const char *shortName)>;

    MeshSerial();
    ~MeshSerial();

    // Open serial port directly. Returns true on success.
    bool open(const char *port, int baud = 115200);

    // Launch mesh_relay.py as a subprocess and talk to it over pipes.
    // relayScript: path to mesh_relay.py
    // serialPort:  passed as argv[1] to the script
    bool openRelay(const char *relayScript, const char *serialPort);

    void close();
    bool isOpen() const { return fd_ >= 0 || relayPid_ > 0; }
    const char *portPath() const { return portPath_; }

    // Set callbacks
    void setPacketCallback(PacketCallback cb)   { packetCb_  = cb; }
    void setNodeInfoCallback(NodeInfoCallback cb) { nodeInfoCb_ = cb; }

    // Send a MonsterMesh packet to a specific node via the mesh
    // destNode: 0xFFFFFFFF = broadcast
    bool sendPacket(uint32_t destNode, uint32_t channel,
                    const uint8_t *payload, uint16_t len);

    // Poll for incoming data. Call regularly from the main loop.
    // Returns number of packets received.
    int poll();

    // Auto-detect the Meshtastic serial port
    static std::string autoDetect();

    // Get our own node ID (populated after first NodeInfo from radio)
    uint32_t localNodeId() const { return localNodeId_; }

private:
    int fd_ = -1;          // raw serial fd (direct mode)
    int relayRxFd_ = -1;   // pipe from relay subprocess (relay mode)
    int relayTxFd_ = -1;   // pipe to relay subprocess   (relay mode)
    pid_t relayPid_ = -1;  // relay subprocess PID
    char portPath_[64] = {};
    uint32_t localNodeId_ = 0;

    PacketCallback   packetCb_;
    NodeInfoCallback nodeInfoCb_;

    // Receive buffer (shared between direct and relay modes)
    uint8_t rxBuf_[512];
    int rxLen_ = 0;

    // Parse incoming framed data
    void processRx();
    bool parseFromRadio(const uint8_t *pb, uint16_t len);

    // Minimal protobuf encode/decode
    static int encodeToRadio(uint8_t *out, int outMax,
                             uint32_t destNode, uint32_t channel,
                             const uint8_t *payload, uint16_t payloadLen);
    static bool decodeFromRadio(const uint8_t *pb, uint16_t len, MeshPacketIn &out);

    // Protobuf helpers
    static int writeVarint(uint8_t *out, int pos, uint64_t val);
    static int writeLenDelim(uint8_t *out, int pos, uint8_t fieldTag,
                             const uint8_t *data, uint16_t dataLen);
    static bool readVarint(const uint8_t *buf, int len, int &pos, uint64_t &val);
};
