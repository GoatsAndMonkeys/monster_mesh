#pragma once
// ── ParsedMon: neutral cross-gen save-reader output ─────────────────────────
// One canonical definition shared by the Gen 2 and Gen 3 .sav readers (and any
// future readers). Holds the NATIONAL dex number plus the mon's FINAL computed
// battle stats — generation-specific details (DVs/IVs, stat-exp/EVs, internal
// species indices) are resolved by the readers before filling this struct.

#include <stdint.h>

struct ParsedMon {
    uint16_t dex;                              // national dex 1-386, 0 = empty slot
    uint8_t  level;
    uint16_t maxHp, atk, def, spe, spa, spd;   // FINAL battle stats (computed)
    uint16_t moves[4];
    char     nickname[11];
};
