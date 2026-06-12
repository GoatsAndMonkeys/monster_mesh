// ── MonsterMesh Daemon — orchestrator ────────────────────────────────────────

#include "MonsterMeshDaemon.h"
#include "../shared/DaycareData.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

volatile bool MonsterMeshDaemon::shouldStop = false;

// ── Constructor / Destructor ──────────────────────────────────────────────────

MonsterMeshDaemon::MonsterMeshDaemon() {
    strncpy(shortName_, "GPI",     sizeof(shortName_) - 1);
    strncpy(gameName_,  "TRAINER", sizeof(gameName_)  - 1);
    shortName_[sizeof(shortName_) - 1] = '\0';
    gameName_[sizeof(gameName_)   - 1] = '\0';
    serialPort_[0] = '\0';
    saveDir_[0] = '\0';
}

MonsterMeshDaemon::~MonsterMeshDaemon() {
    mesh_.close();
    watcher_.stop();
    ipc_.stop();
}

// ── Init ──────────────────────────────────────────────────────────────────────

bool MonsterMeshDaemon::init(const char *serialPort, const char *saveDir) {
    strncpy(serialPort_, serialPort, sizeof(serialPort_) - 1);
    serialPort_[sizeof(serialPort_) - 1] = '\0';
    strncpy(saveDir_, saveDir, sizeof(saveDir_) - 1);
    saveDir_[sizeof(saveDir_) - 1] = '\0';

    // ── Daycare ────────────────────────────────────────────────────────────────
    daycare_.init();

    // Wire daycare callbacks using lambdas (single-core Pi — no thread safety needed)
    daycare_.setSendDm([](uint32_t destNodeId, const char *msg, void *ctx) {
        auto *self = static_cast<MonsterMeshDaemon *>(ctx);
        self->onDmSend(destNodeId, msg);
    }, this);

    daycare_.setBroadcast([](const char *msg, void *ctx) {
        auto *self = static_cast<MonsterMeshDaemon *>(ctx);
        self->onBroadcast(msg);
    }, this);

    daycare_.setSendBeacon([](const DaycareBeacon &beacon, void *ctx) {
        auto *self = static_cast<MonsterMeshDaemon *>(ctx);
        self->onBeaconSend(beacon);
    }, this);

    daycare_.setEventCallback([](const DaycareEvent &evt, void *ctx) {
        auto *self = static_cast<MonsterMeshDaemon *>(ctx);
        self->onDaycareEvent(evt);
    }, this);

    // ── Mesh serial ────────────────────────────────────────────────────────────
    mesh_.setPacketCallback([this](const MeshPacketIn &pkt) {
        onMeshPacket(pkt);
    });
    mesh_.setNodeInfoCallback([this](uint32_t nodeId, const char *sname) {
        onNodeInfo(nodeId, sname);
    });

    // Open serial: relay subprocess mode or direct serial
    bool serialOk = false;
    if (relayScript_[0] != '\0') {
        serialOk = mesh_.openRelay(relayScript_, serialPort_);
    } else {
        if (serialPort_[0] != '\0') {
            serialOk = mesh_.open(serialPort_);
        }
        if (!serialOk) {
            std::string detected = MeshSerial::autoDetect();
            if (!detected.empty()) {
                serialOk = mesh_.open(detected.c_str());
                if (serialOk)
                    strncpy(serialPort_, detected.c_str(), sizeof(serialPort_) - 1);
            }
        }
    }
    if (!serialOk) {
        LOG_WARN("MonsterMeshDaemon: serial not available — will retry");
    }

    // ── Save watcher ───────────────────────────────────────────────────────────
    watcher_.setPartyCallback([this](const Gen1Party &party, const char *savPath) {
        // When a SAV is loaded, do daycare checkIn with raw SAV buffer
        uint8_t rawSav[32768];
        if (watcher_.getRawSav(rawSav, sizeof(rawSav))) {
            // Pull the real Gen 1 trainer name out of the SAV header
            // (offset 0x2598, 11 bytes, terminator 0x50, character-set
            // mapped: 0x80..0x99 = A..Z, 0xA0..0xB9 = a..z, 0xF6..0xFF =
            // 0..9, 0x7F = space).  Replaces the "TRAINER" default so the
            // status bar and beacons show the player's real handle.
            char raw[12] = {};
            for (int i = 0; i < 11; i++) {
                uint8_t b = rawSav[0x2598 + i];
                if (b == 0x50 || b == 0x00) break;
                char c = '?';
                if      (b >= 0x80 && b <= 0x99) c = 'A' + (b - 0x80);
                else if (b >= 0xA0 && b <= 0xB9) c = 'a' + (b - 0xA0);
                else if (b >= 0xF6 && b <= 0xFF) c = '0' + (b - 0xF6);
                else if (b == 0x7F)              c = ' ';
                raw[i] = c;
            }
            if (raw[0]) {
                strncpy(gameName_, raw, sizeof(gameName_) - 1);
                gameName_[sizeof(gameName_) - 1] = '\0';
            }
            daycare_.checkIn(rawSav, shortName_, gameName_);
            pushPartyUpdate();
            // Re-broadcast NODE_INFO so the terminal status bar picks up
            // the freshly-parsed trainer name immediately.
            onNodeInfo(mesh_.localNodeId(), shortName_);
            LOG_INFO("MonsterMeshDaemon: checked in %s (%s) from %s",
                     shortName_, gameName_, savPath);
        }
    });

    if (!watcher_.start(saveDir_)) {
        LOG_WARN("MonsterMeshDaemon: SaveWatcher failed to start for %s", saveDir_);
        // Non-fatal — daemon can run without a SAV file initially
    }

    // ── IPC server ─────────────────────────────────────────────────────────────
    ipc_.setMessageCallback([this](const std::string &msg) {
        onIpcMessage(msg);
    });

    // When a new mmterm client connects, re-broadcast the identity + party
    // immediately so the header bar shows the real shortName/trainerName
    // (instead of the "?" fallback) right away — the periodic broadcast
    // would otherwise leave the new client looking empty for tens of seconds.
    ipc_.setConnectCallback([this]() {
        // Re-emit NODE_INFO using the current shortName_/gameName_.
        char jsonBuf[160];
        snprintf(jsonBuf, sizeof(jsonBuf),
                 "{\"type\":\"NODE_INFO\",\"node_id\":%u,"
                 "\"short_name\":\"%s\",\"game_name\":\"%s\"}",
                 (unsigned)mesh_.localNodeId(), shortName_, gameName_);
        ipc_.push(jsonBuf);
        // Push current party state too — if a SAV is loaded, the new client
        // will see the party list immediately instead of "waiting for daemon…".
        pushPartyUpdate();
    });

    // Ensure state directory exists
    mkdir(MMD_STATE_DIR, 0755);

    if (!ipc_.start(MMD_SOCK_PATH)) {
        LOG_ERROR("MonsterMeshDaemon: IPC server failed to start");
        return false;
    }

    lastTickMs_ = millis();
    LOG_INFO("MonsterMeshDaemon: init complete");
    return true;
}

// ── Main tick ─────────────────────────────────────────────────────────────────

void MonsterMeshDaemon::tick() {
    uint32_t now = millis();

    // Reconnect serial if disconnected
    if (!mesh_.isOpen()) {
        if (now - lastSerialRetryMs_ >= SERIAL_RETRY_INTERVAL_MS) {
            lastSerialRetryMs_ = now;
            tryReconnectSerial();
        }
    } else {
        mesh_.poll();
    }

    watcher_.poll();
    ipc_.poll();
    daycare_.tick(now);

    lastTickMs_ = now;
}

// ── Serial reconnect ──────────────────────────────────────────────────────────

void MonsterMeshDaemon::tryReconnectSerial() {
    bool ok = false;

    // If we were launched in relay mode, relaunch the relay subprocess.
    // First: don't spawn a Python subprocess just to discover the port is
    // missing — check stat() ourselves.  On macOS we also need to translate
    // /dev/tty.usbmodemXXXX to /dev/cu.usbmodemXXXX (the relay does this
    // too, but if neither exists we shouldn't spawn anything).
    if (relayScript_[0] != '\0') {
        char checkPath[64];
        strncpy(checkPath, serialPort_, sizeof(checkPath) - 1);
        checkPath[sizeof(checkPath) - 1] = '\0';
#ifdef __APPLE__
        // tty.* blocks on DCD; cu.* doesn't.  Test cu.* first.
        if (strncmp(checkPath, "/dev/tty.", 9) == 0) {
            char cuPath[64];
            snprintf(cuPath, sizeof(cuPath), "/dev/cu.%s", checkPath + 9);
            struct stat st;
            if (stat(cuPath, &st) == 0) {
                strncpy(checkPath, cuPath, sizeof(checkPath) - 1);
            }
        }
#endif
        struct stat st;
        if (stat(checkPath, &st) != 0) {
            // Stored port is gone — happens whenever the user replugs the
            // node into a different USB slot on macOS (the kernel assigns a
            // fresh /dev/cu.usbmodem<N>).  Walk /dev for a Meshtastic node;
            // if we find one, adopt it and spawn the relay against it.
            std::string detected = MeshSerial::autoDetect();
            if (detected.empty()) {
                // No port at all — don't burn 1-2 seconds spinning up Python
                // just to print "No such file".  Wait for the next tick.
                return;
            }
            LOG_INFO("MonsterMeshDaemon: serial port moved %s -> %s",
                     serialPort_, detected.c_str());
            strncpy(serialPort_, detected.c_str(), sizeof(serialPort_) - 1);
            serialPort_[sizeof(serialPort_) - 1] = '\0';
        }
        ok = mesh_.openRelay(relayScript_, serialPort_);
        if (ok) LOG_INFO("MonsterMeshDaemon: relay subprocess relaunched on %s",
                         serialPort_);
    } else {
        // Direct mode — try stored port then auto-detect
        if (serialPort_[0] != '\0') {
            ok = mesh_.open(serialPort_);
        }
        if (!ok) {
            std::string detected = MeshSerial::autoDetect();
            if (!detected.empty()) {
                ok = mesh_.open(detected.c_str());
                if (ok) {
                    strncpy(serialPort_, detected.c_str(), sizeof(serialPort_) - 1);
                    LOG_INFO("MonsterMeshDaemon: reconnected on %s", serialPort_);
                }
            }
        }
    }

    if (!ok) {
        LOG_WARN("MonsterMeshDaemon: serial reconnect failed -- retry in %us",
                 SERIAL_RETRY_INTERVAL_MS / 1000);
    }
}

// ── Daycare callbacks ─────────────────────────────────────────────────────────

void MonsterMeshDaemon::onDaycareEvent(const DaycareEvent &evt) {
    pushDaycareEvent(evt);

    // If targetNodeId is set, DM the remote trainer
    if (evt.targetNodeId != 0 && mesh_.isOpen()) {
        // Build a simple text message packet
        char dmBuf[220];
        snprintf(dmBuf, sizeof(dmBuf), "[Daycare] %s", evt.remoteMessage[0] ? evt.remoteMessage : evt.message);
        uint8_t payload[220];
        size_t msgLen = strlen(dmBuf);
        if (msgLen > sizeof(payload)) msgLen = sizeof(payload) - 1;
        memcpy(payload, dmBuf, msgLen);
        mesh_.sendPacket(evt.targetNodeId, MONSTERMESH_CHANNEL, payload, (uint16_t)msgLen);
    }
}

void MonsterMeshDaemon::onBeaconSend(const DaycareBeacon &beacon) {
    if (!mesh_.isOpen()) {
        tryReconnectSerial();
        if (!mesh_.isOpen()) {
            ipc_.push("{\"type\":\"BEACON_RESULT\",\"ok\":0,\"reason\":\"no serial\"}");
            return;
        }
    }

    // Fill nodeId from the radio's actual node ID. The DaycareBeacon
    // struct's `type` field IS the 0x60 discriminator on the wire — no
    // separate prefix byte. Matches T-Deck's MonsterMeshModule.cpp:2103.
    DaycareBeacon b = beacon;
    b.type   = static_cast<uint8_t>(PktType::DAYCARE_BEACON);
    b.nodeId = mesh_.localNodeId();

    mesh_.sendPacket(0xFFFFFFFF, MONSTERMESH_CHANNEL,
                     reinterpret_cast<const uint8_t *>(&b), sizeof(DaycareBeacon));
    LOG_INFO("MonsterMeshDaemon: beacon broadcast nodeId=0x%08X party=%d shortName=%s",
             b.nodeId, b.partyCount, b.shortName);

    // Notify terminal
    char jsonBuf[128];
    snprintf(jsonBuf, sizeof(jsonBuf),
             "{\"type\":\"BEACON_RESULT\",\"ok\":1,\"node_id\":%u,\"party\":%d}",
             b.nodeId, b.partyCount);
    ipc_.push(jsonBuf);
}

void MonsterMeshDaemon::onDmSend(uint32_t destNodeId, const char *msg) {
    if (!mesh_.isOpen()) return;

    size_t msgLen = strlen(msg);
    if (msgLen > 230) msgLen = 230;
    mesh_.sendPacket(destNodeId, MONSTERMESH_CHANNEL, (const uint8_t *)msg, (uint16_t)msgLen);
}

void MonsterMeshDaemon::onBroadcast(const char *msg) {
    if (!mesh_.isOpen()) return;

    size_t msgLen = strlen(msg);
    if (msgLen > 230) msgLen = 230;
    mesh_.sendPacket(0xFFFFFFFF, MONSTERMESH_CHANNEL, (const uint8_t *)msg, (uint16_t)msgLen);

    // Also push to IPC
    char jsonBuf[512];
    snprintf(jsonBuf, sizeof(jsonBuf),
             "{\"type\":\"BROADCAST\",\"text\":\"%s\"}",
             msg);
    ipc_.push(jsonBuf);
}

// ── Mesh packet callback ──────────────────────────────────────────────────────

void MonsterMeshDaemon::onMeshPacket(const MeshPacketIn &pkt) {
    if (pkt.payloadLen == 0) return;

    // Sentinel: MonsterMesh-channel TEXT_MESSAGE_APP (daycare event /
    // achievement broadcast / DM). Forward straight to the UI as a
    // DAYCARE_EVENT push so the activity feed surfaces it.
    if (pkt.channel == 0xFE) {
        char text[221] = {};
        uint16_t copyLen = pkt.payloadLen < sizeof(text) - 1
                            ? pkt.payloadLen : sizeof(text) - 1;
        memcpy(text, pkt.payload, copyLen);
        text[copyLen] = '\0';

        // Escape for JSON
        char escaped[260] = {};
        char *dst = escaped;
        char *end = escaped + sizeof(escaped) - 2;
        for (uint16_t i = 0; i < copyLen && dst < end; i++) {
            char c = text[i];
            if (c == '"' || c == '\\') *dst++ = '\\';
            if (c < 0x20) continue;  // drop control chars
            *dst++ = c;
        }
        *dst = '\0';

        char jsonBuf[400];
        snprintf(jsonBuf, sizeof(jsonBuf),
                 "{\"type\":\"DAYCARE_EVENT\",\"text\":\"%s\","
                 "\"xp\":0,\"slot\":0,\"from\":%u}",
                 escaped, (unsigned)pkt.fromNode);
        ipc_.push(jsonBuf);
        LOG_INFO("MonsterMeshDaemon: MM-text from 0x%08X: %s",
                 pkt.fromNode, text);
        return;
    }

    uint8_t pktType = pkt.payload[0];

    if (pktType == static_cast<uint8_t>(PktType::DAYCARE_BEACON)) {
        // Daycare beacon from another node. The wire format is the raw
        // DaycareBeacon struct (no prefix); type=0x60 lives at offset 0.
        if (pkt.payloadLen < sizeof(DaycareBeacon)) {
            LOG_WARN("MonsterMeshDaemon: short DAYCARE_BEACON (%d bytes, want >=%zu)",
                     pkt.payloadLen, sizeof(DaycareBeacon));
            return;
        }
        DaycareBeacon beacon;
        memcpy(&beacon, pkt.payload, sizeof(DaycareBeacon));
        beacon.nodeId = pkt.fromNode;  // override with actual sender node ID

        LOG_INFO("MonsterMeshDaemon: beacon from 0x%08X (%s-%s, %d pkmn)",
                 beacon.nodeId, beacon.shortName, beacon.gameName, beacon.partyCount);
        daycare_.handleBeacon(beacon);

        // Push updated neighbor list to UI so the user sees them appear
        pushNeighbors();

    } else if (pktType == static_cast<uint8_t>(PktType::TEXT_BATTLE_CHALLENGE)) {
        // BattlePacket layout: [0]=type [1]=sessionHi [2]=sessionLo [3]=seq [4..]=payload
        // CHALLENGE payload (T-Deck serverAuthSendChallenge wire format):
        //   BP.payload[0..3] = targetId (4B BE)
        //   BP.payload[4]    = gen
        //   BP.payload[5]    = nameLen
        //   BP.payload[6..]  = name
        //   BP.payload[6+n..] = partyMin (109B)
        // pkt.payload offsets = BP.payload offsets + 4 (BattlePacket header: type,sessionHi,sessionLo,seq)
        if (pkt.payloadLen < 10) return;

        uint16_t sessionId = ((uint16_t)pkt.payload[1] << 8) | pkt.payload[2];
        // payload[4..7] = targetId (skip — we accept challenges addressed to us)
        // payload[8]    = gen
        uint8_t nameLen = pkt.payload[9];   // BP.payload[5]
        if (nameLen > TB_MAX_NAME_LEN) nameLen = TB_MAX_NAME_LEN;
        if ((size_t)(10 + nameLen) > pkt.payloadLen) return;

        memset(challengerName_, 0, sizeof(challengerName_));
        memcpy(challengerName_, pkt.payload + 10, nameLen);
        challengerName_[nameLen] = '\0';

        // Grab partyMin if present
        size_t partyOffset = 10 + nameLen;
        hasChallengeParty_ = false;
        if (partyOffset + TB_PARTY_MIN_BYTES <= pkt.payloadLen) {
            memcpy(challengePartyMin_, pkt.payload + partyOffset, TB_PARTY_MIN_BYTES);
            hasChallengeParty_ = true;
        }

        hasPendingChallenge_  = true;
        challengeNodeId_      = pkt.fromNode;
        challengeSessionId_   = sessionId;
        pvpPeerNodeId_        = pkt.fromNode;
        pvpSessionId_         = sessionId;
        pvpLastAppliedSeq_    = 0;
        pvpTurn_              = 0;

        LOG_INFO("MonsterMeshDaemon: PvP CHALLENGE from 0x%08X (%s) session=0x%04X",
                 challengeNodeId_, challengerName_, sessionId);

        char jsonBuf[256];
        snprintf(jsonBuf, sizeof(jsonBuf),
                 "{\"type\":\"CHALLENGE_RECEIVED\",\"node_id\":%u,\"trainer\":\"%s\"}",
                 challengeNodeId_, challengerName_);
        ipc_.push(jsonBuf);

    } else if (pktType == static_cast<uint8_t>(PktType::TEXT_BATTLE_UPDATE)) {
        // Only handle if we have an active PvP session
        if (!pvpActive_) return;
        uint16_t sessionId = ((uint16_t)pkt.payload[1] << 8) | pkt.payload[2];
        if (sessionId != pvpSessionId_) return;

        uint8_t  seq  = pkt.payload[3];
        // payload[4..] = UPDATE body (turn, flags, hash, conditional sections)
        if (pkt.payloadLen < 10) return;
        const uint8_t *upd = pkt.payload + 4;  // BattlePacket.payload start
        size_t updLen = pkt.payloadLen - 4;

        uint8_t  turn  = upd[0];
        uint16_t flags = ((uint16_t)upd[1] << 8) | upd[2];
        // upd[3..5] = boardHash24 (not checked here)

        pvpTurn_ = turn;
        pvpLastAppliedSeq_ = seq;

        size_t r = 6;  // read cursor past turn+flags+hash
        auto take = [&](size_t n) -> const uint8_t * {
            if (r + n > updLen) return nullptr;
            const uint8_t *p = upd + r;
            r += n;
            return p;
        };

        uint16_t myHp = 0, enemyHp = 0;
        uint8_t  myStatus = 0, enemyStatus = 0;
        uint8_t  myPp[4] = {};
        uint8_t  result = 0;
        bool     needSwitch = (flags & TB_UPD_NEED_PLAYER_SWITCH) != 0;

        if (flags & TB_UPD_HP) {
            const uint8_t *p = take(4);
            if (p) {
                myHp    = ((uint16_t)p[0] << 8) | p[1];
                enemyHp = ((uint16_t)p[2] << 8) | p[3];
            }
        }
        if (flags & TB_UPD_PP) {
            const uint8_t *p = take(4);
            if (p) memcpy(myPp, p, 4);
        }
        if (flags & TB_UPD_SWITCH) take(2);   // active slots (not needed for display here)
        if (flags & TB_UPD_STATUS) {
            const uint8_t *p = take(2);
            if (p) { myStatus = p[0]; enemyStatus = p[1]; }
        }
        if (flags & TB_UPD_RESULT) {
            const uint8_t *p = take(1);
            if (p) result = *p;
        }

        // Collect log lines
        char logJson[512] = {};
        int  logPos = 0;
        logPos += snprintf(logJson + logPos, sizeof(logJson) - logPos, "[");
        if (flags & TB_UPD_LOG) {
            const uint8_t *np = take(1);
            if (np) {
                uint8_t numLines = *np;
                for (uint8_t i = 0; i < numLines; i++) {
                    const uint8_t *lp = take(1); if (!lp) break;
                    uint8_t llen = *lp;
                    const uint8_t *lb = take(llen); if (!lb) break;
                    char line[65] = {};
                    if (llen >= sizeof(line)) llen = sizeof(line) - 1;
                    memcpy(line, lb, llen);
                    line[llen] = '\0';
                    // Escape quotes
                    char esc[130] = {};
                    int ep = 0;
                    for (int j = 0; line[j] && ep < 128; j++) {
                        if (line[j] == '"' || line[j] == '\\') esc[ep++] = '\\';
                        esc[ep++] = line[j];
                    }
                    if (i > 0) logPos += snprintf(logJson + logPos, sizeof(logJson) - logPos, ",");
                    logPos += snprintf(logJson + logPos, sizeof(logJson) - logPos, "\"%s\"", esc);
                }
            }
        }
        snprintf(logJson + logPos, sizeof(logJson) - logPos, "]");

        if (result != 0) { pvpActive_ = false; pvpServerMode_ = false; }  // battle ended

        char jsonBuf[768];
        snprintf(jsonBuf, sizeof(jsonBuf),
                 "{\"type\":\"BATTLE_UPDATE\","
                 "\"turn\":%u,\"seq\":%u,"
                 "\"my_hp\":%u,\"enemy_hp\":%u,"
                 "\"my_pp\":[%u,%u,%u,%u],"
                 "\"my_status\":%u,\"enemy_status\":%u,"
                 "\"result\":%u,\"need_switch\":%s,"
                 "\"log\":%s}",
                 (unsigned)turn, (unsigned)seq,
                 (unsigned)myHp, (unsigned)enemyHp,
                 (unsigned)myPp[0],(unsigned)myPp[1],(unsigned)myPp[2],(unsigned)myPp[3],
                 (unsigned)myStatus, (unsigned)enemyStatus,
                 (unsigned)result, needSwitch ? "1" : "0",
                 logJson);
        ipc_.push(jsonBuf);
        LOG_INFO("MonsterMeshDaemon: UPDATE turn=%u seq=%u myHp=%u enemyHp=%u result=%u",
                 (unsigned)turn, (unsigned)seq, (unsigned)myHp, (unsigned)enemyHp, (unsigned)result);

    } else if (pktType == static_cast<uint8_t>(PktType::TEXT_BATTLE_ACCEPT)) {
        // Server role: we sent the CHALLENGE, the remote node is sending ACCEPT
        if (!pvpServerMode_ || !pvpAwaitingAccept_) return;
        uint16_t sessionId = ((uint16_t)pkt.payload[1] << 8) | pkt.payload[2];
        if (sessionId != pvpSessionId_) return;
        // ACCEPT payload: BP.payload[0]=accepted [1]=nameLen [2..]=name [2+n..]=partyMin
        // pkt.payload offsets: +4 for BattlePacket header
        if (pkt.payloadLen < 6) return;
        uint8_t accepted = pkt.payload[4];  // BP.payload[0]
        if (!accepted) {
            pvpServerMode_     = false;
            pvpAwaitingAccept_ = false;
            ipc_.push("{\"type\":\"PVP_ACCEPT_RECEIVED\",\"accepted\":0}");
            return;
        }
        uint8_t nameLen = pkt.payload[5];   // BP.payload[1]
        if (nameLen > TB_MAX_NAME_LEN) nameLen = TB_MAX_NAME_LEN;
        if ((size_t)(6 + nameLen) > pkt.payloadLen) return;
        char peerName[TB_MAX_NAME_LEN + 1] = {};
        memcpy(peerName, pkt.payload + 6, nameLen);

        pvpAwaitingAccept_ = false;
        pvpActive_         = true;
        pvpPeerNodeId_     = pkt.fromNode;
        pvpUpdateSeq_      = 0;

        // Collect partyMin
        uint8_t partyMinBuf[TB_PARTY_MIN_BYTES] = {};
        int hasParty = 0;
        size_t partyOff = 6 + nameLen;
        if (partyOff + TB_PARTY_MIN_BYTES <= pkt.payloadLen) {
            memcpy(partyMinBuf, pkt.payload + partyOff, TB_PARTY_MIN_BYTES);
            hasParty = 1;
        }

        // Forward to terminal as JSON with partyMin byte array
        char jsonBuf[768];
        int pos = snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"type\":\"PVP_ACCEPT_RECEIVED\",\"accepted\":1,"
            "\"node_id\":%u,\"trainer\":\"%s\",\"has_party\":%d,\"party_min\":[",
            (unsigned)pkt.fromNode, peerName, hasParty);
        for (int i = 0; i < TB_PARTY_MIN_BYTES; i++) {
            if (i > 0) pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, ",");
            pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "%u", (unsigned)partyMinBuf[i]);
        }
        snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "]}");
        ipc_.push(jsonBuf);
        LOG_INFO("MonsterMeshDaemon: PvP ACCEPT from 0x%08X (%s) — server mode active",
                 (unsigned)pkt.fromNode, peerName);

    } else if (pktType == static_cast<uint8_t>(PktType::TEXT_BATTLE_ACTION_V2)) {
        // Server role: client (T-Deck) is sending their move
        if (!pvpServerMode_ || !pvpActive_) return;
        uint16_t sessionId = ((uint16_t)pkt.payload[1] << 8) | pkt.payload[2];
        if (sessionId != pvpSessionId_) return;
        // ACTION_V2 payload starts at pkt.payload+4 (BP.payload[0..])
        if (pkt.payloadLen < 4 + TB_ACTION_BYTES) return;
        uint8_t turn, actionType, index, lastAckedSeq;
        if (!tbUnpackAction(pkt.payload + 4, pkt.payloadLen - 4,
                            turn, actionType, index, lastAckedSeq)) return;
        char jsonBuf[128];
        snprintf(jsonBuf, sizeof(jsonBuf),
                 "{\"type\":\"PVP_ACTION_RECEIVED\","
                 "\"turn\":%u,\"action\":%u,\"index\":%u}",
                 (unsigned)turn, (unsigned)actionType, (unsigned)index);
        ipc_.push(jsonBuf);
        LOG_INFO("MonsterMeshDaemon: ACTION_V2 from client turn=%u action=%u index=%u",
                 (unsigned)turn, (unsigned)actionType, (unsigned)index);
    }
    // Other packet types not handled
}

void MonsterMeshDaemon::onNodeInfo(uint32_t nodeId, const char *sname) {
    LOG_INFO("MonsterMeshDaemon: local node 0x%08X shortName=%s", nodeId, sname ? sname : "");
    if (sname && sname[0]) {
        strncpy(shortName_, sname, sizeof(shortName_) - 1);
        shortName_[sizeof(shortName_) - 1] = '\0';
    }
    // Push node info to terminal so status bar can show the real short
    // name + the SAV's trainer (game_name) — terminal needs both so it can
    // render the header line "  GPI / RED  | Nbrs:N | LEAD Lv##".
    char jsonBuf[160];
    snprintf(jsonBuf, sizeof(jsonBuf),
             "{\"type\":\"NODE_INFO\",\"node_id\":%u,"
             "\"short_name\":\"%s\",\"game_name\":\"%s\"}",
             nodeId, shortName_, gameName_);
    ipc_.push(jsonBuf);
}

// ── IPC message handler ───────────────────────────────────────────────────────

void MonsterMeshDaemon::onIpcMessage(const std::string &msg) {
    const char *s = msg.c_str();

    // Extract "cmd" field using simple strstr search
    const char *cmdKey = strstr(s, "\"cmd\"");
    if (!cmdKey) return;

    // Find the value after "cmd":
    const char *colon = strchr(cmdKey + 5, ':');
    if (!colon) return;

    // Skip whitespace and quote
    const char *valStart = colon + 1;
    while (*valStart == ' ' || *valStart == '\t') valStart++;
    if (*valStart != '"') return;
    valStart++;

    const char *valEnd = strchr(valStart, '"');
    if (!valEnd) return;

    char cmd[64] = {};
    size_t cmdLen = (size_t)(valEnd - valStart);
    if (cmdLen >= sizeof(cmd)) cmdLen = sizeof(cmd) - 1;
    memcpy(cmd, valStart, cmdLen);
    cmd[cmdLen] = '\0';

    if (strcmp(cmd, "GET_PARTY") == 0) {
        pushPartyUpdate();
    } else if (strcmp(cmd, "GET_STATUS") == 0) {
        pushStatus();
        pushNeighbors();
    } else if (strcmp(cmd, "FORCE_BEACON") == 0) {
        daycare_.forceBeacon();
        ipc_.push("{\"type\":\"ACK\",\"cmd\":\"FORCE_BEACON\"}");
    } else if (strcmp(cmd, "FORCE_EVENT") == 0) {
        daycare_.forceEvent();
        ipc_.push("{\"type\":\"ACK\",\"cmd\":\"FORCE_EVENT\"}");
    } else if (strcmp(cmd, "PING") == 0) {
        ipc_.push("{\"type\":\"PONG\"}");
    } else if (strcmp(cmd, "ACCEPT_CHALLENGE") == 0) {
        if (hasPendingChallenge_) {
            sendBattleAccept(challengeNodeId_, challengeSessionId_, true);
            pvpActive_ = true;
            hasPendingChallenge_ = false;
            ipc_.push("{\"type\":\"ACK\",\"cmd\":\"ACCEPT_CHALLENGE\"}");
        }
    } else if (strcmp(cmd, "DECLINE_CHALLENGE") == 0) {
        if (hasPendingChallenge_) {
            sendBattleAccept(challengeNodeId_, challengeSessionId_, false);
            hasPendingChallenge_ = false;
            pvpActive_ = false;
            ipc_.push("{\"type\":\"ACK\",\"cmd\":\"DECLINE_CHALLENGE\"}");
        }
    } else if (strcmp(cmd, "BATTLE_ACTION") == 0) {
        // {"cmd":"BATTLE_ACTION","action":0,"index":2,"turn":5}
        if (!pvpActive_) return;
        auto extractInt = [&](const char *key) -> int {
            char search[32];
            snprintf(search, sizeof(search), "\"%s\":", key);
            const char *p = strstr(s, search);
            if (!p) return -1;
            p += strlen(search);
            while (*p == ' ') p++;
            return atoi(p);
        };
        int action = extractInt("action");
        int index  = extractInt("index");
        int turn   = extractInt("turn");
        if (action < 0 || index < 0 || turn < 0) return;

        // Build TEXT_BATTLE_ACTION_V2 as a BattlePacket
        uint8_t buf[BATTLELINK_MAX_PKT] = {};
        BattlePacket *pkt = (BattlePacket *)buf;
        pkt->type = (uint8_t)PktType::TEXT_BATTLE_ACTION_V2;
        pkt->setSessionId(pvpSessionId_);
        pkt->seq = 0;
        tbPackAction(pkt->payload, (uint8_t)turn, (uint8_t)action,
                     (uint8_t)index, pvpLastAppliedSeq_);
        mesh_.sendPacket(pvpPeerNodeId_, MONSTERMESH_CHANNEL, buf, BATTLELINK_HDR_SIZE + TB_ACTION_BYTES);
        LOG_INFO("MonsterMeshDaemon: ACTION_V2 turn=%u action=%u index=%u seq=%u",
                 (unsigned)turn, (unsigned)action, (unsigned)index,
                 (unsigned)pvpLastAppliedSeq_);
    } else if (strcmp(cmd, "SEND_CHALLENGE") == 0) {
        // {"cmd":"SEND_CHALLENGE","node_id":N}
        const char *p = strstr(s, "\"node_id\":");
        if (!p) return;
        uint32_t targetId = (uint32_t)strtoul(p + 10, nullptr, 10);
        sendBattleChallenge(targetId);

    } else if (strcmp(cmd, "SEND_BATTLE_UPDATE") == 0) {
        // {"cmd":"SEND_BATTLE_UPDATE","data":[N,N,...]}
        // Terminal sends raw UPDATE body bytes; we wrap in BattlePacket and send.
        if (!pvpServerMode_ || !pvpActive_ || !mesh_.isOpen()) return;
        const char *dp = strstr(s, "\"data\":[");
        if (!dp) return;
        dp += 8;
        uint8_t body[BATTLELINK_MAX_PAYLOAD] = {};
        int bodyLen = 0;
        while (*dp && *dp != ']' && bodyLen < (int)sizeof(body)) {
            while (*dp == ' ' || *dp == ',') dp++;
            if (*dp == ']' || !*dp) break;
            body[bodyLen++] = (uint8_t)atoi(dp);
            while (*dp && *dp != ',' && *dp != ']') dp++;
        }
        uint8_t buf[BATTLELINK_MAX_PKT] = {};
        BattlePacket *pkt = (BattlePacket *)buf;
        pkt->type = (uint8_t)PktType::TEXT_BATTLE_UPDATE;
        pkt->setSessionId(pvpSessionId_);
        pkt->seq = pvpUpdateSeq_++;
        memcpy(pkt->payload, body, (size_t)bodyLen);
        mesh_.sendPacket(pvpPeerNodeId_, MONSTERMESH_CHANNEL, buf,
                         (uint16_t)(BATTLELINK_HDR_SIZE + bodyLen));
        LOG_INFO("MonsterMeshDaemon: server UPDATE seq=%u bodyLen=%d to 0x%08X",
                 (unsigned)(pvpUpdateSeq_ - 1), bodyLen, (unsigned)pvpPeerNodeId_);

    } else if (strcmp(cmd, "GET_NEIGHBOR_PARTY") == 0) {
        // {"cmd":"GET_NEIGHBOR_PARTY","node_id":N}
        const char *p = strstr(s, "\"node_id\":");
        if (!p) return;
        uint32_t targetId = (uint32_t)strtoul(p + 10, nullptr, 10);

        const DaycareNeighborPokemon *all = daycare_.getNeighbors();
        const DaycareNeighborPokemon *found = nullptr;
        for (uint8_t i = 0; i < daycare_.getNeighborCount(); i++) {
            if (all[i].nodeId == targetId) { found = &all[i]; break; }
        }
        if (!found) {
            ipc_.push("{\"type\":\"NEIGHBOR_PARTY\",\"ok\":0,\"reason\":\"not found\"}");
            return;
        }

        // Build party JSON: per-slot species + level + nick + moves
        char buf[1400];
        int pos = snprintf(buf, sizeof(buf),
            "{\"type\":\"NEIGHBOR_PARTY\",\"ok\":1,\"node_id\":%u,"
            "\"short_name\":\"%.4s\",\"game_name\":\"%.7s\","
            "\"count\":%d,\"party\":[",
            (unsigned)found->nodeId, found->shortName, found->gameName,
            (int)found->partyCount);
        for (uint8_t i = 0; i < found->partyCount && i < 6; i++) {
            char nick[16] = {};
            for (int j = 0; j < 10 && found->party[i].nickname[j]; j++) {
                char c = found->party[i].nickname[j];
                if (c == '"' || c == '\\' || c < 0x20) continue;
                nick[strlen(nick)] = c;
            }
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "%s{\"dex\":%u,\"level\":%u,\"nick\":\"%s\","
                "\"moves\":[%u,%u,%u,%u]}",
                i ? "," : "",
                (unsigned)found->party[i].species,
                (unsigned)found->party[i].level, nick,
                (unsigned)found->party[i].moves[0],
                (unsigned)found->party[i].moves[1],
                (unsigned)found->party[i].moves[2],
                (unsigned)found->party[i].moves[3]);
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
        ipc_.push(buf);

    } else if (strcmp(cmd, "ANNOUNCE_RESULT") == 0) {
        // {"cmd":"ANNOUNCE_RESULT","text":"..."}
        const char *tk = strstr(s, "\"text\":\"");
        if (!tk) return;
        tk += 8;
        const char *end = strchr(tk, '"');
        if (!end) return;
        char text[200] = {};
        size_t len = (size_t)(end - tk);
        if (len >= sizeof(text)) len = sizeof(text) - 1;
        memcpy(text, tk, len);
        if (mesh_.isOpen()) {
            // Sentinel 0xFFFFFFF2 tells the relay to send as TEXT_MESSAGE_APP
            // on the MonsterMesh channel so peers see it as chat.
            mesh_.sendPacket(0xFFFFFFF2, MONSTERMESH_CHANNEL,
                             (const uint8_t *)text, (uint16_t)len);
            LOG_INFO("ANNOUNCE_RESULT broadcast: %s", text);
        }

    } else if (strcmp(cmd, "CREDIT_XP") == 0) {
        // {"cmd":"CREDIT_XP","slot":N,"xp":N}
        auto extractInt = [&](const char *key) -> int {
            char search[32];
            snprintf(search, sizeof(search), "\"%s\":", key);
            const char *p = strstr(s, search);
            if (!p) return -1;
            p += strlen(search);
            while (*p == ' ') p++;
            return atoi(p);
        };
        int slot = extractInt("slot");
        int xp   = extractInt("xp");
        if (slot >= 0 && slot < 6 && xp > 0) {
            daycare_.creditBattleXp((uint8_t)slot, (uint32_t)xp);
            LOG_INFO("CREDIT_XP slot=%d xp=%d (total now %u)", slot, xp,
                     (unsigned)daycare_.getState().pokemon[slot].totalXpGained);
        }
    } else if (strcmp(cmd, "WRITEBACK_SAV") == 0) {
        // Apply accumulated totalXpGained back to the on-disk .sav.
        if (!watcher_.hasParty()) {
            ipc_.push("{\"type\":\"SAV_WRITEBACK\",\"ok\":0,\"reason\":\"no party\"}");
        } else {
            const char *savPath = watcher_.currentSavPath();
            if (!savPath || !savPath[0]) {
                ipc_.push("{\"type\":\"SAV_WRITEBACK\",\"ok\":0,\"reason\":\"no path\"}");
            } else {
                uint8_t sram[32768] = {};
                if (!watcher_.getRawSav(sram, sizeof(sram))) {
                    ipc_.push("{\"type\":\"SAV_WRITEBACK\",\"ok\":0,\"reason\":\"raw\"}");
                } else {
                    const DaycareState &st = daycare_.getState();
                    uint8_t  dexNums[6]  = {};
                    uint32_t xpGained[6] = {};
                    uint8_t  oldLevels[6] = {};
                    char     nicks[6][12] = {};
                    for (uint8_t i = 0; i < st.partyCount && i < 6; i++) {
                        dexNums[i]   = st.pokemon[i].speciesDex;
                        xpGained[i]  = st.pokemon[i].totalXpGained;
                        // Snapshot pre-patch party level so we can report
                        // the delta to the user as "MEWTWO 70 -> 72".
                        oldLevels[i] = sram[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE
                                            + PKM_LEVEL_PARTY];
                        // Copy nickname (ASCII already)
                        const char *src = st.pokemon[i].nickname;
                        for (int j = 0; j < 11 && src[j]; j++) {
                            char c = src[j];
                            nicks[i][j] = (c < 0x20 || c == '"' || c == '\\') ? '?' : c;
                        }
                    }
                    bool patched = DaycareSavPatcher::checkout(
                        sram, dexNums, xpGained, st.partyCount);
                    if (!patched) {
                        ipc_.push("{\"type\":\"SAV_WRITEBACK\",\"ok\":1,\"applied\":0}");
                    } else {
                        // Atomic write: write to a temp file, fsync, rename.
                        // Guarantees the original .sav is preserved on any
                        // failure (out of disk, killed mid-write, etc.).
                        char tmpPath[300];
                        snprintf(tmpPath, sizeof(tmpPath), "%s.mmtmp", savPath);
                        FILE *f = fopen(tmpPath, "wb");
                        bool wrote_ok = false;
                        if (f) {
                            size_t w = fwrite(sram, 1, sizeof(sram), f);
                            fflush(f);
                            fsync(fileno(f));
                            fclose(f);
                            wrote_ok = (w == sizeof(sram));
                        }
                        if (!wrote_ok) {
                            unlink(tmpPath);  // remove partial temp
                            ipc_.push("{\"type\":\"SAV_WRITEBACK\",\"ok\":0,\"reason\":\"write\"}");
                        } else if (rename(tmpPath, savPath) != 0) {
                            unlink(tmpPath);
                            ipc_.push("{\"type\":\"SAV_WRITEBACK\",\"ok\":0,\"reason\":\"rename\"}");
                        } else {
                            LOG_INFO("SAV writeback OK (atomic): %s", savPath);
                            // Build a per-slot summary so the UI can show
                            // "MEWTWO 70 -> 72 (+1024 XP)" etc.  Levels are
                            // re-read from the now-patched SRAM buffer.
                            char summary[1024];
                            int p = 0;
                            p += snprintf(summary + p, sizeof(summary) - p,
                                "{\"type\":\"SAV_WRITEBACK\",\"ok\":1,"
                                "\"applied\":1,\"path\":\"%s\",\"slots\":[",
                                savPath);
                            bool first_slot = true;
                            for (uint8_t i = 0; i < st.partyCount && i < 6; i++) {
                                if (xpGained[i] == 0) continue;
                                uint8_t newLevel = sram[
                                    SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE
                                    + PKM_LEVEL_PARTY];
                                p += snprintf(summary + p, sizeof(summary) - p,
                                    "%s{\"slot\":%u,\"nick\":\"%s\","
                                    "\"old_level\":%u,\"new_level\":%u,"
                                    "\"xp\":%u}",
                                    first_slot ? "" : ",",
                                    (unsigned)i, nicks[i],
                                    (unsigned)oldLevels[i],
                                    (unsigned)newLevel,
                                    (unsigned)xpGained[i]);
                                first_slot = false;
                            }
                            p += snprintf(summary + p, sizeof(summary) - p, "]}");
                            daycare_.clearTotalXpGained();
                            ipc_.push(summary);
                        }
                    }
                }
            }
        }
    } else {
        LOG_WARN("MonsterMeshDaemon: unknown IPC cmd: %s", cmd);
    }
}

// ── IPC push helpers ──────────────────────────────────────────────────────────

void MonsterMeshDaemon::pushDaycareEvent(const DaycareEvent &evt) {
    // Escape the message for JSON (replace " with \")
    char escaped[220];
    const char *src = evt.message;
    char *dst = escaped;
    char *end = escaped + sizeof(escaped) - 2;
    while (*src && dst < end) {
        if (*src == '"' || *src == '\\') *dst++ = '\\';
        *dst++ = *src++;
    }
    *dst = '\0';

    char jsonBuf[512];
    snprintf(jsonBuf, sizeof(jsonBuf),
             "{\"type\":\"DAYCARE_EVENT\",\"text\":\"%s\",\"xp\":%d,\"slot\":%d}",
             escaped, evt.xp, evt.targetSpeciesIdx);
    ipc_.push(jsonBuf);
}

std::string MonsterMeshDaemon::buildPartyJson() {
    const DaycareState &state = daycare_.getState();
    // The daycare-side state only tracks level/XP/moves; DVs and stat
    // experience live in the raw Gen1Pokemon struct on the SaveWatcher.
    // Merge both so the terminal can rebuild the engine's stats faithfully
    // (otherwise Recover heals 50% of a too-small maxHp etc.).
    const Gen1Party &sav = watcher_.party();
    bool haveSav = watcher_.hasParty();

    char buf[2048];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[");

    for (uint8_t i = 0; i < state.partyCount && i < 6; i++) {
        const DaycarePokemonState &p = state.pokemon[i];
        const char *speciesName = (p.speciesDex >= 1 && p.speciesDex <= DAYCARE_SPECIES_COUNT)
                                  ? daycareSpeciesNames[p.speciesDex] : "???";
        uint8_t effectiveLevel = p.savLevel + (uint8_t)p.totalLevelsGained;

        // Escape nickname
        char nick[32] = {};
        const char *ns = p.nickname;
        char *nd = nick;
        char *nend = nick + sizeof(nick) - 2;
        while (*ns && nd < nend) {
            if (*ns == '"' || *ns == '\\') *nd++ = '\\';
            *nd++ = *ns++;
        }
        *nd = '\0';

        // Pull DVs and stat experience from the raw SAV struct if we have one
        uint8_t  dv0 = 0x88, dv1 = 0x88;    // fallback: avg DVs
        uint16_t hpEv = 0, atkEv = 0, defEv = 0, spdEv = 0, spcEv = 0;
        if (haveSav && i < sav.count) {
            const Gen1Pokemon &m = sav.mons[i];
            dv0  = m.dvs[0];
            dv1  = m.dvs[1];
            hpEv  = (uint16_t)((m.hpExp[0]  << 8) | m.hpExp[1]);
            atkEv = (uint16_t)((m.atkExp[0] << 8) | m.atkExp[1]);
            defEv = (uint16_t)((m.defExp[0] << 8) | m.defExp[1]);
            spdEv = (uint16_t)((m.spdExp[0] << 8) | m.spdExp[1]);
            spcEv = (uint16_t)((m.spcExp[0] << 8) | m.spcExp[1]);
        }

        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"dex\":%d,\"name\":\"%s\",\"nick\":\"%s\","
            "\"level\":%d,\"sav_level\":%d,"
            "\"total_xp_gained\":%u,\"total_hours\":%u,"
            "\"mood\":%d,\"moves\":[%u,%u,%u,%u],"
            "\"dvs\":[%u,%u],"
            "\"stat_exp\":[%u,%u,%u,%u,%u]}",
            p.speciesDex, speciesName, nick,
            effectiveLevel, p.savLevel,
            (unsigned)p.totalXpGained, (unsigned)p.totalHours,
            (int)p.mood,
            (unsigned)p.moves[0], (unsigned)p.moves[1],
            (unsigned)p.moves[2], (unsigned)p.moves[3],
            (unsigned)dv0, (unsigned)dv1,
            (unsigned)hpEv, (unsigned)atkEv, (unsigned)defEv,
            (unsigned)spdEv, (unsigned)spcEv);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]");
    return std::string(buf);
}

void MonsterMeshDaemon::pushPartyUpdate() {
    std::string partyJson = buildPartyJson();
    const DaycareState &state = daycare_.getState();

    // 6 slots × ~250 B JSON each (with dvs + stat_exp arrays) plus wrapper.
    // 4 KB gives comfortable headroom -- with the old 1200 B buffer the 6th
    // slot was getting truncated mid-JSON, leaving "???/Lv0" in the UI.
    char jsonBuf[4096];
    snprintf(jsonBuf, sizeof(jsonBuf),
             "{\"type\":\"PARTY_UPDATE\",\"active\":%s,\"count\":%d,\"party\":%s}",
             daycare_.isActive() ? "1" : "0",
             state.partyCount,
             partyJson.c_str());
    ipc_.push(jsonBuf);
}

void MonsterMeshDaemon::pushStatus() {
    const DaycareState &state = daycare_.getState();

    // Escape last event message
    char escaped[220] = {};
    const char *src = daycare_.getLastEvent().message;
    char *dst = escaped;
    char *end = escaped + sizeof(escaped) - 2;
    while (*src && dst < end) {
        if (*src == '"' || *src == '\\') *dst++ = '\\';
        *dst++ = *src++;
    }
    *dst = '\0';

    char jsonBuf[512];
    snprintf(jsonBuf, sizeof(jsonBuf),
             "{\"type\":\"STATUS\","
             "\"active\":%s,"
             "\"serial_connected\":%s,"
             "\"serial_port\":\"%s\","
             "\"neighbors\":%d,"
             "\"total_events\":%u,"
             "\"last_event\":\"%s\","
             "\"last_event_xp\":%d,"
             "\"sav_path\":\"%s\"}",
             daycare_.isActive() ? "1" : "0",
             mesh_.isOpen() ? "1" : "0",
             mesh_.portPath(),
             daycare_.getNeighborCount(),
             (unsigned)state.totalEvents,
             escaped,
             daycare_.getLastEvent().xp,
             watcher_.currentSavPath());
    ipc_.push(jsonBuf);
}

// Push the current list of MonsterMesh neighbors to the terminal so the
// MESH > Neighbors screen can show them by short name + lead pokemon.
void MonsterMeshDaemon::pushNeighbors() {
    const DaycareNeighborPokemon *n = daycare_.getNeighbors();
    const uint32_t *lastSeen = daycare_.getNeighborLastSeen();
    uint8_t count = daycare_.getNeighborCount();

    char buf[1400];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "{\"type\":\"NEIGHBORS\",\"count\":%d,\"list\":[", count);
    for (uint8_t i = 0; i < count && i < 16; i++) {
        const DaycareNeighborPokemon &nb = n[i];
        // Sanitize strings — strip quote/backslash
        auto safe = [](char *out, const char *in, size_t maxLen) {
            size_t j = 0;
            for (size_t k = 0; in[k] && j < maxLen - 1; k++) {
                char c = in[k];
                if (c == '"' || c == '\\' || c < 0x20) continue;
                out[j++] = c;
            }
            out[j] = '\0';
        };
        char shortBuf[8] = {}, gameBuf[16] = {}, nickBuf[16] = {};
        safe(shortBuf, nb.shortName, sizeof(shortBuf));
        safe(gameBuf,  nb.gameName,  sizeof(gameBuf));
        safe(nickBuf,  nb.nickname,  sizeof(nickBuf));

        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"node_id\":%u,\"short_name\":\"%s\",\"game_name\":\"%s\","
            "\"party_count\":%d,\"lead_nick\":\"%s\",\"lead_level\":%d,"
            "\"lead_dex\":%d,\"last_seen_ms\":%u}",
            (unsigned)nb.nodeId, shortBuf, gameBuf,
            (int)nb.partyCount, nickBuf, (int)nb.level, (int)nb.speciesDex,
            (unsigned)(lastSeen ? lastSeen[i] : 0));
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    ipc_.push(buf);
}

// ── PvP helpers ───────────────────────────────────────────────────────────────

// Pack Gen1Party into 109-byte partyMin wire format:
// 1 byte count + count × 18 bytes (species,level,dvs[2],hpExp[2],atkExp[2],defExp[2],spdExp[2],spcExp[2],moves[4])
size_t MonsterMeshDaemon::packPartyMin(uint8_t out[TB_PARTY_MIN_BYTES], const Gen1Party &party) {
    uint8_t count = party.count < 6 ? party.count : 6;
    out[0] = count;
    size_t pos = 1;
    for (uint8_t i = 0; i < count; i++) {
        const Gen1Pokemon &m = party.mons[i];
        out[pos++] = m.species;
        out[pos++] = m.level ? m.level : m.boxLevel;
        out[pos++] = m.dvs[0];
        out[pos++] = m.dvs[1];
        // hpExp, atkExp, defExp, spdExp, spcExp (2 bytes each BE)
        out[pos++] = m.hpExp[0];  out[pos++] = m.hpExp[1];
        out[pos++] = m.atkExp[0]; out[pos++] = m.atkExp[1];
        out[pos++] = m.defExp[0]; out[pos++] = m.defExp[1];
        out[pos++] = m.spdExp[0]; out[pos++] = m.spdExp[1];
        out[pos++] = m.spcExp[0]; out[pos++] = m.spcExp[1];
        out[pos++] = m.moves[0];  out[pos++] = m.moves[1];
        out[pos++] = m.moves[2];  out[pos++] = m.moves[3];
    }
    // Zero-fill remaining slots
    while (pos < TB_PARTY_MIN_BYTES) out[pos++] = 0;
    return TB_PARTY_MIN_BYTES;
}

void MonsterMeshDaemon::sendBattleAccept(uint32_t peerNodeId, uint16_t sessionId, bool accepted) {
    if (!mesh_.isOpen()) return;

    // Build BattlePacket: type(1)+sessionHi(1)+sessionLo(1)+seq(1)+payload
    // ACCEPT payload: [0]=accepted [1]=nameLen [2..nameLen+1]=name [nameLen+2..]=partyMin
    uint8_t buf[BATTLELINK_MAX_PKT] = {};
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_ACCEPT;
    pkt->setSessionId(sessionId);
    pkt->seq = 0;

    size_t w = 0;
    pkt->payload[w++] = accepted ? 1 : 0;
    uint8_t nlen = (uint8_t)strnlen(shortName_, TB_MAX_NAME_LEN);
    pkt->payload[w++] = nlen;
    memcpy(pkt->payload + w, shortName_, nlen);
    w += nlen;

    if (accepted) {
        // Include our party in partyMin format
        Gen1Party ourParty;
        bool hasParty = watcher_.hasParty();
        if (hasParty) {
            ourParty = watcher_.party();
        } else {
            // Fallback: hardcoded Pikachu Lv30 (moves: ThunderShock=84, TailWhip=39, QuickAttack=98, ThunderWave=86)
            memset(&ourParty, 0, sizeof(ourParty));
            ourParty.count = 1;
            ourParty.species[0] = 0x54;   // internal index for Pikachu
            ourParty.mons[0].species   = 0x54;
            ourParty.mons[0].level     = 30;
            ourParty.mons[0].boxLevel  = 30;
            ourParty.mons[0].moves[0]  = 84;  // ThunderShock
            ourParty.mons[0].moves[1]  = 39;  // TailWhip
            ourParty.mons[0].moves[2]  = 98;  // QuickAttack
            ourParty.mons[0].moves[3]  = 86;  // ThunderWave
            ourParty.mons[0].dvs[0]    = 0xFF; // max DVs
            ourParty.mons[0].dvs[1]    = 0xFF;
        }
        packPartyMin(pkt->payload + w, ourParty);
        w += TB_PARTY_MIN_BYTES;
    }

    size_t totalLen = BATTLELINK_HDR_SIZE + w;
    mesh_.sendPacket(peerNodeId, MONSTERMESH_CHANNEL, buf, (uint16_t)totalLen);
    LOG_INFO("MonsterMeshDaemon: sent ACCEPT(%s) session=0x%04X to 0x%08X",
             accepted ? "yes" : "no", sessionId, peerNodeId);
}

void MonsterMeshDaemon::sendBattleChallenge(uint32_t targetNodeId) {
    if (!mesh_.isOpen()) {
        ipc_.push("{\"type\":\"PVP_CHALLENGE_SENT\",\"ok\":0,\"reason\":\"not connected\"}");
        return;
    }
    uint8_t buf[BATTLELINK_MAX_PKT] = {};
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_CHALLENGE;

    uint16_t session = (uint16_t)((millis() ^ (millis() >> 7)) & 0xFFFF);
    pvpSessionId_      = session;
    pvpPeerNodeId_     = targetNodeId;
    pvpServerMode_     = true;
    pvpAwaitingAccept_ = true;
    pvpActive_         = false;
    pvpUpdateSeq_      = 0;

    pkt->setSessionId(session);
    pkt->seq = 0;

    // CHALLENGE payload: [0..3]=targetId(BE) [4]=gen [5]=nameLen [6..n-1]=name [6+n..]=partyMin
    size_t w = 0;
    pkt->payload[w++] = (targetNodeId >> 24) & 0xFF;
    pkt->payload[w++] = (targetNodeId >> 16) & 0xFF;
    pkt->payload[w++] = (targetNodeId >>  8) & 0xFF;
    pkt->payload[w++] =  targetNodeId        & 0xFF;
    pkt->payload[w++] = 1;  // gen 1

    uint8_t nlen = (uint8_t)strnlen(shortName_, TB_MAX_NAME_LEN);
    pkt->payload[w++] = nlen;
    memcpy(pkt->payload + w, shortName_, nlen);
    w += nlen;

    Gen1Party ourParty;
    if (watcher_.hasParty()) {
        ourParty = watcher_.party();
    } else {
        memset(&ourParty, 0, sizeof(ourParty));
        ourParty.count        = 1;
        ourParty.species[0]   = 0x54;  // Pikachu
        ourParty.mons[0].species  = 0x54;
        ourParty.mons[0].level    = 30;
        ourParty.mons[0].boxLevel = 30;
        ourParty.mons[0].moves[0] = 84;  // ThunderShock
        ourParty.mons[0].moves[1] = 39;  // TailWhip
        ourParty.mons[0].moves[2] = 98;  // QuickAttack
        ourParty.mons[0].moves[3] = 86;  // ThunderWave
        ourParty.mons[0].dvs[0]   = 0xFF;
        ourParty.mons[0].dvs[1]   = 0xFF;
    }
    packPartyMin(pkt->payload + w, ourParty);
    w += TB_PARTY_MIN_BYTES;

    size_t totalLen = BATTLELINK_HDR_SIZE + w;
    mesh_.sendPacket(targetNodeId, MONSTERMESH_CHANNEL, buf, (uint16_t)totalLen);
    LOG_INFO("MonsterMeshDaemon: sent CHALLENGE to 0x%08X session=0x%04X", targetNodeId, session);

    char jsonBuf[64];
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"type\":\"PVP_CHALLENGE_SENT\",\"ok\":1}");
    ipc_.push(jsonBuf);
}

void MonsterMeshDaemon::pushAchievement(const char *name, const char *desc) {
    char jsonBuf[256];
    snprintf(jsonBuf, sizeof(jsonBuf),
             "{\"type\":\"ACHIEVEMENT\",\"name\":\"%s\",\"description\":\"%s\"}",
             name, desc);
    ipc_.push(jsonBuf);
}
