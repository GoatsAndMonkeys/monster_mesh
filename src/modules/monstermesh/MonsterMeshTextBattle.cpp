// SPDX-License-Identifier: MIT
// See MonsterMeshTextBattle.h for description.

#include "MonsterMeshTextBattle.h"
#include "showdown_gen1_moves.h"
#include "showdown_gen1_basestats.h"
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
    dirty_ = true;
}

// ── Networked initiator ─────────────────────────────────────────────────────

void MonsterMeshTextBattle::startNetworkedAsInitiator(uint32_t remoteId,
                                                     const Gen1Party &myParty)
{
    mode_     = Mode::NETWORKED;
    phase_    = Phase::WAIT_REMOTE;
    remoteId_ = remoteId;
    session_  = (uint16_t)(millis() & 0xFFFF);
    cursor_   = 0; switchCursor_ = 0;
    pendingRemoteAction_ = false;
    logFill_ = logHead_ = 0; scrollPending_ = 0;

    // Pick a deterministic seed; broadcast it so the receiver matches us.
    uint32_t rngSeed = (uint32_t)(esp_random() ^ remoteId ^ session_);

    // For now, both sides use the same party for the receiver — the *real*
    // protocol broadcasts both parties via TEXT_BATTLE_PARTY chunks. As an
    // intermediate step we mirror our party on the opponent side so the engine
    // can run; full party exchange is a follow-up TODO.
    Gen1Party fakeOpp = myParty;
    engine_.start(myParty, fakeOpp, rngSeed);

    sendStart(rngSeed, myParty);
    appendLog("Battle started!");
    appendLog("Waiting for opponent…");
    lastRecvMs_ = millis();
    handleFaints();
    if (engine_.result() == Gen1BattleEngine::Result::ONGOING)
        phase_ = Phase::WAIT_ACTION;
    dirty_ = true;
}

void MonsterMeshTextBattle::startNetworkedAsReceiver(uint32_t remoteId,
                                                     const Gen1Party &myParty,
                                                     uint32_t rngSeed)
{
    mode_     = Mode::NETWORKED;
    phase_    = Phase::WAIT_ACTION;
    remoteId_ = remoteId;
    cursor_   = 0; switchCursor_ = 0;
    logFill_ = logHead_ = 0; scrollPending_ = 0;

    Gen1Party fakeOpp = myParty;
    engine_.start(myParty, fakeOpp, rngSeed);
    appendLog("Battle started!");
    lastRecvMs_ = millis();
    dirty_ = true;
}

void MonsterMeshTextBattle::startLocal(const Gen1Party &myParty,
                                       const Gen1Party &cpuParty,
                                       const char *ourTag,
                                       const char *theirTag)
{
    headerOverride_[0] = '\0';

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

void MonsterMeshTextBattle::nextOpponent(const Gen1Party &cpu, const char *theirTag)
{
    // Caller has just observed isActive()==false (because exit() ran on the
    // result screen). Bring the battle back to life — mode and phase reset
    // so the engine and tick loop both consider us in-battle again.
    mode_ = Mode::LOCAL_ROGUELIKE;
    engine_.replaceOpponent(cpu);
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
    dirty_ = true;
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

void MonsterMeshTextBattle::sendAction(uint8_t actionType, uint8_t index)
{
    uint8_t buf[BATTLELINK_MAX_PKT];
    BattlePacket *pkt = (BattlePacket *)buf;
    memset(buf, 0, sizeof(buf));
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_ACTION;
    pkt->setSessionId(session_);
    pkt->seq  = engine_.turn() & 0xFF;
    uint16_t turn = engine_.turn();
    pkt->payload[0] = (turn >> 8) & 0xFF;
    pkt->payload[1] =  turn       & 0xFF;
    pkt->payload[2] = actionType;
    pkt->payload[3] = index;
    transport_.queueSend(buf, BATTLELINK_HDR_SIZE + 4);
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
    if (pkt->sessionId() != session_) return false;

    switch (t) {
        case PktType::TEXT_BATTLE_ACTION: {
            if (len < BATTLELINK_HDR_SIZE + 4) return true;
            uint16_t turn = ((uint16_t)pkt->payload[0] << 8) | pkt->payload[1];
            if (turn != engine_.turn()) return true;  // stale or future packet
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
            uint8_t mine[8]; engine_.hashState(mine);
            if (memcmp(mine, pkt->payload + 2, 8) != 0) {
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
                    uint32_t threshold =
                        3u * (uint32_t)curLvl * curLvl +
                        3u * (uint32_t)curLvl + 1u;
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
            case Gen1BattleEngine::Result::P1_WIN: appendLog("You won!");      break;
            case Gen1BattleEngine::Result::P2_WIN: appendLog("You blacked out…"); break;
            case Gen1BattleEngine::Result::DRAW:   appendLog("It's a draw.");  break;
            default: break;
        }
        phase_ = Phase::FINISHED;
        return;
    }
    pendingRemoteAction_ = false;
    if (mode_ == Mode::NETWORKED &&
        engine_.turn() % TEXT_BATTLE_HASH_INTERVAL == 0) {
        sendHash();
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

    char line[40];
    snprintf(line, sizeof(line), "%.10s grew to L%u!",
             m.nickname[0] ? m.nickname : "?", (unsigned)newLvl);
    appendLog(line);
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
        // Re-broadcast our pending action periodically — LoRa is lossy.
        if (phase_ == Phase::WAIT_REMOTE && lastSentAction_ != 0xFF &&
            (nowMs - lastSendMs_ >= RESEND_INTERVAL_MS)) {
            sendAction(lastSentAction_, lastSentIndex_);
        }
        // Timeout: opponent hasn't sent anything in a while.
        if ((nowMs - lastRecvMs_) > REMOTE_TIMEOUT_MS &&
            phase_ != Phase::FINISHED) {
            appendLog("Opponent timed out.");
            engine_.forfeit(1, engineLogCb, this);
            phase_ = Phase::FINISHED;
        }
        // If both sides have submitted, resolve.
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
        // Any key dismisses the result screen.
        exit();
        return;
    }

    // ESC = forfeit/exit (Nintendo Start menu quit).
    if (c == 27 /* ESC */) {
        if (mode_ == Mode::NETWORKED) sendForfeit();
        engine_.forfeit(0, engineLogCb, this);
        phase_ = Phase::FINISHED;
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
        if (mode_ == Mode::NETWORKED) {
            sendForfeit(); engine_.forfeit(0, engineLogCb, this); phase_ = Phase::FINISHED;
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
                engine_.submitAction(0, 1 /*SWITCH*/, switchCursor_);
                if (mode_ == Mode::NETWORKED) sendAction(1, switchCursor_);
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
        return;
    }

    if (keyAccept) {
        const auto &mon = engine_.party(0).mons[engine_.party(0).active];
        // Out of PP across the whole moveset → Gen 1 Struggle (id sentinel
        // 0xFE on the wire). Without this, K-accept silently no-oped and the
        // battle looked frozen after a long gauntlet drained every move.
        bool anyPp = false;
        for (uint8_t i = 0; i < 4; ++i) {
            if (mon.moves[i] != 0 && mon.pp[i] > 0) { anyPp = true; break; }
        }
        if (!anyPp) {
            engine_.submitAction(0, 0 /*USE_MOVE*/, 0xFE /*STRUGGLE*/);
            if (mode_ == Mode::NETWORKED) sendAction(0, 0xFE);
            phase_ = Phase::WAIT_REMOTE;
            return;
        }
        if (mon.moves[cursor_] == 0 || mon.pp[cursor_] == 0) return;
        engine_.submitAction(0, 0 /*USE_MOVE*/, cursor_);
        if (mode_ == Mode::NETWORKED) sendAction(0, cursor_);
        phase_ = Phase::WAIT_REMOTE;
        return;
    }

    // P = switch (Pokemon menu — X is reserved for exit).
    if (c == 'p' || c == 'P') {
        switchCursor_ = engine_.party(0).active;
        phase_ = Phase::WAIT_SWITCH;
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
    // Status badge.
    const char *st = nullptr;
    if      (m.status & Gen1BattleEngine::ST_SLP) st = "SLP";
    else if (m.status & Gen1BattleEngine::ST_PSN) st = "PSN";
    else if (m.status & Gen1BattleEngine::ST_BRN) st = "BRN";
    else if (m.status & Gen1BattleEngine::ST_PAR) st = "PAR";
    else if (m.status & Gen1BattleEngine::ST_FRZ) st = "FRZ";
    if (st) {
        g->setCursor(SCREEN_W - 38, y);
        g->setTextColor(rgb565(0xC03020), BG);
        g->print(st);
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
        bool usable   = mon.pp[i] > 0;

        // Highlight the selected slot with a DARK background bar.
        if (selected) g->fillRect(cx - 2, cy - 1, colW - 8, rowH, DARK);
        uint16_t fg = !usable ? DIM : (selected ? FG : DIM);
        uint16_t bg = selected ? DARK : BG;

        // 2x2 grid layout: WASD steers the cursor, K accepts. The number
        // labels remain as a quick-jump fallback.
        static const char *kLabel[4] = { "1.", "2.", "3.", "4." };
        g->setTextColor(fg, bg);
        g->setCursor(cx, cy);
        if (mv) g->printf("%s %-9.9s %u/%u",
                          kLabel[i], mv->name, mon.pp[i], mv->pp);
        else    g->printf("%s ---",  kLabel[i]);
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
        g->printf("LoRa Battle  T%u", engine_.turn());
    } else {
        g->printf("Roguelike  T%u",  engine_.turn());
    }
}

void MonsterMeshTextBattle::render(lgfx::LGFX_Device *g)
{
    if (!g || mode_ == Mode::OFF) return;
    g->fillScreen(BG);
    drawHeader(g);
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
        g->print(mode_ == Mode::NETWORKED ? "Waiting for opponent…" : "…");
    } else if (phase_ == Phase::FINISHED) {
        g->setTextColor(FG, BG);
        g->setCursor(8, SCREEN_H - 24);
        g->print("Press any key to exit.");
    }
    dirty_ = false;
}
