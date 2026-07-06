#pragma once
// ── Pokemon Gold/Silver/Crystal (Gen 2) .sav Party Reader ──────────────────
// Reads the party (species, level, moves, nickname) from a 32KB GSC SRAM
// buffer and computes each mon's FINAL battle stats.
//
// Spec / offsets: reference_gen2_sav_format.md (verified vs Bulbapedia +
// pokecrystal). Multi-byte ints in the mon struct are BIG-ENDIAN (like Gen 1).
// In Gen 2 the struct species byte IS the national dex number (no internal→dex
// table like Gen 1). Stat formula is identical to Gen 1; only difference is the
// Special stat is split into Sp.Atk / Sp.Def sharing ONE Special DV + one
// Special stat-exp.
//
// Helper formulas (readBE16/24, isqrt16, calcStat/calcStatHP, gen1CharToAscii,
// HP-DV derivation) are copied inline from DaycareSavPatcher.h to keep this
// header self-contained and free of duplicate-symbol risk. They MUST match the
// Gen 1 versions verbatim.
//
// Base stats come from showdown_gen3_basestats.h (GEN3_BASE_STATS, indexed by
// national dex), which already carries split spa/spd.
//
// NEW standalone file — does not modify any existing source.

#include <stdint.h>
#include <string.h>
#include "showdown_gen3_basestats.h"  // Gen3BaseStats, GEN3_BASE_STATS[387]

// ── Party base offsets (English) ───────────────────────────────────────────
// Gold/Silver and Crystal differ. Layout: count(1) | species list(7, 0xFF term)
// | party data(6×48) ... | nicknames(6×11).
struct Gen2SavLayout {
    uint16_t count;
    uint16_t partyData;
    uint16_t nicknames;
};

static constexpr Gen2SavLayout GEN2_LAYOUT_GS      = { 0x288A, 0x2892, 0x29F4 };
static constexpr Gen2SavLayout GEN2_LAYOUT_CRYSTAL = { 0x2865, 0x286D, 0x29CF };

static constexpr uint8_t  GEN2_MON_SIZE          = 48;
static constexpr uint8_t  GEN2_NAME_SIZE         = 11;
static constexpr uint8_t  GEN2_STRING_TERMINATOR = 0x50;

// ── Offsets within the 48-byte Gen 2 mon struct ────────────────────────────
static constexpr uint8_t G2_SPECIES  = 0x00;  // 1 byte = national dex
static constexpr uint8_t G2_MOVES     = 0x02;  // moves[4]
static constexpr uint8_t G2_HP_EV      = 0x0B;  // 2 bytes BE (stat exp)
static constexpr uint8_t G2_ATK_EV     = 0x0D;  // 2 bytes BE
static constexpr uint8_t G2_DEF_EV     = 0x0F;  // 2 bytes BE
static constexpr uint8_t G2_SPD_EV     = 0x11;  // 2 bytes BE (Speed stat exp)
static constexpr uint8_t G2_SPC_EV     = 0x13;  // 2 bytes BE (Special stat exp)
static constexpr uint8_t G2_DVS        = 0x15;  // 2 bytes packed DVs
static constexpr uint8_t G2_LEVEL      = 0x1F;  // 1 byte (authoritative)

// ── Result struct ──────────────────────────────────────────────────────────
struct ParsedMon {
    uint16_t dex;                              // national dex 1-386, 0 = empty slot
    uint8_t  level;
    uint16_t maxHp, atk, def, spe, spa, spd;   // FINAL battle stats (computed)
    uint16_t moves[4];
    char     nickname[11];
};

// ── Inlined helpers (copied verbatim from DaycareSavPatcher.h) ──────────────

static inline uint16_t gen2ReadBE16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

// Integer square root for stat-exp bonus. (from DaycareSavPatcher::isqrt16)
static inline uint16_t gen2Isqrt16(uint16_t n) {
    if (n == 0) return 0;
    uint16_t x = n;
    uint16_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

// Gen 1/2 HP stat. (from DaycareSavPatcher::calcStatHP)
static inline uint16_t gen2CalcStatHP(uint8_t base, uint8_t dv, uint16_t statExp,
                                      uint8_t level) {
    uint16_t sqrtExp = gen2Isqrt16(statExp);
    if ((uint32_t)sqrtExp * sqrtExp < statExp) sqrtExp++;  // ceil(sqrt)
    uint16_t evBonus = sqrtExp / 4;
    return ((uint16_t)((base + dv) * 2 + evBonus) * level / 100) + level + 10;
}

// Gen 1/2 non-HP stat. (from DaycareSavPatcher::calcStat)
static inline uint16_t gen2CalcStat(uint8_t base, uint8_t dv, uint16_t statExp,
                                    uint8_t level) {
    uint16_t sqrtExp = gen2Isqrt16(statExp);
    if ((uint32_t)sqrtExp * sqrtExp < statExp) sqrtExp++;  // ceil(sqrt)
    uint16_t evBonus = sqrtExp / 4;
    return ((uint16_t)((base + dv) * 2 + evBonus) * level / 100) + 5;
}

// Gen 1/2 text encoding → ASCII. (from DaycareSavPatcher::gen1CharToAscii)
// Same encoding table + 0x50 terminator applies to Gen 2.
static inline char gen2CharToAscii(uint8_t c) {
    if (c >= 0x80 && c <= 0x99) return 'A' + (c - 0x80);  // A-Z
    if (c >= 0xA0 && c <= 0xB9) return 'a' + (c - 0xA0);  // a-z
    if (c >= 0xF6 && c <= 0xFF) return '0' + (c - 0xF6);  // 0-9
    if (c == 0x7F) return ' ';
    if (c == 0xE8) return '.';
    if (c == 0xE3) return '-';
    if (c == 0xEF) return '\'';
    if (c == 0xF4) return ',';
    if (c == 0xF3) return '/';
    if (c == 0x50) return '\0';  // string terminator
    return '?';
}

// ── Reader ─────────────────────────────────────────────────────────────────
// Parse a GSC .sav (32KB SRAM buffer). `crystal` selects Crystal vs Gold/Silver
// offsets. Fills out[0..5], returns party count (0-6). Computes final stats from
// base(GEN3_BASE_STATS[dex]) + DVs + stat-exp + level using the Gen 1/2 formula.
// Special: spa uses base.spa, spd uses base.spd, both with the single Special DV
// and Special stat-exp.
static inline uint8_t gen2ReadParty(const uint8_t *sram, bool crystal,
                                    ParsedMon out[6]) {
    const Gen2SavLayout &L = crystal ? GEN2_LAYOUT_CRYSTAL : GEN2_LAYOUT_GS;

    // Zero the output so empty/skipped slots are well-defined.
    for (uint8_t i = 0; i < 6; i++) {
        memset(&out[i], 0, sizeof(ParsedMon));
    }

    uint8_t count = sram[L.count];
    if (count > 6) count = 6;

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t *pkm  = &sram[L.partyData + i * GEN2_MON_SIZE];
        ParsedMon     &mon  = out[i];

        // Species = national dex directly. Egg (0xFD) or out-of-range → empty.
        uint8_t species = pkm[G2_SPECIES];
        if (species < 1 || species > 251) {
            mon.dex = 0;  // empty / egg — leave slot zeroed
            continue;
        }
        mon.dex   = species;
        mon.level = pkm[G2_LEVEL];

        // Moves (Gen 2 range 1-251, 0 = empty) → uint16.
        for (int m = 0; m < 4; m++) mon.moves[m] = pkm[G2_MOVES + m];

        // Nickname (Gen 1/2 encoding → ASCII, 0x50 terminator).
        const uint8_t *nickRaw = &sram[L.nicknames + i * GEN2_NAME_SIZE];
        int j = 0;
        for (; j < 10; j++) {
            if (nickRaw[j] == GEN2_STRING_TERMINATOR) break;
            mon.nickname[j] = gen2CharToAscii(nickRaw[j]);
        }
        mon.nickname[j] = '\0';

        // ── DVs ──────────────────────────────────────────────────────────
        // byte0x15: hi=AtkDV lo=DefDV. byte0x16: hi=SpeedDV lo=SpecialDV.
        uint8_t atkDV   = (pkm[G2_DVS]     >> 4) & 0x0F;
        uint8_t defDV   =  pkm[G2_DVS]           & 0x0F;
        uint8_t speedDV = (pkm[G2_DVS + 1] >> 4) & 0x0F;
        uint8_t spcDV   =  pkm[G2_DVS + 1]       & 0x0F;
        uint8_t hpDV    = ((atkDV & 1) << 3) | ((defDV & 1) << 2) |
                          ((speedDV & 1) << 1) | (spcDV & 1);

        // ── Stat experience (all 2-byte BE) ──────────────────────────────
        uint16_t hpEV    = gen2ReadBE16(&pkm[G2_HP_EV]);
        uint16_t atkEV   = gen2ReadBE16(&pkm[G2_ATK_EV]);
        uint16_t defEV   = gen2ReadBE16(&pkm[G2_DEF_EV]);
        uint16_t speedEV = gen2ReadBE16(&pkm[G2_SPD_EV]);
        uint16_t spcEV   = gen2ReadBE16(&pkm[G2_SPC_EV]);

        // ── Base stats (national dex). Gen3BaseStats: spa=Sp.Atk, spd=Sp.Def,
        //    spe=Speed. species is validated 1-251, always in bounds of
        //    GEN3_BASE_STATS[387]. ───────────────────────────────────────────
        const Gen3BaseStats &base = GEN3_BASE_STATS[species];

        // ── Final stats (Gen 1/2 formula) ────────────────────────────────
        mon.maxHp = gen2CalcStatHP(base.hp,  hpDV,    hpEV,    mon.level);
        mon.atk   = gen2CalcStat  (base.atk, atkDV,   atkEV,   mon.level);
        mon.def   = gen2CalcStat  (base.def, defDV,   defEV,   mon.level);
        mon.spe   = gen2CalcStat  (base.spe, speedDV, speedEV, mon.level);
        // Special split: both stats share the single Special DV + stat-exp.
        mon.spa   = gen2CalcStat  (base.spa, spcDV,   spcEV,   mon.level);
        mon.spd   = gen2CalcStat  (base.spd, spcDV,   spcEV,   mon.level);
    }

    return count;
}
