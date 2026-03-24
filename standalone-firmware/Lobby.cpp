#include "Lobby.h"
#include "BattleShim.h"
#include "EmulatorApp.h"
#include "AlertDriver.h"
#include <math.h>
#include <string.h>

// ── Constructor ─────────────────────────────────────────────────────────────

Lobby::Lobby(RadioTransport &transport, EmulatorApp &emu)
    : transport_(transport), emu_(emu) {
    // Note: do NOT call loadStats() here — LittleFS isn't mounted yet
    // (globals construct before setup()). Call loadStats() from setup().
}

// ── Open / Close ────────────────────────────────────────────────────────────

void Lobby::open() {
    if (state_ != State::CLOSED) return;
    state_ = State::BROWSING;
    cursor_ = 0;
    beaconSent_ = false;  // force immediate beacon
    Serial.println("[LOBBY] opened");
}

void Lobby::close() {
    if (state_ == State::CHALLENGING) {
        // Cancel outgoing challenge silently
        challengeTarget_ = 0;
    }
    if (state_ == State::INCOMING) {
        sendReject(challengeFrom_);
        challengeFrom_ = 0;
    }
    state_ = State::CLOSED;
    Serial.println("[LOBBY] closed");
}

// ── tick() — called every frame from emuTask ────────────────────────────────

void Lobby::tick(uint32_t now) {
    if (state_ == State::CLOSED) return;

    // ── Send first beacon immediately on open ──────────────────────────────
    if (!beaconSent_) {
        sendBeacon();
        lastBeaconMs_ = now;
        beaconSent_ = true;
    }

    // ── Periodic beacon ────────────────────────────────────────────────────
    if (now - lastBeaconMs_ >= BEACON_INTERVAL_MS) {
        sendBeacon();
        lastBeaconMs_ = now;
    }

    // ── Expire stale peers ─────────────────────────────────────────────────
    expirePeers(now);

    // ── Challenge timeout ──────────────────────────────────────────────────
    if (state_ == State::CHALLENGING && challengeMs_ &&
        (now - challengeMs_ > CHALLENGE_TIMEOUT_MS)) {
        Serial.println("[LOBBY] challenge timed out");
        challengeTarget_ = 0;
        state_ = State::BROWSING;
    }
    if (state_ == State::INCOMING && challengeMs_ &&
        (now - challengeMs_ > CHALLENGE_TIMEOUT_MS)) {
        Serial.println("[LOBBY] incoming challenge expired");
        challengeFrom_ = 0;
        state_ = State::BROWSING;
    }
}

// ── UI navigation ───────────────────────────────────────────────────────────

void Lobby::navigateUp() {
    if (state_ != State::BROWSING || peerCount_ == 0) return;
    cursor_ = (cursor_ == 0) ? peerCount_ - 1 : cursor_ - 1;
}

void Lobby::navigateDown() {
    if (state_ != State::BROWSING || peerCount_ == 0) return;
    cursor_ = (cursor_ + 1) % peerCount_;
}

void Lobby::selectPeer() {
    if (state_ == State::BROWSING && peerCount_ > 0) {
        // Send challenge to selected peer
        uint32_t target = peers_[cursor_].chipId;
        sendChallenge(target);
        challengeTarget_ = target;
        challengeMs_ = millis();
        state_ = State::CHALLENGING;
        Serial.printf("[LOBBY] challenging 0x%08X\n", (unsigned)target);
    } else if (state_ == State::INCOMING) {
        // Accept incoming challenge
        sendAccept(challengeFrom_);
        // Transition to PAIRED — BattleShim will take over
        state_ = State::PAIRED;
        if (shim_) {
            shim_->pairWith(challengeFrom_);
        }
        Serial.printf("[LOBBY] accepted challenge from 0x%08X\n", (unsigned)challengeFrom_);
    }
}

void Lobby::rejectIncoming() {
    if (state_ != State::INCOMING) return;
    sendReject(challengeFrom_);
    challengeFrom_ = 0;
    state_ = State::BROWSING;
    Serial.println("[LOBBY] rejected incoming challenge");
}

// ── handlePacket() — called from radio task ─────────────────────────────────

void Lobby::handlePacket(const uint8_t *buf, size_t len) {
    if (len < BATTLELINK_HDR_SIZE) return;

    const BattlePacket &pkt = *reinterpret_cast<const BattlePacket *>(buf);
    auto type = static_cast<PktType>(pkt.type);
    uint8_t payloadLen = (uint8_t)(len - BATTLELINK_HDR_SIZE);

    switch (type) {
        case PktType::LOBBY_BEACON:
            handleBeacon(pkt, payloadLen);
            break;
        case PktType::LOBBY_CHALLENGE:
            handleChallenge(pkt, payloadLen);
            break;
        case PktType::LOBBY_ACCEPT:
            handleAcceptPkt(pkt, payloadLen);
            break;
        case PktType::LOBBY_REJECT:
            handleRejectPkt(pkt, payloadLen);
            break;
        default:
            break;
    }
}

// ── Beacon ──────────────────────────────────────────────────────────────────
// Extended beacon payload v1 (48 bytes):
//   [0-3]   chipId (BE)
//   [4-13]  trainer name (ASCII, 10 chars, null-padded)
//   [14]    lead species (internal ID)
//   [15]    lead level
//   [16]    party count
//   [17-18] ELO (BE)
//   [19]    ROM version (RomVersion enum)
//   [20]    preferences bitfield
//   [21]    party max level
//   [22]    party min level
//   [23]    count of Lv100 mons
//   [24-29] all 6 party species IDs
//   [30-35] all 6 party levels
//   [36-37] total battles (BE)
//   [38]    beacon version (1)
//   [39-47] reserved (zero)

static constexpr uint8_t BEACON_PAYLOAD_SIZE     = 48;
static constexpr uint8_t BEACON_PAYLOAD_SIZE_V0  = 24;  // legacy compat

void Lobby::sendBeacon() {
    uint8_t pl[BEACON_PAYLOAD_SIZE];
    memset(pl, 0, sizeof(pl));

    // Chip ID
    uint32_t id = transport_.nodeId();
    pl[0] = (uint8_t)(id >> 24);
    pl[1] = (uint8_t)(id >> 16);
    pl[2] = (uint8_t)(id >>  8);
    pl[3] = (uint8_t)(id);

    // Trainer name from WRAM (Gen1-encoded → ASCII)
    uint8_t rawName[Gen1::NAME_LEN];
    for (uint8_t i = 0; i < Gen1::NAME_LEN; i++) {
        rawName[i] = emu_.readWRAM(Gen1::wPlayerName + i);
    }
    gen1NameToAscii(rawName, Gen1::NAME_LEN, (char *)&pl[4], 10);

    // Party info from WRAM
    uint8_t partyCount = emu_.readWRAM(Gen1::wPartyCount);
    if (partyCount > 6) partyCount = 6;
    pl[16] = partyCount;

    uint8_t maxLv = 0, minLv = 255, lv100Count = 0;
    for (uint8_t i = 0; i < partyCount; i++) {
        uint8_t species = emu_.readWRAM(Gen1::wPartySpecies + i);
        uint8_t level   = emu_.readWRAM(Gen1::wPartyMons + i * Gen1::PARTY_MON_SIZE + 0x21);

        // Lead mon (first slot)
        if (i == 0) {
            pl[14] = species;
            pl[15] = level;
        }

        // Extended: all party species and levels
        pl[24 + i] = species;
        pl[30 + i] = level;

        if (level > maxLv) maxLv = level;
        if (level < minLv) minLv = level;
        if (level >= 100)  lv100Count++;
    }

    // ELO
    pl[17] = (uint8_t)(stats_.elo >> 8);
    pl[18] = (uint8_t)(stats_.elo);

    // Extended fields
    pl[19] = (uint8_t)emu_.romVersion();
    pl[20] = preferences_;
    pl[21] = (partyCount > 0) ? maxLv : 0;
    pl[22] = (partyCount > 0) ? minLv : 0;
    pl[23] = lv100Count;

    // Total battles
    uint16_t totalBattles = stats_.wins + stats_.losses;
    pl[36] = (uint8_t)(totalBattles >> 8);
    pl[37] = (uint8_t)(totalBattles);

    // Beacon version
    pl[38] = 1;

    // Build and send packet
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::LOBBY_BEACON;
    pkt.seq = 0;
    memcpy(pkt.payload, pl, BEACON_PAYLOAD_SIZE);

    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + BEACON_PAYLOAD_SIZE);
    Serial.println("[LOBBY] → BEACON (v1)");
}

void Lobby::handleBeacon(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < BEACON_PAYLOAD_SIZE_V0) return;  // minimum: legacy 24 bytes

    uint32_t chipId = ((uint32_t)pkt.payload[0] << 24) |
                      ((uint32_t)pkt.payload[1] << 16) |
                      ((uint32_t)pkt.payload[2] <<  8) |
                      pkt.payload[3];

    if (chipId == transport_.nodeId()) return;  // own beacon

    PeerEntry *p = addOrUpdatePeer(chipId);
    if (!p) return;  // table full

    memcpy(p->name, &pkt.payload[4], 10);
    p->name[10] = '\0';

    p->leadSpecies = pkt.payload[14];
    p->leadLevel   = pkt.payload[15];
    p->partyCount  = pkt.payload[16];
    p->elo         = ((uint16_t)pkt.payload[17] << 8) | pkt.payload[18];
    p->lastSeenMs  = millis();
    p->rssi        = transport_.lastRssi();

    // Resolve lead species name
    const char *sn = gen1SpeciesName(p->leadSpecies);
    strncpy(p->leadName, sn, 10);
    p->leadName[10] = '\0';

    // ── Parse extended fields (v1 beacon, 48 bytes) ──────────────────────
    if (payloadLen >= BEACON_PAYLOAD_SIZE && pkt.payload[38] >= 1) {
        p->beaconVersion = pkt.payload[38];
        p->romVersion    = static_cast<RomVersion>(pkt.payload[19]);
        p->preferences   = pkt.payload[20];
        p->partyMaxLevel = pkt.payload[21];
        p->partyMinLevel = pkt.payload[22];
        p->hasLv100      = pkt.payload[23];
        memcpy(p->partySpecies, &pkt.payload[24], 6);
        memcpy(p->partyLevels,  &pkt.payload[30], 6);
        p->totalBattles  = ((uint16_t)pkt.payload[36] << 8) | pkt.payload[37];
    } else {
        p->beaconVersion = 0;
        p->romVersion    = RomVersion::UNKNOWN;
    }

    Serial.printf("[LOBBY] ← BEACON  %s  ELO=%u  party=%u  rom=%u\n",
                  p->name, p->elo, p->partyCount, (unsigned)p->romVersion);
}

// ── Challenge / Accept / Reject ─────────────────────────────────────────────
// Challenge payload (4 bytes): sender chipId (BE)

void Lobby::sendChallenge(uint32_t targetId) {
    uint32_t id = transport_.nodeId();
    uint8_t pl[4] = {
        (uint8_t)(id >> 24), (uint8_t)(id >> 16),
        (uint8_t)(id >>  8), (uint8_t)(id)
    };
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::LOBBY_CHALLENGE;
    memcpy(pkt.payload, pl, 4);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 4);
}

void Lobby::sendAccept(uint32_t targetId) {
    uint32_t id = transport_.nodeId();
    uint8_t pl[4] = {
        (uint8_t)(id >> 24), (uint8_t)(id >> 16),
        (uint8_t)(id >>  8), (uint8_t)(id)
    };
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::LOBBY_ACCEPT;
    memcpy(pkt.payload, pl, 4);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 4);
}

void Lobby::sendReject(uint32_t targetId) {
    uint32_t id = transport_.nodeId();
    uint8_t pl[4] = {
        (uint8_t)(id >> 24), (uint8_t)(id >> 16),
        (uint8_t)(id >>  8), (uint8_t)(id)
    };
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)PktType::LOBBY_REJECT;
    memcpy(pkt.payload, pl, 4);
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + 4);
}

void Lobby::handleChallenge(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < 4) return;

    uint32_t fromId = ((uint32_t)pkt.payload[0] << 24) |
                      ((uint32_t)pkt.payload[1] << 16) |
                      ((uint32_t)pkt.payload[2] <<  8) |
                      pkt.payload[3];

    if (fromId == transport_.nodeId()) return;

    // Only accept challenges while actively browsing the lobby
    if (state_ != State::BROWSING) {
        sendReject(fromId);
        return;
    }

    challengeFrom_ = fromId;
    challengeMs_ = millis();
    state_ = State::INCOMING;

    // Alert: someone is challenging us!
    if (alert_) alert_->playChallenge();

    // Find peer name for display
    PeerEntry *p = findPeer(fromId);
    Serial.printf("[LOBBY] ← CHALLENGE from %s (0x%08X)\n",
                  p ? p->name : "???", (unsigned)fromId);
}

void Lobby::handleAcceptPkt(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < 4) return;

    uint32_t fromId = ((uint32_t)pkt.payload[0] << 24) |
                      ((uint32_t)pkt.payload[1] << 16) |
                      ((uint32_t)pkt.payload[2] <<  8) |
                      pkt.payload[3];

    if (state_ != State::CHALLENGING || fromId != challengeTarget_) return;

    Serial.printf("[LOBBY] ← ACCEPT from 0x%08X\n", (unsigned)fromId);
    state_ = State::PAIRED;
    if (alert_) alert_->playAccepted();
    if (shim_) {
        shim_->pairWith(fromId);
    }
}

void Lobby::handleRejectPkt(const BattlePacket &pkt, uint8_t payloadLen) {
    if (payloadLen < 4) return;

    uint32_t fromId = ((uint32_t)pkt.payload[0] << 24) |
                      ((uint32_t)pkt.payload[1] << 16) |
                      ((uint32_t)pkt.payload[2] <<  8) |
                      pkt.payload[3];

    if (state_ != State::CHALLENGING || fromId != challengeTarget_) return;

    Serial.println("[LOBBY] ← REJECT");
    if (alert_) alert_->playRejected();
    challengeTarget_ = 0;
    state_ = State::BROWSING;
}

// ── Peer table management ───────────────────────────────────────────────────

PeerEntry *Lobby::findPeer(uint32_t chipId) {
    for (uint8_t i = 0; i < peerCount_; i++) {
        if (peers_[i].chipId == chipId) return &peers_[i];
    }
    return nullptr;
}

PeerEntry *Lobby::addOrUpdatePeer(uint32_t chipId) {
    // Check if already in table
    PeerEntry *p = findPeer(chipId);
    if (p) return p;

    // Add new entry
    if (peerCount_ < MAX_PEERS) {
        p = &peers_[peerCount_++];
        memset(p, 0, sizeof(PeerEntry));
        p->chipId = chipId;
        return p;
    }

    // Table full — evict oldest
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

void Lobby::expirePeers(uint32_t now) {
    for (uint8_t i = 0; i < peerCount_; ) {
        if (now - peers_[i].lastSeenMs > PEER_TIMEOUT_MS) {
            // Shift remaining entries down
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

float Lobby::expectedScore(uint16_t self, uint16_t opponent) {
    return 1.0f / (1.0f + powf(10.0f, (float)((int)opponent - (int)self) / 400.0f));
}

uint16_t Lobby::clampElo(int32_t raw) {
    if (raw < (int32_t)ELO_FLOOR) return ELO_FLOOR;
    if (raw > 9999) return 9999;
    return (uint16_t)raw;
}

void Lobby::recordResult(bool won, uint16_t opponentElo) {
    float expected = expectedScore(stats_.elo, opponentElo);
    float actual = won ? 1.0f : 0.0f;
    int32_t newElo = (int32_t)stats_.elo + (int32_t)(ELO_K * (actual - expected));
    stats_.elo = clampElo(newElo);

    if (won) stats_.wins++;
    else     stats_.losses++;

    saveStats();
    Serial.printf("[LOBBY] ELO update: %u  (W:%u L:%u)\n",
                  stats_.elo, stats_.wins, stats_.losses);
}

// ── Stats persistence ───────────────────────────────────────────────────────

void Lobby::loadStats() {
    File f = LittleFS.open(STATS_PATH, "r");
    if (!f || f.size() != sizeof(PlayerStats)) {
        stats_ = PlayerStats{};  // defaults
        Serial.println("[LOBBY] no saved stats — using defaults");
        return;
    }
    f.read((uint8_t *)&stats_, sizeof(PlayerStats));
    f.close();
    Serial.printf("[LOBBY] loaded stats: ELO=%u W=%u L=%u\n",
                  stats_.elo, stats_.wins, stats_.losses);
}

void Lobby::saveStats() {
    File f = LittleFS.open(STATS_PATH, "w");
    if (!f) {
        Serial.println("[LOBBY] failed to write stats");
        return;
    }
    f.write((uint8_t *)&stats_, sizeof(PlayerStats));
    f.close();
}
