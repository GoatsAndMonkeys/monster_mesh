// SPDX-License-Identifier: MIT
#pragma once

#include "BattlePacket.h"
#include "Gen1Species.h"
#include "PokemonData.h"
#include "WirePartyCodec.h"           // TB_WIRE_PARTY_BYTES, wireRd16, WireParty
#include "showdown_gen1_moves.h"      // gen1Move()
#include "showdown_gen3_moves.h"      // gen3Move()

#include <string.h>

// Validation and decoding for the party carried by the server-authoritative
// battle handshake.  Kept separate from MonsterMeshTextBattle so the
// untrusted-wire checks can be exercised in native tests without reaching into
// battle UI internals.
//
// TWO wire formats are validated here:
//   • The legacy V1 Gen-1 "partyMin" blob (109 B, Red/Blue internal species +
//     DV/stat-exp) carried by TEXT_BATTLE_PARTY_MIN and the MMB chunk path —
//     tbValidateParty / tbUnpackAndValidatePartyMin (UNCHANGED from the audit).
//   • The neutral cross-gen V2 WireParty blob (139 B, national dex 1-386 +
//     FINAL computed stats) carried by TEXT_BATTLE_CHALLENGE_V2 /
//     TEXT_BATTLE_ACCEPT_V2 — tbValidateWireParty / tbUnpackAndValidateWireParty.
//
// Both decode into a temporary and install only after full validation, so a
// malformed packet can never partially mutate staged/battle state.  Neither
// clamps a bad count/species into something acceptable — the whole payload is
// rejected.

enum class TbPartyValidationError : uint8_t {
    NONE = 0,
    SHORT_PAYLOAD,
    INVALID_COUNT,
    INVALID_SPECIES,
    INVALID_LEVEL,
    INVALID_MOVE,
};

// ── V1 Gen-1 internal-species party (legacy PARTY_MIN / MMB chunk path) ────────

static inline bool tbIsValidGen1InternalSpecies(uint8_t species)
{
    // gen1SpeciesName() is indexed by the Red/Blue internal species byte.
    // It returns "???" for 0, the MissingNo gaps, and values above 0xBE.
    const char *name = gen1SpeciesName(species);
    return name != nullptr && name[0] != '?';
}

static inline bool tbValidateParty(const Gen1Party &party,
                                   TbPartyValidationError *error = nullptr)
{
    auto fail = [&](TbPartyValidationError why) {
        if (error) *error = why;
        return false;
    };
    if (error) *error = TbPartyValidationError::NONE;
    if (party.count == 0 || party.count > 6)
        return fail(TbPartyValidationError::INVALID_COUNT);
    for (uint8_t i = 0; i < party.count; ++i) {
        const Gen1Pokemon &mon = party.mons[i];
        if (!tbIsValidGen1InternalSpecies(mon.species) ||
            party.species[i] != mon.species)
            return fail(TbPartyValidationError::INVALID_SPECIES);
        // The active-party wire level is the party-record level byte. Do not
        // let a nonzero PC/box-level byte mask a corrupted level 0.
        if (mon.level == 0 || mon.level > 100)
            return fail(TbPartyValidationError::INVALID_LEVEL);
        for (uint8_t k = 0; k < 4; ++k) {
            if (mon.moves[k] != 0 && gen1Move(mon.moves[k]) == nullptr)
                return fail(TbPartyValidationError::INVALID_MOVE);
        }
    }
    return true;
}

static inline bool tbUnpackAndValidatePartyMin(const uint8_t *in, size_t len,
                                               Gen1Party &dst,
                                               TbPartyValidationError *error = nullptr)
{
    auto fail = [&](TbPartyValidationError why) {
        if (error) *error = why;
        return false;
    };

    if (error) *error = TbPartyValidationError::NONE;
    if (!in || len < TB_PARTY_MIN_BYTES)
        return fail(TbPartyValidationError::SHORT_PAYLOAD);

    const uint8_t count = in[0];
    if (count == 0 || count > 6)
        return fail(TbPartyValidationError::INVALID_COUNT);

    // Decode into a temporary so a malformed packet cannot partially alter
    // a staged party before validation fails.
    Gen1Party decoded = {};
    decoded.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *p = in + 1 + (size_t)i * 18;
        Gen1Pokemon &m = decoded.mons[i];

        if (!tbIsValidGen1InternalSpecies(p[0]))
            return fail(TbPartyValidationError::INVALID_SPECIES);
        if (p[1] == 0 || p[1] > 100)
            return fail(TbPartyValidationError::INVALID_LEVEL);
        for (uint8_t k = 0; k < 4; ++k) {
            const uint8_t move = p[14 + k];
            if (move != 0 && gen1Move(move) == nullptr)
                return fail(TbPartyValidationError::INVALID_MOVE);
        }

        m.species  = p[0];
        m.level    = p[1];
        m.boxLevel = p[1];
        m.dvs[0]   = p[2];
        m.dvs[1]   = p[3];
        memcpy(m.hpExp,  p +  4, 2);
        memcpy(m.atkExp, p +  6, 2);
        memcpy(m.defExp, p +  8, 2);
        memcpy(m.spdExp, p + 10, 2);
        memcpy(m.spcExp, p + 12, 2);
        memcpy(m.moves,  p + 14, 4);
        for (uint8_t k = 0; k < 4; ++k) {
            const Gen1MoveData *md = gen1Move(m.moves[k]);
            m.pp[k] = md ? md->pp : 0;
        }
    }

    for (uint8_t i = 0; i < 7; ++i)
        decoded.species[i] = (i < count) ? decoded.mons[i].species : 0xFF;

    if (!tbValidateParty(decoded, error)) return false;
    dst = decoded;
    return true;
}

// ── V2 neutral cross-gen WireParty (CHALLENGE_V2 / ACCEPT_V2 path) ─────────────

// National dex 1-386.  0 is the empty-slot sentinel and is never valid in an
// active slot; anything above 386 is garbage or a future generation.
static inline bool tbIsValidNationalSpecies(uint16_t species)
{
    return species >= 1 && species <= 386;
}

// A nonzero move id must resolve under the negotiated generation's move table.
// Gen 1 uses the Gen-1 table; Gen 2/3 use the Gen-3 national superset (which
// contains every Gen-1/2 national move id), so a legitimate cross-gen party is
// never rejected while true garbage (e.g. 0x9999) still fails.
static inline bool tbIsValidWireMove(uint16_t move, uint8_t gen)
{
    if (move == 0) return true;   // empty move slot
    return (gen <= 1 ? gen1Move(move) : gen3Move(move)) != nullptr;
}

// Validate an already-decoded WireParty.  `gen` is the negotiated generation
// (1, 2, or 3); it only selects the move table.  Does not mutate anything.
static inline bool tbValidateWireParty(const Gen1BattleEngine::WireParty &party,
                                       uint8_t gen = 3,
                                       TbPartyValidationError *error = nullptr)
{
    auto fail = [&](TbPartyValidationError why) {
        if (error) *error = why;
        return false;
    };
    if (error) *error = TbPartyValidationError::NONE;
    if (party.count == 0 || party.count > 6)
        return fail(TbPartyValidationError::INVALID_COUNT);
    for (uint8_t i = 0; i < party.count; ++i) {
        const Gen1BattleEngine::WireMon &m = party.mons[i];
        if (!tbIsValidNationalSpecies(m.species))
            return fail(TbPartyValidationError::INVALID_SPECIES);
        if (m.level == 0 || m.level > 100)
            return fail(TbPartyValidationError::INVALID_LEVEL);
        for (uint8_t k = 0; k < 4; ++k) {
            if (!tbIsValidWireMove(m.moves[k], gen))
                return fail(TbPartyValidationError::INVALID_MOVE);
        }
    }
    return true;
}

// Decode a raw 139-byte WireParty blob into `dst`, but ONLY after it fully
// validates.  Reads the raw wire fields directly (not unpackWireParty, which
// clamps species>386 to 0 and count>6 to 6 — clamping is exactly what the
// audit forbids here).  On any failure `dst` is left untouched.
static inline bool tbUnpackAndValidateWireParty(const uint8_t *in, size_t len,
                                                Gen1BattleEngine::WireParty &dst,
                                                uint8_t gen = 3,
                                                TbPartyValidationError *error = nullptr)
{
    auto fail = [&](TbPartyValidationError why) {
        if (error) *error = why;
        return false;
    };
    if (error) *error = TbPartyValidationError::NONE;
    if (!in || len < TB_WIRE_PARTY_BYTES)
        return fail(TbPartyValidationError::SHORT_PAYLOAD);

    const uint8_t count = in[0];
    if (count == 0 || count > 6)
        return fail(TbPartyValidationError::INVALID_COUNT);

    Gen1BattleEngine::WireParty decoded = Gen1BattleEngine::WireParty();
    decoded.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *p = in + 1 + (size_t)i * TB_WIRE_MON_BYTES;
        Gen1BattleEngine::WireMon &m = decoded.mons[i];

        const uint16_t species = wireRd16(p + 0);
        if (!tbIsValidNationalSpecies(species))
            return fail(TbPartyValidationError::INVALID_SPECIES);
        const uint8_t level = p[2];
        if (level == 0 || level > 100)
            return fail(TbPartyValidationError::INVALID_LEVEL);
        for (uint8_t k = 0; k < 4; ++k) {
            if (!tbIsValidWireMove(wireRd16(p + 15 + k * 2), gen))
                return fail(TbPartyValidationError::INVALID_MOVE);
        }

        m.species = species;
        m.level   = level;
        m.maxHp   = wireRd16(p +  3);
        m.atk     = wireRd16(p +  5);
        m.def     = wireRd16(p +  7);
        m.spe     = wireRd16(p +  9);
        m.spa     = wireRd16(p + 11);
        m.spd     = wireRd16(p + 13);
        for (uint8_t k = 0; k < 4; ++k)
            m.moves[k] = wireRd16(p + 15 + k * 2);
    }

    dst = decoded;
    return true;
}
