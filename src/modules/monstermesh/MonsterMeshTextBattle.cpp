// SPDX-License-Identifier: MIT
// See MonsterMeshTextBattle.h for description.

#include "MonsterMeshTextBattle.h"
#include "showdown_gen1_moves.h"
#include "showdown_gen1_basestats.h"
#include "showdown_gen3_moves.h"  // gen3Move for gen-aware PP refill
#include "Gen1Species.h"  // gen1NameToAscii for level-up messages
#include "WirePartyCodec.h"  // protocol-V2 neutral cross-gen party blob
#include "MonsterMeshBattleValidation.h"  // transactional wire-party validation
#include "DaycareSavPatcher.h"  // expForLevel() — growth-rate-correct level thresholds
#include <string.h>
#include <stdio.h>

// 24-bit RGB → RGB565 packing for lgfx draw calls.
static inline uint16_t rgb565(uint32_t c)
{
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8)  & 0xFF;
    uint8_t b =  c        & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Battle UI uses the fixed GBC-green palette (matching the boot loader and
// emulator chrome) regardless of the user's selected theme. Keeps the
// game-feel consistent with the rest of the cart experience.
//   GBC_0 darkest   GBC_3 dark   GBC_4 mid   GBC_5 light   GBC_6 lightest
#define BG    rgb565(0x081820)
#define DARK  rgb565(0x346856)
#define DIM   rgb565(0x5E9464)
#define ACC   rgb565(0x88C070)
#define FG    rgb565(0xE0F8D0)

// ── Logging glue from engine ────────────────────────────────────────────────

void MonsterMeshTextBattle::engineLogCb(const char *line, void *ctx)
{
    static_cast<MonsterMeshTextBattle *>(ctx)->appendLog(line);
}

void MonsterMeshTextBattle::appendLog(const char *line)
{
    if (!line) return;
    snprintf(log_[logHead_], sizeof(log_[logHead_]), "%s", line);
    logHead_ = (logHead_ + 1) % LOG_LINES;
    if (logFill_ < LOG_LINES) logFill_++;
    if (unshippedLogLines_ < LOG_LINES) unshippedLogLines_++;
    dirty_ = true;
}

uint8_t MonsterMeshTextBattle::getRecentLog(char *out, size_t outCap,
                                            uint8_t maxLines) const
{
    if (!out || outCap == 0) return 0;
    out[0] = '\0';
    if (logFill_ == 0) return 0;
    uint8_t lines = (logFill_ < maxLines) ? logFill_ : maxLines;
    // Oldest of the kept window: walk back `lines` from logHead_.
    int start = ((int)logHead_ - (int)lines + (int)LOG_LINES) % (int)LOG_LINES;
    size_t pos = 0;
    uint8_t written = 0;
    for (uint8_t i = 0; i < lines && pos < outCap - 1; ++i) {
        int idx = (start + i) % LOG_LINES;
        size_t avail = outCap - 1 - pos;
        int n = snprintf(out + pos, avail, "%s%s",
                         (i == 0) ? "" : "\n", log_[idx]);
        if (n <= 0) break;
        pos += (size_t)n;
        ++written;
    }
    return written;
}

// ── Networked initiator ─────────────────────────────────────────────────────

void MonsterMeshTextBattle::startNetworkedAsInitiator(uint32_t remoteId,
                                                     const Gen1Party &myParty,
                                                     const Gen1Party &oppParty,
                                                     uint32_t existingSeed,
                                                     uint16_t existingSession)
{
    resultXpAwarded_ = false;
    mode_     = Mode::NETWORKED;
    phase_    = Phase::WAIT_REMOTE;
    remoteId_ = remoteId;
    // Block move submission until we hear from the peer once. Receiver
    // sends nothing automatically, so we rely on the LoRa-thread-driven
    // handlePacket to set this true when ANY in-session packet arrives
    // (typically the receiver's ACTION for turn 0, or an early HASH).
    peerReady_ = false;
    // If the caller (module's sendMmbBattleStart) already broadcast START
    // with a specific session_id, use it so our outgoing ACTION packets
    // match what the receiver is filtering on. Otherwise pick a fresh one.
    session_  = existingSession ? existingSession
                                : (uint16_t)(millis() & 0xFFFF);
    cursor_   = 0; switchCursor_ = 0;
    pendingRemoteAction_ = false;
    logFill_ = logHead_ = 0; scrollPending_ = 0;

    // If the caller already broadcast TEXT_BATTLE_START (with this seed),
    // reuse it and skip the duplicate sendStart below. Otherwise generate
    // one and emit the start packet ourselves.
    uint32_t rngSeed = existingSeed
                         ? existingSeed
                         : (uint32_t)(esp_random() ^ remoteId ^ session_);

    Gen1Party opp = (oppParty.count > 0) ? oppParty : myParty;
    engine_.start(myParty, opp, rngSeed);
    // Heal both sides to full HP, refill PP from the move table, clear
    // status. The PARTY_MIN wire format strips current HP / PP / status
    // because "everyone starts fully healed in mesh PvP", so the
    // *receiver's* side1 always lands at maxHp. If we leave our own side0
    // at the SAV's actual (possibly half-played) state, side0 hashes will
    // diverge from what our opponent sees on their side1 — instant turn-0
    // desync. Healing both sides locally on both decks keeps the hashes
    // aligned. Mirrors startLocal's roguelike "clean slate" setup.
    for (uint8_t side = 0; side < 2; ++side) {
        auto &p = engine_.party(side);
        for (uint8_t i = 0; i < p.count && i < 6; ++i) {
            auto &m = p.mons[i];
            m.hp     = m.maxHp;
            m.status = 0;
            for (uint8_t s = 0; s < 4; ++s) {
                const Gen1MoveData *mv = gen1Move(m.moves[s]);
                m.pp[s] = mv ? mv->pp : 0;
            }
        }
    }

    if (!existingSeed) sendStart(rngSeed, myParty);
    Serial.printf("[MMB] init startNetworked: session=0x%04X seed=0x%08X peer=0x%08X\n",
                  (unsigned)session_, (unsigned)rngSeed, (unsigned)remoteId);
    appendLog("Battle ready — waiting for peer…");
    lastRecvMs_ = millis();
    handleFaints();
    // Stay in WAIT_PEER_READY until receiver sends TEXT_BATTLE_READY.
    // handlePacket transitions to WAIT_ACTION when that packet arrives.
    // A 10s fallback in tick() unblocks in case the peer is an older build.
    if (engine_.result() == Gen1BattleEngine::Result::ONGOING)
        phase_ = Phase::WAIT_PEER_READY;
    peerReadyTimeoutMs_ = millis() + 10000;
    dirty_ = true;
}

void MonsterMeshTextBattle::startNetworkedAsReceiver(uint32_t remoteId,
                                                     const Gen1Party &myParty,
                                                     uint32_t rngSeed,
                                                     const Gen1Party &oppParty,
                                                     uint16_t existingSession)
{
    resultXpAwarded_ = false;
    mode_     = Mode::NETWORKED;
    phase_    = Phase::WAIT_ACTION;
    remoteId_ = remoteId;
    // Use the session_id we captured from the incoming START packet so
    // ACTION packets we emit match the initiator's session filter — and
    // ACTION packets the initiator sends pass our session filter.
    session_  = existingSession;
    cursor_   = 0; switchCursor_ = 0;
    logFill_ = logHead_ = 0; scrollPending_ = 0;

    Gen1Party opp = (oppParty.count > 0) ? oppParty : myParty;
    engine_.start(myParty, opp, rngSeed);
    // Same heal/refill as the initiator path — see comment above. Without
    // this, our own side0 starts at SAV HP while the initiator's side1
    // (the unpacked PARTY_MIN view of us) is at maxHp, causing a turn-0
    // hash desync.
    for (uint8_t side = 0; side < 2; ++side) {
        auto &p = engine_.party(side);
        for (uint8_t i = 0; i < p.count && i < 6; ++i) {
            auto &m = p.mons[i];
            m.hp     = m.maxHp;
            m.status = 0;
            for (uint8_t s = 0; s < 4; ++s) {
                const Gen1MoveData *mv = gen1Move(m.moves[s]);
                m.pp[s] = mv ? mv->pp : 0;
            }
        }
    }
    Serial.printf("[MMB] recv startNetworked: session=0x%04X seed=0x%08X peer=0x%08X\n",
                  (unsigned)session_, (unsigned)rngSeed, (unsigned)remoteId);
    appendLog("Battle started!");
    lastRecvMs_ = millis();
    // Notify the initiator that our battle station is up. sendReady() is the
    // explicit signal; sendHash() follows for turn-0 desync detection.
    // Initiator transitions from WAIT_PEER_READY to WAIT_ACTION on receipt.
    peerReady_ = true;
    sendReady();
    sendHash();
    dirty_ = true;
}

void MonsterMeshTextBattle::startLocal(const Gen1Party &myParty,
                                       const Gen1Party &cpuParty,
                                       const char *ourTag,
                                       const char *theirTag)
{
    headerOverride_[0] = '\0';
    endPromptOverride_[0] = '\0';
    playerWon_ = false;

    mode_         = Mode::LOCAL_ROGUELIKE;
    phase_        = Phase::WAIT_ACTION;
    remoteId_     = 0;
    cursor_       = 0; switchCursor_ = 0;
    fleeAttempts_ = 0;
    logFill_ = logHead_ = 0; scrollPending_ = 0;
    // Start with the lead pokemon as the only participant; trainer-battle
    // multiplier matches Gen 1 (1.5× wild) since LOCAL_ROGUELIKE is always
    // a trainer/gym/E4 fight or a single wild explore — wild routes flip
    // this off below.
    participantMask_  = 0x01;
    lastPlayerActive_ = 0;
    lastEnemyActive_  = 0;
    isTrainerBattle_  = true;
    pendingXpDrops_   = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        pendingXpPerSlot_[i] = 0;
        slotXpAccum_[i]      = 0;
    }

    uint32_t rngSeed = (uint32_t)(millis() ^ esp_random());
    engine_.start(myParty, cpuParty, rngSeed);

    // Tag only the opponent's nicknames (side 1) so log messages like
    // "BROK-MEW used Tackle" disambiguate a mirror match. Our own pokemon
    // (side 0) keep their plain nicknames — the switch menu needs the room
    // and the user already knows it's their team. ourTag is unused here but
    // kept in the API for future networked-PvP work.
    (void)ourTag;
    strncpy(oppTag_, theirTag && theirTag[0] ? theirTag : "", sizeof(oppTag_) - 1);
    oppTag_[sizeof(oppTag_) - 1] = '\0';
    if (theirTag && theirTag[0]) {
        auto &p = engine_.party(1);
        for (uint8_t i = 0; i < p.count && i < 6; ++i) {
            char tmp[11] = {};
            snprintf(tmp, sizeof(tmp), "%.4s-%.5s", theirTag, p.mons[i].nickname);
            memcpy(p.mons[i].nickname, tmp, sizeof(p.mons[i].nickname));
            p.mons[i].nickname[10] = '\0';
        }
    }

    // Roguelike-style fight: heal everyone to full and refresh PP. The SAV
    // values may be a half-played in-game state, so this gives both sides a
    // clean slate so the fight is actually playable from the move menu.
    for (uint8_t side = 0; side < 2; ++side) {
        auto &p = engine_.party(side);
        for (uint8_t i = 0; i < p.count && i < 6; ++i) {
            auto &m = p.mons[i];
            m.hp     = m.maxHp;
            m.status = 0;
            for (uint8_t s = 0; s < 4; ++s) {
                const Gen1MoveData *mv = gen1Move(m.moves[s]);
                m.pp[s] = mv ? mv->pp : 0;
            }
        }
    }
    appendLog("A wild battle begins!");
    dirty_ = true;
}

void MonsterMeshTextBattle::healPlayer()
{
    // HP + status only. PP carries over between gym members so the
    // ladder still demands move management — Pokemon canon: only the
    // Pokemon Center (post-gym) restores PP.
    auto &p = engine_.party(0);
    for (uint8_t i = 0; i < p.count && i < 6; ++i) {
        auto &m = p.mons[i];
        m.hp     = m.maxHp;
        m.status = 0;
    }
    appendLog("HP restored.");
    dirty_ = true;
}

void MonsterMeshTextBattle::nextOpponent(const Gen1Party &cpu, const char *theirTag)
{
    // Caller has just observed isActive()==false (because exit() ran on the
    // result screen). Bring the battle back to life — mode and phase reset
    // so the engine and tick loop both consider us in-battle again.
    mode_ = Mode::LOCAL_ROGUELIKE;
    engine_.replaceOpponent(cpu);
    strncpy(oppTag_, theirTag && theirTag[0] ? theirTag : "", sizeof(oppTag_) - 1);
    oppTag_[sizeof(oppTag_) - 1] = '\0';
    // Re-tag opponent's nicknames with the new trainer prefix.
    if (theirTag && theirTag[0]) {
        auto &p = engine_.party(1);
        for (uint8_t i = 0; i < p.count && i < 6; ++i) {
            char tmp[11] = {};
            snprintf(tmp, sizeof(tmp), "%.4s-%.5s", theirTag, p.mons[i].nickname);
            memcpy(p.mons[i].nickname, tmp, sizeof(p.mons[i].nickname));
            p.mons[i].nickname[10] = '\0';
        }
    }
    // Heal opponent + refresh PP (player keeps their state, gauntlet rule).
    auto &op = engine_.party(1);
    for (uint8_t i = 0; i < op.count && i < 6; ++i) {
        auto &m = op.mons[i];
        m.hp     = m.maxHp;
        m.status = 0;
        for (uint8_t s = 0; s < 4; ++s) {
            const Gen1MoveData *mv = gen1Move(m.moves[s]);
            m.pp[s] = mv ? mv->pp : 0;
        }
    }
    phase_ = Phase::WAIT_ACTION;
    pendingRemoteAction_ = false;
    cursor_ = 0; switchCursor_ = 0;
    appendLog("Next trainer steps up!");
    dirty_ = true;
}

void MonsterMeshTextBattle::exit()
{
    mode_ = Mode::OFF;
    phase_ = Phase::IDLE;
    role_  = Role::LEGACY;
    awaitingAccept_ = false;
    awaitingFirstUpdate_   = false;
    awaitingUpdateSinceMs_ = 0;
    unackedUpdate_   = false;
    pendingKeyExit_  = false;
    finishedAtMs_    = 0;
    challengeTries_  = 0;
    clientActionType_ = 0xFF;
    clientTurn_ = 0;
    clientNeedsFullState_ = false;
    unshippedLogLines_ = 0;
    preAcceptUpdateLen_ = 0;
    dirty_ = true;
}

// ── Server-authoritative initiator entry point ──────────────────────────────
//
// Stages our party + name, picks a session id, and broadcasts CHALLENGE.
// The engine is NOT started here — it waits for the client's ACCEPT (in
// serverAuthOnAcceptPkt), which carries the client's party. Until ACCEPT
// arrives (or 3 retries expire), no battle UI exists; we just sit in
// awaitingAccept_ and the tick() loop retransmits.
void MonsterMeshTextBattle::startServerAuthAsInitiator(uint32_t remoteId,
                                                       const Gen1Party &myParty,
                                                       const char *myName)
{
    playerWon_ = false;
    resultXpAwarded_ = false;
    mode_     = Mode::NETWORKED;
    phase_    = Phase::WAIT_REMOTE;   // "waiting for opponent to accept"
    role_     = Role::SERVER;
    remoteId_ = remoteId;
    session_  = (uint16_t)((millis() ^ remoteId) & 0xFFFF);
    cursor_   = 0; switchCursor_ = 0;
    pendingRemoteAction_ = false;
    logFill_ = logHead_ = 0; scrollPending_ = 0;
    peerReady_ = false;
    awaitingAccept_ = true;
    challengeTries_ = 0;
    lastChallengeMs_ = 0;
    unackedUpdate_  = false;
    updateSeq_      = 0;
    lastUpdateLen_  = 0;
    clientActionType_      = 0xFF;
    lastAppliedUpdateSeq_  = 0;
    clientNeedsFullState_  = false;

    pendingMyParty_ = myParty;
    myTbName_[0] = '\0';
    if (myName && *myName) {
        snprintf(myTbName_, sizeof(myTbName_), "%.*s",
                 (int)TB_MAX_NAME_LEN, myName);
    }
    peerTbName_[0] = '\0';

    // P2.34: pre-load engine with our own party in both slots so the
    // battle station shows the player's pokemon while waiting for
    // ACCEPT. We then zero opponent count so the foe panel renders
    // "---" (placeholder). ACCEPT handler re-calls engine_.start()
    // with the real opponent party + real seed, replacing this stub.
    engine_.start(myParty, myParty, /*rngSeed*/ 0xCAFEBABE);
    engine_.party(1).count = 0;

    appendLog("Sending challenge…");
    Serial.printf("[MMB] startServerAuthAsInitiator: pre-sendChallenge\n");
    serverAuthSendChallenge();          // first burst
    Serial.printf("[MMB] startServerAuthAsInitiator: post-sendChallenge\n");
    lastRecvMs_ = millis();
    dirty_ = true;
    Serial.printf("[MMB] server-auth CHALLENGE → 0x%08X session=0x%04X "
                  "name=%s count=%u\n",
                  (unsigned)remoteId, (unsigned)session_,
                  myTbName_, (unsigned)myParty.count);
}

// ── Wire packet emit/receive ────────────────────────────────────────────────

void MonsterMeshTextBattle::sendStart(uint32_t rngSeed, const Gen1Party &myParty)
{
    uint8_t buf[BATTLELINK_MAX_PKT];
    BattlePacket *pkt = (BattlePacket *)buf;
    memset(buf, 0, sizeof(buf));
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_START;
    pkt->setSessionId(session_);
    pkt->seq  = 0;
    pkt->payload[0] = (rngSeed >> 24) & 0xFF;
    pkt->payload[1] = (rngSeed >> 16) & 0xFF;
    pkt->payload[2] = (rngSeed >> 8)  & 0xFF;
    pkt->payload[3] =  rngSeed        & 0xFF;
    pkt->payload[4] = 1;                  // gen
    pkt->payload[5] = myParty.count;
    // Party hash placeholder — real impl computes a quick fingerprint.
    for (int i = 0; i < 8; ++i) pkt->payload[6 + i] = myParty.species[i % 7];
    transport_.queueSend(buf, BATTLELINK_HDR_SIZE + 14);
}

// Internal: send an ACTION packet with an explicit turn number. Used by
// the public sendAction for live submissions (turn = current) and by the
// stale-ACTION recovery path in handlePacket for catch-up replays
// (turn = the past turn the peer is still waiting on).
static void sendActionPacket(MeshtasticTransport &transport,
                             uint16_t session, uint16_t turn,
                             uint8_t actionType, uint8_t index)
{
    uint8_t buf[BATTLELINK_MAX_PKT];
    BattlePacket *pkt = (BattlePacket *)buf;
    memset(buf, 0, sizeof(buf));
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_ACTION;
    pkt->setSessionId(session);
    pkt->seq  = turn & 0xFF;
    pkt->payload[0] = (turn >> 8) & 0xFF;
    pkt->payload[1] =  turn       & 0xFF;
    pkt->payload[2] = actionType;
    pkt->payload[3] = index;
    transport.queueSend(buf, BATTLELINK_HDR_SIZE + 4);
}

void MonsterMeshTextBattle::sendAction(uint8_t actionType, uint8_t index)
{
    uint16_t turn = engine_.turn();
    // Demote lastSent* to prevSent* before overwriting. The peer-catch-up
    // path in handlePacket needs the action we submitted for a turn we've
    // already advanced past.
    if (lastSentAction_ != 0xFF) {
        prevSentAction_ = lastSentAction_;
        prevSentIndex_  = lastSentIndex_;
        prevSentTurn_   = lastSentTurn_;
    }
    sendActionPacket(transport_, session_, turn, actionType, index);
    lastSentAction_ = actionType;
    lastSentIndex_  = index;
    lastSentTurn_   = turn;
    lastSendMs_     = millis();
}

void MonsterMeshTextBattle::sendForfeit()
{
    uint8_t buf[BATTLELINK_HDR_SIZE];
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_FORFEIT;
    pkt->setSessionId(session_);
    pkt->seq = 0;
    transport_.queueSend(buf, sizeof(buf));
}

void MonsterMeshTextBattle::sendReady()
{
    uint8_t buf[BATTLELINK_HDR_SIZE];
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_READY;
    pkt->setSessionId(session_);
    pkt->seq = 0;
    transport_.queueSend(buf, sizeof(buf));
}

void MonsterMeshTextBattle::sendHash()
{
    uint8_t buf[BATTLELINK_MAX_PKT];
    BattlePacket *pkt = (BattlePacket *)buf;
    memset(buf, 0, sizeof(buf));
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_HASH;
    pkt->setSessionId(session_);
    pkt->seq  = engine_.turn() & 0xFF;
    uint16_t turn = engine_.turn();
    pkt->payload[0] = (turn >> 8) & 0xFF;
    pkt->payload[1] =  turn       & 0xFF;
    engine_.hashState(pkt->payload + 2);
    transport_.queueSend(buf, BATTLELINK_HDR_SIZE + 10);
}

bool MonsterMeshTextBattle::handlePacket(uint32_t fromId,
                                          const uint8_t *buf, size_t len)
{
    if (len < BATTLELINK_HDR_SIZE) return false;
    const BattlePacket *pkt = (const BattlePacket *)buf;
    PktType t = (PktType)pkt->type;
    Serial.printf("[MMB] handlePacket type=0x%02X from=0x%08X len=%u mode=%d role=%d\n",
                  (unsigned)pkt->type, (unsigned)fromId, (unsigned)len,
                  (int)mode_, (int)role_);

    // ── Audit finding 5: sender/session sanity gate (applies to every packet).
    // Session zero is never a valid wire session; a zero sender cannot be
    // bound as a peer; and a packet echoed back to us from our own node id
    // (the MQTT broker fan-out delivers to the publisher too) must never be
    // treated as an opponent's message. Reject all three before any handler
    // can mutate mode_/session_/remoteId_ or a staged party.
    if (pkt->sessionId() == 0) {
        Serial.printf("[MMB] DROP type=0x%02X from=0x%08X reason=zero_session\n",
                      (unsigned)pkt->type, (unsigned)fromId);
        return true;
    }
    if (fromId == 0) {
        Serial.printf("[MMB] DROP type=0x%02X reason=zero_sender\n",
                      (unsigned)pkt->type);
        return true;
    }
    if (myNodeNum_ != 0 && fromId == myNodeNum_) {
        Serial.printf("[MMB] DROP type=0x%02X reason=self_echo (from=0x%08X)\n",
                      (unsigned)pkt->type, (unsigned)fromId);
        return true;
    }

    // ── Server-auth dispatch (must precede the mode_/session checks since
    // CHALLENGE arrives when mode_ == OFF and ACCEPT/ACTION_V2 may carry
    // session ids unknown to the legacy session_ field). ─────────────────
    if (t == PktType::TEXT_BATTLE_CHALLENGE_V2) {
        // A fresh CHALLENGE is only entertained while idle. Once we have bound
        // a peer, only that peer's retransmit of the SAME session may re-enter
        // (discovery races); any other CHALLENGE while busy is rejected so a
        // third node cannot reset our handshake.
        if (mode_ != Mode::OFF &&
            !(role_ == Role::CLIENT && fromId == remoteId_ &&
              pkt->sessionId() == session_)) {
            Serial.printf("[MMB] DROP CHALLENGE_V2 from=0x%08X reason=busy "
                          "(peer=0x%08X)\n", (unsigned)fromId, (unsigned)remoteId_);
            return true;
        }
        clientAuthOnChallengePkt(fromId, buf, len);
        return true;
    }
    if (t == PktType::TEXT_BATTLE_CHALLENGE) {
        // Protocol V1 (Gen-1 partyMin) peer — incompatible with the V2
        // cross-gen wire format. Ignore; both sides need current firmware.
        Serial.printf("[MMB] DROP V1 CHALLENGE from=0x%08X (peer needs V2 firmware)\n",
                      (unsigned)fromId);
        return true;
    }
    if (mode_ != Mode::OFF && role_ == Role::SERVER) {
        if (t == PktType::TEXT_BATTLE_ACCEPT_V2) {
            serverAuthOnAcceptPkt(fromId, buf, len);
            return true;
        }
        if (t == PktType::TEXT_BATTLE_ACTION_V2) {
            serverAuthOnActionV2Pkt(fromId, buf, len);
            return true;
        }
        if (t == PktType::TEXT_BATTLE_STATE_REQUEST) {
            serverAuthOnStateRequestPkt(fromId, buf, len);
            return true;
        }
    }
    if (mode_ != Mode::OFF && role_ == Role::CLIENT) {
        if (t == PktType::TEXT_BATTLE_UPDATE) {
            clientAuthOnUpdatePkt(fromId, buf, len);
            return true;
        }
        if (t == PktType::TEXT_BATTLE_FULL_STATE) {
            clientAuthOnFullStatePkt(fromId, buf, len);
            return true;
        }
    }

    if (t == PktType::TEXT_BATTLE_START) {
        if (mode_ != Mode::OFF) return true;  // already in a battle, ignore
        if (len < BATTLELINK_HDR_SIZE + 14) return true;
        uint32_t seed = ((uint32_t)pkt->payload[0] << 24) |
                        ((uint32_t)pkt->payload[1] << 16) |
                        ((uint32_t)pkt->payload[2] << 8)  |
                                  pkt->payload[3];
        // Caller is responsible for supplying our party — we expose a separate
        // entry point. This packet path is for *unsolicited* battles where the
        // caller already loaded our party into a globally-known place. For now,
        // log and let MonsterMeshModule wire up the party.
        session_  = pkt->sessionId();
        remoteId_ = fromId;
        // Store seed in remoteId_ via a side channel? Cleaner: signal via a
        // pending-incoming flag the module checks. Out of scope here — the
        // module calls startNetworkedAsReceiver() with the right party.
        return true;
    }
    if (mode_ == Mode::OFF) return false;
    if (pkt->sessionId() != session_) {
        // Silent drop has bitten us in PvP debugging — log the mismatch
        // so a stuck-waiting-for-opponent state is decodable.
        Serial.printf("[MMB] pkt session mismatch from=0x%08X type=0x%02X "
                      "pkt_session=0x%04X our_session=0x%04X — dropping\n",
                      (unsigned)fromId, (unsigned)pkt->type,
                      (unsigned)pkt->sessionId(), (unsigned)session_);
        return false;
    }
    // Forward-port of b402: reject our own packets bounced back via the
    // MQTT bridge. The broker delivers to all subscribers including the
    // publisher, so each ACTION/HASH/FORFEIT we transmit comes back to us
    // with from=our_nodeId. Without this filter the engine treats the echo
    // as the opponent's submission and overwrites side 1 with our own move
    // — silent desync that the hash check only catches several turns later
    // once one deck's accumulated state diverges from the mirror.
    if (remoteId_ != 0 && fromId != remoteId_) {
        return false;
    }
    // First in-session packet from the peer proves their battle station is
    // up — unblock initiator move submission. Receiver path leaves this
    // false until it sees the initiator's own packets, which is fine
    // because the receiver only enters the battle AFTER receiving START
    // so it implicitly knows the initiator is ready.
    if (!peerReady_) {
        peerReady_ = true;
        if (phase_ == Phase::WAIT_PEER_READY) {
            phase_ = Phase::WAIT_ACTION;
            appendLog("Opponent ready!");
        } else {
            appendLog("Opponent ready — go!");
        }
        dirty_ = true;
    }

    // Explicit ready ping from receiver — same effect as first peerReady_.
    if (t == PktType::TEXT_BATTLE_READY) return true;

    switch (t) {
        case PktType::TEXT_BATTLE_ACTION: {
            if (len < BATTLELINK_HDR_SIZE + 4) return true;
            uint16_t turn = ((uint16_t)pkt->payload[0] << 8) | pkt->payload[1];
            if (turn != engine_.turn()) {
                // Peer is behind us — we already resolved turn `turn` and
                // moved on. Their ACTION packet for that turn must have
                // crossed paths with our own packet that they're still
                // waiting on (asymmetric loss). Replay our action for
                // THAT turn so they can finish executing it. Check both
                // lastSent (the most recent submission) and prevSent (one
                // turn back) — after a resolveTurn on our side that
                // crossed a turn boundary, lastSent now holds the action
                // for the JUST-PASSED turn the peer is stuck on. Without
                // this, the peer sits at "Waiting for opponent…" until
                // the 60s forfeit timer fires.
                if (turn < engine_.turn()) {
                    uint8_t replayAct = 0xFF, replayIdx = 0;
                    if (lastSentAction_ != 0xFF && lastSentTurn_ == turn) {
                        replayAct = lastSentAction_;
                        replayIdx = lastSentIndex_;
                    } else if (prevSentAction_ != 0xFF && prevSentTurn_ == turn) {
                        replayAct = prevSentAction_;
                        replayIdx = prevSentIndex_;
                    }
                    if (replayAct != 0xFF) {
                        Serial.printf("[MMB] peer behind: replaying our turn-%u "
                                      "action (act=%u idx=%u) — they're stuck "
                                      "waiting on us\n",
                                      (unsigned)turn,
                                      (unsigned)replayAct,
                                      (unsigned)replayIdx);
                        sendActionPacket(transport_, session_, turn,
                                         replayAct, replayIdx);
                    } else {
                        Serial.printf("[MMB] peer too far behind: pktTurn=%u "
                                      "myTurn=%u (no history)\n",
                                      (unsigned)turn,
                                      (unsigned)engine_.turn());
                    }
                } else {
                    // Peer is ahead — we're missing their action(s) for our
                    // current turn. The retry loop in tick() will keep
                    // re-broadcasting our own action so eventually the peer
                    // re-replies (this same path on their side) and we
                    // catch up. Just log so the gap is visible.
                    Serial.printf("[MMB] peer ahead: pktTurn=%u myTurn=%u "
                                  "— waiting for retransmit\n",
                                  (unsigned)turn, (unsigned)engine_.turn());
                }
                lastRecvMs_ = millis();
                return true;
            }
            uint8_t act = pkt->payload[2];
            uint8_t idx = pkt->payload[3];
            engine_.submitAction(1, act, idx);
            pendingRemoteAction_ = true;
            lastRecvMs_ = millis();
            return true;
        }
        case PktType::TEXT_BATTLE_FORFEIT:
            engine_.forfeit(1, engineLogCb, this);
            phase_ = Phase::FINISHED;
            return true;
        case PktType::TEXT_BATTLE_HASH: {
            if (len < BATTLELINK_HDR_SIZE + 10) return true;
            uint16_t pktTurn = ((uint16_t)pkt->payload[0] << 8) | pkt->payload[1];
            // Drop hashes from a different turn than we're currently at.
            // The hash includes the turn counter, so comparing a peer's
            // turn-N hash against our (advanced) turn-N+k local state
            // would false-positive as a desync. The protocol resends a
            // hash every HASH_INTERVAL turns, so dropping one stale one
            // is harmless — the next aligned hash will catch any real
            // divergence. Observed live: turn race between the two decks
            // (peer's hash arrives after we've already advanced past
            // that turn over MQTT).
            if (pktTurn != engine_.turn()) return true;
            uint8_t mine[8]; engine_.hashState(mine);
            const uint8_t *theirs = pkt->payload + 2;
            if (memcmp(mine, theirs, 8) != 0) {
                // Diagnostic log so future desyncs are decodable: show both
                // hashes + the turn the peer's hash was computed at vs our
                // current turn. Use Serial directly (not LOG_INFO) to keep
                // the line easy to grep for.
                Serial.printf("[MMB] DESYNC hash mismatch peerTurn=%u myTurn=%u "
                              "mine=%02x%02x%02x%02x%02x%02x%02x%02x "
                              "theirs=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                              (unsigned)pktTurn, (unsigned)engine_.turn(),
                              mine[0], mine[1], mine[2], mine[3],
                              mine[4], mine[5], mine[6], mine[7],
                              theirs[0], theirs[1], theirs[2], theirs[3],
                              theirs[4], theirs[5], theirs[6], theirs[7]);
                // Also dump the per-side state so we can see WHICH fields
                // diverged. p0/p1 are local labels — initiator/receiver
                // have these flipped, but the dump lets us see absolute
                // HP/PP/status/active values to compare both decks.
                for (int s = 0; s < 2; ++s) {
                    const auto &party = engine_.party(s);
                    Serial.printf("[MMB]  side%d active=%u count=%u:",
                                  s, (unsigned)party.active,
                                  (unsigned)party.count);
                    for (uint8_t i = 0; i < party.count && i < 6; ++i) {
                        const auto &m = party.mons[i];
                        Serial.printf(" m%u(sp=%u hp=%u/%u st=%02x pp=%u,%u,%u,%u)",
                                      i, m.species, m.hp, m.maxHp, m.status,
                                      m.pp[0], m.pp[1], m.pp[2], m.pp[3]);
                    }
                    Serial.println();
                }
                appendLog("Desync detected — match aborted.");
                engine_.forfeit(0, engineLogCb, this);
                phase_ = Phase::FINISHED;
            }
            return true;
        }
        default: break;
    }
    return false;
}

// ── Turn resolution ─────────────────────────────────────────────────────────

void MonsterMeshTextBattle::resolveTurn()
{
    // Snapshot opponent HPs so we can credit per-faint XP after executeTurn.
    {
        const auto &p1 = engine_.party(1);
        for (uint8_t i = 0; i < 6; ++i) {
            lastEnemyHp_[i] = (i < p1.count) ? p1.mons[i].hp : 0;
        }
    }

    engine_.executeTurn(engineLogCb, this);
    handleFaints();

    // Per-faint XP using the Gen 1 formula:
    //     xp = (baseYield * level * a) / (7 * participants)
    //   where a = 1.5 for trainer, 1.0 for wild. baseYield approximated as
    //   sum-of-base-stats / 3 — lands within ~20% of real Gen 1 base
    //   yields (Lapras 150 vs real 219, Mewtwo 196 vs real 220, Geodude 90
    //   vs real 73, Caterpie 58 vs real 53). Trainer's 1.5 stays as ×3/×2
    //   to keep integer math.
    const auto &p1 = engine_.party(1);
    for (uint8_t i = 0; i < p1.count && i < 6; ++i) {
        if (lastEnemyHp_[i] > 0 && p1.mons[i].hp == 0) {
            uint8_t pcount = (uint8_t)__builtin_popcount(participantMask_);
            if (pcount == 0) pcount = 1;
            uint32_t lvl = p1.mons[i].level ? p1.mons[i].level : 1;
            uint8_t  dex = p1.mons[i].species;
            const Gen1BaseStats &b =
                GEN1_BASE_STATS[dex < 152 ? dex : 0];
            uint32_t baseYield = (uint32_t)(b.hp + b.atk + b.def + b.spd + b.spc) / 3;
            if (baseYield < 20) baseYield = 20;
            uint32_t numerMult = isTrainerBattle_ ? 3u : 2u;  // 1.5 vs 1.0
            uint32_t xpThisFaint =
                (baseYield * lvl * numerMult) / (uint32_t)(14u * pcount);
            if (xpThisFaint == 0) xpThisFaint = 1;
            pendingXpDrops_++;
            char line[40];
            snprintf(line, sizeof(line), "Each earned %u XP!",
                     (unsigned)xpThisFaint);
            appendLog(line);
            // Per-participant credit: each participant slot's pending XP
            // gets the full xpThisFaint (already pre-divided by participant
            // count). Drains per-slot via consumePendingXp so the saved
            // party doesn't get re-split across all 6 mons.
            const auto &p0 = engine_.party(0);
            for (uint8_t s = 0; s < p0.count && s < 6; ++s) {
                if (!(participantMask_ & (1u << s))) continue;
                if (p0.mons[s].hp == 0) continue;   // fainted, no XP
                pendingXpPerSlot_[s] += xpThisFaint;
                slotXpAccum_[s]      += xpThisFaint;
                while (true) {
                    uint8_t curLvl = engine_.party(0).mons[s].level;
                    if (curLvl >= 100) break;
                    // Growth-rate-correct in-battle level threshold. Gen-1
                    // species (dex 1-151) use their true curve so Slow mons
                    // (Mewtwo, Dragonite, Gyarados...) stop over-leveling on
                    // screen mid-fight; Gen-2/3 (dex>151, no growth table) keep
                    // the medium-fast delta exactly as before. Matches the
                    // authoritative SAV writeback (creditBattleXpPerSlot).
                    uint16_t dex = engine_.party(0).mons[s].species;
                    uint32_t threshold =
                        (dex >= 1 && dex <= 151)
                            ? (expForLevel((uint8_t)dex, curLvl + 1) -
                               expForLevel((uint8_t)dex, curLvl))
                            : (3u * (uint32_t)curLvl * curLvl +
                               3u * (uint32_t)curLvl + 1u);
                    if (slotXpAccum_[s] < threshold) break;
                    slotXpAccum_[s] -= threshold;
                    inBattleLevelUp(s);
                }
            }
            // New enemy will switch in via autoReplace; reset participant
            // mask to just whichever player slot is currently active so the
            // next defeat only shares with mons that were actually present
            // for that fight.
            participantMask_ = (uint8_t)(1u << engine_.party(0).active);
            lastEnemyActive_ = engine_.party(1).active;
        }
    }

    // Player-active changes (switch or auto-replace) accumulate into the
    // participant mask for the current enemy's life span. The move cursor
    // also snaps back to slot 0 — keeping the last-pick "spam K" UX is
    // nice for the same pokemon but pointless after a faint, where slot 0
    // is the natural starting position for the new mon.
    uint8_t curPlayerActive = engine_.party(0).active;
    if (curPlayerActive != lastPlayerActive_) {
        participantMask_ |= (uint8_t)(1u << curPlayerActive);
        lastPlayerActive_ = curPlayerActive;
        cursor_ = 0;
    }

    if (engine_.result() != Gen1BattleEngine::Result::ONGOING) {
        switch (engine_.result()) {
            case Gen1BattleEngine::Result::P1_WIN:
                appendLog("You won!");
                playerWon_ = true;
                break;
            case Gen1BattleEngine::Result::P2_WIN: appendLog("You blacked out…"); break;
            case Gen1BattleEngine::Result::DRAW:   appendLog("It's a draw.");  break;
            default: break;
        }
        // Mesh PvP: one-time result-based XP bonus for the local party. Must
        // run BEFORE serverAuthSendUpdate() so the pendingXpPerSlot_ bump is
        // drained by the module on this same finishing tick. No-op for LOCAL.
        awardMeshResultXp();
        // Server-auth: ship a final UPDATE carrying result so the client
        // sees the end-of-battle state. Legacy path: nothing extra.
        if (role_ == Role::SERVER) serverAuthSendUpdate();
        phase_ = Phase::FINISHED;
        return;
    }
    pendingRemoteAction_ = false;
    if (mode_ == Mode::NETWORKED) {
        if (role_ == Role::SERVER) {
            // Server-authoritative: ship UPDATE with the new board, no hash.
            serverAuthSendUpdate();
        } else if (engine_.turn() % TEXT_BATTLE_HASH_INTERVAL == 0) {
            sendHash();
        }
    }
    phase_ = Phase::WAIT_ACTION;
    // Keep cursor_ on whatever move the user picked last turn — Gen 1 UX:
    // tapping K/A repeatedly spams the same move.
}

void MonsterMeshTextBattle::inBattleLevelUp(uint8_t slot)
{
    auto &p  = engine_.party(0);
    if (slot >= p.count || slot >= 6) return;
    auto &m  = p.mons[slot];
    uint8_t oldLvl = m.level;
    if (oldLvl >= 100) return;
    uint8_t newLvl = (uint8_t)(oldLvl + 1);

    // Linear stat scale — not strictly Gen 1 accurate but avoids replumbing
    // base stats + DVs through the engine. The proper recompute happens
    // when the engine reloads from save next battle. Current hp rises by
    // the same amount maxHp does so the user feels the level-up heal.
    auto scale = [&](uint16_t v) -> uint16_t {
        if (oldLvl == 0) return v;
        uint32_t r = (uint32_t)v * newLvl / oldLvl;
        return r > 0xFFFF ? 0xFFFF : (uint16_t)r;
    };
    uint16_t newMaxHp = scale(m.maxHp);
    uint16_t deltaHp  = (newMaxHp > m.maxHp) ? (uint16_t)(newMaxHp - m.maxHp) : 0;
    m.maxHp = newMaxHp;
    if (m.hp + deltaHp > m.maxHp) m.hp = m.maxHp;
    else                          m.hp = (uint16_t)(m.hp + deltaHp);
    m.atk = scale(m.atk);
    m.def = scale(m.def);
    m.spd = scale(m.spd);
    m.spc = scale(m.spc);
    m.level = newLvl;

    // Convert nickname from Gen 1 charset bytes to ASCII before display —
    // m.nickname is the raw SAV-loaded byte sequence (PIKACHU = 0x8F88898081...).
    // Without conversion, snprintf("%s") only renders the rare bytes that
    // happen to be valid ASCII, which is why the message showed "P grew to
    // L34" instead of "PIKACHU grew to L34".
    char ascii[16];
    gen1NameToAscii((const uint8_t *)m.nickname, sizeof(m.nickname),
                    ascii, sizeof(ascii));
    char line[40];
    snprintf(line, sizeof(line), "%.10s grew to L%u!",
             ascii[0] ? ascii : "?", (unsigned)newLvl);
    appendLog(line);
}

// Result-based XP bonus for mesh (PvP) battles. Both sides earn regardless of
// which device ran the engine. base = opponentAvgLevel * (won ? 6 : 2); added
// to each surviving Gen-1 party slot's pendingXpPerSlot_ so it flows through
// the EXISTING consumePendingXp → creditBattleXpPerSlot → SD-sav writeback
// path (no new save-writer code). Idempotent per battle; NETWORKED only, so
// local gym/wild fights are unaffected.
void MonsterMeshTextBattle::awardMeshResultXp()
{
    if (resultXpAwarded_)        return;
    if (mode_ != Mode::NETWORKED) return;   // mesh PvP only
    resultXpAwarded_ = true;

    const auto &me  = engine_.party(0);   // local party
    const auto &foe = engine_.party(1);   // opponent party

    // Opponent average level (fall back to 10 if levels are unavailable).
    uint32_t sum = 0, n = 0;
    for (uint8_t i = 0; i < foe.count && i < 6; ++i) {
        uint8_t lv = foe.mons[i].level;
        if (lv) { sum += lv; ++n; }
    }
    uint8_t oppLv = n ? (uint8_t)(sum / n) : 10;

    uint32_t base = (uint32_t)oppLv * (playerWon_ ? 6u : 2u);
    if (base < 1) base = 1;

    for (uint8_t i = 0; i < me.count && i < 6; ++i) {
        uint8_t dex = me.mons[i].species;
        if (dex < 1 || dex > 151) continue;                 // Gen-1 species only
        bool sentOut = (me.mons[i].hp > 0) ||
                       (participantMask_ & (uint8_t)(1u << i));
        if (!sentOut) continue;                             // alive or was sent out
        pendingXpPerSlot_[i] += base;
    }
}

void MonsterMeshTextBattle::handleFaints()
{
    for (int s = 0; s < 2; ++s) {
        if (engine_.party(s).mons[engine_.party(s).active].hp == 0) {
            engine_.autoReplaceIfFainted(s, engineLogCb, this);
        }
    }
}

// ── Tick — drive remote-action resolution + scroll throttle ─────────────────

void MonsterMeshTextBattle::tick(uint32_t nowMs)
{
    if (mode_ == Mode::OFF) return;

    // (No periodic reveal: drawLog renders the whole log buffer at once,
    // so a per-line scroll throttle here just caused steady-state repaints
    // that read as screen flicker.)

    if (mode_ == Mode::NETWORKED) {
        if (role_ == Role::SERVER) {
            // Stamp when the server first reaches FINISHED (for pendingKeyExit_ timeout).
            if (phase_ == Phase::FINISHED && finishedAtMs_ == 0) {
                finishedAtMs_ = nowMs;
            }
            serverAuthRetransmit(nowMs);
            // Release the key-exit hold once client acked or 45 s have elapsed.
            if (pendingKeyExit_) {
                if (!unackedUpdate_ || (finishedAtMs_ && (nowMs - finishedAtMs_) > 45000u)) {
                    pendingKeyExit_ = false;
                    exit();
                    return;
                }
            }
        } else if (role_ == Role::CLIENT) {
            clientAuthRetransmit(nowMs);
        } else {
            // Re-broadcast our pending action periodically — LoRa is lossy.
            if (phase_ == Phase::WAIT_REMOTE && lastSentAction_ != 0xFF &&
                (nowMs - lastSendMs_ >= RESEND_INTERVAL_MS)) {
                sendAction(lastSentAction_, lastSentIndex_);
            }
        }
        // Fallback: if WAIT_PEER_READY hasn't resolved in 10 s (peer is an
        // older build without TEXT_BATTLE_READY), unblock and show the UI.
        if (phase_ == Phase::WAIT_PEER_READY && nowMs >= peerReadyTimeoutMs_) {
            phase_ = Phase::WAIT_ACTION;
            appendLog("Peer ready (timeout)");
            dirty_ = true;
        }
        // Timeout: opponent hasn't sent anything in a while. Server-auth
        // uses the spec's 30 s window; legacy uses the legacy 60 s.
        uint32_t timeout = (role_ != Role::LEGACY)
                             ? TB_NO_TRAFFIC_TIMEOUT_MS
                             : REMOTE_TIMEOUT_MS;
        if ((nowMs - lastRecvMs_) > timeout &&
            phase_ != Phase::FINISHED &&
            phase_ != Phase::WAIT_PEER_READY && !awaitingAccept_) {
            appendLog("Opponent timed out.");
            if (engine_.result() == Gen1BattleEngine::Result::ONGOING) {
                engine_.forfeit(1, engineLogCb, this);
            }
            phase_ = Phase::FINISHED;
        }
        // If both sides have submitted, resolve. Server-auth uses the same
        // pendingRemoteAction_ flag (set in serverAuthOnActionV2Pkt).
        if (phase_ == Phase::WAIT_REMOTE && pendingRemoteAction_) {
            resolveTurn();
        }
    } else if (mode_ == Mode::LOCAL_ROGUELIKE) {
        // CPU acts immediately after we submit.
        if (phase_ == Phase::WAIT_REMOTE) {
            uint8_t a, i;
            engine_.cpuPickAction(1, a, i);
            engine_.submitAction(1, a, i);
            resolveTurn();
        }
    }
}

// ── Input ───────────────────────────────────────────────────────────────────

void MonsterMeshTextBattle::handleKey(uint8_t c)
{
    if (mode_ == Mode::OFF) return;
    dirty_ = true;

    if (phase_ == Phase::FINISHED) {
        // Server: if the final UPDATE hasn't been acked yet, hold the
        // screen until the client confirms (or 45s fallback). This prevents
        // exit() from clearing unackedUpdate_ and stopping the retransmit
        // before the opponent receives the battle result.
        if (role_ == Role::SERVER && unackedUpdate_ && !pendingKeyExit_) {
            pendingKeyExit_ = true;
            snprintf(endPromptOverride_, sizeof(endPromptOverride_),
                     "Syncing to opponent...");
            dirty_ = true;
            return;
        }
        exit();
        return;
    }

    // Server-auth client: CHALLENGE overlay. Y accepts, N declines.
    // (K/L are reserved for the battle UI itself — K is the move-accept
    // button and could be hit by accident while typing in the app.)
    if (phase_ == Phase::WAIT_CHALLENGE_OVERLAY) {
        if (c == 'y' || c == 'Y') {
            clientAuthSendAccept(true);
        } else if (c == 'n' || c == 'N' || c == 27 /*ESC*/) {
            clientAuthSendAccept(false);
        }
        // Any other key — ignore, leave the overlay up.
        return;
    }

    // ESC = forfeit/exit (Nintendo Start menu quit).
    if (c == 27 /* ESC */) {
        if (role_ == Role::CLIENT) {
            // Client doesn't run an engine; just send a flee ACTION_V2 so
            // the server forfeits us cleanly, then drop to FINISHED.
            clientAuthSendActionV2(2 /*FLEE*/, 0);
            phase_ = Phase::FINISHED;
            return;
        }
        if (role_ == Role::SERVER) {
            // Server-auth: ship a final UPDATE carrying the forfeit so the
            // client transitions cleanly. Legacy FORFEIT packet isn't on
            // the client's dispatch path here.
            engine_.forfeit(0, engineLogCb, this);
            serverAuthSendUpdate();
            phase_ = Phase::FINISHED;
            return;
        }
        if (mode_ == Mode::NETWORKED) sendForfeit();
        engine_.forfeit(0, engineLogCb, this);
        phase_ = Phase::FINISHED;
        return;
    }

    // Block ALL non-ESC input while we're waiting for the opponent's
    // action or rendering animations. Without this gate, repeated K
    // presses after submitting a move re-call handleKey → keyAccept →
    // engine_.submitAction(0,…) + sendAction(…) with the SAME turn,
    // which overwrites our prevSent* history (every press demotes
    // lastSent → prevSent, so after the second press both slots hold
    // the same value and the peer-catch-up path can no longer help a
    // peer stuck on the previous turn). It also floods the wire with
    // duplicate ACTION packets out of band with the 4 s resend timer.
    if (phase_ == Phase::WAIT_REMOTE || phase_ == Phase::ANIMATING ||
        phase_ == Phase::WAIT_PEER_READY) {
        return;
    }

    // Flee confirmation: K confirms (run the actual flee logic from below),
    // anything else cancels back to the move menu. F was easy to hit by
    // accident next to WASD, so a confirm gate keeps gauntlets recoverable.
    if (phase_ == Phase::WAIT_FLEE) {
        bool yes = (c == 'k' || c == 'K' || c == '\n' || c == '\r' || c == ' ');
        if (!yes) {
            phase_ = Phase::WAIT_ACTION;
            return;
        }
        if (role_ == Role::CLIENT) {
            clientAuthSendActionV2(2 /*FLEE*/, 0);
            phase_ = Phase::FINISHED;
            return;
        }
        if (mode_ == Mode::NETWORKED) {
            sendForfeit(); engine_.forfeit(0, engineLogCb, this); phase_ = Phase::FINISHED;
        } else if (role_ == Role::LEGACY) {
            // Local CPU fight: flee always succeeds. No RNG formula needed.
            appendLog("Got away safely!");
            phase_ = Phase::FINISHED;
        } else {
            ++fleeAttempts_;
            const auto &player = engine_.party(0).mons[engine_.party(0).active];
            const auto &enemy  = engine_.party(1).mons[engine_.party(1).active];
            uint32_t f = (uint32_t)player.spd * 32 / (enemy.spd + 1) + 30u * fleeAttempts_;
            bool escaped = (f >= 255) || ((uint8_t)(millis() & 0xFF) < (uint8_t)f);
            if (escaped) { appendLog("Got away safely!"); phase_ = Phase::FINISHED; }
            else { appendLog("Can't escape!"); engine_.submitAction(0, 2 /*FLEE_FAIL*/, 0); phase_ = Phase::WAIT_REMOTE; }
        }
        return;
    }

    if (c == 'f' || c == 'F') {
        // F is right next to WASD, easy to hit by accident — bounce into a
        // confirm phase instead of fleeing immediately. K confirms, L/F
        // cancels back to the previous menu.
        appendLog("Flee? K=yes  any=no");
        phase_ = Phase::WAIT_FLEE;
        dirty_ = true;
        return;
    }

    // Nintendo-style mapping: K = A (accept) and L = B (back/cancel).
    // Enter still works as an alternate accept; ESC falls through to forfeit.
    bool keyAccept = (c == 'k' || c == 'K' || c == '\n' || c == '\r' || c == ' ');
    bool keyBack   = (c == 'l' || c == 'L');

    if (phase_ == Phase::WAIT_SWITCH) {
        const auto &p = engine_.party(0);
        // 3×2 grid: W/S move between rows (±2 slots), A/D between columns.
        // Arrow keys do the obvious thing: up/down by row, left/right by col.
        auto wrap = [&](int v) {
            int n = p.count;
            return (uint8_t)((v % n + n) % n);
        };
        if (c == 'w' || c == 'W' || c == 0xB5 /* up */)
            switchCursor_ = wrap((int)switchCursor_ - 2);
        else if (c == 's' || c == 'S' || c == 0xB6 /* down */)
            switchCursor_ = wrap((int)switchCursor_ + 2);
        else if (c == 'a' || c == 'A')
            switchCursor_ = wrap((int)switchCursor_ - 1);
        else if (c == 'd' || c == 'D')
            switchCursor_ = wrap((int)switchCursor_ + 1);
        else if (keyAccept) {
            if (p.mons[switchCursor_].hp > 0 && switchCursor_ != p.active) {
                if (role_ == Role::CLIENT) {
                    clientAuthSendActionV2(1 /*SWITCH*/, switchCursor_);
                } else {
                    engine_.submitAction(0, 1 /*SWITCH*/, switchCursor_);
                    if (mode_ == Mode::NETWORKED && role_ == Role::LEGACY) {
                        sendAction(1, switchCursor_);
                    }
                }
                phase_ = Phase::WAIT_REMOTE;
            }
        } else if (keyBack || c == 27) {
            phase_ = Phase::WAIT_ACTION;
        }
        return;
    }

    if (phase_ != Phase::WAIT_ACTION) return;

    // Move-pick navigation matches the switch menu's WASD pattern:
    // 2×2 grid (slots 0/1 top, 2/3 bottom). W/S move between rows
    // (±2 with wrap), A/D between columns (±1 with wrap). 1-4 still
    // jump directly. The user accepts with K (Nintendo A).
    auto wrap4 = [](int v) { return (uint8_t)((v % 4 + 4) % 4); };
    int slot = -1;
    if (c >= '1' && c <= '4') {
        slot = c - '1';
    } else if (c == 'w' || c == 'W' || c == 0xB5 /* up */) {
        cursor_ = wrap4((int)cursor_ - 2);
        dirty_ = true;
        return;
    } else if (c == 's' || c == 'S' || c == 0xB6 /* down */) {
        cursor_ = wrap4((int)cursor_ + 2);
        dirty_ = true;
        return;
    } else if (c == 'a' || c == 'A') {
        cursor_ = wrap4((int)cursor_ - 1);
        dirty_ = true;
        return;
    } else if (c == 'd' || c == 'D') {
        cursor_ = wrap4((int)cursor_ + 1);
        dirty_ = true;
        return;
    }

    if (slot >= 0) {
        cursor_ = (uint8_t)slot;
        keyAccept = true;  // number key directly submits — no separate K press needed
    }

    if (keyAccept) {
        // Networked initiator only: block move submission until the peer
        // has proven their battle station is up by sending us anything in
        // this session. Without this, an initiator who presses a move
        // before the receiver's screen finished rendering produced a
        // turn-0 ACTION the receiver missed → engines desynced from turn 0.
        if (mode_ == Mode::NETWORKED && !peerReady_) {
            appendLog("Waiting for opponent...");
            dirty_ = true;
            return;
        }
        const auto &mon = engine_.party(0).mons[engine_.party(0).active];
        // Out of PP across the whole moveset → Gen 1 Struggle (id sentinel
        // 0xFE on the wire). Without this, K-accept silently no-oped and the
        // battle looked frozen after a long gauntlet drained every move.
        bool anyPp = false;
        for (uint8_t i = 0; i < 4; ++i) {
            if (mon.moves[i] != 0 && mon.pp[i] > 0) { anyPp = true; break; }
        }
        if (!anyPp) {
            // Client never runs the engine — just send ACTION_V2(Struggle).
            if (role_ == Role::CLIENT) {
                clientAuthSendActionV2(0 /*USE_MOVE*/, 0xFE /*STRUGGLE*/);
            } else {
                engine_.submitAction(0, 0 /*USE_MOVE*/, 0xFE /*STRUGGLE*/);
                // Server-auth: client doesn't listen on the legacy ACTION
                // wire; UPDATE conveys whatever happened post-resolve.
                if (mode_ == Mode::NETWORKED && role_ == Role::LEGACY) {
                    sendAction(0, 0xFE);
                }
            }
            phase_ = Phase::WAIT_REMOTE;
            return;
        }
        if (mon.moves[cursor_] == 0 || mon.pp[cursor_] == 0) {
            Serial.printf("[MMB] K-accept: REJECTED moves[%u]=%u pp[%u]=%u\n",
                          (unsigned)cursor_, (unsigned)mon.moves[cursor_],
                          (unsigned)cursor_, (unsigned)mon.pp[cursor_]);
            return;
        }
        // Disabled move (Disable, mostly) — block selection so the user
        // can't burn the K-accept on a move that won't fire. The greyed
        // "DIS N" label in drawMoveMenu signals this visually.
        if (mon.disabledSlot == cursor_ && mon.disabledTurns > 0) {
            appendLog("That move is disabled.");
            dirty_ = true;
            return;
        }
        if (role_ == Role::CLIENT) {
            clientAuthSendActionV2(0 /*USE_MOVE*/, cursor_);
        } else {
            engine_.submitAction(0, 0 /*USE_MOVE*/, cursor_);
            if (mode_ == Mode::NETWORKED && role_ == Role::LEGACY) {
                sendAction(0, cursor_);
            }
        }
        phase_ = Phase::WAIT_REMOTE;
        return;
    }

    // P or L = open switch menu. L is the Gen-1 "B" button — natural pair
    // with K=A for accept inside the switch menu (which also cancels).
    // If the active mon is wrapped/bound (trapTurns > 0), block the
    // switch — Gen 1's trapping moves prevent the target from switching
    // out.
    if (c == 'p' || c == 'P' || keyBack) {
        const auto &activeMon = engine_.party(0).mons[engine_.party(0).active];
        if (activeMon.trapTurns > 0) {
            char buf[40];
            snprintf(buf, sizeof(buf), "Can't switch — trapped for %u!",
                     (unsigned)activeMon.trapTurns);
            appendLog(buf);
            dirty_ = true;
            return;
        }
        switchCursor_ = engine_.party(0).active;
        phase_ = Phase::WAIT_SWITCH;
        dirty_ = true;
    }
}

// ── Render ──────────────────────────────────────────────────────────────────

static void drawHpBar(lgfx::LGFX_Device *g, int x, int y, int w, int h,
                       uint16_t hp, uint16_t maxHp)
{
    int filled = maxHp > 0 ? (w - 2) * hp / maxHp : 0;
    g->drawRect(x, y, w, h, FG);
    g->fillRect(x + 1, y + 1, w - 2, h - 2, DARK);
    // HP bar fill: theme accent for healthy, theme light for mid, kept red
    // for "danger" so the visual cue stands out regardless of palette.
    uint16_t color = (hp * 4 > maxHp) ? ACC
                   : (hp * 2 > maxHp) ? DIM
                   : rgb565(0xC03020);  // crisis red — only off-palette spike
    g->fillRect(x + 1, y + 1, filled, h - 2, color);
}

void MonsterMeshTextBattle::drawHpPanel(lgfx::LGFX_Device *g, uint8_t side, int y)
{
    const auto &p = engine_.party(side);
    const auto &m = p.mons[p.active];
    // Count alive vs total — a "X/N" suffix shows how much of the team is
    // still standing. Mostly useful for opponent gauntlets so the player
    // can see whether they're on Brock's last mon.
    uint8_t alive = 0;
    for (uint8_t i = 0; i < p.count && i < 6; ++i) {
        if (p.mons[i].hp > 0) alive++;
    }
    char hdr[56];
    snprintf(hdr, sizeof(hdr), "%s  L%u  %u/%u  [%u/%u]",
             m.nickname[0] ? m.nickname : "?",
             (unsigned)m.level, (unsigned)m.hp, (unsigned)m.maxHp,
             (unsigned)alive, (unsigned)p.count);
    g->setTextColor(FG, BG);
    g->setCursor(8, y);
    g->print(hdr);
    drawHpBar(g, 8, y + 14, SCREEN_W - 16, 8, m.hp, m.maxHp);
    // Substitute HP overlay: when sub is up, draw a second smaller bar in
    // accent color just above the HP bar. Lets the player see the sub
    // taking damage independently of HP.
    if (m.substituteHp > 0) {
        // Sub max in Gen 1 = maxHp / 4 (set on Substitute use).
        uint16_t subMax = m.maxHp / 4;
        if (subMax == 0) subMax = 1;
        drawHpBar(g, 8, y + 8, SCREEN_W - 16, 4, m.substituteHp, subMax);
    }
    // Status badge + sleep/confuse counter when applicable.
    const char *st = nullptr;
    if      (m.status & Gen1BattleEngine::ST_SLP) st = "SLP";
    else if (m.status & Gen1BattleEngine::ST_PSN) st = "PSN";
    else if (m.status & Gen1BattleEngine::ST_BRN) st = "BRN";
    else if (m.status & Gen1BattleEngine::ST_PAR) st = "PAR";
    else if (m.status & Gen1BattleEngine::ST_FRZ) st = "FRZ";
    if (st) {
        char badge[12];
        if (m.status & Gen1BattleEngine::ST_SLP && m.sleepTurns)
            snprintf(badge, sizeof(badge), "SLP%u", (unsigned)m.sleepTurns);
        else
            snprintf(badge, sizeof(badge), "%s", st);
        g->setCursor(SCREEN_W - 50, y);
        g->setTextColor(rgb565(0xC03020), BG);
        g->print(badge);
    } else if (m.confuseTurns) {
        char badge[12];
        snprintf(badge, sizeof(badge), "CNF%u", (unsigned)m.confuseTurns);
        g->setCursor(SCREEN_W - 50, y);
        g->setTextColor(rgb565(0xC09020), BG);
        g->print(badge);
    }

    // Stat-boost mini-badges: print non-zero stages in a compact row right
    // under the HP bar. Only show on side 0 (the player's own panel) to
    // avoid revealing the opponent's stage tracking — Gen 1 messages tell
    // the player about boosts as they happen but don't surface a board.
    if (side == 0) {
        char row[40] = {};
        size_t rl = 0;
        auto append = [&](const char *lbl, int8_t v) {
            if (!v) return;
            int n = snprintf(row + rl, sizeof(row) - rl,
                             "%s%s%d ", lbl, v > 0 ? "+" : "", (int)v);
            if (n > 0) rl += (size_t)n;
        };
        append("A", m.atkBoost);
        append("D", m.defBoost);
        append("S", m.spdBoost);
        append("C", m.spcBoost);
        if (rl > 0) {
            g->setTextColor(ACC, BG);
            g->setCursor(8, y + 24);
            g->print(row);
        }
        // Field effects on our side (active screens / mist / focused).
        char fx[24] = {};
        size_t fl = 0;
        if (p.reflect)     fl += snprintf(fx+fl, sizeof(fx)-fl, "REF ");
        if (p.lightScreen) fl += snprintf(fx+fl, sizeof(fx)-fl, "LSC ");
        if (p.mist)        fl += snprintf(fx+fl, sizeof(fx)-fl, "MIST ");
        if (p.focused)     fl += snprintf(fx+fl, sizeof(fx)-fl, "FOC ");
        if (fl > 0) {
            g->setTextColor(DIM, BG);
            g->setCursor(SCREEN_W - 96, y + 24);
            g->print(fx);
        }
    } else {
        // Opponent side: just show field effects (visible in Gen 1) but
        // not stat boost stages.
        char fx[24] = {};
        size_t fl = 0;
        if (p.reflect)     fl += snprintf(fx+fl, sizeof(fx)-fl, "REF ");
        if (p.lightScreen) fl += snprintf(fx+fl, sizeof(fx)-fl, "LSC ");
        if (p.mist)        fl += snprintf(fx+fl, sizeof(fx)-fl, "MIST ");
        if (fl > 0) {
            g->setTextColor(DIM, BG);
            g->setCursor(SCREEN_W - 96, y);
            g->print(fx);
        }
    }
}

void MonsterMeshTextBattle::drawMoveMenu(lgfx::LGFX_Device *g)
{
    const auto &mon = engine_.party(0).mons[engine_.party(0).active];
    int y = SCREEN_H - 60;

    // Two-column layout — the move name is the widest field, so we anchor
    // the slot label, name, and PP at the same x. Two columns × 2 rows fit
    // comfortably in the bottom band without overflow.
    static constexpr int colW = 152;   // 320 - 16 / 2 ≈ 152
    static constexpr int rowH = 16;
    int colX[2] = { 8, 8 + colW };
    int rowY[2] = { y + 0, y + rowH };

    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *mv = gen1Move(mon.moves[i]);
        int cx = colX[i % 2];
        int cy = rowY[i / 2];
        bool selected = (i == cursor_);
        bool disabled = (mon.disabledSlot == i && mon.disabledTurns > 0);
        bool usable   = mon.pp[i] > 0 && !disabled;

        // Highlight the selected slot with a DARK background bar.
        if (selected) g->fillRect(cx - 2, cy - 1, colW - 8, rowH, DARK);
        uint16_t fg = !usable ? DIM : (selected ? FG : DIM);
        uint16_t bg = selected ? DARK : BG;

        // 2x2 grid layout: WASD steers the cursor, K accepts. The number
        // labels remain as a quick-jump fallback.
        static const char *kLabel[4] = { "1.", "2.", "3.", "4." };
        g->setTextColor(fg, bg);
        g->setCursor(cx, cy);
        if (mv) {
            if (disabled) {
                g->printf("%s %-9.9s DIS%u",
                          kLabel[i], mv->name, (unsigned)mon.disabledTurns);
            } else {
                g->printf("%s %-9.9s %u/%u",
                          kLabel[i], mv->name, mon.pp[i], mv->pp);
            }
        } else {
            g->printf("%s ---",  kLabel[i]);
        }
    }

    // Footer: K=accept, S=switch, F=flee, L=cancel — all on one row.
    // When every move is at 0 PP we fall back to Struggle on K, so swap the
    // hint so the user isn't confused by an empty PP column.
    bool anyPp = false;
    for (uint8_t i = 0; i < 4; ++i) {
        if (mon.moves[i] != 0 && mon.pp[i] > 0) { anyPp = true; break; }
    }
    g->setCursor(8, y + 2 * rowH + 4);
    g->setTextColor(DIM, BG);
    if (anyPp) g->print("WASD=move  K=ok  P=mons  F=flee");
    else       g->print("Out of PP — K=Struggle  P=mons  F=flee");
}

void MonsterMeshTextBattle::drawSwitchMenu(lgfx::LGFX_Device *g)
{
    // 3 rows × 2 columns gives each slot ~150px horizontally — enough room
    // for nickname + level + HP without clipping. Wastes less screen than
    // the old 6×1 stack and frees up vertical space for the log behind it.
    const auto &p = engine_.party(0);
    int yTop = SCREEN_H - 60;
    int colW = SCREEN_W / 2;
    int rowH = 18;

    g->setTextColor(FG, BG);
    g->setCursor(8, yTop - 14); g->print("Pick a Pokemon:");

    for (int i = 0; i < p.count && i < 6; ++i) {
        int col = i % 2;
        int row = i / 2;
        int x = 4 + col * colW;
        int y = yTop + row * rowH;
        bool selected = (i == switchCursor_);
        uint16_t bg = selected ? DARK : BG;
        if (selected) g->fillRect(x, y - 1, colW - 4, rowH, bg);
        const auto &m = p.mons[i];
        g->setTextColor(m.hp == 0 ? DIM : FG, bg);
        g->setCursor(x + 4, y + 2);
        g->printf("%c%-10.10s L%u %u/%u",
                  i == p.active ? '*' : ' ',
                  m.nickname[0] ? m.nickname : "?",
                  (unsigned)m.level, (unsigned)m.hp, (unsigned)m.maxHp);
    }
}

void MonsterMeshTextBattle::drawLog(lgfx::LGFX_Device *g)
{
    // Terminal-style log: oldest entry at the top of the band, newest at
    // the bottom. When the band fills, the oldest scrolls off the top and
    // every other line shifts up one row.
    //
    // We dropped the per-line scrollPending throttle that was previously
    // here — it was revealing the newest line first at row 0 and back-
    // filling older lines below it, which read backwards.
    int yBase = 46;
    g->setTextColor(FG, BG);
    uint8_t shown = logFill_;
    for (uint8_t i = 0; i < shown; ++i) {
        int idx = (logHead_ + LOG_LINES - shown + i) % LOG_LINES;
        g->setCursor(8, yBase + i * 12);
        g->print(log_[idx]);
    }
}

void MonsterMeshTextBattle::drawHeader(lgfx::LGFX_Device *g)
{
    g->setTextColor(ACC, BG);
    g->setCursor(8, 4);
    if (headerOverride_[0]) {
        g->print(headerOverride_);
    } else if (mode_ == Mode::NETWORKED) {
        const char *tag = (role_ == Role::SERVER) ? "MMB-S"
                        : (role_ == Role::CLIENT) ? "MMB-C"
                                                  : "LoRa";
        unsigned t = (role_ == Role::CLIENT) ? (unsigned)clientTurn_
                                              : (unsigned)engine_.turn();
        g->printf("%s vs %.6s  T%u", tag,
                  peerTbName_[0] ? peerTbName_ : "?", t);
    } else {
        g->printf("Roguelike  T%u",  engine_.turn());
    }
}

void MonsterMeshTextBattle::render(lgfx::LGFX_Device *g)
{
    if (!g || mode_ == Mode::OFF) return;
    g->fillScreen(BG);
    drawHeader(g);

    // CHALLENGE overlay: engine hasn't been initialized yet (start is
    // deferred to ACCEPT), so the HP panels would render an empty
    // "? L0 0/0" placeholder. Replace the whole battle UI with a
    // centred challenge prompt instead.
    if (phase_ == Phase::WAIT_CHALLENGE_OVERLAY) {
        int by = SCREEN_H / 2 - 50;
        int bh = 100;
        g->fillRect(20, by, SCREEN_W - 40, bh, DARK);
        g->drawRect(20, by, SCREEN_W - 40, bh, ACC);
        g->setTextColor(FG, DARK);
        g->setCursor(34, by + 14);
        g->print("MonsterMesh Battle!");
        g->setTextColor(ACC, DARK);
        char who[40];
        snprintf(who, sizeof(who), "%.12s wants to fight",
                 peerTbName_[0] ? peerTbName_ : "Trainer");
        g->setCursor(34, by + 36);
        g->print(who);
        char party[40];
        snprintf(party, sizeof(party), "Party: %u mon",
                 (unsigned)wirePeer_.count);
        g->setTextColor(DIM, DARK);
        g->setCursor(34, by + 56);
        g->print(party);
        g->setTextColor(FG, DARK);
        g->setCursor(34, by + 76);
        g->print("Y = ACCEPT    N = DECLINE");
        dirty_ = false;
        return;
    }

    // Initiator: hold full battle UI until receiver confirms readiness.
    if (phase_ == Phase::WAIT_PEER_READY) {
        g->setTextColor(FG, BG);
        g->setCursor(8, SCREEN_H / 2 - 8);
        g->print("Waiting for opponent…");
        g->setTextColor(DIM, BG);
        g->setCursor(8, SCREEN_H / 2 + 12);
        g->print("Battle will start when they connect.");
        dirty_ = false;
        return;
    }

    // Layout reads top-down so it's obvious whose HP is whose:
    //   header → opponent HP → log text → my HP → move/switch menu
    drawHpPanel(g, 1, 18);    // opponent on top
    drawLog(g);                // log fills the middle band (y=46–118)
    drawHpPanel(g, 0, 124);    // my HP just above the move menu
    if (phase_ == Phase::WAIT_SWITCH)      drawSwitchMenu(g);
    else if (phase_ == Phase::WAIT_ACTION) drawMoveMenu(g);
    else if (phase_ == Phase::WAIT_FLEE) {
        // Flee-confirm overlay: a centered box at the bottom band asking
        // for K to confirm or anything else to cancel. Drawn over the
        // move menu's slot so the previous selection stays visible above.
        int by = SCREEN_H - 60;
        int bh = 56;
        g->fillRect(8, by, SCREEN_W - 16, bh, DARK);
        g->drawRect(8, by, SCREEN_W - 16, bh, ACC);
        g->setTextColor(FG, DARK);
        g->setCursor(20, by + 8);
        g->print("Flee?");
        g->setTextColor(DIM, DARK);
        g->setCursor(20, by + 28);
        g->print("K=yes  L/F=no");
    }
    else if (phase_ == Phase::WAIT_REMOTE) {
        g->setTextColor(FG, BG);
        g->setCursor(8, SCREEN_H - 24);
        if (mode_ == Mode::NETWORKED && role_ == Role::SERVER && awaitingAccept_) {
            g->print("Awaiting accept…");
        } else {
            g->print(mode_ == Mode::NETWORKED ? "Waiting for opponent…" : "…");
        }
    } else if (phase_ == Phase::FINISHED) {
        g->setTextColor(FG, BG);
        g->setCursor(8, SCREEN_H - 24);
        g->print(endPromptOverride_[0] ? endPromptOverride_ : "Press any key to exit.");
    }
    dirty_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Server-authoritative PvP — wire glue
// ─────────────────────────────────────────────────────────────────────────────

// Protocol V2: the party blob is the neutral cross-gen WireParty — see
// WirePartyCodec.h (packWireParty / unpackWireParty / gen1PartyToWireParty).
// The V1 Gen-1-only packPartyMinTb/unpackPartyMinTb helpers are gone with it.

// Send the CHALLENGE packet. Layout:
//   header (4B) | [0..3]=targetId (BE) [4]=gen [5]=nameLen [6..]=name | WireParty (139 B)
void MonsterMeshTextBattle::serverAuthSendChallenge()
{
    uint8_t buf[BATTLELINK_MAX_PKT];
    memset(buf, 0, sizeof(buf));
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_CHALLENGE_V2;
    pkt->setSessionId(session_);
    pkt->seq = 0;  // CHALLENGE doesn't participate in the UPDATE seq stream

    // targetId (4B BE): receiving nodes drop the challenge if it's not for them.
    pkt->payload[0] = (remoteId_ >> 24) & 0xFF;
    pkt->payload[1] = (remoteId_ >> 16) & 0xFF;
    pkt->payload[2] = (remoteId_ >>  8) & 0xFF;
    pkt->payload[3] =  remoteId_        & 0xFF;

    size_t nameLen = strlen(myTbName_);
    if (nameLen > TB_MAX_NAME_LEN) nameLen = TB_MAX_NAME_LEN;
    pkt->payload[4] = 3;                          // gen-3 mechanics (default)
    pkt->payload[5] = (uint8_t)nameLen;
    memcpy(pkt->payload + 6, myTbName_, nameLen);
    // Wire form of our party: staged directly by a Gen 2/3 save, or derived
    // from the Gen-1 party here (same numbers the engine computes locally).
    if (!haveWireParty_) gen1PartyToWireParty(pendingMyParty_, wireMy_);
    packWireParty(wireMy_, pkt->payload + 6 + nameLen);

    size_t payloadLen = 6 + nameLen + TB_WIRE_PARTY_BYTES;
    Serial.printf("[MMB] serverAuthSendChallenge: sz=%u tries=%u session=0x%04X target=0x%08X\n",
                  (unsigned)(BATTLELINK_HDR_SIZE + payloadLen),
                  (unsigned)challengeTries_ + 1, (unsigned)session_, (unsigned)remoteId_);
    transport_.queueSend(buf, BATTLELINK_HDR_SIZE + payloadLen);
    lastChallengeMs_ = millis();
    challengeTries_++;
}

// Build the canonical client-visible board buffer. Must be byte-identical
// on server and client for the boardHash24 check to pass — i.e. wire side
// 0 is "client's party" and wire side 1 is "server's party" on BOTH
// sides. Each side initializes its local engine with itself as P0, so:
//   on the SERVER (role=SERVER/LEGACY): wire 0 = engine P1, wire 1 = P0
//   on the CLIENT  (role=CLIENT):       wire 0 = engine P0, wire 1 = P1
// Similarly, the trailing "client's active mon PP" maps to engine P1 on
// the server and engine P0 on the client. Both sides agree on which
// 4-byte PP they're sampling.
//
// Same layout is the body of FULL_STATE.
size_t MonsterMeshTextBattle::packClientStateFromEngine(uint8_t out[])
{
    bool roleClient = (role_ == Role::CLIENT);
    // Client never calls engine_.executeTurn, so engine_.turn() stays at 0
    // forever. Use the server-mirrored clientTurn_ instead — otherwise the
    // hash mismatches every UPDATE after turn 0 and we fall into a
    // STATE_REQUEST → FULL_STATE loop on every move.
    uint16_t turn = roleClient ? (uint16_t)clientTurn_ : engine_.turn();
    // Result is always ONGOING in the board hash. The client never calls
    // executeTurn so engine_.result() is permanently ONGOING on that side,
    // which caused the board hash to mismatch on every final UPDATE and
    // forced a STATE_REQUEST → FULL_STATE cycle that never showed the result.
    // The actual outcome is conveyed via the TB_UPD_RESULT flag in the UPDATE
    // packet (serverAuthSendUpdate), not via the board hash.
    uint8_t result = TB_RESULT_ONGOING;

    size_t w = 0;
    out[w++] = (turn >> 8) & 0xFF;
    out[w++] =  turn       & 0xFF;
    out[w++] = result;

    // Role-dependent wire→engine mapping (see comment above).
    static const uint8_t wireToEngineSrv[2] = { 1, 0 };
    static const uint8_t wireToEngineCli[2] = { 0, 1 };
    const uint8_t *map = roleClient ? wireToEngineCli : wireToEngineSrv;
    for (uint8_t ws = 0; ws < 2; ++ws) {
        const auto &party = engine_.party(map[ws]);
        out[w++] = party.active;
        out[w++] = party.count;
        for (uint8_t i = 0; i < party.count && i < 6; ++i) {
            const auto &m = party.mons[i];
            out[w++] = (m.hp >> 8) & 0xFF;
            out[w++] =  m.hp       & 0xFF;
            out[w++] = m.status;
        }
    }

    // "Client's active mon PP" — wire side 0 → engine P1 (server) or P0
    // (client) depending on role.
    uint8_t ppSide = roleClient ? 0 : 1;
    const auto &cp = engine_.party(ppSide);
    const auto &cm = cp.mons[cp.active];
    for (uint8_t i = 0; i < 4; ++i) out[w++] = cm.pp[i];

    return w;
}

// Build + send an UPDATE. For simplicity, every UPDATE carries every flag
// section (HP/PP/switch/status) plus the inline boardHash24. Result + log
// + needPlayerSwitch are conditional. This is bandwidth-wasteful vs a
// true diff but keeps the implementation simple and the apply logic
// trivially idempotent on the client side.
void MonsterMeshTextBattle::serverAuthSendUpdate()
{
    uint8_t board[80];
    size_t blen = packClientStateFromEngine(board);
    uint32_t h24 = tbBoardHash24(board, blen);
    // Heavy server-side buffer dump removed — was producing ~150 B of
    // serial output PER UPDATE, which over 27+ turns saturated the
    // USB-CDC and probably contributed to a watchdog reset. The client
    // still dumps its own board on hash mismatch, which is rare enough
    // to be diagnostic without being lethal.

    uint8_t buf[BATTLELINK_MAX_PKT];
    memset(buf, 0, sizeof(buf));
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_UPDATE;
    pkt->setSessionId(session_);
    pkt->seq = ++updateSeq_;

    uint16_t flags = TB_UPD_HP | TB_UPD_PP | TB_UPD_SWITCH | TB_UPD_STATUS |
                     TB_UPD_BENCH | TB_UPD_FX;
    bool finished = (engine_.result() != Gen1BattleEngine::Result::ONGOING);
    if (finished) flags |= TB_UPD_RESULT;

    // Drain the log buffer into the packet if there's room.
    uint8_t numLogLines = 0;
    if (unshippedLogLines_ > 0) {
        numLogLines = unshippedLogLines_;
        if (numLogLines > TB_UPDATE_MAX_LOG_LINES) {
            numLogLines = TB_UPDATE_MAX_LOG_LINES;
        }
        flags |= TB_UPD_LOG;
    }

    // needPlayerSwitch on the CLIENT side (wire 0 = engine P1) when their
    // active mon has fainted but the battle is still ongoing.
    if (!finished) {
        const auto &cp = engine_.party(1);
        if (cp.mons[cp.active].hp == 0) flags |= TB_UPD_NEED_PLAYER_SWITCH;
    }

    size_t w = 0;
    pkt->payload[w++] = (engine_.turn() & 0xFF);
    pkt->payload[w++] = (flags >> 8) & 0xFF;
    pkt->payload[w++] =  flags       & 0xFF;
    pkt->payload[w++] = (h24 >> 16) & 0xFF;
    pkt->payload[w++] = (h24 >>  8) & 0xFF;
    pkt->payload[w++] =  h24        & 0xFF;

    if (flags & TB_UPD_HP) {
        const auto &c = engine_.party(1).mons[engine_.party(1).active];
        const auto &s = engine_.party(0).mons[engine_.party(0).active];
        pkt->payload[w++] = (c.hp >> 8) & 0xFF;
        pkt->payload[w++] =  c.hp       & 0xFF;
        pkt->payload[w++] = (s.hp >> 8) & 0xFF;
        pkt->payload[w++] =  s.hp       & 0xFF;
    }
    if (flags & TB_UPD_PP) {
        const auto &c = engine_.party(1).mons[engine_.party(1).active];
        for (uint8_t i = 0; i < 4; ++i) pkt->payload[w++] = c.pp[i];
    }
    if (flags & TB_UPD_SWITCH) {
        pkt->payload[w++] = engine_.party(1).active;
        pkt->payload[w++] = engine_.party(0).active;
    }
    if (flags & TB_UPD_STATUS) {
        pkt->payload[w++] = engine_.party(1).mons[engine_.party(1).active].status;
        pkt->payload[w++] = engine_.party(0).mons[engine_.party(0).active].status;
    }
    if (flags & TB_UPD_RESULT) {
        // Map engine result → client POV (engine P1 = client).
        uint8_t r = TB_RESULT_ONGOING;
        switch (engine_.result()) {
            case Gen1BattleEngine::Result::P2_WIN: r = TB_RESULT_YOU_WIN;  break;
            case Gen1BattleEngine::Result::P1_WIN: r = TB_RESULT_YOU_LOSE; break;
            case Gen1BattleEngine::Result::DRAW:   r = TB_RESULT_DRAW;     break;
            default: break;
        }
        pkt->payload[w++] = r;
    }
    if (flags & TB_UPD_LOG) {
        // Reserve room for BENCH (≤49 B) + FX (≤19 B) that follow LOG —
        // both write unconditionally without their own bounds checks, so
        // if LOG fills close to the limit they'd scribble past
        // pkt->payload[196] into adjacent stack memory and crash the
        // deck. Reserve: BENCH(49) + EnemyBench(19) + FX(19) = 87 bytes.
        const size_t kPostLogReserve = 95;
        size_t logBudgetEnd = (BATTLELINK_MAX_PAYLOAD > kPostLogReserve)
                                 ? (BATTLELINK_MAX_PAYLOAD - kPostLogReserve)
                                 : 0;
        // Patch numLogLines AFTER we know how many we actually fit, since
        // the count drives the client-side parser. Without this patch the
        // client would expect 6 lines but find 3, run off the buffer,
        // and bail without applying BENCH/FX.
        size_t countSlot = w;
        pkt->payload[w++] = 0;
        uint8_t shippedLines = 0;
        // Walk back from the newest line by `numLogLines` so we ship
        // exactly the lines that haven't been shipped yet, in oldest →
        // newest order.
        uint8_t startOffset = unshippedLogLines_;
        if (startOffset > numLogLines) startOffset = numLogLines;
        for (uint8_t i = 0; i < startOffset; ++i) {
            uint8_t idx = (logHead_ + LOG_LINES - startOffset + i) % LOG_LINES;
            size_t llen = strnlen(log_[idx], LOG_WIDTH);
            if (w + 1 + llen > logBudgetEnd) break;
            pkt->payload[w++] = (uint8_t)llen;
            memcpy(pkt->payload + w, log_[idx], llen);
            w += llen;
            shippedLines++;
        }
        pkt->payload[countSlot] = shippedLines;
        // Keep log_[] / logHead_ / logFill_ alive so the server's own
        // drawLog() still has lines to render — without this the server
        // screen went blank between turns. Just mark the lines as
        // shipped so the next UPDATE doesn't re-ship them.
        unshippedLogLines_ = 0;
    }
    if (flags & TB_UPD_BENCH) {
        // CLIENT-side party state (wire 0 = engine P1). Keeps the client's
        // switch menu accurate for HP, status, and per-slot PP that may
        // have decremented while a mon was active. Also ships the active
        // mon's stat-boost stages so the UI can show "+1 ATK" badges.
        const auto &cp = engine_.party(1);
        pkt->payload[w++] = cp.count;
        for (uint8_t i = 0; i < cp.count && i < 6; ++i) {
            const auto &m = cp.mons[i];
            pkt->payload[w++] = (m.hp >> 8) & 0xFF;
            pkt->payload[w++] =  m.hp       & 0xFF;
            pkt->payload[w++] = m.status;
            pkt->payload[w++] = m.pp[0];
            pkt->payload[w++] = m.pp[1];
            pkt->payload[w++] = m.pp[2];
            pkt->payload[w++] = m.pp[3];
        }
        const auto &cmActive = cp.mons[cp.active];
        pkt->payload[w++] = (uint8_t)cmActive.atkBoost;
        pkt->payload[w++] = (uint8_t)cmActive.defBoost;
        pkt->payload[w++] = (uint8_t)cmActive.spdBoost;
        pkt->payload[w++] = (uint8_t)cmActive.spcBoost;
        pkt->payload[w++] = (uint8_t)cmActive.accBoost;
        pkt->payload[w++] = (uint8_t)cmActive.evaBoost;
        // Server/enemy bench: HP + status for all of the server's mons
        // (engine P0 on server = P1 from client's POV). Without this the
        // client's engine retains stale HP for benched/fainted server mons
        // and hashes differently after a server pokemon faints and switches.
        const auto &sbp = engine_.party(0);
        pkt->payload[w++] = sbp.count;
        for (uint8_t i = 0; i < sbp.count && i < 6; ++i) {
            pkt->payload[w++] = (sbp.mons[i].hp >> 8) & 0xFF;
            pkt->payload[w++] =  sbp.mons[i].hp       & 0xFF;
            pkt->payload[w++] = sbp.mons[i].status;
        }
    }
    if (flags & TB_UPD_FX) {
        // Per active mon + per side: visible Gen-1 FX state. Order:
        //   wire 0 = client (engine P1), wire 1 = server (engine P0).
        static const uint8_t wireToEng[2] = { 1, 0 };
        for (uint8_t ws = 0; ws < 2; ++ws) {
            const auto &party = engine_.party(wireToEng[ws]);
            const auto &m     = party.mons[party.active];
            pkt->payload[w++] = m.sleepTurns;
            pkt->payload[w++] = m.confuseTurns;
            pkt->payload[w++] = (m.substituteHp >> 8) & 0xFF;
            pkt->payload[w++] =  m.substituteHp       & 0xFF;
            uint8_t mf = 0;
            if (m.mustRecharge)         mf |= 1u << 0;
            if (m.flinched)             mf |= 1u << 1;
            if (m.thrashing)            mf |= 1u << 2;
            if (m.rageActive)           mf |= 1u << 3;
            if (m.transformed)          mf |= 1u << 4;
            if (m.chargingSlot != 0xFF) mf |= 1u << 5;
            pkt->payload[w++] = mf;
            uint8_t ff = 0;
            if (party.reflect)     ff |= 1u << 0;
            if (party.lightScreen) ff |= 1u << 1;
            if (party.mist)        ff |= 1u << 2;
            if (party.focused)     ff |= 1u << 3;
            pkt->payload[w++] = ff;
            pkt->payload[w++] = party.reflectTurns;
            pkt->payload[w++] = party.lightScreenTurns;
        }
        // Client-side only: disabled move + trap counters (the opponent
        // doesn't need to know exactly which slot is disabled — only the
        // owner's UI uses these to grey out the disabled move and the
        // switch option when wrapped).
        const auto &cp2 = engine_.party(1);
        const auto &cm2 = cp2.mons[cp2.active];
        pkt->payload[w++] = cm2.disabledSlot;
        pkt->payload[w++] = cm2.disabledTurns;
        pkt->payload[w++] = cm2.trapTurns;
    }

    size_t pktLen = BATTLELINK_HDR_SIZE + w;
    transport_.queueSend(buf, pktLen);
    memcpy(lastUpdateBuf_, buf, pktLen);
    lastUpdateLen_     = pktLen;
    unackedUpdate_     = true;
    lastUpdateSendMs_  = millis();
    Serial.printf("[MMB] server UPDATE tx seq=%u flags=0x%04X turn=%u "
                  "logLines=%u pktLen=%u\n",
                  (unsigned)updateSeq_, (unsigned)flags,
                  (unsigned)engine_.turn(), (unsigned)numLogLines,
                  (unsigned)pktLen);
}

void MonsterMeshTextBattle::serverAuthSendFullState()
{
    uint8_t buf[BATTLELINK_MAX_PKT];
    memset(buf, 0, sizeof(buf));
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_FULL_STATE;
    pkt->setSessionId(session_);
    pkt->seq = updateSeq_;
    size_t blen = packClientStateFromEngine(pkt->payload);
    transport_.queueSend(buf, BATTLELINK_HDR_SIZE + blen);
}

// CLIENT → SERVER: an ACCEPT(1, party, name) lands. Unpack, init engine
// with both parties, transition into RUN. ACCEPT(0) → "Declined" → exit.
void MonsterMeshTextBattle::serverAuthOnAcceptPkt(uint32_t fromId,
                                                  const uint8_t *buf, size_t len)
{
    if (!awaitingAccept_) {
        Serial.printf("[MMB] DROP ACCEPT from=0x%08X reason=not_awaiting\n",
                      (unsigned)fromId);
        return;
    }
    // Audit finding 5: only the bound challenged peer may accept or decline.
    // Reject before any accept/decline handling so a third node cannot end or
    // hijack our pending challenge merely by knowing the session id.
    if (fromId != remoteId_) {
        Serial.printf("[MMB] DROP ACCEPT from=0x%08X reason=not_peer (peer=0x%08X)\n",
                      (unsigned)fromId, (unsigned)remoteId_);
        return;
    }
    if (len < BATTLELINK_HDR_SIZE + 2 + TB_WIRE_PARTY_BYTES) {
        Serial.printf("[MMB] DROP ACCEPT from=0x%08X reason=short_len=%u min=%u\n",
                      (unsigned)fromId, (unsigned)len,
                      (unsigned)(BATTLELINK_HDR_SIZE + 2 + TB_WIRE_PARTY_BYTES));
        return;
    }
    const BattlePacket *pkt = (const BattlePacket *)buf;
    if (pkt->sessionId() != session_) {
        Serial.printf("[MMB] DROP ACCEPT from=0x%08X reason=session pkt=0x%04X our=0x%04X\n",
                      (unsigned)fromId, (unsigned)pkt->sessionId(),
                      (unsigned)session_);
        return;
    }

    uint8_t accepted = pkt->payload[0];
    uint8_t nameLen  = pkt->payload[1];
    if (nameLen > TB_MAX_NAME_LEN) nameLen = TB_MAX_NAME_LEN;
    if (len < (size_t)BATTLELINK_HDR_SIZE + 2 + nameLen + TB_WIRE_PARTY_BYTES) {
        Serial.printf("[MMB] DROP ACCEPT from=0x%08X reason=short_after_name "
                      "nameLen=%u len=%u\n",
                      (unsigned)fromId, nameLen, (unsigned)len);
        return;
    }

    if (!accepted) {
        appendLog("Challenge declined.");
        phase_ = Phase::FINISHED;
        awaitingAccept_ = false;
        dirty_ = true;
        Serial.printf("[MMB] server-auth ACCEPT(0) from=0x%08X\n",
                      (unsigned)fromId);
        return;
    }

    // Audit finding 4/5: decode + validate the accepter's party into a LOCAL
    // before binding it or starting the engine. A malformed ACCEPT party must
    // not partially install into wirePeer_ or launch a battle from garbage.
    Gen1BattleEngine::WireParty peerParty;
    TbPartyValidationError partyError = TbPartyValidationError::NONE;
    const size_t partyOffset = (size_t)BATTLELINK_HDR_SIZE + 2 + nameLen;
    if (!tbUnpackAndValidateWireParty(buf + partyOffset, len - partyOffset,
                                      peerParty, /*gen superset*/ 3, &partyError)) {
        Serial.printf("[MMB] DROP ACCEPT from=0x%08X reason=invalid_party error=%u\n",
                      (unsigned)fromId, (unsigned)partyError);
        return;
    }

    memset(peerTbName_, 0, sizeof(peerTbName_));
    memcpy(peerTbName_, pkt->payload + 2, nameLen);
    wirePeer_ = peerParty;   // bind only the validated party

    // Init engine: P0 = us, P1 = client. Seed is private to server (client
    // never runs the engine so doesn't need it). wireMy_ was built when the
    // CHALLENGE went out, so both engines seed from identical parties.
    uint32_t rngSeed = (uint32_t)(millis() ^ esp_random() ^ fromId);
    engine_.start(wireMy_, wirePeer_, rngSeed);
    // Heal both sides — same rationale as legacy networked path (the wire
    // party strips HP/PP/status; everyone starts fully healed in mesh PvP).
    for (uint8_t side = 0; side < 2; ++side) {
        auto &p = engine_.party(side);
        for (uint8_t i = 0; i < p.count && i < 6; ++i) {
            auto &m = p.mons[i];
            m.hp     = m.maxHp;
            m.status = 0;
            for (uint8_t s = 0; s < 4; ++s) {
                const Gen1MoveData *mv = (engine_.gen() >= 3)
                                             ? gen3Move(m.moves[s])
                                             : gen1Move(m.moves[s]);
                m.pp[s] = mv ? mv->pp : 0;
            }
        }
    }

    awaitingAccept_ = false;
    peerReady_      = true;
    remoteId_       = fromId;
    pendingRemoteAction_ = false;
    phase_          = Phase::WAIT_ACTION;
    // Refresh the no-traffic timer: until now we were awaitingAccept_ and
    // the timeout was gated off, but lastRecvMs_ was stamped at CHALLENGE-
    // send time. Without this refresh the 30 s clock could expire within
    // a few seconds of the battle actually starting if the CHALLENGE +
    // ACCEPT round-trip took most of the budget already.
    lastRecvMs_     = millis();
    handleFaints();
    appendLog("Battle begins!");
    char wlog[40];
    snprintf(wlog, sizeof(wlog), "Vs %.12s",
             peerTbName_[0] ? peerTbName_ : "trainer");
    appendLog(wlog);

    // Drop any CHALLENGE retransmits still sitting in the TX queue —
    // ACCEPT is in, the handshake is done, those would only delay the
    // first UPDATE (LoRa drains at ~116 B/s, queue can have 2-3 stale
    // 127-byte CHALLENGEs from before ACCEPT made it back).
    transport_.dropPendingSends();

    // Ship a turn-0 UPDATE immediately so the client renders the initial
    // board even before either side has acted.
    serverAuthSendUpdate();

    dirty_ = true;
    Serial.printf("[MMB] server-auth ACCEPT(1) from=0x%08X name=%s "
                  "count=%u seed=0x%08X\n",
                  (unsigned)fromId, peerTbName_,
                  (unsigned)wirePeer_.count,
                  (unsigned)rngSeed);
}

void MonsterMeshTextBattle::serverAuthOnActionV2Pkt(uint32_t fromId,
                                                    const uint8_t *buf, size_t len)
{
    if (len < BATTLELINK_HDR_SIZE + TB_ACTION_BYTES) {
        Serial.printf("[MMB] DROP ACTION_V2 from=0x%08X reason=short_len=%u\n",
                      (unsigned)fromId, (unsigned)len);
        return;
    }
    const BattlePacket *pkt = (const BattlePacket *)buf;
    if (pkt->sessionId() != session_) {
        Serial.printf("[MMB] DROP ACTION_V2 from=0x%08X reason=session pkt=0x%04X our=0x%04X\n",
                      (unsigned)fromId, (unsigned)pkt->sessionId(),
                      (unsigned)session_);
        return;
    }
    if (fromId != remoteId_) {
        Serial.printf("[MMB] DROP ACTION_V2 from=0x%08X reason=not_peer (peer=0x%08X)\n",
                      (unsigned)fromId, (unsigned)remoteId_);
        return;
    }
    Serial.printf("[MMB] server ACTION_V2 rx from=0x%08X\n", (unsigned)fromId);

    uint8_t turn, actionType, index, lastAckedSeq;
    if (!tbUnpackAction(pkt->payload, len - BATTLELINK_HDR_SIZE,
                        turn, actionType, index, lastAckedSeq)) return;

    // ACK piggyback: clear unackedUpdate_ if the client confirmed our seq.
    if (unackedUpdate_ && lastAckedSeq == updateSeq_) {
        unackedUpdate_ = false;
    }

    // Ignore action for the wrong turn (idempotent: client retransmits).
    if (turn != (uint8_t)(engine_.turn() & 0xFF)) {
        Serial.printf("[MMB] server-auth ACTION turn=%u (we're at %u) — "
                      "dropping\n", turn, (unsigned)(engine_.turn() & 0xFF));
        return;
    }

    if (actionType == 0xFE /* pure-ACK sentinel — client has no action yet */) {
        lastRecvMs_ = millis();
        return;
    }
    if (actionType == 2 /* flee */) {
        engine_.forfeit(1, engineLogCb, this);
        appendLog("Opponent fled.");
        phase_ = Phase::FINISHED;
        serverAuthSendUpdate();
        return;
    }

    engine_.submitAction(1, actionType, index);
    pendingRemoteAction_ = true;
    lastRecvMs_ = millis();
}

void MonsterMeshTextBattle::serverAuthOnStateRequestPkt(uint32_t fromId,
                                                        const uint8_t *buf, size_t len)
{
    if (len < BATTLELINK_HDR_SIZE + TB_STATE_REQUEST_BYTES) return;
    const BattlePacket *pkt = (const BattlePacket *)buf;
    if (pkt->sessionId() != session_) return;
    if (fromId != remoteId_) return;
    uint8_t lastAppliedSeq = 0;
    tbUnpackStateRequest(pkt->payload, len - BATTLELINK_HDR_SIZE, lastAppliedSeq);
    Serial.printf("[MMB] server-auth STATE_REQUEST from=0x%08X "
                  "client_lastApplied=%u our_updateSeq=%u\n",
                  (unsigned)fromId, lastAppliedSeq, (unsigned)updateSeq_);
    serverAuthSendFullState();
}

void MonsterMeshTextBattle::serverAuthRetransmit(uint32_t nowMs)
{
    // CHALLENGE retransmit until ACCEPT arrives or tries exhausted.
    if (awaitingAccept_) {
        if (nowMs - lastChallengeMs_ >= TB_CHALLENGE_RESEND_MS) {
            if (challengeTries_ >= TB_CHALLENGE_MAX_TRIES) {
                appendLog("No response — cancelled.");
                phase_ = Phase::FINISHED;
                awaitingAccept_ = false;
                dirty_ = true;
                return;
            }
            serverAuthSendChallenge();
        }
        return;
    }

    // UPDATE retransmit until client acks via ACTION_V2.lastAckedSeq.
    if (unackedUpdate_ && lastUpdateLen_ > 0 &&
        (nowMs - lastUpdateSendMs_) >= TB_UPDATE_RESEND_MS) {
        transport_.queueSend(lastUpdateBuf_, lastUpdateLen_);
        lastUpdateSendMs_ = nowMs;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CLIENT — receive CHALLENGE → overlay → ACCEPT → drive battle from UPDATEs.
//
// The client uses engine_ as a static-data view: parties are loaded once
// from CHALLENGE + ACCEPT (so maxHp / move tables / nicknames are populated
// correctly for the existing renderer), but executeTurn / submitAction are
// NEVER called. UPDATE packets mutate engine_.party() fields directly, so
// every draw* helper renders authoritative server state with no changes.
// ─────────────────────────────────────────────────────────────────────────────

void MonsterMeshTextBattle::clientAuthOnChallengePkt(uint32_t fromId,
                                                     const uint8_t *buf, size_t len)
{
    if (len < BATTLELINK_HDR_SIZE + 6 + TB_WIRE_PARTY_BYTES) {
        Serial.printf("[MMB] DROP CHALLENGE from=0x%08X reason=short_len=%u min=%u\n",
                      (unsigned)fromId, (unsigned)len,
                      (unsigned)(BATTLELINK_HDR_SIZE + 6 + TB_WIRE_PARTY_BYTES));
        return;
    }
    const BattlePacket *pkt = (const BattlePacket *)buf;

    // targetId: drop challenges not addressed to this node.
    uint32_t targetId = ((uint32_t)pkt->payload[0] << 24) |
                        ((uint32_t)pkt->payload[1] << 16) |
                        ((uint32_t)pkt->payload[2] <<  8) |
                                   pkt->payload[3];
    if (targetId != 0 && myNodeNum_ != 0 && targetId != myNodeNum_) {
        Serial.printf("[MMB] DROP CHALLENGE from=0x%08X target=0x%08X (not us 0x%08X)\n",
                      (unsigned)fromId, (unsigned)targetId, (unsigned)myNodeNum_);
        return;
    }

    uint8_t gen     = pkt->payload[4];
    uint8_t nameLen = pkt->payload[5];
    if (gen != 1 && gen != 3) {
        Serial.printf("[MMB] DROP CHALLENGE from=0x%08X reason=gen=%u (expected 1|3)\n",
                      (unsigned)fromId, gen);
        return;
    }
    if (nameLen > TB_MAX_NAME_LEN) nameLen = TB_MAX_NAME_LEN;
    if (len < (size_t)BATTLELINK_HDR_SIZE + 6 + nameLen + TB_WIRE_PARTY_BYTES) {
        Serial.printf("[MMB] DROP CHALLENGE from=0x%08X reason=short_after_name "
                      "nameLen=%u len=%u\n",
                      (unsigned)fromId, nameLen, (unsigned)len);
        return;
    }

    // Duplicate CHALLENGE arriving after we've already ACCEPTed — server's
    // CHALLENGE-retransmit ran longer than our ACCEPT made it back, or
    // both arrived in the same cycle. Re-emit ACCEPT and don't reset
    // state.
    if (mode_ != Mode::OFF && role_ == Role::CLIENT &&
        awaitingFirstUpdate_ && pkt->sessionId() == session_) {
        Serial.printf("[MMB] CHALLENGE re-rx for our active session "
                      "(0x%04X) — re-emitting ACCEPT\n",
                      (unsigned)session_);
        clientAuthSendAccept(true);
        return;
    }

    // Audit finding 4/5: decode the challenger's party into a LOCAL and fully
    // validate it BEFORE mutating any battle state or binding the peer. A
    // malformed party must leave mode_/role_/remoteId_/session_ untouched so a
    // bad packet can neither reset an idle station nor bind a bogus opponent.
    Gen1BattleEngine::WireParty peerParty;
    TbPartyValidationError partyError = TbPartyValidationError::NONE;
    const size_t partyOffset = (size_t)BATTLELINK_HDR_SIZE + 6 + nameLen;
    if (!tbUnpackAndValidateWireParty(buf + partyOffset, len - partyOffset,
                                      peerParty, gen, &partyError)) {
        Serial.printf("[MMB] DROP CHALLENGE from=0x%08X reason=invalid_party error=%u\n",
                      (unsigned)fromId, (unsigned)partyError);
        return;
    }

    playerWon_ = false;
    resultXpAwarded_ = false;
    mode_     = Mode::NETWORKED;
    role_     = Role::CLIENT;
    phase_    = Phase::WAIT_CHALLENGE_OVERLAY;
    remoteId_ = fromId;
    session_  = pkt->sessionId();
    cursor_   = 0; switchCursor_ = 0;
    logFill_ = logHead_ = 0; scrollPending_ = 0;
    peerReady_ = true;        // server already proved liveness by sending CHALLENGE
    lastAppliedUpdateSeq_ = 0;
    clientActionType_     = 0xFF;
    clientNeedsFullState_ = false;
    pendingRemoteAction_  = false;
    preAcceptUpdateLen_   = 0;

    memset(peerTbName_, 0, sizeof(peerTbName_));
    memcpy(peerTbName_, pkt->payload + 6, nameLen);
    wirePeer_   = peerParty;   // bind only the validated party
    sessionGen_ = gen;

    char line[40];
    snprintf(line, sizeof(line), "%.12s wants to battle!",
             peerTbName_[0] ? peerTbName_ : "Trainer");
    appendLog(line);
    appendLog("Y=accept  N=decline");
    lastRecvMs_ = millis();
    dirty_ = true;
    Serial.printf("[MMB] server-auth CHALLENGE rx from=0x%08X session=0x%04X "
                  "name=%s count=%u gen=%u\n",
                  (unsigned)fromId, (unsigned)session_,
                  peerTbName_, (unsigned)wirePeer_.count, (unsigned)gen);
}

void MonsterMeshTextBattle::clientAuthSendAccept(bool accepted)
{
    // Audit finding 5: revalidate our staged SAV party at the last moment. The
    // CHALLENGE overlay may have opened just before a daycare/save writer
    // changed the party; sending a structurally valid but stale party would
    // desync the battle. Convert such an ACCEPT into a clean decline instead.
    if (accepted && localPartyReadyFn_ &&
        !localPartyReadyFn_(localPartyReadyCtx_)) {
        accepted = false;
        appendLog("Party changed; challenge declined.");
        Serial.printf("[MMB] ACCEPT converted to decline: local SAV party invalidated\n");
    }

    uint8_t buf[BATTLELINK_MAX_PKT];
    memset(buf, 0, sizeof(buf));
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_ACCEPT_V2;
    pkt->setSessionId(session_);
    pkt->seq = 0;

    size_t nameLen = strlen(myTbName_);
    if (nameLen > TB_MAX_NAME_LEN) nameLen = TB_MAX_NAME_LEN;

    pkt->payload[0] = accepted ? 1 : 0;
    pkt->payload[1] = (uint8_t)nameLen;
    memcpy(pkt->payload + 2, myTbName_, nameLen);
    if (!haveWireParty_) gen1PartyToWireParty(pendingMyParty_, wireMy_);
    packWireParty(wireMy_, pkt->payload + 2 + nameLen);

    size_t payloadLen = 2 + nameLen + TB_WIRE_PARTY_BYTES;
    bool qOk = transport_.queueSend(buf, BATTLELINK_HDR_SIZE + payloadLen);
    Serial.printf("[MMB] clientAuthSendAccept: accepted=%u sz=%u session=0x%04X "
                  "awaitingFirstUpdate=%u queueOk=%d\n",
                  (unsigned)accepted,
                  (unsigned)(BATTLELINK_HDR_SIZE + payloadLen),
                  (unsigned)session_, (unsigned)awaitingFirstUpdate_,
                  (int)qOk);
    lastAcceptSendMs_ = millis();

    if (!accepted) {
        appendLog("Declined.");
        phase_ = Phase::FINISHED;
        awaitingFirstUpdate_   = false;
        awaitingUpdateSinceMs_ = 0;
        return;
    }

    // Retransmit path: engine already started, just re-emit the wire
    // packet so the server gets it if the first ACCEPT was lost.
    if (awaitingFirstUpdate_) {
        Serial.printf("[MMB] server-auth ACCEPT(1) re-tx\n");
        return;
    }

    // First-time path: init engine_ with both parties so the existing
    // renderer has the static data it needs (maxHp, move tables, nicknames).
    // We never call executeTurn — UPDATEs mutate engine_.party() fields.
    engine_.start(wireMy_, wirePeer_, /*rngSeed*/0, sessionGen_);
    for (uint8_t side = 0; side < 2; ++side) {
        auto &p = engine_.party(side);
        for (uint8_t i = 0; i < p.count && i < 6; ++i) {
            auto &m = p.mons[i];
            m.hp     = m.maxHp;
            m.status = 0;
            for (uint8_t s = 0; s < 4; ++s) {
                const Gen1MoveData *mv = (engine_.gen() >= 3)
                                             ? gen3Move(m.moves[s])
                                             : gen1Move(m.moves[s]);
                m.pp[s] = mv ? mv->pp : 0;
            }
        }
    }

    phase_ = Phase::WAIT_REMOTE;   // await first UPDATE
    awaitingFirstUpdate_   = true;
    awaitingUpdateSinceMs_ = millis();
    appendLog("Battle begins!");
    lastRecvMs_ = millis();
    dirty_ = true;
    Serial.printf("[MMB] server-auth ACCEPT(1) tx — awaiting first UPDATE\n");

    // If the server sent TURN_UPDATE before we pressed Y, apply it now.
    if (preAcceptUpdateLen_ > 0) {
        size_t plen = preAcceptUpdateLen_;
        preAcceptUpdateLen_ = 0;
        Serial.printf("[MMB] applying buffered pre-accept UPDATE len=%u\n", (unsigned)plen);
        clientAuthOnUpdatePkt(remoteId_, preAcceptUpdateBuf_, plen);
    }
}

// Apply an UPDATE delta to engine_ so the renderer shows authoritative
// state. Wire side 0 = us (engine P0), wire side 1 = server (engine P1).
void MonsterMeshTextBattle::clientAuthOnUpdatePkt(uint32_t fromId,
                                                  const uint8_t *buf, size_t len)
{
    if (len < BATTLELINK_HDR_SIZE + 6) {
        Serial.printf("[MMB] client UPDATE too short len=%u\n", (unsigned)len);
        return;
    }
    // Audit finding 5: only the bound server may drive our client state.
    // Reject a wrong-peer UPDATE before it can buffer or mutate the engine.
    if (fromId != remoteId_) {
        Serial.printf("[MMB] DROP UPDATE from=0x%08X reason=not_peer (peer=0x%08X)\n",
                      (unsigned)fromId, (unsigned)remoteId_);
        return;
    }
    const BattlePacket *pkt = (const BattlePacket *)buf;
    if (pkt->sessionId() != session_) {
        Serial.printf("[MMB] client UPDATE session mismatch pkt=0x%04X our=0x%04X\n",
                      (unsigned)pkt->sessionId(), (unsigned)session_);
        return;
    }
    // Audit finding 5f: validate the COMPLETE variable-length UPDATE payload
    // before mutating ANY battle state. The section decode further down
    // interleaves its bounds check (the take() lambda) with engine mutation,
    // so a packet that is truncated in a later section — or that carries a
    // bogus embedded count — could apply the earlier sections and leave the
    // client's engine half-updated. Walk the exact flag-ordered layout first,
    // mirroring the reader's own count clamping, and reject the whole packet
    // on any overflow before a single field is touched. Same up-front posture
    // as clientAuthOnFullStatePkt(). The len>=HDR+6 guard above already proved
    // payload[0..5] (turn+flags+hash) are present, so reading the flags here
    // is in-bounds. The UPDATE payload carries no species/move/party fields,
    // so this is a purely structural (size/count) validation.
    {
        const uint16_t updFlags =
            ((uint16_t)pkt->payload[1] << 8) | pkt->payload[2];
        const size_t payloadLen = len - BATTLELINK_HDR_SIZE;
        size_t v = 6;   // turn(1) + flags(2) + hash(3), already accounted for
        auto need = [&](size_t n) -> bool {
            if (v + n > payloadLen) return false;
            v += n;
            return true;
        };
        if (updFlags & TB_UPD_HP)     { if (!need(4)) return; }
        if (updFlags & TB_UPD_PP)     { if (!need(4)) return; }
        if (updFlags & TB_UPD_SWITCH) { if (!need(2)) return; }
        if (updFlags & TB_UPD_STATUS) { if (!need(2)) return; }
        if (updFlags & TB_UPD_RESULT) { if (!need(1)) return; }
        if (updFlags & TB_UPD_LOG) {
            if (!need(1)) return;
            const uint8_t numLines = pkt->payload[v - 1];
            for (uint8_t i = 0; i < numLines; ++i) {
                if (!need(1)) return;                 // per-line length byte
                const uint8_t llen = pkt->payload[v - 1];
                if (!need(llen)) return;              // line body
            }
        }
        if (updFlags & TB_UPD_BENCH) {
            if (!need(1)) return;
            uint8_t count = pkt->payload[v - 1];
            if (count > 6) count = 6;                 // reader clamps to 6
            if (!need((size_t)count * 7)) return;     // hp/status/4×pp per slot
            if (!need(6)) return;                     // active-mon boost stages
            if (!need(1)) return;
            uint8_t ecount = pkt->payload[v - 1];
            if (ecount > 6) ecount = 6;               // reader clamps to 6
            if (!need((size_t)ecount * 3)) return;    // enemy bench hp/status
        }
        if (updFlags & TB_UPD_FX) { if (!need(2 * 8 + 3)) return; }
    }
    // Engine not started yet — buffer this UPDATE for after Y is pressed.
    if (phase_ == Phase::WAIT_CHALLENGE_OVERLAY) {
        size_t copy = len < sizeof(preAcceptUpdateBuf_) ? len : sizeof(preAcceptUpdateBuf_);
        memcpy(preAcceptUpdateBuf_, buf, copy);
        preAcceptUpdateLen_ = copy;
        Serial.printf("[MMB] client UPDATE buffered (pre-accept) seq=%u\n",
                      (unsigned)pkt->seq);
        return;
    }
    Serial.printf("[MMB] client UPDATE rx seq=%u len=%u flags=0x%04X\n",
                  (unsigned)pkt->seq, (unsigned)len,
                  (unsigned)(((uint16_t)pkt->payload[1] << 8) | pkt->payload[2]));
    // First UPDATE proves the server got our ACCEPT — stop retransmitting it.
    awaitingFirstUpdate_   = false;
    awaitingUpdateSinceMs_ = 0;

    // Snapshot server-side mons' HP BEFORE applying this UPDATE so we can
    // detect any server mon that faints (= our kill) and credit XP / level
    // up the corresponding active mon on our side. The server's
    // resolveTurn only awards XP to side 0 (server's player); without
    // this mirror on the client, the client never levels up from a
    // PvP win.
    uint16_t preEnemyHp[6] = {};
    {
        const auto &ep = engine_.party(1);
        for (uint8_t i = 0; i < ep.count && i < 6; ++i) {
            preEnemyHp[i] = ep.mons[i].hp;
        }
    }

    // Idempotent dedupe: same seq already applied → ACK so server can
    // clear unackedUpdate_, but don't reapply state. If the user hasn't
    // picked an action yet (clientActionType_ == 0xFF), send a pure-ACK
    // sentinel (0xFE) so the server doesn't treat our re-emit as a real
    // engine submission. Without this gate, every server-side UPDATE
    // retransmit advanced the server's turn counter with garbage on the
    // client's side of the engine.
    uint8_t seq = pkt->seq;
    if (seq == lastAppliedUpdateSeq_ && lastAppliedUpdateSeq_ != 0) {
        if (clientActionType_ != 0xFF) {
            clientAuthSendActionV2(clientActionType_, clientActionIndex_);
        } else {
            clientAuthSendActionV2(0xFE /*pure-ACK*/, 0);
        }
        return;
    }

    size_t r = 0;
    uint8_t  turn   = pkt->payload[r++];
    clientTurn_ = turn;   // mirror so ACTION_V2 carries the right turn
    uint16_t flags  = ((uint16_t)pkt->payload[r] << 8) | pkt->payload[r + 1];
    r += 2;
    uint8_t  h0 = pkt->payload[r++];
    uint8_t  h1 = pkt->payload[r++];
    uint8_t  h2 = pkt->payload[r++];
    uint32_t serverHash = ((uint32_t)h0 << 16) |
                          ((uint32_t)h1 <<  8) |
                                     h2;

    auto take = [&](size_t n) -> const uint8_t * {
        if (r + n > len - BATTLELINK_HDR_SIZE) return nullptr;
        const uint8_t *p = pkt->payload + r;
        r += n;
        return p;
    };

    // Sections in flag-order (TbUpdateFlag bit order).
    if (flags & TB_UPD_HP) {
        const uint8_t *p = take(4); if (!p) return;
        uint16_t myHp     = ((uint16_t)p[0] << 8) | p[1];
        uint16_t enemyHp  = ((uint16_t)p[2] << 8) | p[3];
        auto &mp = engine_.party(0);
        auto &ep = engine_.party(1);
        if (mp.count) mp.mons[mp.active].hp = myHp;
        if (ep.count) ep.mons[ep.active].hp = enemyHp;
    }
    if (flags & TB_UPD_PP) {
        const uint8_t *p = take(4); if (!p) return;
        auto &mp = engine_.party(0);
        if (mp.count) for (uint8_t i = 0; i < 4; ++i) mp.mons[mp.active].pp[i] = p[i];
    }
    if (flags & TB_UPD_SWITCH) {
        const uint8_t *p = take(2); if (!p) return;
        if (p[0] < engine_.party(0).count) engine_.party(0).active = p[0];
        if (p[1] < engine_.party(1).count) engine_.party(1).active = p[1];
    }
    if (flags & TB_UPD_STATUS) {
        const uint8_t *p = take(2); if (!p) return;
        auto &mp = engine_.party(0);
        auto &ep = engine_.party(1);
        if (mp.count) mp.mons[mp.active].status = p[0];
        if (ep.count) ep.mons[ep.active].status = p[1];
    }
    bool finished = false;
    if (flags & TB_UPD_RESULT) {
        const uint8_t *p = take(1); if (!p) return;
        uint8_t result = p[0];
        if (result != TB_RESULT_ONGOING) {
            finished = true;
            switch (result) {
                case TB_RESULT_YOU_WIN:
                    appendLog("You won!");
                    playerWon_ = true;
                    break;
                case TB_RESULT_YOU_LOSE: appendLog("You blacked out…"); break;
                case TB_RESULT_DRAW:     appendLog("It's a draw."); break;
                case TB_RESULT_FLED:     appendLog("Got away safely!"); break;
                default: break;
            }
        }
    }
    if (flags & TB_UPD_LOG) {
        const uint8_t *p = take(1); if (!p) return;
        uint8_t numLines = *p;
        for (uint8_t i = 0; i < numLines; ++i) {
            const uint8_t *lp = take(1); if (!lp) break;
            uint8_t llen = *lp;
            const uint8_t *lb = take(llen); if (!lb) break;
            char line[LOG_WIDTH + 1] = {};
            uint8_t cp = llen < LOG_WIDTH ? llen : LOG_WIDTH;
            memcpy(line, lb, cp);
            line[cp] = '\0';
            appendLog(line);
        }
    }
    if (flags & TB_UPD_BENCH) {
        const uint8_t *cp = take(1); if (!cp) return;
        uint8_t count = *cp;
        if (count > 6) count = 6;
        auto &mp = engine_.party(0);
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t *p = take(7); if (!p) return;
            if (i < mp.count) {
                mp.mons[i].hp     = ((uint16_t)p[0] << 8) | p[1];
                mp.mons[i].status = p[2];
                mp.mons[i].pp[0]  = p[3];
                mp.mons[i].pp[1]  = p[4];
                mp.mons[i].pp[2]  = p[5];
                mp.mons[i].pp[3]  = p[6];
            }
        }
        const uint8_t *b = take(6); if (!b) return;
        if (mp.count) {
            auto &m = mp.mons[mp.active];
            m.atkBoost = (int8_t)b[0];
            m.defBoost = (int8_t)b[1];
            m.spdBoost = (int8_t)b[2];
            m.spcBoost = (int8_t)b[3];
            m.accBoost = (int8_t)b[4];
            m.evaBoost = (int8_t)b[5];
        }
        // Enemy bench: HP + status for all server mons (engine P1 from client).
        // Keeps benched/fainted mons in sync so the hash matches after a switch.
        const uint8_t *ecp = take(1); if (!ecp) return;
        uint8_t ecount = *ecp; if (ecount > 6) ecount = 6;
        auto &ep2 = engine_.party(1);
        for (uint8_t i = 0; i < ecount; ++i) {
            const uint8_t *p = take(3); if (!p) return;
            if (i < ep2.count) {
                ep2.mons[i].hp     = ((uint16_t)p[0] << 8) | p[1];
                ep2.mons[i].status = p[2];
            }
        }
    }
    if (flags & TB_UPD_FX) {
        // Wire 0 = us (engine P0); wire 1 = server (engine P1).
        // 8 bytes per side: sleepTurns, confuseTurns, substituteHp(2 BE),
        // monFlags, fieldFlags, reflectTurns, lightScreenTurns.
        for (uint8_t ws = 0; ws < 2; ++ws) {
            const uint8_t *p = take(8); if (!p) return;
            auto &party = engine_.party(ws);
            auto &m     = party.mons[party.active];
            m.sleepTurns      = p[0];
            m.confuseTurns    = p[1];
            m.substituteHp    = ((uint16_t)p[2] << 8) | p[3];
            uint8_t mf = p[4];
            m.mustRecharge = (mf & (1u << 0)) != 0;
            m.flinched     = (mf & (1u << 1)) != 0;
            m.thrashing    = (mf & (1u << 2)) != 0;
            m.rageActive   = (mf & (1u << 3)) != 0;
            m.transformed  = (mf & (1u << 4)) != 0;
            // Bit 5 (chargingSlot) is informational for the UI; engine
            // tracks the actual slot index, so we don't restore that here.
            uint8_t ff = p[5];
            party.reflect     = (ff & (1u << 0)) != 0;
            party.lightScreen = (ff & (1u << 1)) != 0;
            party.mist        = (ff & (1u << 2)) != 0;
            party.focused     = (ff & (1u << 3)) != 0;
            party.reflectTurns     = p[6];
            party.lightScreenTurns = p[7];
        }
        // Client-side disabled-move + trap counters.
        const uint8_t *dis = take(3); if (!dis) return;
        auto &mp2 = engine_.party(0);
        if (mp2.count) {
            auto &m = mp2.mons[mp2.active];
            m.disabledSlot  = dis[0];
            m.disabledTurns = dis[1];
            m.trapTurns     = dis[2];
        }
    }
    bool needSwitch = (flags & TB_UPD_NEED_PLAYER_SWITCH) != 0;

    // ── Mirror the server's XP / in-battle level-up bookkeeping ─────────
    // For every server-side mon that transitioned HP > 0 → 0 in this
    // UPDATE, the corresponding kill belongs to one of OUR active mons.
    // Apply the same Gen 1 formula the server uses in resolveTurn so
    // the client's party levels up in sync with what we'd see on a
    // local battle. Uses Gen1 base stats + level → baseYield, plus
    // the legacy participantMask_ for share splitting.
    {
        const auto &ep = engine_.party(1);
        for (uint8_t i = 0; i < ep.count && i < 6; ++i) {
            if (preEnemyHp[i] == 0)            continue;  // already dead
            if (ep.mons[i].hp != 0)            continue;  // still alive
            // Just fainted — credit XP.
            uint8_t pcount = (uint8_t)__builtin_popcount(participantMask_);
            if (pcount == 0) pcount = 1;
            uint32_t lvl = ep.mons[i].level ? ep.mons[i].level : 1;
            uint8_t  dex = ep.mons[i].species;
            const Gen1BaseStats &b =
                GEN1_BASE_STATS[dex < 152 ? dex : 0];
            uint32_t baseYield = (uint32_t)(b.hp + b.atk + b.def +
                                            b.spd + b.spc) / 3;
            if (baseYield < 20) baseYield = 20;
            // PvP counts as a trainer battle (1.5× multiplier → ×3/×2
            // to keep integer math).
            uint32_t numerMult = 3u;
            uint32_t xpThisFaint =
                (baseYield * lvl * numerMult) /
                (uint32_t)(14u * pcount);
            if (xpThisFaint == 0) xpThisFaint = 1;
            pendingXpDrops_++;
            char line[40];
            snprintf(line, sizeof(line), "Each earned %u XP!",
                     (unsigned)xpThisFaint);
            appendLog(line);
            const auto &mp = engine_.party(0);
            for (uint8_t s = 0; s < mp.count && s < 6; ++s) {
                if (!(participantMask_ & (1u << s))) continue;
                if (mp.mons[s].hp == 0) continue;
                pendingXpPerSlot_[s] += xpThisFaint;
                slotXpAccum_[s]      += xpThisFaint;
                while (true) {
                    uint8_t curLvl = engine_.party(0).mons[s].level;
                    if (curLvl >= 100) break;
                    // Growth-rate-correct in-battle level threshold. Gen-1
                    // species (dex 1-151) use their true curve so Slow mons
                    // (Mewtwo, Dragonite, Gyarados...) stop over-leveling on
                    // screen mid-fight; Gen-2/3 (dex>151, no growth table) keep
                    // the medium-fast delta exactly as before. Matches the
                    // authoritative SAV writeback (creditBattleXpPerSlot).
                    uint16_t dex = engine_.party(0).mons[s].species;
                    uint32_t threshold =
                        (dex >= 1 && dex <= 151)
                            ? (expForLevel((uint8_t)dex, curLvl + 1) -
                               expForLevel((uint8_t)dex, curLvl))
                            : (3u * (uint32_t)curLvl * curLvl +
                               3u * (uint32_t)curLvl + 1u);
                    if (slotXpAccum_[s] < threshold) break;
                    slotXpAccum_[s] -= threshold;
                    inBattleLevelUp(s);
                }
            }
            // Reset participant mask to whoever's currently active — only
            // mons present for the NEXT enemy share that one's XP.
            participantMask_ = (uint8_t)(1u << engine_.party(0).active);
        }
    }
    // Track new-active on our side too so participant mask grows when
    // we switch in mid-battle.
    {
        uint8_t curActive = engine_.party(0).active;
        if (curActive != lastPlayerActive_) {
            participantMask_ |= (uint8_t)(1u << curActive);
            lastPlayerActive_ = curActive;
        }
    }

    // After applying, re-hash the canonical buffer and compare with the
    // server's inline hash. Mismatch → STATE_REQUEST (P4). Match → ACK.
    uint8_t board[80];
    size_t blen = packClientStateFromEngine(board);
    uint32_t myHash = tbBoardHash24(board, blen);
    if (myHash != serverHash) {
        clientNeedsFullState_ = true;
        clientAuthSendStateRequest();
        Serial.printf("[MMB] client UPDATE hash mismatch turn=%u "
                      "server=0x%06X mine=0x%06X blen=%u\n",
                      turn, (unsigned)serverHash, (unsigned)myHash,
                      (unsigned)blen);
        // Dump the byte-by-byte canonical buffer so we can compare what
        // the client computed against the server's payload (which we
        // can reconstruct from the UPDATE wire). The hash inputs MUST
        // match byte-for-byte; whatever differs points at the bug.
        Serial.printf("[MMB] client board: ");
        for (size_t i = 0; i < blen; ++i) Serial.printf("%02X ", board[i]);
        Serial.printf("\n");
        // Don't advance lastAppliedUpdateSeq_ — wait for FULL_STATE.
        lastRecvMs_ = millis();
        dirty_ = true;
        return;
    }

    lastAppliedUpdateSeq_ = seq;
    lastRecvMs_ = millis();
    dirty_ = true;

    if (finished) {
        // Mesh PvP client: one-time result-based XP bonus for the local party
        // (mirrors the server's award so both sides earn). Feeds the existing
        // pendingXpPerSlot_ → SD-sav writeback path.
        awardMeshResultXp();
        phase_ = Phase::FINISHED;
        // Still ACK so server clears unackedUpdate_ and stops retransmitting.
        clientAuthSendActionV2(0xFE /*null action*/, 0);
        return;
    }
    if (needSwitch) {
        phase_ = Phase::WAIT_SWITCH;
        switchCursor_ = engine_.party(0).active;
        clientActionType_ = 0xFF;  // clear stale action so dedup can't replay it
        Serial.printf("[MMB] client UPDATE applied seq=%u → WAIT_SWITCH\n",
                      (unsigned)seq);
        return;
    }
    // Normal flow: server has resolved the turn and is now waiting for our
    // next action.
    phase_ = Phase::WAIT_ACTION;
    clientActionType_ = 0xFF;  // clear stale action so dedup can't replay it
    Serial.printf("[MMB] client UPDATE applied seq=%u → WAIT_ACTION turn=%u\n",
                  (unsigned)seq, (unsigned)clientTurn_);
}

void MonsterMeshTextBattle::clientAuthOnFullStatePkt(uint32_t fromId,
                                                     const uint8_t *buf, size_t len)
{
    if (len < BATTLELINK_HDR_SIZE + 5) return;
    // Audit finding 5: only the bound server may resynchronise our state.
    if (fromId != remoteId_) {
        Serial.printf("[MMB] DROP FULL_STATE from=0x%08X reason=not_peer (peer=0x%08X)\n",
                      (unsigned)fromId, (unsigned)remoteId_);
        return;
    }
    const BattlePacket *pkt = (const BattlePacket *)buf;
    if (pkt->sessionId() != session_) return;

    // Audit finding 5: validate the COMPLETE variable-length payload before
    // mutating EITHER party. Interleaving validation with mutation (as the
    // old body did) can leave side 0 partially resynchronised when the packet
    // is truncated at side 1. Walk the exact layout first; only a fully
    // well-formed FULL_STATE (turn(2)+result(1), then per side active/count +
    // count×[hp:2,status:1], then the 4-byte PP tail) is applied.
    {
        const size_t payloadLen = len - BATTLELINK_HDR_SIZE;
        size_t v = 3;   // turn(2) + result(1)
        for (uint8_t ws = 0; ws < 2; ++ws) {
            if (v + 2 > payloadLen) return;
            const uint8_t active = pkt->payload[v];
            const uint8_t count  = pkt->payload[v + 1];
            if (count == 0 || count > 6 || active >= count ||
                count != engine_.party(ws).count) return;   // cross-check engine
            v += 2;
            if ((size_t)count * 3 > payloadLen - v) return;
            v += (size_t)count * 3;
        }
        if (payloadLen - v != 4) return;   // exactly the PP tail, no more/less
    }
    size_t r = 0;
    uint16_t turn = ((uint16_t)pkt->payload[r] << 8) | pkt->payload[r + 1]; r += 2;
    uint8_t  result = pkt->payload[r++];
    (void)turn;

    // Per-side decode: [active][count] then count×[hp:2][status]
    for (uint8_t ws = 0; ws < 2; ++ws) {
        if (r + 2 > len - BATTLELINK_HDR_SIZE) return;
        uint8_t active = pkt->payload[r++];
        uint8_t count  = pkt->payload[r++];
        if (count > 6) count = 6;
        // Wire side 0 = us (engine P0); side 1 = server (engine P1).
        auto &party = engine_.party(ws);
        if (active < count) party.active = active;
        // Note: party.count is set when engine_.start() ran on ACCEPT; we
        // don't change it here — counts must match. Just patch hp/status.
        for (uint8_t i = 0; i < count; ++i) {
            if (r + 3 > len - BATTLELINK_HDR_SIZE) return;
            uint16_t hp = ((uint16_t)pkt->payload[r] << 8) | pkt->payload[r + 1];
            uint8_t  st = pkt->payload[r + 2];
            r += 3;
            if (i < party.count) {
                party.mons[i].hp     = hp;
                party.mons[i].status = st;
            }
        }
    }
    // PP for our active mon.
    if (r + 4 <= len - BATTLELINK_HDR_SIZE) {
        auto &mp = engine_.party(0);
        if (mp.count) for (uint8_t i = 0; i < 4; ++i) mp.mons[mp.active].pp[i] = pkt->payload[r + i];
    }

    clientNeedsFullState_  = false;
    awaitingFirstUpdate_   = false;  // FULL_STATE is as good as the first UPDATE
    awaitingUpdateSinceMs_ = 0;
    lastAppliedUpdateSeq_ = pkt->seq;
    lastRecvMs_ = millis();
    dirty_ = true;
    // Discard any pending action from the diverged state so the retransmit
    // timer doesn't replay it for the wrong turn after resync.
    clientActionType_ = 0xFF;
    appendLog("State reconverged.");

    if (result == TB_RESULT_ONGOING) {
        phase_ = Phase::WAIT_ACTION;
    } else {
        phase_ = Phase::FINISHED;
    }
    Serial.printf("[MMB] client FULL_STATE applied seq=%u\n", (unsigned)pkt->seq);
}

void MonsterMeshTextBattle::clientAuthSendActionV2(uint8_t actionType, uint8_t index)
{
    uint8_t buf[BATTLELINK_MAX_PKT];
    memset(buf, 0, sizeof(buf));
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_ACTION_V2;
    pkt->setSessionId(session_);
    pkt->seq = 0;
    // Use clientTurn_ (mirrored from each UPDATE) — engine_.turn() never
    // advances on the client since we don't call executeTurn.
    tbPackAction(pkt->payload, clientTurn_, actionType, index,
                 lastAppliedUpdateSeq_);
    transport_.queueSend(buf, BATTLELINK_HDR_SIZE + TB_ACTION_BYTES);
    if (actionType != 0xFE) {  // 0xFE = pure-ACK sentinel
        clientActionType_       = actionType;
        clientActionIndex_      = index;
        clientActionTurn_       = clientTurn_;
        lastClientActionSendMs_ = millis();
    }
}

void MonsterMeshTextBattle::clientAuthSendStateRequest()
{
    uint8_t buf[BATTLELINK_HDR_SIZE + TB_STATE_REQUEST_BYTES];
    memset(buf, 0, sizeof(buf));
    BattlePacket *pkt = (BattlePacket *)buf;
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_STATE_REQUEST;
    pkt->setSessionId(session_);
    pkt->seq = 0;
    tbPackStateRequest(pkt->payload, lastAppliedUpdateSeq_);
    transport_.queueSend(buf, sizeof(buf));
}

void MonsterMeshTextBattle::clientAuthRetransmit(uint32_t nowMs)
{
    // P2.37: ACCEPT retransmit cadence is intentionally faster AND
    // staggered relative to the server's CHALLENGE cadence. With both
    // sides flood-broadcasting on 8 s intervals, the LoRa air would
    // saturate and both radios "busyRx"-throttled each other's actual
    // retransmits — neither side ever heard the other's packet.
    // Client retries at ~5 s + jitter so the schedules don't lock.
    //
    // After 15 s the server has almost certainly received our ACCEPT
    // (we'd have gotten a CHALLENGE re-tx otherwise). At that point
    // the server is retransmitting the UPDATE every 15 s, but the
    // re-transmitted ACCEPT is being dropped by the server (not_awaiting).
    // Switch to STATE_REQUEST so the server sends a FULL_STATE, which
    // the client can process to exit awaitingFirstUpdate_.
    if (awaitingFirstUpdate_) {
        uint32_t elapsedMs = nowMs - awaitingUpdateSinceMs_;
        if (elapsedMs >= 15000u) {
            // Server has our ACCEPT — pull state instead of re-sending ACCEPT.
            if ((nowMs - lastAcceptSendMs_) >= 12000u) {
                Serial.printf("[MMB] awaitingFirstUpdate >15s — sending STATE_REQUEST\n");
                clientAuthSendStateRequest();
                lastAcceptSendMs_ = nowMs;
            }
        } else if ((nowMs - lastAcceptSendMs_) >= 5000u + (uint32_t)(esp_random() & 0x7FF)) {
            clientAuthSendAccept(true);
        }
    }
    // ACTION_V2 retransmit while waiting for next UPDATE.
    // Blocked while clientNeedsFullState_ — retransmitting with an advanced
    // clientTurn_ while the state is diverged causes the server to advance
    // another turn from the wrong base, producing a cascade of mismatches.
    // clientActionTurn_ guard: after each UPDATE clientTurn_ advances; only
    // retransmit if the stored action was for THIS turn (not a stale action
    // from the previous turn that would advance the server without the user
    // picking a fresh move).
    if (!clientNeedsFullState_ && clientActionType_ != 0xFF &&
        clientActionTurn_ == clientTurn_ &&
        (nowMs - lastClientActionSendMs_) >= TB_ACTION_RESEND_MS) {
        clientAuthSendActionV2(clientActionType_, clientActionIndex_);
    }
}
