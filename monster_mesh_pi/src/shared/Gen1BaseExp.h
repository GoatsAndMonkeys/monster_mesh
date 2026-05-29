#pragma once
// ── Gen 1 base experience yield per species ──────────────────────────────────
// Source: pret/pokered data/pokemon/base_stats/*.asm
//
// Used in the XP-per-kill formula:   exp = (baseExp * level / 7) * trainerMult
// (trainerMult = 1.5 for trainer battles, 1.0 for wild encounters in Gen 1.
// Traded mons also get +50% in the real game; ignored here.)
//
// Indexed by Pokédex number (1-151). Slot 0 is a placeholder.

#include <stdint.h>

static constexpr uint8_t GEN1_BASE_EXP[152] = {
    0,
    // 1-10
     64, 141, 208,  65, 142, 209,  66, 143, 210,  53,
    // 11-20
     72, 160,  52,  71, 159,  55, 113, 172,  57, 116,
    // 21-30
     58, 162,  62, 147,  82, 122,  93, 163,  59, 117,
    // 31-40
    194,  60, 118, 195,  68, 129,  63, 178,  76, 109,
    // 41-50
     54, 171,  78, 132, 184,  70, 128,  75, 138,  81,
    // 51-60
    153,  69, 148,  80, 174,  74, 149,  91, 213,  77,
    // 61-70
    131, 185,  73, 145, 186,  88, 146, 193,  84, 151,
    // 71-80
    191, 105, 205,  86, 134, 177, 152, 192,  99, 164,
    // 81-90
     89, 161,  94,  96, 158, 100, 176,  90, 157,  97,
    // 91-100
    203,  95, 126, 190, 108, 102, 165, 115, 206, 103,
    // 101-110
    150,  98, 212,  87, 124, 139, 140, 127, 114, 173,
    // 111-120
    135, 204, 255, 166, 175,  83, 155, 111, 170, 106,
    // 121-130
    207, 136, 187, 137, 156, 167, 168, 211,  23, 214,
    // 131-140
    219,  61,  92, 196, 197, 198, 130, 120, 199, 119,
    // 141-151
    201, 202, 154, 215, 216, 217,  67, 144, 218, 220,
     64
};

// Compute Gen 1 XP yield for defeating a Pokemon.
//   dex          : Pokédex number (1-151), 0 returns 0
//   level        : level of the defeated Pokemon
//   isTrainer    : true for trainer battles, false for wild
//
// Formula from pokered's GainExperience routine:
//   xp = (baseExp * level / 7) * trainerMult
inline uint32_t gen1XpYield(uint8_t dex, uint8_t level, bool isTrainer)
{
    if (dex == 0 || dex >= 152) return 0;
    uint32_t baseExp = GEN1_BASE_EXP[dex];
    uint32_t xp      = baseExp * (uint32_t)level / 7;
    if (isTrainer) xp = (xp * 3) / 2;   // 1.5x for trainer battles
    return xp;
}
