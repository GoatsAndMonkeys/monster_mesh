#include "PokemonLinkProxy.h"
#include "MeshtasticTransport.h"
#include "MonsterMeshEmulator.h"
#include <string.h>

// ── Gen 1 Cable Club protocol constants ──────────────────────────────────────
static constexpr uint8_t PKMN_MASTER    = 0x01;  // master role byte
static constexpr uint8_t PKMN_SLAVE     = 0x02;  // slave role byte
static constexpr uint8_t PKMN_CONNECTED = 0x60;  // both roles confirmed
static constexpr uint8_t PKMN_PREAMBLE  = 0xFD;  // preamble / sync byte

// WRAM addresses (Pokemon Red / Blue — same layout)
// The 415-byte trade block is stored contiguously at 0xD158–0xD2F6:
//   0xD158 + 11  = trainer name  (10 chars + 0x50 terminator)
//   0xD163 + 1   = party count
//   0xD164 + 7   = species list  (6 species + 0xFF terminator)
//   0xD16B + 264 = party data    (6 × 44 bytes)
//   0xD273 + 66  = OT names      (6 × 11 bytes)
//   0xD2B5 + 66  = nicknames     (6 × 11 bytes)
static constexpr uint16_t WRAM_TRADE_BLOCK = 0xD158;

// ── Trade block fragment layout ───────────────────────────────────────────────
// TRADE_BLOCK_PART / PATCH_LIST_PART payload:
//   [0] fragIdx   (0-based)
//   [1] totalFrags
//   [2..] data    (up to FRAG_DATA_SIZE = 194 bytes)
static constexpr uint8_t TRADE_TOTAL_FRAGS = 3;  // ceil(415/194)
static constexpr uint8_t PATCH_TOTAL_FRAGS = 2;  // ceil(200/194)

// ── Constructor ───────────────────────────────────────────────────────────────

PokemonLinkProxy::PokemonLinkProxy(MeshtasticTransport &transport,
                                   MonsterMeshEmulator  &emulator)
    : transport_(transport), emulator_(emulator) {}

void PokemonLinkProxy::begin() {
    reset();
}

void PokemonLinkProxy::reset() {
    phase_             = Phase::DISCONNECTED;
    counter_           = 0;
    responsePending_   = false;
    responseByte_      = 0xFF;
    lastGbByte_        = 0;
    seq_               = 0;
    tradeFragMask_     = 0;
    patchFragMask_     = 0;
    remoteTradeReady_  = false;
    remotePatchReady_  = false;
    remoteDataIdx_     = 0;
    patchRemoteIdx_    = 0;
    remoteSelectReady_ = false;
    remoteConfirmReady_= false;
    remoteSelect_      = 0;
    remoteConfirm_     = 0;
    memset(localTradeBlock_,  0xFF, sizeof(localTradeBlock_));
    memset(remoteTradeBlock_, 0xFF, sizeof(remoteTradeBlock_));
    memset(localPatchList_,   0x00, sizeof(localPatchList_));
    memset(remotePatchList_,  0x00, sizeof(remotePatchList_));
}

// ── transitionTo ─────────────────────────────────────────────────────────────

void PokemonLinkProxy::transitionTo(Phase p) {
    static const char *const names[] = {
        "DISCONNECTED", "NEGOTIATION", "MENU", "TRADE_INIT",
        "TRADE_PREAMBLE", "TRADE_RANDOM", "TRADE_DATA",
        "TRADE_DATA_END", "TRADE_PATCH_HEADER", "TRADE_PATCH_DATA",
        "TRADE_SELECT", "TRADE_CONFIRM", "TRADE_DONE", "BATTLE"
    };
    Serial.printf("[PROXY] %s -> %s\n",
                  names[(int)phase_], names[(int)p]);
    phase_ = p;
}

// ── onGbTx ───────────────────────────────────────────────────────────────────
// Called by BattleShim when the GB sends a serial byte.
// We run the state machine and prepare a response (or mark as stalling).

void PokemonLinkProxy::onGbTx(uint8_t byte) {
    lastGbByte_      = byte;
    responsePending_ = false;
    uint8_t resp     = 0xFF;
    bool    ready    = false;

    switch (phase_) {

    // ── Phase 1: Role negotiation ─────────────────────────────────────────
    case Phase::DISCONNECTED:
        if (byte == PKMN_MASTER) {
            transitionTo(Phase::NEGOTIATION);
            gbIsMaster_ = true;
            resp = PKMN_SLAVE;
        } else if (byte == PKMN_SLAVE) {
            transitionTo(Phase::NEGOTIATION);
            gbIsMaster_ = false;
            resp = PKMN_MASTER;
        } else {
            resp = 0xFF;
        }
        ready = true;
        break;

    case Phase::NEGOTIATION:
        if (byte == PKMN_MASTER) {
            resp = PKMN_SLAVE;
        } else if (byte == PKMN_SLAVE) {
            resp = PKMN_MASTER;
        } else if (byte == PKMN_CONNECTED) {
            resp = PKMN_CONNECTED;
        } else if (byte == 0x00) {
            transitionTo(Phase::MENU);
            counter_ = 0;
            resp = 0x00;
        } else if (byte >= 0xD0 && byte <= 0xD6) {
            // Game skipped 0x00 and jumped straight to menu bytes
            transitionTo(Phase::MENU);
            counter_ = 0;
            goto handle_menu_byte;
        } else {
            resp = byte;
        }
        ready = true;
        break;

    // ── Phase 2: Cable Club menu ──────────────────────────────────────────
    case Phase::MENU:
        if (byte == PKMN_MASTER || byte == PKMN_SLAVE) {
            // Game reconnecting — restart negotiation
            if (byte == PKMN_MASTER) {
                transitionTo(Phase::NEGOTIATION);
                gbIsMaster_ = true;
                resp = PKMN_SLAVE;
            } else {
                transitionTo(Phase::NEGOTIATION);
                gbIsMaster_ = false;
                resp = PKMN_MASTER;
            }
            ready = true;
            break;
        }
        handle_menu_byte:
        if (byte == 0xD4) {
            // Trade Centre selected: read WRAM, build + send our data, stall GB
            readLocalTradeBlock();
            buildAndPatchLocalBlock();
            sendTradeReady();
            sendTradeBlockFragments();
            sendPatchListFragments();
            transitionTo(Phase::TRADE_INIT);
            counter_ = 0;
            // Echo 0xD4 so the game knows the "remote" also chose Trade Centre
            resp  = 0xD4;
            ready = true;
        } else if (byte == 0xD5) {
            // Colosseum (battle): dumb relay mode
            transitionTo(Phase::BATTLE);
            counter_ = 0;
            resp  = byte;
            ready = true;
        } else {
            // Echo all other menu bytes (0xD0 highlight, 0xD6 cancel, etc.)
            resp  = byte;
            ready = true;
        }
        break;

    // ── TRADE_INIT: stall until remote party data arrives ─────────────────
    // The game immediately starts sending 0xFD preamble bytes after 0xD4.
    // We stall on the first one until all LoRa fragments have been received.
    // getResponse() polls remoteTradeReady_ + remotePatchReady_ each frame.
    case Phase::TRADE_INIT:
        ready = false;
        break;

    // ── TRADE_PREAMBLE: 10× 0xFD (local echo) ────────────────────────────
    // counter_ starts at 1 (first byte handled in getResponse() unstall).
    case Phase::TRADE_PREAMBLE:
        counter_++;
        if (counter_ >= 10) {
            transitionTo(Phase::TRADE_RANDOM);
            counter_ = 0;
        }
        resp  = PKMN_PREAMBLE;
        ready = true;
        break;

    // ── TRADE_RANDOM: 10 random + 9× 0xFD (local echo) ───────────────────
    case Phase::TRADE_RANDOM:
        // counter_ 0-9: random bytes (echo back as "our" random seed)
        // counter_ 10-18: 9 preamble bytes
        resp = (counter_ < 10) ? byte : PKMN_PREAMBLE;
        counter_++;
        if (counter_ >= 19) {
            transitionTo(Phase::TRADE_DATA);
            counter_ = 0;
        }
        ready = true;
        break;

    // ── TRADE_DATA: 415-byte party block ─────────────────────────────────
    // We ignore what the GB sends (already read from WRAM).
    // We feed back the remote's pre-buffered party block.
    case Phase::TRADE_DATA:
        resp = (remoteDataIdx_ < TRADE_BLOCK_SIZE)
               ? remoteTradeBlock_[remoteDataIdx_++]
               : 0xFF;
        counter_++;
        if (counter_ >= TRADE_BLOCK_SIZE) {
            transitionTo(Phase::TRADE_DATA_END);
            counter_ = 0;
        }
        ready = true;
        break;

    // ── TRADE_DATA_END: 3 bytes (0xDF 0xFE 0x15) + 6× 0xFD ──────────────
    case Phase::TRADE_DATA_END:
        resp = byte;   // echo each byte in the end-of-data sequence
        counter_++;
        if (counter_ >= 9) {
            transitionTo(Phase::TRADE_PATCH_HEADER);
            counter_ = 0;
        }
        ready = true;
        break;

    // ── TRADE_PATCH_HEADER: 7× 0x00 ──────────────────────────────────────
    case Phase::TRADE_PATCH_HEADER:
        resp = byte;
        counter_++;
        if (counter_ >= 7) {
            transitionTo(Phase::TRADE_PATCH_DATA);
            counter_ = 0;
        }
        ready = true;
        break;

    // ── TRADE_PATCH_DATA: 200-byte patch list ─────────────────────────────
    // Stall if remote patches haven't arrived yet (should be rare — they were
    // sent before preamble started, ~441 bytes of exchange have since passed).
    case Phase::TRADE_PATCH_DATA:
        if (!remotePatchReady_) {
            ready = false;   // getResponse() will retry when patches arrive
        } else {
            resp = (patchRemoteIdx_ < PATCH_LIST_SIZE)
                   ? remotePatchList_[patchRemoteIdx_++]
                   : 0x00;
            counter_++;
            if (counter_ >= PATCH_LIST_SIZE) {
                applyRemotePatchList();
                transitionTo(Phase::TRADE_SELECT);
                counter_ = 0;
            }
            ready = true;
        }
        break;

    // ── TRADE_SELECT: single-byte pokemon selection ───────────────────────
    // Forward over LoRa; stall until remote's selection arrives.
    case Phase::TRADE_SELECT: {
        uint8_t pl[1] = { byte };
        sendPacket(PktType::TRADE_SELECT, pl, 1);
        Serial.printf("[PROXY] -> TRADE_SELECT 0x%02X\n", byte);
        ready = false;  // getResponse() returns remote's selection when it arrives
        break;
    }

    // ── TRADE_CONFIRM: accept / reject ────────────────────────────────────
    case Phase::TRADE_CONFIRM: {
        uint8_t pl[1] = { byte };
        sendPacket(PktType::TRADE_CONFIRM, pl, 1);
        Serial.printf("[PROXY] -> TRADE_CONFIRM 0x%02X\n", byte);
        ready = false;
        break;
    }

    case Phase::TRADE_DONE:
        // Trade complete; game will return to menu or disconnect
        transitionTo(Phase::MENU);
        resp  = byte;
        ready = true;
        break;

    // ── BATTLE: dumb relay via txQ_/rxQ_ ─────────────────────────────────
    case Phase::BATTLE:
        if (txQ_) xQueueSend(txQ_, &byte, 0);
        ready = false;  // response comes asynchronously from rxQ_
        break;
    }

    responsePending_ = ready;
    responseByte_    = resp;
}

// ── getResponse ──────────────────────────────────────────────────────────────
// Called by peanut_gb each frame after a serial TX until we return true
// (transfer completes) or the 30-second stall timer fires.

bool PokemonLinkProxy::getResponse(uint8_t &out) {

    // BATTLE mode: pop async bytes from rxQ_
    if (phase_ == Phase::BATTLE) {
        if (rxQ_ && xQueueReceive(rxQ_, &out, 0) == pdTRUE) return true;
        return false;
    }

    // TRADE_INIT stall: lift when both trade block AND patch list are ready
    if (phase_ == Phase::TRADE_INIT) {
        if (remoteTradeReady_ && remotePatchReady_) {
            // Transition to preamble; respond to the stalled first 0xFD byte
            transitionTo(Phase::TRADE_PREAMBLE);
            counter_ = 1;           // first preamble byte accounted for here
            out = PKMN_PREAMBLE;
            return true;
        }
        return false;
    }

    // TRADE_PATCH_DATA stall: lift when remote patches arrive mid-exchange
    if (phase_ == Phase::TRADE_PATCH_DATA && !responsePending_) {
        if (!remotePatchReady_) return false;
        // Resume the byte that caused the stall
        out = (patchRemoteIdx_ < PATCH_LIST_SIZE)
              ? remotePatchList_[patchRemoteIdx_++]
              : 0x00;
        counter_++;
        if (counter_ >= PATCH_LIST_SIZE) {
            applyRemotePatchList();
            transitionTo(Phase::TRADE_SELECT);
            counter_ = 0;
        }
        return true;
    }

    // TRADE_SELECT stall: lift when remote's selection byte arrives
    if (phase_ == Phase::TRADE_SELECT && !responsePending_) {
        if (!remoteSelectReady_) return false;
        out = remoteSelect_;
        remoteSelectReady_ = false;
        transitionTo(Phase::TRADE_CONFIRM);
        counter_ = 0;
        return true;
    }

    // TRADE_CONFIRM stall: lift when remote's confirm byte arrives
    if (phase_ == Phase::TRADE_CONFIRM && !responsePending_) {
        if (!remoteConfirmReady_) return false;
        out = remoteConfirm_;
        remoteConfirmReady_ = false;
        transitionTo(Phase::TRADE_DONE);
        return true;
    }

    // Normal (instant) response path
    if (!responsePending_) return false;
    out = responseByte_;
    responsePending_ = false;
    return true;
}

// ── onRemotePacket ────────────────────────────────────────────────────────────
// Called by MonsterMeshBattleShim::handlePacket() for TRADE_* packet types.

void PokemonLinkProxy::onRemotePacket(PktType type, const uint8_t *payload,
                                       uint8_t payloadLen) {
    switch (type) {

    case PktType::TRADE_READY:
        Serial.println("[PROXY] <- TRADE_READY");
        // No action needed beyond receiving; actual unstall requires the data.
        break;

    case PktType::TRADE_BLOCK_PART: {
        if (payloadLen < 2) break;
        uint8_t fragIdx    = payload[0];
        uint8_t totalFrags = payload[1];
        if (fragIdx >= 8 || fragIdx >= totalFrags) break;
        uint8_t  dataLen = payloadLen - 2;
        uint16_t offset  = (uint16_t)fragIdx * FRAG_DATA_SIZE;
        if (offset + dataLen > TRADE_BLOCK_SIZE) {
            dataLen = (uint8_t)(TRADE_BLOCK_SIZE - offset);
        }
        memcpy(remoteTradeBlock_ + offset, payload + 2, dataLen);
        tradeFragMask_ |= (1 << fragIdx);
        Serial.printf("[PROXY] <- TRADE_BLOCK_PART frag=%d/%d (%d bytes)\n",
                      fragIdx + 1, totalFrags, dataLen);
        uint8_t allMask = (1 << totalFrags) - 1;
        if ((tradeFragMask_ & allMask) == allMask) {
            Serial.println("[PROXY] remote trade block complete");
            checkUnstall();
        }
        break;
    }

    case PktType::PATCH_LIST_PART: {
        if (payloadLen < 2) break;
        uint8_t fragIdx    = payload[0];
        uint8_t totalFrags = payload[1];
        if (fragIdx >= 8 || fragIdx >= totalFrags) break;
        uint8_t  dataLen = payloadLen - 2;
        uint16_t offset  = (uint16_t)fragIdx * FRAG_DATA_SIZE;
        if (offset + dataLen > PATCH_LIST_SIZE) {
            dataLen = (uint8_t)(PATCH_LIST_SIZE - offset);
        }
        memcpy(remotePatchList_ + offset, payload + 2, dataLen);
        patchFragMask_ |= (1 << fragIdx);
        Serial.printf("[PROXY] <- PATCH_LIST_PART frag=%d/%d (%d bytes)\n",
                      fragIdx + 1, totalFrags, dataLen);
        uint8_t allMask = (1 << totalFrags) - 1;
        if ((patchFragMask_ & allMask) == allMask) {
            Serial.println("[PROXY] remote patch list complete");
            remotePatchReady_ = true;
            checkUnstall();
        }
        break;
    }

    case PktType::TRADE_SELECT:
        if (payloadLen >= 1) {
            remoteSelect_      = payload[0];
            remoteSelectReady_ = true;
            Serial.printf("[PROXY] <- TRADE_SELECT 0x%02X\n", remoteSelect_);
        }
        break;

    case PktType::TRADE_CONFIRM:
        if (payloadLen >= 1) {
            remoteConfirm_      = payload[0];
            remoteConfirmReady_ = true;
            Serial.printf("[PROXY] <- TRADE_CONFIRM 0x%02X\n", remoteConfirm_);
        }
        break;

    default:
        break;
    }
}

// ── checkUnstall ─────────────────────────────────────────────────────────────
// Called whenever a fragment arrives. Sets remoteTradeReady_ when the full
// trade block is present (patch list readiness is checked separately).

void PokemonLinkProxy::checkUnstall() {
    uint8_t tradeAllMask = (1 << TRADE_TOTAL_FRAGS) - 1;  // 0x07
    if ((tradeFragMask_ & tradeAllMask) == tradeAllMask) {
        remoteTradeReady_ = true;
    }
    // remotePatchReady_ is set directly in the PATCH_LIST_PART handler.
    // The TRADE_INIT → TRADE_PREAMBLE unstall requires BOTH flags.
}

// ── readLocalTradeBlock ───────────────────────────────────────────────────────

void PokemonLinkProxy::readLocalTradeBlock() {
    emulator_.readWRAMRange(WRAM_TRADE_BLOCK, localTradeBlock_, TRADE_BLOCK_SIZE);
    Serial.printf("[PROXY] read trade block from WRAM 0x%04X (%d bytes)\n",
                  WRAM_TRADE_BLOCK, TRADE_BLOCK_SIZE);
}

// ── buildAndPatchLocalBlock ───────────────────────────────────────────────────
// Scan localTradeBlock_ for 0xFE bytes (which the link protocol uses as
// "no data" — they must not appear in the actual data stream).
// Replace each with 0xFF and record the index in localPatchList_:
//   Part 1: indices 0..251, stored as (index+1), terminated by 0xFF
//   Part 2: indices 252..414, stored as (index-251), terminated by 0xFF
// Remaining bytes padded to PATCH_LIST_SIZE with 0x00.

void PokemonLinkProxy::buildAndPatchLocalBlock() {
    uint8_t *p = localPatchList_;

    // Part 1 — indices 0..251
    for (uint16_t i = 0; i < 252 && i < TRADE_BLOCK_SIZE; i++) {
        if (localTradeBlock_[i] == 0xFE) {
            *p++ = (uint8_t)(i + 1);
            localTradeBlock_[i] = 0xFF;
        }
    }
    *p++ = 0xFF;  // terminator

    // Part 2 — indices 252..414
    for (uint16_t i = 252; i < TRADE_BLOCK_SIZE; i++) {
        if (localTradeBlock_[i] == 0xFE) {
            *p++ = (uint8_t)(i - 251);
            localTradeBlock_[i] = 0xFF;
        }
    }
    *p++ = 0xFF;  // terminator

    // Pad to PATCH_LIST_SIZE
    while ((uint16_t)(p - localPatchList_) < PATCH_LIST_SIZE) *p++ = 0x00;

    Serial.printf("[PROXY] patch list: %d entries\n",
                  (int)(p - localPatchList_));
}

// ── applyRemotePatchList ──────────────────────────────────────────────────────
// Reverse the patch: restore 0xFE bytes in remoteTradeBlock_ at the indices
// recorded in remotePatchList_.

void PokemonLinkProxy::applyRemotePatchList() {
    const uint8_t *p   = remotePatchList_;
    const uint8_t *end = remotePatchList_ + PATCH_LIST_SIZE;

    // Part 1 — stored as (index+1)
    while (p < end && *p != 0xFF) {
        uint16_t idx = (uint16_t)(*p++) - 1;
        if (idx < TRADE_BLOCK_SIZE) remoteTradeBlock_[idx] = 0xFE;
    }
    if (p < end && *p == 0xFF) p++;

    // Part 2 — stored as (index-251)
    while (p < end && *p != 0xFF) {
        uint16_t idx = (uint16_t)(*p++) + 251;
        if (idx < TRADE_BLOCK_SIZE) remoteTradeBlock_[idx] = 0xFE;
    }

    Serial.println("[PROXY] applied remote patch list");
}

// ── Network send helpers ──────────────────────────────────────────────────────

void PokemonLinkProxy::sendPacket(PktType type, const uint8_t *payload,
                                   uint8_t payloadLen) {
    BattlePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = (uint8_t)type;
    pkt.setSessionId(sessionId_);
    pkt.seq = seq_++;
    if (payload && payloadLen > 0) {
        if (payloadLen > BATTLELINK_MAX_PAYLOAD) payloadLen = BATTLELINK_MAX_PAYLOAD;
        memcpy(pkt.payload, payload, payloadLen);
    }
    transport_.send((uint8_t *)&pkt, BATTLELINK_HDR_SIZE + payloadLen);
}

void PokemonLinkProxy::sendTradeReady() {
    sendPacket(PktType::TRADE_READY, nullptr, 0);
    Serial.println("[PROXY] -> TRADE_READY");
}

void PokemonLinkProxy::sendTradeBlockFragments() {
    uint16_t offset = 0;
    for (uint8_t i = 0; i < TRADE_TOTAL_FRAGS; i++) {
        uint16_t end     = offset + FRAG_DATA_SIZE;
        if (end > TRADE_BLOCK_SIZE) end = TRADE_BLOCK_SIZE;
        uint8_t dataLen  = (uint8_t)(end - offset);

        uint8_t pl[2 + FRAG_DATA_SIZE];
        pl[0] = i;
        pl[1] = TRADE_TOTAL_FRAGS;
        memcpy(pl + 2, localTradeBlock_ + offset, dataLen);
        sendPacket(PktType::TRADE_BLOCK_PART, pl, 2 + dataLen);
        Serial.printf("[PROXY] -> TRADE_BLOCK_PART frag=%d/%d (%d bytes)\n",
                      i + 1, TRADE_TOTAL_FRAGS, dataLen);
        offset = end;
    }
}

void PokemonLinkProxy::sendPatchListFragments() {
    uint16_t offset = 0;
    for (uint8_t i = 0; i < PATCH_TOTAL_FRAGS; i++) {
        uint16_t end    = offset + FRAG_DATA_SIZE;
        if (end > PATCH_LIST_SIZE) end = PATCH_LIST_SIZE;
        uint8_t dataLen = (uint8_t)(end - offset);

        uint8_t pl[2 + FRAG_DATA_SIZE];
        pl[0] = i;
        pl[1] = PATCH_TOTAL_FRAGS;
        memcpy(pl + 2, localPatchList_ + offset, dataLen);
        sendPacket(PktType::PATCH_LIST_PART, pl, 2 + dataLen);
        Serial.printf("[PROXY] -> PATCH_LIST_PART frag=%d/%d (%d bytes)\n",
                      i + 1, PATCH_TOTAL_FRAGS, dataLen);
        offset = end;
    }
}
