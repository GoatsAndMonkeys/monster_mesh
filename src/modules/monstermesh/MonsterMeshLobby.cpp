#include "MonsterMeshLobby.h"
#include "MonsterMeshBattleShim.h"
#include "MonsterMeshEmulator.h"
#include <math.h>
#include <string.h>
#include "MonsterMeshSerial.h"

// Bridge function for BattleShim
void monstermeshLobbyHandlePacket(MonsterMeshLobby *lobby, const uint8_t *buf, size_t len) {
    if (lobby) lobby->handlePacket(buf, len);
}

MonsterMeshLobby::MonsterMeshLobby(MeshtasticTransport &transport, MonsterMeshEmulator &emu)
    : transport_(transport), emu_(emu) {}

void MonsterMeshLobby::open() {
    if (state_ != State::CLOSED) return;
    state_ = State::BROWSING;
    cursor_ = 0;
    beaconSent_ = false;
    MMSer.println("[LOBBY] opened");
}

void MonsterMeshLobby::close() {
    if (state_ == State::CHALLENGING) challengeTarget_ = 0;
    if (state_ == State::INCOMING) {
        sendReject(challengeFrom_);
        challengeFrom_ = 0;
    }
    state_ = State::CLOSED;
    MMSer.println("[LOBBY] closed");
}

void MonsterMeshLobby::tick(uint32_t now) {
    if (state_ == State::CLOSED) return;

    if (!beaconSent_) {
        sendBeacon();
        lastBeaconMs_ = now;
        beaconSent_ = true;
    }

    if (now - lastBeaconMs_ >= BEACON_INTERVAL_MS) {
        sendBeacon();
        lastBeaconMs_ = now;
    }

    expirePeers(now);

    if (state_ == State::CHALLENGING && challengeMs_ &&
        (now - challengeMs_ > CHALLENGE_TIMEOUT_MS)) {
        MMSer.println("[LOBBY] challenge timed out");
        challengeTarget_ = 0;
        state_ = State::BROWSING;
    }
    if (state_ == State::INCOMING && challengeMs_ &&
        (now - challengeMs_ > CHALLENGE_TIMEOUT_MS)) {
        MMSer.println("[LOBBY] incoming challenge expired");
        challengeFrom_ = 0;
        state_ = State::BROWSING;
    }
}

void MonsterMeshLobby::navigateUp() {
    if (state_ != State::BROWSING || peerCount_ == 0) return;
    cursor_ = (cursor_ == 0) ? peerCount_ - 1 : cursor_ - 1;
}

void MonsterMeshLobby::navigateDown() {
    if (state_ != State::BROWSING || peerCount_ == 0) return;
    cursor_ = (cursor_ + 1) % peerCount_;
}

void MonsterMeshLobby::selectPeer() {
    if (state_ == State::BROWSING && peerCount_ > 0) {
        uint32_t target = peers_[cursor_].chipId;
        sendChallenge(target);
        challengeTarget_ = target;
        challengeMs_ = millis();
        state_ = State::CHALLENGING;
    } else if (state_ == State::INCOMING) {
        sendAccept(challengeFrom_);
        state_ = State::PAIRED;
        if (shim_) shim_->pairWith(challengeFrom_);
    }
}

void MonsterMeshLobby::rejectIncoming() {
    if (state_ != State::INCOMING) return;
    sendReject(challengeFrom_);
    challengeFrom_ = 0;
    state_ = State::BROWSING;
}

// ── handlePacket() ──────────────────────────────────────────────────────────

void MonsterMeshLobby::handlePacket(const uint8_t *buf, size_t len) {
    if (len < BATTLELINK_HDR_SIZE) return;

    const BattlePacket &pkt = *reinterpret_cast<const BattlePacket *>(buf);
    auto type = static_cast<PktType>(pkt.type);
    uint8_t payloadLen = (uint8_t)(len - BATTLELINK_HDR_SIZE);

    switch (type) {
        case PktType::LOBBY_BEACON:    handleBeacon(pkt, payloadLen);    break;
        case PktType::LOBBY_CHALLENGE: handleChallenge(pkt, payloadLen); break;
        case PktType::LOBBY_ACCEPT:    handleAcceptPkt(pkt, payloadLen); break;
        case PktType::LOBBY_REJECT:    handleRejectPkt(pkt, payloadLen); break;
        default: break;
    }
}

// ── Beacon ──────────────────────────────────────────────────────────────────

static constexpr uint8_t BEACON_PAYLOAD_SIZE = 24;

void MonsterMeshLobby::sendBeacon() {
    uint8_t pl[BEACON_PAYLOAD_SIZE];
    memset(pl, 0, sizeof(pl));

    uint32_t id = transport_.nodeId();
    pl[0] = (uint8_t)(id >> 24);
    pl[1] = (uint8_t)(id >> 16);
    pl[2] = (uint8_t)(id >>  8);
    pl[3] = (uint8_t)(id);

    uint8_t rawName[Gen1::NAME_LEN];
    for (uint8_t i = 0; i < Gen1::NAME_LEN; i++) {
        rawName[i] = emu_.readWRAM(Gen1::wPlayerName + i);
    }
    gen1NameToAscii(rawName, Gen1::NAME_LEN, (char *)&pl[4], 10);

    uint8_t partyCount = emu_.readWRAM(Gen1::wPartyCount);
    if (partyCount > 6) partyCount = 6;
    pl[16] = partyCount;

    if (partyCount > 0) {
        pl[14] = emu_.readWRAM(Gen1::wPartySpecies);
        pl[15] = emu_.readWRAM(Gen1::wPartyMons + 0x21);
    }

    pl[17] = (uint8_t)(stats_.elo >> 8);
    pl[18] = (uint8_t)(stats_.elo);

    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::LOBBY_BEACON;
    pkt.seq = 0;
    memcpy(pkt.payload, pl, BEACON_PAYLOAD_SIZE);

    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + BEACON_PAYLOAD_SIZE);
}

void MonsterMeshLobby::handleBeacon(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < BEACON_PAYLOAD_SIZE) return;

    uint32_t chipId = ((uint32_t)pkt.payload[0] << 24) |
                      ((uint32_t)pkt.payload[1] << 16) |
                      ((uint32_t)pkt.payload[2] <<  8) |
                      pkt.payload[3];

    if (chipId == transport_.nodeId()) return;

    PeerEntry *p = addOrUpdatePeer(chipId);
    if (!p) return;

    memcpy(p->name, &pkt.payload[4], 10);
    p->name[10] = '\0';
    p->leadSpecies = pkt.payload[14];
    p->leadLevel   = pkt.payload[15];
    p->partyCount  = pkt.payload[16];
    p->elo         = ((uint16_t)pkt.payload[17] << 8) | pkt.payload[18];
    p->lastSeenMs  = millis();
    p->rssi        = transport_.lastRssi();

    const char *sn = gen1SpeciesName(p->leadSpecies);
    strncpy(p->leadName, sn, 10);
    p->leadName[10] = '\0';
}

// ── Challenge / Accept / Reject ─────────────────────────────────────────────
// Payload layout (8 bytes):
//   Bytes 0-3: sender chipId (big-endian)
//   Bytes 4-7: target chipId (big-endian)

void MonsterMeshLobby::sendChallenge(uint32_t targetId) {
    uint32_t id = transport_.nodeId();
    uint8_t pl[8] = {
        (uint8_t)(id       >> 24), (uint8_t)(id       >> 16),
        (uint8_t)(id       >>  8), (uint8_t)(id),
        (uint8_t)(targetId >> 24), (uint8_t)(targetId >> 16),
        (uint8_t)(targetId >>  8), (uint8_t)(targetId)
    };
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::LOBBY_CHALLENGE;
    memcpy(pkt.payload, pl, 8);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 8);
}

void MonsterMeshLobby::sendAccept(uint32_t targetId) {
    uint32_t id = transport_.nodeId();
    uint8_t pl[8] = {
        (uint8_t)(id       >> 24), (uint8_t)(id       >> 16),
        (uint8_t)(id       >>  8), (uint8_t)(id),
        (uint8_t)(targetId >> 24), (uint8_t)(targetId >> 16),
        (uint8_t)(targetId >>  8), (uint8_t)(targetId)
    };
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::LOBBY_ACCEPT;
    memcpy(pkt.payload, pl, 8);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 8);
}

void MonsterMeshLobby::sendReject(uint32_t targetId) {
    uint32_t id = transport_.nodeId();
    uint8_t pl[8] = {
        (uint8_t)(id       >> 24), (uint8_t)(id       >> 16),
        (uint8_t)(id       >>  8), (uint8_t)(id),
        (uint8_t)(targetId >> 24), (uint8_t)(targetId >> 16),
        (uint8_t)(targetId >>  8), (uint8_t)(targetId)
    };
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::LOBBY_REJECT;
    memcpy(pkt.payload, pl, 8);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 8);
}

void MonsterMeshLobby::handleChallenge(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < 8) return;

    uint32_t fromId = ((uint32_t)pkt.payload[0] << 24) |
                      ((uint32_t)pkt.payload[1] << 16) |
                      ((uint32_t)pkt.payload[2] <<  8) |
                      pkt.payload[3];

    uint32_t targetId = ((uint32_t)pkt.payload[4] << 24) |
                        ((uint32_t)pkt.payload[5] << 16) |
                        ((uint32_t)pkt.payload[6] <<  8) |
                        pkt.payload[7];

    if (fromId == transport_.nodeId()) return;
    if (targetId != transport_.nodeId()) return;  // not addressed to us
    if (state_ != State::BROWSING) {
        sendReject(fromId);
        return;
    }

    challengeFrom_ = fromId;
    challengeMs_ = millis();
    state_ = State::INCOMING;
}

void MonsterMeshLobby::handleAcceptPkt(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < 8) return;

    uint32_t fromId = ((uint32_t)pkt.payload[0] << 24) |
                      ((uint32_t)pkt.payload[1] << 16) |
                      ((uint32_t)pkt.payload[2] <<  8) |
                      pkt.payload[3];

    uint32_t targetId = ((uint32_t)pkt.payload[4] << 24) |
                        ((uint32_t)pkt.payload[5] << 16) |
                        ((uint32_t)pkt.payload[6] <<  8) |
                        pkt.payload[7];

    if (targetId != transport_.nodeId()) return;  // not addressed to us
    if (state_ != State::CHALLENGING || fromId != challengeTarget_) return;

    state_ = State::PAIRED;
    if (shim_) shim_->pairWith(fromId);
}

void MonsterMeshLobby::handleRejectPkt(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < 8) return;

    uint32_t fromId = ((uint32_t)pkt.payload[0] << 24) |
                      ((uint32_t)pkt.payload[1] << 16) |
                      ((uint32_t)pkt.payload[2] <<  8) |
                      pkt.payload[3];

    uint32_t targetId = ((uint32_t)pkt.payload[4] << 24) |
                        ((uint32_t)pkt.payload[5] << 16) |
                        ((uint32_t)pkt.payload[6] <<  8) |
                        pkt.payload[7];

    if (targetId != transport_.nodeId()) return;  // not addressed to us
    if (state_ != State::CHALLENGING || fromId != challengeTarget_) return;
    challengeTarget_ = 0;
    state_ = State::BROWSING;
}

// ── Peer table ──────────────────────────────────────────────────────────────

PeerEntry *MonsterMeshLobby::findPeer(uint32_t chipId) {
    for (uint8_t i = 0; i < peerCount_; i++) {
        if (peers_[i].chipId == chipId) return &peers_[i];
    }
    return nullptr;
}

PeerEntry *MonsterMeshLobby::addOrUpdatePeer(uint32_t chipId) {
    PeerEntry *p = findPeer(chipId);
    if (p) return p;

    if (peerCount_ < MAX_PEERS) {
        p = &peers_[peerCount_++];
        memset(p, 0, sizeof(PeerEntry));
        p->chipId = chipId;
        return p;
    }

    uint8_t oldest = 0;
    uint32_t oldestMs = UINT32_MAX;
    for (uint8_t i = 0; i < MAX_PEERS; i++) {
        if (peers_[i].lastSeenMs < oldestMs) {
            oldestMs = peers_[i].lastSeenMs;
            oldest = i;
        }
    }
    p = &peers_[oldest];
    memset(p, 0, sizeof(PeerEntry));
    p->chipId = chipId;
    return p;
}

void MonsterMeshLobby::expirePeers(uint32_t now) {
    for (uint8_t i = 0; i < peerCount_; ) {
        if (now - peers_[i].lastSeenMs > PEER_TIMEOUT_MS) {
            for (uint8_t j = i; j + 1 < peerCount_; j++) {
                peers_[j] = peers_[j + 1];
            }
            peerCount_--;
            if (cursor_ >= peerCount_ && peerCount_ > 0) {
                cursor_ = peerCount_ - 1;
            }
        } else {
            i++;
        }
    }
}

// ── ELO ─────────────────────────────────────────────────────────────────────

float MonsterMeshLobby::expectedScore(uint16_t self, uint16_t opponent) {
    return 1.0f / (1.0f + powf(10.0f, (float)((int)opponent - (int)self) / 400.0f));
}

uint16_t MonsterMeshLobby::clampElo(int32_t raw) {
    if (raw < (int32_t)ELO_FLOOR) return ELO_FLOOR;
    if (raw > 9999) return 9999;
    return (uint16_t)raw;
}

void MonsterMeshLobby::recordResult(bool won, uint16_t opponentElo) {
    float expected = expectedScore(stats_.elo, opponentElo);
    float actual = won ? 1.0f : 0.0f;
    int32_t newElo = (int32_t)stats_.elo + (int32_t)(ELO_K * (actual - expected));
    stats_.elo = clampElo(newElo);

    if (won) stats_.wins++;
    else     stats_.losses++;

    saveStats();
}

void MonsterMeshLobby::loadStats() {
    File f = LittleFS.open(STATS_PATH, "r");
    if (!f || f.size() != sizeof(PlayerStats)) {
        stats_ = PlayerStats{};
        MMSer.println("[LOBBY] no saved stats");
        return;
    }
    f.read((uint8_t *)&stats_, sizeof(PlayerStats));
    f.close();
    MMSer.printf("[LOBBY] loaded stats: ELO=%u W=%u L=%u\n",
                  stats_.elo, stats_.wins, stats_.losses);
}

void MonsterMeshLobby::saveStats() {
    File f = LittleFS.open(STATS_PATH, "w");
    if (!f) return;
    f.write((uint8_t *)&stats_, sizeof(PlayerStats));
    f.close();
}
