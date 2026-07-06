#pragma once
// ── Gen 3 (Ruby/Sapphire/Emerald/FireRed/LeafGreen) .sav party reader ───────
// Self-contained parser for a 128KB (131072-byte) GBA battery save. Reads the
// active party: species (mapped internal->national dex), level, final battle
// stats, and the 4 move IDs of each mon.
//
// Spec / verification source:
//   ~/.claude/projects/-Users-goatsandmonkeys-Documents-pokemesh/memory/
//       reference_gen3_sav_format.md
//   (cross-checked against Bulbapedia + pret/pokeemerald). All multi-byte
//   fields are LITTLE-ENDIAN.
//
// NEW file — does not modify any existing reader. ParsedMon is shared with the
// Gen 2 reader; see the include guard note below.

#include <stdint.h>
#include <string.h>
#include "showdown_gen3_basestats.h"   // Gen3BaseStats / GEN3_BASE_STATS[387]

// ── Shared parsed-party record ──────────────────────────────────────────────
// This MUST match the layout the Gen 2 reader uses. It is guarded so both
// readers can be included in the same translation unit without a redefinition
// error. NOTE FOR DEDUPE: if the Gen 2 reader defines ParsedMon under a
// different guard macro, unify them (e.g. move this into a tiny shared
// "ParsedMon.h") so the two definitions cannot silently diverge.
#ifndef PARSEDMON_H
#define PARSEDMON_H
struct ParsedMon {
    uint16_t dex;                              // national dex 1-386, 0 = empty
    uint8_t  level;
    uint16_t maxHp, atk, def, spe, spa, spd;   // FINAL battle stats
    uint16_t moves[4];
    char     nickname[11];
};
#endif  // PARSEDMON_H

// ── little-endian readers ───────────────────────────────────────────────────
static inline uint16_t gen3RD16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t gen3RD32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ── internal species index -> national dex ──────────────────────────────────
// internal 1-251 = national 1-251 (identity). internal 252-276 are unused
// placeholder ("?") slots -> 0. internal 277-411 use the permutation below
// (national 252-386). Table transcribed verbatim from the spec.
static const uint16_t GEN3_INTERNAL_277[135] = {
    /*277-300*/ 252,253,254,255,256,257,258,259,260,261,262,263,
                264,265,266,267,268,269,270,271,272,273,274,275,
    /*301-*/    290,291,292,276,277,285,286,327,278,279,283,284,
                320,321,300,301,352,343,344,299,324,302,339,340,
                370,341,342,349,350,318,319,328,329,330,296,297,
                309,310,322,323,363,364,365,331,332,361,362,337,
                338,298,325,326,311,312,303,307,308,333,334,360,
                355,356,315,287,288,289,316,317,357,293,294,295,
                366,367,368,359,353,354,336,335,369,304,305,306,
                351,313,314,345,346,347,348,280,281,282,371,372,
                373,374,375,376,377,378,379,382,383,384,380,381,
    /*-411*/    385,386,358
};

static inline uint16_t gen3InternalToNational(uint16_t internal) {
    if (internal >= 1 && internal <= 251) return internal;   // identity
    if (internal >= 252 && internal <= 276) return 0;         // unused
    if (internal >= 277 && internal <= 411) return GEN3_INTERNAL_277[internal - 277];
    return 0;
}

// ── PID % 24 substructure permutation table ─────────────────────────────────
// G=Growth, A=Attacks, E=EVs, M=Misc. The letter's position in the string is
// which 12-byte block (0-3) that substructure occupies in the 48-byte payload.
static const char GEN3_ORDER[24][5] = {
    "GAEM","GAME","GEAM","GEMA","GMAE","GMEA",
    "AGEM","AGME","AEGM","AEMG","AMGE","AMEG",
    "EGAM","EGMA","EAGM","EAMG","EMGA","EMAG",
    "MGAE","MGEA","MAGE","MAEG","MEGA","MEAG"
};

static inline int gen3BlockPos(const char *perm, char letter) {
    for (int i = 0; i < 4; i++) if (perm[i] == letter) return i;
    return 0;  // unreachable for a well-formed table
}

// ── final-stat formulas (Gen 3, no natures — nature multiplier = 1.0) ───────
//   other = floor((2*base + IV + floor(EV/4)) * level / 100) + 5
//   HP    = floor((2*baseHP + IV_HP + floor(EV_HP/4)) * level / 100) + level + 10
static inline uint16_t gen3CalcOther(uint8_t base, uint8_t iv, uint8_t ev, uint8_t level) {
    uint32_t v = ((uint32_t)(2 * base + iv + ev / 4) * level) / 100 + 5;
    return (uint16_t)v;
}
static inline uint16_t gen3CalcHP(uint8_t base, uint8_t iv, uint8_t ev, uint8_t level) {
    uint32_t v = ((uint32_t)(2 * base + iv + ev / 4) * level) / 100 + level + 10;
    return (uint16_t)v;
}

// ── locate section 1 (party) in the current save block ──────────────────────
// Sections are rotated across 14 physical slots per block and routed by footer
// sectionID. Pick the block (A@0x0000 / B@0xE000) with the larger save index,
// counting only slots whose signature == 0x08012025. Returns a pointer to the
// 4096-byte section whose ID == 1, or nullptr.
static const uint8_t *gen3FindSection1(const uint8_t *save) {
    static const uint32_t kBlockBase[2] = { 0x00000u, 0x0E000u };
    static const uint32_t kSig = 0x08012025u;

    int      chosenBlk   = -1;
    uint32_t chosenIndex = 0;
    for (int blk = 0; blk < 2; blk++) {
        bool     valid = false;
        uint32_t idx   = 0;
        for (int s = 0; s < 14; s++) {
            const uint8_t *sec = save + kBlockBase[blk] + (uint32_t)s * 0x1000u;
            if (gen3RD32(sec + 0x0FF8) != kSig) continue;
            idx   = gen3RD32(sec + 0x0FFC);  // all sections in a block share it
            valid = true;
        }
        if (!valid) continue;
        // Never wrap-compare; simply take the larger index. Prefer block B on tie.
        if (chosenBlk < 0 || idx >= chosenIndex) {
            chosenBlk   = blk;
            chosenIndex = idx;
        }
    }
    if (chosenBlk < 0) return nullptr;

    for (int s = 0; s < 14; s++) {
        const uint8_t *sec = save + kBlockBase[chosenBlk] + (uint32_t)s * 0x1000u;
        if (gen3RD32(sec + 0x0FF8) != kSig) continue;
        if (gen3RD16(sec + 0x0FF4) == 1) return sec;
    }
    return nullptr;
}

// ── decode a single 100-byte mon record into a ParsedMon ────────────────────
// Returns true if the slot holds a real, decodable Pokemon.
static inline bool gen3DecodeMon(const uint8_t *rec, ParsedMon &m) {
    uint32_t pid   = gen3RD32(rec + 0x00);
    uint32_t otid  = gen3RD32(rec + 0x04);
    uint8_t  flags = rec[0x13];
    bool hasSpecies = (flags & 0x02) != 0;
    bool badEgg     = (flags & 0x01) != 0;
    if (pid == 0 || !hasSpecies || badEgg) return false;

    // Decrypt the 48-byte (12-word) substructure payload.
    uint32_t key = otid ^ pid;
    uint8_t  dec[48];
    for (int w = 0; w < 12; w++) {
        uint32_t v = gen3RD32(rec + 0x20 + w * 4) ^ key;
        dec[w * 4 + 0] = (uint8_t)(v & 0xFF);
        dec[w * 4 + 1] = (uint8_t)((v >> 8) & 0xFF);
        dec[w * 4 + 2] = (uint8_t)((v >> 16) & 0xFF);
        dec[w * 4 + 3] = (uint8_t)((v >> 24) & 0xFF);
    }

    const char *perm = GEN3_ORDER[pid % 24];
    const uint8_t *growth  = dec + gen3BlockPos(perm, 'G') * 12;
    const uint8_t *attacks = dec + gen3BlockPos(perm, 'A') * 12;
    const uint8_t *evsBlk  = dec + gen3BlockPos(perm, 'E') * 12;
    const uint8_t *misc    = dec + gen3BlockPos(perm, 'M') * 12;

    uint16_t internalSpecies = gen3RD16(growth + 0x00);
    uint16_t national = gen3InternalToNational(internalSpecies);
    if (national == 0 || national > 386) return false;

    // Moves (0-354).
    for (int i = 0; i < 4; i++) m.moves[i] = gen3RD16(attacks + i * 2);

    // EVs are plain bytes: HP,Atk,Def,Spe,SpA,SpD.
    uint8_t evHP  = evsBlk[0], evAtk = evsBlk[1], evDef = evsBlk[2];
    uint8_t evSpe = evsBlk[3], evSpA = evsBlk[4], evSpD = evsBlk[5];

    // IVs are 5 bits each in the Misc IV word: HP,Atk,Def,Spe,SpA,SpD.
    uint32_t ivWord = gen3RD32(misc + 0x04);
    uint8_t ivHP  = (uint8_t)(ivWord        & 31);
    uint8_t ivAtk = (uint8_t)((ivWord >> 5)  & 31);
    uint8_t ivDef = (uint8_t)((ivWord >> 10) & 31);
    uint8_t ivSpe = (uint8_t)((ivWord >> 15) & 31);
    uint8_t ivSpA = (uint8_t)((ivWord >> 20) & 31);
    uint8_t ivSpD = (uint8_t)((ivWord >> 25) & 31);

    uint8_t level = rec[0x54];  // party-stats level (plaintext)

    const Gen3BaseStats &b = GEN3_BASE_STATS[national];
    m.dex   = national;
    m.level = level;
    m.maxHp = gen3CalcHP(b.hp,  ivHP,  evHP,  level);
    m.atk   = gen3CalcOther(b.atk, ivAtk, evAtk, level);
    m.def   = gen3CalcOther(b.def, ivDef, evDef, level);
    m.spe   = gen3CalcOther(b.spe, ivSpe, evSpe, level);
    m.spa   = gen3CalcOther(b.spa, ivSpA, evSpA, level);
    m.spd   = gen3CalcOther(b.spd, ivSpD, evSpD, level);
    m.nickname[0] = '\0';  // Gen 3 text table is proprietary; left blank
    return true;
}

// ── try one (count,party) offset pair within section 1 ──────────────────────
// Returns the number of valid mons decoded into out (compacted), or -1 if the
// count field is out of range (a strong signal the offset pair is wrong).
static inline int gen3TryParty(const uint8_t *sec1, uint16_t countOff,
                               uint16_t partyOff, ParsedMon out[6]) {
    uint32_t count = gen3RD32(sec1 + countOff);
    if (count > 6) return -1;

    int written = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *rec = sec1 + partyOff + i * 100u;
        ParsedMon m;
        memset(&m, 0, sizeof(m));
        if (gen3DecodeMon(rec, m)) out[written++] = m;
    }
    return written;
}

// ── public entry point ──────────────────────────────────────────────────────
// Parse a Gen 3 .sav (131072-byte buffer). Auto-detects the current save block,
// routes sections by footer ID, and reads the party from section 1. Handles
// RSE (party @0x0238 within section 1) vs FR/LG (@0x0038) by trying both offset
// pairs and keeping whichever decodes more valid Pokemon. Fills out[0..5] and
// returns the party count (0-6).
static inline uint8_t gen3ReadParty(const uint8_t *save, ParsedMon out[6]) {
    for (int i = 0; i < 6; i++) memset(&out[i], 0, sizeof(ParsedMon));
    if (!save) return 0;

    const uint8_t *sec1 = gen3FindSection1(save);
    if (!sec1) return 0;

    ParsedMon rse[6], frlg[6];
    for (int i = 0; i < 6; i++) { memset(&rse[i], 0, sizeof(ParsedMon));
                                  memset(&frlg[i], 0, sizeof(ParsedMon)); }

    int nRSE  = gen3TryParty(sec1, 0x0234, 0x0238, rse);   // Ruby/Sapphire/Emerald
    int nFRLG = gen3TryParty(sec1, 0x0034, 0x0038, frlg);  // FireRed/LeafGreen

    // Keep the layout that yielded more valid mons; prefer RSE on a tie.
    const ParsedMon *pick = nullptr;
    int n = 0;
    if (nRSE >= nFRLG && nRSE > 0)      { pick = rse;  n = nRSE; }
    else if (nFRLG > 0)                 { pick = frlg; n = nFRLG; }
    if (!pick) return 0;

    if (n > 6) n = 6;
    for (int i = 0; i < n; i++) out[i] = pick[i];
    return (uint8_t)n;
}
