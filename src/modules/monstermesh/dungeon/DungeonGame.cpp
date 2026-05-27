#include "DungeonGame.h"
#include <string.h>
#include <stdio.h>

DungeonGame::DungeonGame(MeshtasticTransport &transport)
    : transport_(transport) {
    memset(&run_, 0, sizeof(run_));
    run_.phase = RunPhase::Lobby;
}

void DungeonGame::begin() {
    mtx_ = xSemaphoreCreateMutex();
    LOG_INFO("[DUNGEON] ready  nodeId=0x%08X\n", (unsigned)myId());
}

// ── Core 0: incoming packet dispatch ─────────────────────────────────────────

void DungeonGame::handlePacket(const uint8_t *buf, size_t len) {
    if (len < BATTLELINK_HDR_SIZE) return;
    const BattlePacket &pkt = *reinterpret_cast<const BattlePacket *>(buf);
    uint8_t payloadLen = (uint8_t)(len - BATTLELINK_HDR_SIZE);
    auto type = static_cast<PktType>(pkt.type);

    switch (type) {
        case PktType::DUNGEON_BEACON:   handleBeacon(pkt, payloadLen); break;
        case PktType::DUNGEON_JOIN:     handleJoin(pkt, payloadLen);   break;
        case PktType::DUNGEON_JOIN_ACK: handleJoinAck(pkt, payloadLen);break;
        case PktType::DUNGEON_CMD:      handleCmd(pkt, payloadLen);    break;
        case PktType::DUNGEON_STATE:    handleState(pkt, payloadLen);  break;
        case PktType::DUNGEON_MSG:      handleMsg(pkt, payloadLen);    break;
        case PktType::DUNGEON_PROMPT:   handlePrompt(pkt, payloadLen); break;
        default: break;
    }
}

// ── Core 1: periodic tick ─────────────────────────────────────────────────────

void DungeonGame::tick(uint32_t now) {
    if (!active_) return;
    xSemaphoreTake(mtx_, portMAX_DELAY);
    bool doBeacon = (now - lastBeaconMs_ >= BEACON_INTERVAL_MS);
    xSemaphoreGive(mtx_);

    if (doBeacon) {
        sendBeacon();
        xSemaphoreTake(mtx_, portMAX_DELAY);
        lastBeaconMs_ = now;
        xSemaphoreGive(mtx_);
    }
}

// ── Core 1: local command from CommandBar ─────────────────────────────────────

void DungeonGame::handleLocalCommand(const char *verb, const char *arg) {
    // Normalize verb to lowercase
    char v[16] = {};
    for (int i = 0; verb[i] && i < 15; i++) v[i] = tolower((unsigned char)verb[i]);

    if (strcmp(v, "host") == 0) {
        xSemaphoreTake(mtx_, portMAX_DELAY);
        active_  = true;
        isHost_  = true;
        hostId_  = 0;
        run_.partySize = 1;
        run_.party[0].nodeId = myId();
        run_.party[0].activeSlot = 0;
        for (uint8_t s = 0; s < 6; s++) {
            run_.party[0].slotHp[s]     = 100;
            run_.party[0].slotFainted[s]= false;
        }
        run_.phase = RunPhase::Lobby;
        xSemaphoreGive(mtx_);
        logMsg("[D] You are now the dungeon host.");
        logMsg("[D] Others: type 'dungeon join'");
        sendBeacon();
        return;
    }

    if (strcmp(v, "join") == 0) {
        xSemaphoreTake(mtx_, portMAX_DELAY);
        active_ = true;
        isHost_ = false;
        xSemaphoreGive(mtx_);
        logMsg("[D] Looking for host...");
        sendBeacon();
        sendJoinRequest();
        return;
    }

    if (!active_) return;

    DungeonVerb dv = DungeonVerb::PARTY;
    if      (strcmp(v, "party")  == 0) dv = DungeonVerb::PARTY;
    else if (strcmp(v, "attack") == 0) dv = DungeonVerb::ATTACK;
    else if (strcmp(v, "cast")   == 0) dv = DungeonVerb::CAST;
    else if (strcmp(v, "switch") == 0) dv = DungeonVerb::SWITCH;
    else if (strcmp(v, "item")   == 0) dv = DungeonVerb::ITEM;
    else if (strcmp(v, "answer") == 0) dv = DungeonVerb::ANSWER;
    else if (strcmp(v, "wordle") == 0) dv = DungeonVerb::WORDLE;
    else if (strcmp(v, "hack")   == 0) dv = DungeonVerb::HACK;
    else if (strcmp(v, "flee")   == 0) dv = DungeonVerb::FLEE;
    else if (strcmp(v, "rest")   == 0) dv = DungeonVerb::REST;
    else if (strcmp(v, "start")  == 0) dv = DungeonVerb::HOST_START;
    else { logMsg("[D] Unknown dungeon command."); return; }

    if (isHost_) {
        // Process locally
        hostProcessCmd(myId(), dv, arg);
    } else {
        // Forward to host over radio
        forwardCmd(dv, arg);
    }
}

// ── Host: command processor ───────────────────────────────────────────────────

void DungeonGame::hostProcessCmd(uint32_t fromId, DungeonVerb verb, const char *arg) {
    xSemaphoreTake(mtx_, portMAX_DELAY);

    switch (verb) {
        case DungeonVerb::HOST_START:
            xSemaphoreGive(mtx_);
            hostStartRun();
            return;

        case DungeonVerb::PARTY: {
            char buf[64];
            snprintf(buf, sizeof(buf), "[Party %u/5 | Floor %u | Phase %u]",
                     run_.partySize, run_.currentFloor.depth, (uint8_t)run_.phase);
            xSemaphoreGive(mtx_);
            sendMsg(buf);
            for (uint8_t i = 0; i < run_.partySize; i++) {
                xSemaphoreTake(mtx_, portMAX_DELAY);
                char pb[48];
                snprintf(pb, sizeof(pb), " Slot%u chipId=0x%08X alive=%c",
                         i, (unsigned)run_.party[i].nodeId,
                         run_.party[i].isAlive() ? 'Y' : 'N');
                xSemaphoreGive(mtx_);
                sendMsg(pb);
            }
            return;
        }

        case DungeonVerb::ATTACK:
            xSemaphoreGive(mtx_);
            hostCmdAttack(fromId, arg);
            return;

        case DungeonVerb::CAST:
            xSemaphoreGive(mtx_);
            hostCmdCast(fromId, arg);
            return;

        case DungeonVerb::SWITCH: {
            uint8_t slot = (uint8_t)atoi(arg);
            xSemaphoreGive(mtx_);
            hostCmdSwitch(fromId, slot);
            return;
        }

        case DungeonVerb::ITEM:
            xSemaphoreGive(mtx_);
            hostCmdItem(fromId, arg);
            return;

        case DungeonVerb::ANSWER:
            xSemaphoreGive(mtx_);
            hostCmdAnswer(fromId, arg);
            return;

        case DungeonVerb::WORDLE:
            xSemaphoreGive(mtx_);
            hostCmdWordle(fromId, arg);
            return;

        case DungeonVerb::FLEE:
            xSemaphoreGive(mtx_);
            hostCmdFlee(fromId);
            return;

        case DungeonVerb::REST:
            xSemaphoreGive(mtx_);
            hostCmdRest(fromId);
            return;

        default:
            xSemaphoreGive(mtx_);
            return;
    }
}

void DungeonGame::hostStartRun() {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    if (!isHost_) { xSemaphoreGive(mtx_); return; }
    if (run_.partySize == 0) { xSemaphoreGive(mtx_); return; }

    // Floor 1: Plane of Fire, biome = Volcanic Plains
    run_.currentFloor.plane       = Plane::PlaneFire;
    run_.currentFloor.floorInPlane= 0;
    run_.currentFloor.depth       = 1;
    run_.currentFloor.typeBias[0] = PokeType::Fire;
    run_.currentFloor.typeBias[1] = PokeType::Ground;
    run_.currentFloor.classBias[0]= DnDClass::Fighter;
    run_.currentFloor.classBias[1]= DnDClass::Barbarian;

    // Phase 1: hardcoded test encounter
    testEnemy_ = TestEnemy{};
    run_.phase = RunPhase::InCombat;
    markDirty();
    xSemaphoreGive(mtx_);

    char header[64];
    snprintf(header, sizeof(header),
             "=== FLOOR 1: Volcanic Plains (Plane of Fire) ===");
    sendMsg(header);

    char enc[64];
    snprintf(enc, sizeof(enc),
             "A wild %s [%s Lv.%u] appears! HP: %u",
             testEnemy_.name, testEnemy_.className,
             testEnemy_.classLv, testEnemy_.hp);
    sendMsg(enc);
    sendMsg("Type: dungeon attack [move]");
    sendState();
}

void DungeonGame::hostCmdAttack(uint32_t fromId, const char *moveName) {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    if (run_.phase != RunPhase::InCombat) {
        xSemaphoreGive(mtx_); sendMsg("[D] Not in combat."); return;
    }

    DungeonTrainer *trainer = findTrainer(fromId);
    if (!trainer) { xSemaphoreGive(mtx_); return; }

    // Phase 1: flat 12 damage per attack (Phase 2 will use real damage formula)
    uint16_t playerDmg = 12;
    uint16_t enemyDmg  = testEnemy_.atkDmg;

    bool enemyDied = false;
    bool partyWiped = false;

    // Apply player damage to enemy
    if (testEnemy_.hp <= playerDmg) {
        testEnemy_.hp = 0;
        enemyDied = true;
    } else {
        testEnemy_.hp -= playerDmg;
    }

    // Apply enemy damage to trainer's active slot HP (crude Phase 1 tracking)
    uint8_t slot = trainer->activeSlot;
    if (trainer->slotHp[slot] <= enemyDmg) {
        trainer->slotHp[slot] = 0;
        trainer->slotFainted[slot] = true;
        // Find next alive slot
        bool foundNext = false;
        for (uint8_t i = 0; i < 6; i++) {
            if (!trainer->slotFainted[i]) {
                trainer->activeSlot = i;
                foundNext = true;
                break;
            }
        }
        if (!foundNext) {
            // Check if all party members are out
            partyWiped = true;
            for (uint8_t i = 0; i < run_.partySize; i++) {
                if (run_.party[i].isAlive()) { partyWiped = false; break; }
            }
        }
    } else {
        trainer->slotHp[slot] -= enemyDmg;
    }

    if (enemyDied) run_.phase = RunPhase::FloorComplete;
    if (partyWiped) run_.phase = RunPhase::RunOver;
    markDirty();
    xSemaphoreGive(mtx_);

    // Narrate
    char atk[64];
    const char *mv = (moveName && *moveName) ? moveName : "Tackle";
    snprintf(atk, sizeof(atk), "Used %s! Enemy -%u HP", mv, playerDmg);
    sendMsg(atk);

    char ctr[64];
    snprintf(ctr, sizeof(ctr), "%s attacks! You -%u HP",
             testEnemy_.name, enemyDmg);
    sendMsg(ctr);

    if (enemyDied) {
        char win[64];
        snprintf(win, sizeof(win), "%s fainted! Floor clear!", testEnemy_.name);
        sendMsg(win);
        sendMsg("Type: dungeon rest (heal) or dungeon party (status)");
    } else {
        char status[64];
        snprintf(status, sizeof(status), "Enemy HP: %u/%u",
                 testEnemy_.hp, testEnemy_.maxHp);
        sendMsg(status);
    }

    if (partyWiped) sendMsg("=== PARTY WIPED — run over ===");
    sendState();
}

void DungeonGame::hostCmdCast(uint32_t fromId, const char *spellName) {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    if (run_.phase != RunPhase::InCombat) {
        xSemaphoreGive(mtx_); sendMsg("[D] Not in combat."); return;
    }
    DungeonTrainer *t = findTrainer(fromId);
    if (!t) { xSemaphoreGive(mtx_); return; }

    // Find lowest available spell slot (1-9 for casters; [0] for class abilities).
    // Phase 2: resolve actual spell effect here. For now, print a stub.
    uint8_t slotUsed = 0;
    if (t->isSpellcaster()) {
        for (uint8_t lvl = 1; lvl <= 9; lvl++) {
            if (t->spellSlotsRemaining[lvl] > 0) {
                slotUsed = lvl;
                t->spellSlotsRemaining[lvl]--;
                break;
            }
        }
        if (slotUsed == 0) {
            xSemaphoreGive(mtx_);
            sendMsg("[D] No spell slots remaining. Recharge at a Pokemon Center.");
            return;
        }
    } else {
        if (t->spellSlotsRemaining[0] == 0) {
            xSemaphoreGive(mtx_);
            sendMsg("[D] No class uses remaining. Recharge at a Pokemon Center.");
            return;
        }
        t->spellSlotsRemaining[0]--;
    }
    markDirty();
    xSemaphoreGive(mtx_);

    char msg[64];
    if (slotUsed > 0)
        snprintf(msg, sizeof(msg), "[D] Cast %s (slot lv%u)! (Phase 2: effect TBD)", spellName, slotUsed);
    else
        snprintf(msg, sizeof(msg), "[D] Used %s! (Phase 2: effect TBD)", spellName);
    sendMsg(msg);
}

void DungeonGame::hostCmdSwitch(uint32_t fromId, uint8_t slot) {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    DungeonTrainer *t = findTrainer(fromId);
    if (t && slot < 6 && !t->slotFainted[slot]) {
        t->activeSlot = slot;
        markDirty();
    }
    xSemaphoreGive(mtx_);
    char msg[32];
    snprintf(msg, sizeof(msg), "Switched to slot %u.", slot);
    sendMsg(msg);
    sendState();
}

void DungeonGame::hostCmdItem(uint32_t fromId, const char *itemName) {
    (void)fromId; (void)itemName;
    sendMsg("[D] Items: Phase 2.");
}

void DungeonGame::hostCmdAnswer(uint32_t fromId, const char *answer) {
    (void)fromId; (void)answer;
    sendMsg("[D] Trivia: Phase 3.");
}

void DungeonGame::hostCmdWordle(uint32_t fromId, const char *guess) {
    (void)fromId; (void)guess;
    sendMsg("[D] Wordle: Phase 3.");
}

void DungeonGame::hostCmdFlee(uint32_t fromId) {
    (void)fromId;
    sendMsg("[D] Flee: TBD (open design question).");
}

void DungeonGame::hostCmdRest(uint32_t fromId) {
    (void)fromId;
    xSemaphoreTake(mtx_, portMAX_DELAY);
    if (run_.phase == RunPhase::FloorComplete || run_.phase == RunPhase::Exploring) {
        // Full heal all party HP (Phase 1: reset to 100 each)
        for (uint8_t i = 0; i < run_.partySize; i++) {
            for (uint8_t s = 0; s < 6; s++) {
                run_.party[i].slotHp[s] = 100;
                run_.party[i].slotFainted[s] = false;
            }
        }
        run_.phase = RunPhase::Exploring;
        markDirty();
        xSemaphoreGive(mtx_);
        sendMsg("[PC] Full heal! All Pokemon restored.");
        sendState();
    } else {
        xSemaphoreGive(mtx_);
        sendMsg("[D] No Pokemon Center here yet.");
    }
}

// ── Packet handlers (Core 0) ─────────────────────────────────────────────────

void DungeonGame::handleBeacon(const BattlePacket &pkt, uint8_t payloadLen) {
    if (!active_) return;  // ignore beacons until user types dungeon host/join
    if (payloadLen < DUNGEON_BEACON_SIZE) return;
    uint32_t senderId = ((uint32_t)pkt.payload[0] << 24) |
                        ((uint32_t)pkt.payload[1] << 16) |
                        ((uint32_t)pkt.payload[2] <<  8) |
                        pkt.payload[3];
    if (senderId == myId()) return;

    char name[11] = {};
    memcpy(name, pkt.payload + 4, 10);

    char msg[48];
    snprintf(msg, sizeof(msg), "[D] Beacon: %.10s (0x%08X)", name, (unsigned)senderId);
    logMsg(msg);
    markDirty();
}

void DungeonGame::handleJoin(const BattlePacket &pkt, uint8_t payloadLen) {
    if (!isHost_) return;
    if (payloadLen < DUNGEON_BEACON_SIZE) return;

    uint32_t guestId = ((uint32_t)pkt.payload[0] << 24) |
                       ((uint32_t)pkt.payload[1] << 16) |
                       ((uint32_t)pkt.payload[2] <<  8) |
                       pkt.payload[3];
    if (guestId == myId()) return;

    xSemaphoreTake(mtx_, portMAX_DELAY);
    if (run_.partySize >= 5) {
        xSemaphoreGive(mtx_);
        sendJoinAck(guestId, false, 0);
        return;
    }

    uint8_t slot = run_.partySize;
    run_.party[slot].nodeId = guestId;
    // Init HP pool (Phase 1: flat 100 per slot)
    for (uint8_t s = 0; s < 6; s++) run_.party[slot].slotHp[s] = 100;
    run_.partySize++;
    markDirty();
    xSemaphoreGive(mtx_);

    sendJoinAck(guestId, true, slot);
    char msg[40];
    char name[11] = {};
    memcpy(name, pkt.payload + 4, 10);
    snprintf(msg, sizeof(msg), "[D] %.10s joined (slot %u)", name, slot);
    sendMsg(msg);
    sendState();
}

void DungeonGame::handleJoinAck(const BattlePacket &pkt, uint8_t payloadLen) {
    if (isHost_) return;
    if (payloadLen < DUNGEON_JOIN_ACK_SIZE) return;

    uint32_t targetId = ((uint32_t)pkt.payload[0] << 24) |
                        ((uint32_t)pkt.payload[1] << 16) |
                        ((uint32_t)pkt.payload[2] <<  8) |
                        pkt.payload[3];
    if (targetId != myId()) return;

    bool accepted = pkt.payload[4] != 0;
    uint8_t slot  = pkt.payload[5];

    if (accepted) {
        xSemaphoreTake(mtx_, portMAX_DELAY);
        run_.party[slot].nodeId = myId();
        for (uint8_t s = 0; s < 6; s++) run_.party[slot].slotHp[s] = 100;
        markDirty();
        xSemaphoreGive(mtx_);
        char msg[32];
        snprintf(msg, sizeof(msg), "[D] Joined! Party slot %u.", slot);
        logMsg(msg);
    } else {
        logMsg("[D] Join rejected — party full.");
    }
    markDirty();
}

void DungeonGame::handleCmd(const BattlePacket &pkt, uint8_t payloadLen) {
    if (!isHost_) return;
    if (payloadLen < DUNGEON_CMD_HDR_SIZE) return;

    uint32_t senderId = ((uint32_t)pkt.payload[0] << 24) |
                        ((uint32_t)pkt.payload[1] << 16) |
                        ((uint32_t)pkt.payload[2] <<  8) |
                        pkt.payload[3];
    DungeonVerb verb = static_cast<DungeonVerb>(pkt.payload[4]);
    const char *arg  = (payloadLen > DUNGEON_CMD_HDR_SIZE)
                       ? (const char *)(pkt.payload + DUNGEON_CMD_HDR_SIZE)
                       : "";
    hostProcessCmd(senderId, verb, arg);
}

void DungeonGame::handleState(const BattlePacket &pkt, uint8_t payloadLen) {
    if (!active_) return;   // not joined yet; discard
    if (isHost_) return;
    if (payloadLen < DUNGEON_STATE_HDR_SIZE) return;

    xSemaphoreTake(mtx_, portMAX_DELAY);
    run_.phase = static_cast<RunPhase>(pkt.payload[0]);
    run_.currentFloor.depth = pkt.payload[1];
    uint8_t ps = pkt.payload[2];
    run_.partySize = (ps > 5) ? 5 : ps;

    uint8_t offset = DUNGEON_STATE_HDR_SIZE;
    for (uint8_t i = 0; i < run_.partySize && offset + DUNGEON_STATE_SLOT_SIZE <= payloadLen; i++) {
        run_.party[i].nodeId     = ((uint32_t)pkt.payload[offset+0] << 24) |
                                   ((uint32_t)pkt.payload[offset+1] << 16) |
                                   ((uint32_t)pkt.payload[offset+2] <<  8) |
                                   pkt.payload[offset+3];
        uint16_t curHp           = ((uint16_t)pkt.payload[offset+4] << 8) | pkt.payload[offset+5];
        uint16_t maxHp           = ((uint16_t)pkt.payload[offset+6] << 8) | pkt.payload[offset+7];
        run_.party[i].activeSlot = pkt.payload[offset+8];
        uint8_t alive            = pkt.payload[offset+9];
        for (uint8_t s = 0; s < 6; s++) {
            run_.party[i].slotFainted[s] = !(alive & (1 << s));
        }
        run_.party[i].slotHp[run_.party[i].activeSlot]  = curHp;
        (void)maxHp;
        offset += DUNGEON_STATE_SLOT_SIZE;
    }
    markDirty();
    xSemaphoreGive(mtx_);
}

void DungeonGame::handleMsg(const BattlePacket &pkt, uint8_t payloadLen) {
    if (!active_) return;   // not joined yet; discard
    if (payloadLen == 0) return;
    char msg[DLOG_LEN];
    uint8_t n = (payloadLen < DLOG_LEN - 1) ? payloadLen : DLOG_LEN - 1;
    memcpy(msg, pkt.payload, n);
    msg[n] = '\0';
    logMsg(msg);
    markDirty();
}

void DungeonGame::handlePrompt(const BattlePacket &pkt, uint8_t payloadLen) {
    if (!active_) return;   // not joined yet; discard
    if (payloadLen < 2) return;
    uint8_t ptype = pkt.payload[0];
    char prompt[64];
    uint8_t n = (payloadLen - 1 < 63) ? payloadLen - 1 : 63;
    memcpy(prompt, pkt.payload + 1, n);
    prompt[n] = '\0';
    char msg[72];
    const char *label = ptype == 0 ? "TRIVIA" : ptype == 1 ? "WORDLE" : "HACK";
    snprintf(msg, sizeof(msg), "[%s] %s", label, prompt);
    logMsg(msg);
    markDirty();
}

// ── Packet senders ────────────────────────────────────────────────────────────

void DungeonGame::sendBeacon() {
    uint8_t pl[DUNGEON_BEACON_SIZE] = {};
    uint32_t id = myId();
    pl[0] = (id >> 24) & 0xFF;
    pl[1] = (id >> 16) & 0xFF;
    pl[2] = (id >>  8) & 0xFF;
    pl[3] = id & 0xFF;
    // Trainer name placeholder
    strncpy((char *)pl + 4, "Trainer", 9);
    BattlePacket pkt = buildPacket(PktType::DUNGEON_BEACON, pl, DUNGEON_BEACON_SIZE);
    sendPkt(pkt, DUNGEON_BEACON_SIZE);
}

void DungeonGame::sendJoinRequest() {
    uint8_t pl[DUNGEON_BEACON_SIZE] = {};
    uint32_t id = myId();
    pl[0] = (id >> 24) & 0xFF;
    pl[1] = (id >> 16) & 0xFF;
    pl[2] = (id >>  8) & 0xFF;
    pl[3] = id & 0xFF;
    strncpy((char *)pl + 4, "Trainer", 9);
    BattlePacket pkt = buildPacket(PktType::DUNGEON_JOIN, pl, DUNGEON_BEACON_SIZE);
    sendPkt(pkt, DUNGEON_BEACON_SIZE);
}

void DungeonGame::sendJoinAck(uint32_t targetId, bool accepted, uint8_t slot) {
    uint8_t pl[DUNGEON_JOIN_ACK_SIZE] = {};
    pl[0] = (targetId >> 24) & 0xFF;
    pl[1] = (targetId >> 16) & 0xFF;
    pl[2] = (targetId >>  8) & 0xFF;
    pl[3] = targetId & 0xFF;
    pl[4] = accepted ? 1 : 0;
    pl[5] = slot;
    xSemaphoreTake(mtx_, portMAX_DELAY);
    pl[6] = run_.partySize;
    xSemaphoreGive(mtx_);
    BattlePacket pkt = buildPacket(PktType::DUNGEON_JOIN_ACK, pl, DUNGEON_JOIN_ACK_SIZE);
    sendPkt(pkt, DUNGEON_JOIN_ACK_SIZE);
}

void DungeonGame::sendState() {
    uint8_t pl[DUNGEON_STATE_HDR_SIZE + 5 * DUNGEON_STATE_SLOT_SIZE] = {};
    xSemaphoreTake(mtx_, portMAX_DELAY);
    pl[0] = (uint8_t)run_.phase;
    pl[1] = run_.currentFloor.depth;
    pl[2] = run_.partySize;
    uint8_t off = DUNGEON_STATE_HDR_SIZE;
    for (uint8_t i = 0; i < run_.partySize; i++) {
        uint32_t nid = run_.party[i].nodeId;
        pl[off+0] = (nid >> 24) & 0xFF;
        pl[off+1] = (nid >> 16) & 0xFF;
        pl[off+2] = (nid >>  8) & 0xFF;
        pl[off+3] = nid & 0xFF;
        uint8_t slot = run_.party[i].activeSlot;
        uint16_t hp  = run_.party[i].slotHp[slot];
        pl[off+4] = (hp >> 8) & 0xFF;
        pl[off+5] = hp & 0xFF;
        pl[off+6] = 0; pl[off+7] = 100; // maxHp placeholder
        pl[off+8] = slot;
        uint8_t alive = 0;
        for (uint8_t s = 0; s < 6; s++) {
            if (!run_.party[i].slotFainted[s]) alive |= (1 << s);
        }
        pl[off+9] = alive;
        off += DUNGEON_STATE_SLOT_SIZE;
    }
    xSemaphoreGive(mtx_);

    BattlePacket pkt = buildPacket(PktType::DUNGEON_STATE, pl, off);
    sendPkt(pkt, off);
}

void DungeonGame::sendMsg(const char *msg) {
    logMsg(msg);  // also log locally
    uint8_t len = (uint8_t)strlen(msg);
    if (len > BATTLELINK_MAX_PAYLOAD - 1) len = BATTLELINK_MAX_PAYLOAD - 1;
    BattlePacket pkt = buildPacket(PktType::DUNGEON_MSG,
                                   (const uint8_t *)msg, len + 1);
    sendPkt(pkt, len + 1);
}

void DungeonGame::sendPrompt(uint8_t promptType, const char *data) {
    uint8_t pl[BATTLELINK_MAX_PAYLOAD];
    pl[0] = promptType;
    uint8_t dlen = (uint8_t)strlen(data);
    if (dlen > BATTLELINK_MAX_PAYLOAD - 2) dlen = BATTLELINK_MAX_PAYLOAD - 2;
    memcpy(pl + 1, data, dlen);
    pl[1 + dlen] = '\0';
    BattlePacket pkt = buildPacket(PktType::DUNGEON_PROMPT, pl, 2 + dlen);
    sendPkt(pkt, 2 + dlen);
}

void DungeonGame::forwardCmd(DungeonVerb verb, const char *arg) {
    uint8_t pl[DUNGEON_CMD_HDR_SIZE + 33] = {};
    uint32_t id = myId();
    pl[0] = (id >> 24) & 0xFF;
    pl[1] = (id >> 16) & 0xFF;
    pl[2] = (id >>  8) & 0xFF;
    pl[3] = id & 0xFF;
    pl[4] = (uint8_t)verb;
    uint8_t arglen = arg ? (uint8_t)strlen(arg) : 0;
    if (arglen > 32) arglen = 32;
    if (arglen) memcpy(pl + DUNGEON_CMD_HDR_SIZE, arg, arglen);
    pl[DUNGEON_CMD_HDR_SIZE + arglen] = '\0';
    BattlePacket pkt = buildPacket(PktType::DUNGEON_CMD, pl,
                                   DUNGEON_CMD_HDR_SIZE + arglen + 1);
    sendPkt(pkt, DUNGEON_CMD_HDR_SIZE + arglen + 1);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

BattlePacket DungeonGame::buildPacket(PktType type,
                                       const uint8_t *payload,
                                       uint8_t payloadLen) {
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)type;
    pkt.setSessionId(0);  // dungeon uses session 0 (broadcast)
    pkt.seq = seq_++;
    if (payload && payloadLen > 0) {
        memcpy(pkt.payload, payload, payloadLen);
    }
    return pkt;
}

void DungeonGame::sendPkt(const BattlePacket &pkt, uint8_t payloadLen) {
    transport_.send((const uint8_t *)&pkt, BATTLELINK_HDR_SIZE + payloadLen);
}

DungeonTrainer *DungeonGame::findTrainer(uint32_t chipId) {
    for (uint8_t i = 0; i < run_.partySize; i++) {
        if (run_.party[i].nodeId == chipId) return &run_.party[i];
    }
    return nullptr;
}

void DungeonGame::logMsg(const char *msg) {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    log_.push(msg);
    xSemaphoreGive(mtx_);
}
