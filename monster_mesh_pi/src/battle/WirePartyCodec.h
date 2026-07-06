// SPDX-License-Identifier: MIT
//
// WirePartyCodec — neutral cross-gen party wire format (battle protocol V2).
//
// Carried by TEXT_BATTLE_CHALLENGE_V2 / TEXT_BATTLE_ACCEPT_V2. Replaces the
// V1 partyMin blob (Gen-1-internal species + DV/stat-exp, 109 B), which could
// only describe Gen-1 mons. V2 transmits FINAL computed stats: each save
// reader (Gen 1/2/3) derives stats with its own gen's formula and the
// receiver uses the numbers verbatim — no cross-gen formula ambiguity, and
// both sides seed their engines from byte-identical parties.
//
// Per-mon layout (23 B, all multi-byte fields BIG-ENDIAN like the rest of
// the BattleLink protocol):
//   species(2 = national dex 1-386)  level(1)
//   maxHp(2) atk(2) def(2) spe(2) spa(2) spd(2)
//   moves[4] (2 each, national move id 1-354)
// Party blob = count(1) + 6 × 23 = TB_WIRE_PARTY_BYTES (139 B).
//
// This file must stay BYTE-IDENTICAL between the firmware and Pi trees.
#pragma once

#include "Gen1BattleEngine.h"

static constexpr uint8_t TB_WIRE_MON_BYTES   = 23;
static constexpr uint8_t TB_WIRE_PARTY_BYTES = 1 + 6 * TB_WIRE_MON_BYTES; // 139

inline void wireWr16(uint8_t *p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}
inline uint16_t wireRd16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

inline void packWireParty(const Gen1BattleEngine::WireParty &src,
                          uint8_t out[TB_WIRE_PARTY_BYTES])
{
    out[0] = src.count > 6 ? 6 : src.count;
    for (uint8_t i = 0; i < 6; ++i) {
        uint8_t *p = out + 1 + (size_t)i * TB_WIRE_MON_BYTES;
        const Gen1BattleEngine::WireMon &m = src.mons[i];
        wireWr16(p +  0, m.species);
        p[2] = m.level;
        wireWr16(p +  3, m.maxHp);
        wireWr16(p +  5, m.atk);
        wireWr16(p +  7, m.def);
        wireWr16(p +  9, m.spe);
        wireWr16(p + 11, m.spa);
        wireWr16(p + 13, m.spd);
        for (uint8_t k = 0; k < 4; ++k)
            wireWr16(p + 15 + k * 2, m.moves[k]);
    }
}

inline void unpackWireParty(const uint8_t in[TB_WIRE_PARTY_BYTES],
                            Gen1BattleEngine::WireParty &dst)
{
    dst = Gen1BattleEngine::WireParty();
    dst.count = in[0] > 6 ? 6 : in[0];
    for (uint8_t i = 0; i < 6; ++i) {
        const uint8_t *p = in + 1 + (size_t)i * TB_WIRE_MON_BYTES;
        Gen1BattleEngine::WireMon &m = dst.mons[i];
        m.species = wireRd16(p);
        if (m.species > 386) m.species = 0;   // reject garbage / future dex
        m.level = p[2];
        m.maxHp = wireRd16(p +  3);
        m.atk   = wireRd16(p +  5);
        m.def   = wireRd16(p +  7);
        m.spe   = wireRd16(p +  9);
        m.spa   = wireRd16(p + 11);
        m.spd   = wireRd16(p + 13);
        for (uint8_t k = 0; k < 4; ++k)
            m.moves[k] = wireRd16(p + 15 + k * 2);
    }
}

// Convert a Gen-1 save party into the neutral form: run the engine's own
// save-init (internal->dex, DV/stat-exp -> final stats) and extract. Using
// the exact numbers the engine would compute keeps the sender's engine and
// the receiver's engine byte-identical.
inline void gen1PartyToWireParty(const Gen1Party &src,
                                 Gen1BattleEngine::WireParty &out,
                                 uint8_t gen = 3)
{
    out = Gen1BattleEngine::WireParty();
    out.count = src.count > 6 ? 6 : src.count;
    for (uint8_t i = 0; i < out.count; ++i) {
        Gen1BattleEngine::BattlePoke tmp;
        Gen1BattleEngine::initBattlePokeFromSave(tmp, src.mons[i],
                                                 src.nicknames[i], gen);
        Gen1BattleEngine::battlePokeToWireMon(tmp, out.mons[i]);
    }
}
