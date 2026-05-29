// ── MeshSerial — USB serial link to nRF52 Meshtastic node ────────────────────

#include "MeshSerial.h"
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>

// ── Meshtastic serial framing ─────────────────────────────────────────────────
// Start bytes: 0x94 0xC3
// Then 2-byte big-endian length
// Then protobuf (FromRadio or ToRadio)

static constexpr uint8_t FRAME_START1 = 0x94;
static constexpr uint8_t FRAME_START2 = 0xC3;
static constexpr int     FRAME_HDR_SIZE = 4;  // 2 start + 2 len

// ── Protobuf field tags ───────────────────────────────────────────────────────
// ToRadio (message 1 in meshtastic/mesh.proto):
//   field 1 (packet, len-delim): tag = (1<<3)|2 = 0x0A
// MeshPacket:
//   field 1 (from, varint): tag = (1<<3)|0 = 0x08
//   field 3 (to, varint): tag = (3<<3)|0 = 0x18
//   field 6 (id, varint): tag = (6<<3)|0 = 0x30
//   field 8 (decoded=Data, len-delim): tag = (8<<3)|2 = 0x42
//   field 9 (channel, varint): tag = (9<<3)|0 = 0x48
//   field 14 (want_ack, varint): tag = (14<<3)|0 = 0x70
// Data (MeshPacket.decoded):
//   field 1 (portnum, varint): tag = (1<<3)|0 = 0x08
//   field 2 (payload, len-delim): tag = (2<<3)|2 = 0x12
// FromRadio:
//   field 1 (num/id, varint): tag = 0x08
//   field 2 (packet, len-delim): tag = 0x12
//   field 3 (my_info, len-delim): tag = 0x1A
// MyNodeInfo:
//   field 1 (my_node_num, varint): tag = 0x08

// ── Constructor / Destructor ──────────────────────────────────────────────────

MeshSerial::MeshSerial() {
    memset(rxBuf_, 0, sizeof(rxBuf_));
}

MeshSerial::~MeshSerial() {
    close();
}

// ── Protobuf helpers ──────────────────────────────────────────────────────────

int MeshSerial::writeVarint(uint8_t *out, int pos, uint64_t val) {
    do {
        uint8_t b = val & 0x7F;
        val >>= 7;
        if (val) b |= 0x80;
        out[pos++] = b;
    } while (val);
    return pos;
}

int MeshSerial::writeLenDelim(uint8_t *out, int pos, uint8_t fieldTag,
                               const uint8_t *data, uint16_t dataLen) {
    out[pos++] = fieldTag;
    pos = writeVarint(out, pos, dataLen);
    memcpy(&out[pos], data, dataLen);
    pos += dataLen;
    return pos;
}

bool MeshSerial::readVarint(const uint8_t *buf, int len, int &pos, uint64_t &val) {
    val = 0;
    int shift = 0;
    while (pos < len) {
        uint8_t b = buf[pos++];
        val |= (uint64_t)(b & 0x7F) << shift;
        shift += 7;
        if (!(b & 0x80)) return true;
        if (shift >= 64) return false;
    }
    return false;
}

// ── Encode ToRadio protobuf ───────────────────────────────────────────────────

int MeshSerial::encodeToRadio(uint8_t *out, int outMax,
                               uint32_t destNode, uint32_t channel,
                               const uint8_t *payload, uint16_t payloadLen) {
    // Build Data (portnum + payload)
    uint8_t dataBuf[256];
    int dpos = 0;
    dataBuf[dpos++] = 0x08;  // field 1 (portnum), varint
    dpos = writeVarint(dataBuf, dpos, PORTNUM_PRIVATE_APP);
    dpos = writeLenDelim(dataBuf, dpos, 0x12, payload, payloadLen);  // field 2

    // Build MeshPacket (to + decoded + channel)
    uint8_t pktBuf[320];
    int ppos = 0;
    // field 3 (to): varint
    pktBuf[ppos++] = 0x18;
    ppos = writeVarint(pktBuf, ppos, destNode);
    // field 8 (decoded = Data): len-delim
    ppos = writeLenDelim(pktBuf, ppos, 0x42, dataBuf, (uint16_t)dpos);
    // field 9 (channel): varint
    pktBuf[ppos++] = 0x48;
    ppos = writeVarint(pktBuf, ppos, channel);

    // Build ToRadio (field 1 = packet)
    uint8_t toRadioBuf[400];
    int tpos = 0;
    tpos = writeLenDelim(toRadioBuf, tpos, 0x0A, pktBuf, (uint16_t)ppos);

    // Framed output: 0x94 0xC3 len_hi len_lo [proto]
    if (tpos + FRAME_HDR_SIZE > outMax) return -1;
    out[0] = FRAME_START1;
    out[1] = FRAME_START2;
    out[2] = (tpos >> 8) & 0xFF;
    out[3] = tpos & 0xFF;
    memcpy(&out[4], toRadioBuf, tpos);
    return FRAME_HDR_SIZE + tpos;
}

// ── Decode FromRadio protobuf ─────────────────────────────────────────────────

bool MeshSerial::decodeFromRadio(const uint8_t *pb, uint16_t len, MeshPacketIn &out) {
    memset(&out, 0, sizeof(out));

    int pos = 0;
    while (pos < len) {
        uint64_t tag64;
        if (!readVarint(pb, len, pos, tag64)) return false;
        uint8_t fieldNum = (uint8_t)(tag64 >> 3);
        uint8_t wireType = (uint8_t)(tag64 & 0x07);

        if (wireType == 0) {
            // Varint
            uint64_t val;
            if (!readVarint(pb, len, pos, val)) return false;
            // FromRadio field 1 = num (id) — not used here
        } else if (wireType == 2) {
            // Length-delimited
            uint64_t subLen;
            if (!readVarint(pb, len, pos, subLen)) return false;
            if (pos + (int)subLen > len) return false;
            const uint8_t *sub = &pb[pos];
            int slen = (int)subLen;
            pos += slen;

            if (fieldNum == 2) {
                // FromRadio.packet = MeshPacket
                // Parse MeshPacket fields
                int mpos = 0;
                while (mpos < slen) {
                    uint64_t mtag;
                    if (!readVarint(sub, slen, mpos, mtag)) break;
                    uint8_t mfn = (uint8_t)(mtag >> 3);
                    uint8_t mwt = (uint8_t)(mtag & 0x07);

                    if (mwt == 0) {
                        uint64_t mval;
                        if (!readVarint(sub, slen, mpos, mval)) break;
                        if (mfn == 1) out.fromNode = (uint32_t)mval;  // from
                        if (mfn == 3) out.toNode   = (uint32_t)mval;  // to
                        if (mfn == 9) out.channel  = (uint32_t)mval;  // channel
                    } else if (mwt == 2) {
                        uint64_t msubLen;
                        if (!readVarint(sub, slen, mpos, msubLen)) break;
                        if (mpos + (int)msubLen > slen) break;
                        const uint8_t *msub = &sub[mpos];
                        int mslen = (int)msubLen;
                        mpos += mslen;

                        if (mfn == 8) {
                            // MeshPacket.decoded = Data
                            int dpos = 0;
                            while (dpos < mslen) {
                                uint64_t dtag;
                                if (!readVarint(msub, mslen, dpos, dtag)) break;
                                uint8_t dfn = (uint8_t)(dtag >> 3);
                                uint8_t dwt = (uint8_t)(dtag & 0x07);

                                if (dwt == 0) {
                                    uint64_t dval;
                                    if (!readVarint(msub, mslen, dpos, dval)) break;
                                    // dfn == 1 is portnum — we accept any PRIVATE_APP
                                } else if (dwt == 2) {
                                    uint64_t dsubLen;
                                    if (!readVarint(msub, mslen, dpos, dsubLen)) break;
                                    if (dpos + (int)dsubLen > mslen) break;
                                    if (dfn == 2) {
                                        // Data.payload
                                        uint16_t copyLen = (uint16_t)dsubLen;
                                        if (copyLen > sizeof(out.payload))
                                            copyLen = sizeof(out.payload);
                                        memcpy(out.payload, &msub[dpos], copyLen);
                                        out.payloadLen = copyLen;
                                    }
                                    dpos += (int)dsubLen;
                                } else {
                                    break;  // unknown wire type
                                }
                            }
                        }
                    } else {
                        break;
                    }
                }
                return out.payloadLen > 0;

            } else if (fieldNum == 3) {
                // FromRadio.my_info = MyNodeInfo
                // Parse MyNodeInfo.my_node_num (field 1, varint)
                int ipos = 0;
                uint64_t itag;
                if (readVarint(sub, slen, ipos, itag)) {
                    if ((itag >> 3) == 1 && (itag & 7) == 0) {
                        uint64_t nodeNum;
                        if (readVarint(sub, slen, ipos, nodeNum)) {
                            out.fromNode = (uint32_t)nodeNum;
                            out.toNode   = 0;
                            out.payloadLen = 0;
                            // Signal to caller: this is a MyNodeInfo, not a packet
                            // Use a sentinel payload byte
                            out.payload[0] = 0xFF;
                            out.payloadLen = 0;
                            // We communicate my_node_num via fromNode field
                            return true;
                        }
                    }
                }
            }
        } else {
            // Unknown wire type — skip (best effort)
            break;
        }
    }
    return false;
}

// ── Open serial port ──────────────────────────────────────────────────────────

bool MeshSerial::open(const char *port, int baud) {
    close();

    fd_ = ::open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        LOG_WARN("MeshSerial: cannot open %s: %s", port, strerror(errno));
        return false;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd_, &tty) != 0) {
        LOG_WARN("MeshSerial: tcgetattr failed: %s", strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Raw mode
    cfmakeraw(&tty);

    // Baud rate
    speed_t speed = B115200;
    if (baud == 9600)   speed = B9600;
    if (baud == 57600)  speed = B57600;
    if (baud == 230400) speed = B230400;

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1
    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;

    // No flow control
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Blocking read with 100ms timeout
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        LOG_WARN("MeshSerial: tcsetattr failed: %s", strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    strncpy(portPath_, port, sizeof(portPath_) - 1);
    portPath_[sizeof(portPath_) - 1] = '\0';
    rxLen_ = 0;

    LOG_INFO("MeshSerial: opened %s at %d baud", port, baud);

    // Send want_config to trigger the radio to send MyNodeInfo + config.
    // ToRadio { want_config_id: 1 }
    // Field 100 (want_config_id, varint): tag=0xA0 0x06, value=0x01
    static const uint8_t wantConfig[] = {
        0x94, 0xC3,        // start bytes
        0x00, 0x03,        // length = 3
        0xA0, 0x06, 0x01   // ToRadio.want_config_id = 1
    };
    write(fd_, wantConfig, sizeof(wantConfig));

    return true;
}

void MeshSerial::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        LOG_INFO("MeshSerial: closed %s", portPath_);
    }
    if (relayRxFd_ >= 0) { ::close(relayRxFd_); relayRxFd_ = -1; }
    if (relayTxFd_ >= 0) { ::close(relayTxFd_); relayTxFd_ = -1; }
    if (relayPid_ > 0) {
        kill(relayPid_, SIGTERM);
        waitpid(relayPid_, nullptr, WNOHANG);
        relayPid_ = -1;
        LOG_INFO("MeshSerial: relay subprocess stopped");
    }
    portPath_[0] = '\0';
    rxLen_ = 0;
}

// ── Relay subprocess mode ─────────────────────────────────────────────────────

bool MeshSerial::openRelay(const char *relayScript, const char *serialPort) {
    close();

    // Create two pipes: daemon reads from relay, daemon writes to relay
    int toRelay[2], fromRelay[2];
    if (pipe(toRelay) < 0 || pipe(fromRelay) < 0) {
        LOG_ERROR("MeshSerial: pipe() failed: %s", strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("MeshSerial: fork() failed: %s", strerror(errno));
        return false;
    }

    if (pid == 0) {
        // Child: relay subprocess
        // stdin  ← toRelay[0]   (daemon writes to toRelay[1])
        // stdout → fromRelay[1] (daemon reads from fromRelay[0])
        dup2(toRelay[0],   STDIN_FILENO);
        dup2(fromRelay[1], STDOUT_FILENO);
        ::close(toRelay[1]);
        ::close(fromRelay[0]);
        execlp("python3", "python3", relayScript, serialPort, nullptr);
        // If exec fails:
        fprintf(stderr, "[relay] exec failed: %s\n", strerror(errno));
        _exit(1);
    }

    // Parent: keep the write end of toRelay and read end of fromRelay
    ::close(toRelay[0]);
    ::close(fromRelay[1]);

    relayTxFd_ = toRelay[1];
    relayRxFd_ = fromRelay[0];
    relayPid_  = pid;

    // Make relay read fd non-blocking
    fcntl(relayRxFd_, F_SETFL, O_NONBLOCK);

    snprintf(portPath_, sizeof(portPath_), "relay:%s", serialPort);
    LOG_INFO("MeshSerial: relay subprocess started (pid=%d) for %s", pid, serialPort);
    return true;
}

// ── Poll for incoming data ────────────────────────────────────────────────────

int MeshSerial::poll() {
    // ── Relay mode ────────────────────────────────────────────────────────────
    if (relayPid_ > 0) {
        // Relay framing: [4B from_node BE][2B len BE][payload]
        // Special: from_node=0, len=4 → my node ID frame
        int count = 0;
        while (true) {
            // Read header (6 bytes)
            uint8_t hdr[6];
            ssize_t nh = read(relayRxFd_, hdr, 6);
            if (nh < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                LOG_WARN("MeshSerial: relay pipe read error: %s", strerror(errno));
                close(); break;
            }
            if (nh == 0) { LOG_WARN("MeshSerial: relay exited"); close(); break; }
            if (nh < 6) break;  // partial, wait for more

            uint32_t fromNode = ((uint32_t)hdr[0]<<24)|((uint32_t)hdr[1]<<16)|
                                ((uint32_t)hdr[2]<<8)|hdr[3];
            uint16_t payLen   = ((uint16_t)hdr[4]<<8)|hdr[5];

            uint8_t payload[256] = {};
            ssize_t np = read(relayRxFd_, payload, payLen);
            if (np < (ssize_t)payLen) break;  // partial

            if (fromNode == 0 && payLen == 4) {
                // Node info frame
                uint32_t nodeId = ((uint32_t)payload[0]<<24)|((uint32_t)payload[1]<<16)|
                                  ((uint32_t)payload[2]<<8)|payload[3];
                localNodeId_ = nodeId;
                LOG_INFO("MeshSerial: local node ID = 0x%08X (from relay)", nodeId);
                if (nodeInfoCb_) nodeInfoCb_(nodeId, "");
            } else if (fromNode == 0xFFFFFFF1 && payLen >= 4) {
                // Sentinel: MonsterMesh-channel TEXT_MESSAGE_APP (daycare event
                // broadcast or DM). Payload format = [4B real_sender][text].
                uint32_t realFrom = ((uint32_t)payload[0]<<24)|((uint32_t)payload[1]<<16)|
                                    ((uint32_t)payload[2]<<8)|payload[3];
                uint16_t textLen = payLen - 4;
                MeshPacketIn pkt = {};
                pkt.fromNode   = realFrom;
                pkt.toNode     = 0;
                pkt.channel    = 0xFE;   // sentinel value: MM-channel text
                pkt.payloadLen = (uint16_t)mmMin<size_t>(textLen, sizeof(pkt.payload));
                memcpy(pkt.payload, payload + 4, pkt.payloadLen);
                if (packetCb_) packetCb_(pkt);
                count++;
            } else if (payLen > 0 && packetCb_) {
                MeshPacketIn pkt = {};
                pkt.fromNode  = fromNode;
                pkt.toNode    = 0;
                pkt.channel   = 0;
                pkt.payloadLen = (uint16_t)(payLen < sizeof(pkt.payload) ? payLen : sizeof(pkt.payload));
                memcpy(pkt.payload, payload, pkt.payloadLen);
                packetCb_(pkt);
                count++;
            }
        }
        return count;
    }

    // ── Direct serial mode ────────────────────────────────────────────────────
    if (fd_ < 0) return 0;

    int space = (int)sizeof(rxBuf_) - rxLen_;
    if (space <= 0) { rxLen_ = 0; return 0; }

    ssize_t n = read(fd_, rxBuf_ + rxLen_, space);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EIO || errno == ENODEV || errno == ENXIO) {
            LOG_WARN("MeshSerial: device disconnected (%s)", strerror(errno));
            close();
        }
        return 0;
    }
    if (n == 0) return 0;

    if (rxLen_ == 0 && n > 0) {
        char hex[64] = {};
        int hpos = 0;
        for (int i = 0; i < (int)n && i < 8 && hpos < 60; i++)
            hpos += snprintf(hex + hpos, sizeof(hex) - hpos, "%02X ", rxBuf_[i]);
        LOG_INFO("MeshSerial: rx %zd bytes, first: %s", n, hex);
    }

    rxLen_ += (int)n;
    int beforeLen = rxLen_;
    processRx();
    return (rxLen_ < beforeLen) ? 1 : 0;
}

// ── Parse framed data from rx buffer ─────────────────────────────────────────

void MeshSerial::processRx() {
    while (rxLen_ >= FRAME_HDR_SIZE) {
        // Hunt for start bytes
        int start = -1;
        for (int i = 0; i < rxLen_ - 1; i++) {
            if (rxBuf_[i] == FRAME_START1 && rxBuf_[i + 1] == FRAME_START2) {
                start = i;
                break;
            }
        }

        if (start < 0) {
            // No start bytes — discard all but last byte
            rxLen_ = 1;
            rxBuf_[0] = rxBuf_[rxLen_ - 1];
            return;
        }

        if (start > 0) {
            // Discard bytes before start
            memmove(rxBuf_, rxBuf_ + start, rxLen_ - start);
            rxLen_ -= start;
        }

        if (rxLen_ < FRAME_HDR_SIZE) return;

        uint16_t pbLen = ((uint16_t)rxBuf_[2] << 8) | rxBuf_[3];
        if (pbLen > 512) {
            // Implausible length — resync
            memmove(rxBuf_, rxBuf_ + 2, rxLen_ - 2);
            rxLen_ -= 2;
            continue;
        }

        int frameSize = FRAME_HDR_SIZE + pbLen;
        if (rxLen_ < frameSize) return;  // wait for more data

        // Parse the protobuf
        MeshPacketIn pkt;
        bool ok = parseFromRadio(rxBuf_ + FRAME_HDR_SIZE, pbLen);
        (void)ok;  // parsed and dispatched internally

        // Consume frame
        memmove(rxBuf_, rxBuf_ + frameSize, rxLen_ - frameSize);
        rxLen_ -= frameSize;
    }
}

bool MeshSerial::parseFromRadio(const uint8_t *pb, uint16_t len) {
    MeshPacketIn pkt;
    if (!decodeFromRadio(pb, len, pkt)) return false;

    // Check if this is a MyNodeInfo (fromNode set, payloadLen == 0)
    if (pkt.payloadLen == 0 && pkt.fromNode != 0 && localNodeId_ == 0) {
        localNodeId_ = pkt.fromNode;
        LOG_INFO("MeshSerial: local node ID = 0x%08X", localNodeId_);
        if (nodeInfoCb_) nodeInfoCb_(pkt.fromNode, "");
        return true;
    }

    if (pkt.payloadLen > 0 && packetCb_) {
        packetCb_(pkt);
    }
    return pkt.payloadLen > 0;
}

// ── Send a packet ─────────────────────────────────────────────────────────────

bool MeshSerial::sendPacket(uint32_t destNode, uint32_t channel,
                             const uint8_t *payload, uint16_t len) {
    // ── Relay mode ────────────────────────────────────────────────────────────
    if (relayPid_ > 0) {
        uint8_t hdr[6];
        hdr[0] = (destNode >> 24) & 0xFF;
        hdr[1] = (destNode >> 16) & 0xFF;
        hdr[2] = (destNode >>  8) & 0xFF;
        hdr[3] =  destNode        & 0xFF;
        hdr[4] = (len >> 8) & 0xFF;
        hdr[5] =  len       & 0xFF;
        write(relayTxFd_, hdr, 6);
        write(relayTxFd_, payload, len);
        return true;
    }

    // ── Direct serial mode ────────────────────────────────────────────────────
    if (fd_ < 0) return false;

    uint8_t frameBuf[512];
    int frameLen = encodeToRadio(frameBuf, sizeof(frameBuf),
                                 destNode, channel, payload, len);
    if (frameLen <= 0) {
        LOG_WARN("MeshSerial: encodeToRadio failed (payload too large?)");
        return false;
    }

    ssize_t written = write(fd_, frameBuf, frameLen);
    if (written != frameLen) {
        LOG_WARN("MeshSerial: write error: %s", strerror(errno));
        return false;
    }
    return true;
}

// ── Auto-detect Meshtastic serial port ───────────────────────────────────────

std::string MeshSerial::autoDetect() {
    // Linux names first, then macOS names (T-Deck shows up as usbmodem or usbserial)
    static const char *candidates[] = {
        "/dev/ttyUSB0",   "/dev/ttyACM0",
        "/dev/ttyUSB1",   "/dev/ttyACM1",
        nullptr
    };
    for (int i = 0; candidates[i]; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0) {
            LOG_INFO("MeshSerial: auto-detected %s", candidates[i]);
            return candidates[i];
        }
    }

#ifdef __APPLE__
    // macOS: glob /dev/tty.usbserial-* and /dev/tty.usbmodem*
    static const char *macPrefixes[] = {
        "/dev/tty.usbserial-",
        "/dev/tty.usbmodem",
        "/dev/cu.usbserial-",
        "/dev/cu.usbmodem",
        nullptr
    };
    // Walk /dev looking for matching names
    DIR *d = opendir("/dev");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            for (int j = 0; macPrefixes[j]; j++) {
                size_t plen = strlen(macPrefixes[j]);
                if (strncmp(ent->d_name, macPrefixes[j] + 5, plen - 5) == 0) {
                    char path[64];
                    snprintf(path, sizeof(path), "/dev/%s", ent->d_name);
                    struct stat st;
                    if (stat(path, &st) == 0) {
                        closedir(d);
                        LOG_INFO("MeshSerial: auto-detected %s", path);
                        return path;
                    }
                }
            }
        }
        closedir(d);
    }
#endif

    return "";
}
