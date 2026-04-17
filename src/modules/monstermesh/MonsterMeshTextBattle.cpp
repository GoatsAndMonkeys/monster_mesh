// SPDX-License-Identifier: MIT
// See MonsterMeshTextBattle.h for description.

#include "MonsterMeshTextBattle.h"
#include <string.h>
#include <stdio.h>

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
    scrollPending_++;
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
                                       const Gen1Party &cpuParty)
{
    mode_     = Mode::LOCAL_ROGUELIKE;
    phase_    = Phase::WAIT_ACTION;
    remoteId_ = 0;
    cursor_   = 0; switchCursor_ = 0;
    logFill_ = logHead_ = 0; scrollPending_ = 0;

    uint32_t rngSeed = (uint32_t)(millis() ^ esp_random());
    engine_.start(myParty, cpuParty, rngSeed);
    appendLog("A wild battle begins!");
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
    engine_.executeTurn(engineLogCb, this);
    handleFaints();
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
    cursor_ = 0;
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

    // Reveal queued log lines slowly so the player can read.
    if (scrollPending_ && (nowMs - scrollMs_ >= SCROLL_INTERVAL_MS)) {
        scrollMs_ = nowMs;
        scrollPending_--;
        dirty_ = true;
    }

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

    if (c == 27 /* ESC */ || c == 'f' || c == 'F') {
        if (mode_ == Mode::NETWORKED) sendForfeit();
        engine_.forfeit(0, engineLogCb, this);
        phase_ = Phase::FINISHED;
        return;
    }

    if (phase_ == Phase::WAIT_SWITCH) {
        const auto &p = engine_.party(0);
        if (c == 'w' || c == 'W' || c == 0xB5 /* up */)
            switchCursor_ = (switchCursor_ + p.count - 1) % p.count;
        else if (c == 's' || c == 'S' || c == 0xB6 /* down */)
            switchCursor_ = (switchCursor_ + 1) % p.count;
        else if (c == '\n' || c == '\r' || c == ' ') {
            if (p.mons[switchCursor_].hp > 0 && switchCursor_ != p.active) {
                engine_.submitAction(0, 1 /*SWITCH*/, switchCursor_);
                if (mode_ == Mode::NETWORKED) sendAction(1, switchCursor_);
                phase_ = (mode_ == Mode::NETWORKED) ? Phase::WAIT_REMOTE
                                                     : Phase::WAIT_REMOTE;
            }
        } else if (c == 27) {
            phase_ = Phase::WAIT_ACTION;
        }
        return;
    }

    if (phase_ != Phase::WAIT_ACTION) return;

    if (c >= '1' && c <= '4') {
        uint8_t slot = c - '1';
        const auto &mon = engine_.party(0).mons[engine_.party(0).active];
        if (mon.moves[slot] == 0 || mon.pp[slot] == 0) return;
        engine_.submitAction(0, 0 /*USE_MOVE*/, slot);
        if (mode_ == Mode::NETWORKED) sendAction(0, slot);
        phase_ = Phase::WAIT_REMOTE;
    } else if (c == 's' || c == 'S') {
        switchCursor_ = engine_.party(0).active;
        phase_ = Phase::WAIT_SWITCH;
    }
}

// ── Render ──────────────────────────────────────────────────────────────────

static void drawHpBar(lgfx::LGFX_Device *g, int x, int y, int w, int h,
                       uint16_t hp, uint16_t maxHp)
{
    int filled = maxHp > 0 ? (w - 2) * hp / maxHp : 0;
    g->drawRect(x, y, w, h, 0xFFFF);
    g->fillRect(x + 1, y + 1, w - 2, h - 2, 0x0000);
    uint16_t color = (hp * 4 > maxHp) ? 0x07E0       // green
                   : (hp * 4 > maxHp / 2 * 2) ? 0xFFE0  // yellow
                   : 0xF800;                         // red
    g->fillRect(x + 1, y + 1, filled, h - 2, color);
}

void MonsterMeshTextBattle::drawHpPanel(lgfx::LGFX_Device *g, uint8_t side, int y)
{
    const auto &p = engine_.party(side);
    const auto &m = p.mons[p.active];
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "%s  L%u  %u/%u",
             m.nickname[0] ? m.nickname : "?",
             (unsigned)m.level, (unsigned)m.hp, (unsigned)m.maxHp);
    g->setTextColor(0xFFFF, 0x0000);
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
        g->setTextColor(0xF800, 0x0000);
        g->print(st);
    }
}

void MonsterMeshTextBattle::drawMoveMenu(lgfx::LGFX_Device *g)
{
    const auto &mon = engine_.party(0).mons[engine_.party(0).active];
    int y = SCREEN_H - 60;
    g->setTextColor(0xFFFF, 0x0000);
    g->setCursor(8, y); g->print("Move:  1     2     3     4");
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *mv = gen1Move(mon.moves[i]);
        int x = 50 + i * 64;
        g->setCursor(x, y + 14);
        g->setTextColor(mon.pp[i] > 0 ? 0xFFFF : 0x7BEF, 0x0000);
        if (mv) g->printf("%-7.7s", mv->name); else g->print("---");
        g->setCursor(x, y + 26);
        if (mv) g->printf("%u/%u", mon.pp[i], mv->pp);
    }
    g->setCursor(8, y + 42);
    g->setTextColor(0x7BEF, 0x0000);
    g->print("[S] switch  [F] forfeit");
}

void MonsterMeshTextBattle::drawSwitchMenu(lgfx::LGFX_Device *g)
{
    const auto &p = engine_.party(0);
    int y = SCREEN_H - 90;
    g->setTextColor(0xFFFF, 0x0000);
    g->setCursor(8, y); g->print("Switch to:");
    for (int i = 0; i < p.count; ++i) {
        int row = y + 12 + i * 11;
        if (i == switchCursor_) {
            g->fillRect(4, row - 1, SCREEN_W - 8, 11, 0x4208);
        }
        const auto &m = p.mons[i];
        g->setTextColor(m.hp == 0 ? 0x7BEF : 0xFFFF, i == switchCursor_ ? 0x4208 : 0x0000);
        g->setCursor(8, row);
        g->printf("%c %-10.10s L%u  %u/%u",
                  i == p.active ? '*' : ' ',
                  m.nickname[0] ? m.nickname : "?",
                  (unsigned)m.level, (unsigned)m.hp, (unsigned)m.maxHp);
    }
}

void MonsterMeshTextBattle::drawLog(lgfx::LGFX_Device *g)
{
    int y = 80;
    g->setTextColor(0xFFFF, 0x0000);
    // Show only the lines that have been "revealed" by the scroll throttle.
    uint8_t shown = (logFill_ > scrollPending_) ? logFill_ - scrollPending_ : 0;
    for (uint8_t i = 0; i < shown; ++i) {
        int idx = (logHead_ + LOG_LINES - shown + i) % LOG_LINES;
        g->setCursor(8, y + i * 12);
        g->print(log_[idx]);
    }
}

void MonsterMeshTextBattle::drawHeader(lgfx::LGFX_Device *g)
{
    g->setTextColor(0x07FF, 0x0000);
    g->setCursor(8, 4);
    if (mode_ == Mode::NETWORKED) g->printf("LoRa Battle  T%u", engine_.turn());
    else                          g->printf("Roguelike  T%u",  engine_.turn());
}

void MonsterMeshTextBattle::render(lgfx::LGFX_Device *g)
{
    if (!g || mode_ == Mode::OFF) return;
    g->fillScreen(0x0000);
    drawHeader(g);
    drawHpPanel(g, 1, 18);   // opponent on top
    drawHpPanel(g, 0, 50);   // us below
    drawLog(g);
    if (phase_ == Phase::WAIT_SWITCH)      drawSwitchMenu(g);
    else if (phase_ == Phase::WAIT_ACTION) drawMoveMenu(g);
    else if (phase_ == Phase::WAIT_REMOTE) {
        g->setTextColor(0xFFE0, 0x0000);
        g->setCursor(8, SCREEN_H - 24);
        g->print(mode_ == Mode::NETWORKED ? "Waiting for opponent…" : "…");
    } else if (phase_ == Phase::FINISHED) {
        g->setTextColor(0xFFE0, 0x0000);
        g->setCursor(8, SCREEN_H - 24);
        g->print("Press any key to exit.");
    }
    dirty_ = false;
}
