#pragma once
// ── Pokemon Red/Blue SRAM Patcher for Daycare ──────────────────────────────
// Reads party data from Game Boy SRAM on check-in (species, level, nickname,
// experience). On check-out, writes back gained XP + levels, recalculates
// stats, and fixes the save checksum.
//
// All offsets are for Pokemon Red/Blue/Yellow (English).
// NOT wired into build yet — standalone for validation.

#include <Arduino.h>
#include <string.h>

// ── SRAM layout constants ──────────────────────────────────────────────────

static constexpr uint16_t SAV_PARTY_OFFSET      = 0x2F2C;
static constexpr uint16_t SAV_PARTY_COUNT        = 0x2F2C;  // 1 byte
static constexpr uint16_t SAV_SPECIES_LIST       = 0x2F2D;  // 7 bytes
static constexpr uint16_t SAV_POKEMON_DATA       = 0x2F34;  // 44 bytes x 6
static constexpr uint16_t SAV_OT_NAMES           = 0x303C;  // 11 bytes x 6
static constexpr uint16_t SAV_NICKNAMES          = 0x307E;  // 11 bytes x 6

static constexpr uint16_t SAV_CHECKSUM_OFFSET    = 0x3523;
static constexpr uint16_t SAV_CHECKSUM_START     = 0x2598;
static constexpr uint16_t SAV_CHECKSUM_END       = 0x3522;

static constexpr uint8_t  SAV_POKEMON_SIZE       = 44;      // party pokemon struct
static constexpr uint8_t  SAV_NAME_SIZE          = 11;
static constexpr uint8_t  SAV_STRING_TERMINATOR  = 0x50;

// ── Offsets within the 44-byte Pokemon struct ──────────────────────────────

static constexpr uint8_t PKM_SPECIES     = 0x00;  // 1 byte (internal index!)
static constexpr uint8_t PKM_CURRENT_HP  = 0x01;  // 2 bytes BE
static constexpr uint8_t PKM_LEVEL_PC    = 0x03;  // 1 byte (box level)
static constexpr uint8_t PKM_STATUS      = 0x04;  // 1 byte
static constexpr uint8_t PKM_TYPE1       = 0x05;  // 1 byte
static constexpr uint8_t PKM_TYPE2       = 0x06;  // 1 byte
static constexpr uint8_t PKM_EXP         = 0x0E;  // 3 bytes BE
static constexpr uint8_t PKM_HP_EV       = 0x11;  // 2 bytes BE
static constexpr uint8_t PKM_ATK_EV      = 0x13;  // 2 bytes BE
static constexpr uint8_t PKM_DEF_EV      = 0x15;  // 2 bytes BE
static constexpr uint8_t PKM_SPD_EV      = 0x17;  // 2 bytes BE
static constexpr uint8_t PKM_SPC_EV      = 0x19;  // 2 bytes BE
static constexpr uint8_t PKM_DVS         = 0x1B;  // 2 bytes (packed IVs)
static constexpr uint8_t PKM_LEVEL_PARTY = 0x21;  // 1 byte (authoritative)
static constexpr uint8_t PKM_MAX_HP      = 0x22;  // 2 bytes BE
static constexpr uint8_t PKM_ATTACK      = 0x24;  // 2 bytes BE
static constexpr uint8_t PKM_DEFENSE     = 0x26;  // 2 bytes BE
static constexpr uint8_t PKM_SPEED       = 0x28;  // 2 bytes BE
static constexpr uint8_t PKM_SPECIAL     = 0x2A;  // 2 bytes BE

// ── Experience growth rates ────────────────────────────────────────────────

enum GrowthRate : uint8_t {
    GROWTH_MEDIUM_FAST = 0,  // n^3
    GROWTH_FAST        = 1,  // 4n^3 / 5
    GROWTH_MEDIUM_SLOW = 2,  // 1.2n^3 - 15n^2 + 100n - 140
    GROWTH_SLOW        = 3,  // 5n^3 / 4
};

// Growth rate for each Pokedex number (1-151)
// Source: pret/pokered base_stats/*.asm + Bulbapedia experience type list
// 0=MediumFast(n^3), 1=Fast(4n^3/5), 2=MediumSlow(1.2n^3-15n^2+100n-140), 3=Slow(5n^3/4)
static const uint8_t speciesGrowthRate[152] = {
    0,                          // 0 = unused
    // 1-10: Bulbasaur, Ivysaur, Venusaur, Charmander, Charmeleon, Charizard, Squirtle, Wartortle, Blastoise, Caterpie
    2, 2, 2, 2, 2, 2, 2, 2, 2, 0,
    // 11-20: Metapod, Butterfree, Weedle, Kakuna, Beedrill, Pidgey, Pidgeotto, Pidgeot, Rattata, Raticate
    0, 0, 0, 0, 0, 2, 2, 2, 0, 0,
    // 21-30: Spearow, Fearow, Ekans, Arbok, Pikachu, Raichu, Sandshrew, Sandslash, NidoranF, Nidorina
    0, 0, 0, 0, 0, 0, 0, 0, 2, 2,
    // 31-40: Nidoqueen, NidoranM, Nidorino, Nidoking, Clefairy, Clefable, Vulpix, Ninetales, Jigglypuff, Wigglytuff
    2, 2, 2, 2, 1, 1, 0, 0, 1, 1,
    // 41-50: Zubat, Golbat, Oddish, Gloom, Vileplume, Paras, Parasect, Venonat, Venomoth, Diglett
    0, 0, 2, 2, 2, 0, 0, 0, 0, 0,
    // 51-60: Dugtrio, Meowth, Persian, Psyduck, Golduck, Mankey, Primeape, Growlithe, Arcanine, Poliwag
    0, 0, 0, 0, 0, 0, 0, 3, 3, 2,
    // 61-70: Poliwhirl, Poliwrath, Abra, Kadabra, Alakazam, Machop, Machoke, Machamp, Bellsprout, Weepinbell
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    // 71-80: Victreebel, Tentacool, Tentacruel, Geodude, Graveler, Golem, Ponyta, Rapidash, Slowpoke, Slowbro
    2, 3, 3, 2, 2, 2, 0, 0, 0, 0,
    // 81-90: Magnemite, Magneton, Farfetch'd, Doduo, Dodrio, Seel, Dewgong, Grimer, Muk, Shellder
    0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
    // 91-100: Cloyster, Gastly, Haunter, Gengar, Onix, Drowzee, Hypno, Krabby, Kingler, Voltorb
    3, 2, 2, 2, 0, 0, 0, 0, 0, 0,
    // 101-110: Electrode, Exeggcute, Exeggutor, Cubone, Marowak, Hitmonlee, Hitmonchan, Lickitung, Koffing, Weezing
    0, 3, 3, 0, 0, 0, 0, 0, 0, 0,
    // 111-120: Rhyhorn, Rhydon, Chansey, Tangela, Kangaskhan, Horsea, Seadra, Goldeen, Seaking, Staryu
    3, 3, 1, 0, 0, 0, 0, 0, 0, 3,
    // 121-130: Starmie, Mr. Mime, Scyther, Jynx, Electabuzz, Magmar, Pinsir, Tauros, Magikarp, Gyarados
    3, 0, 0, 0, 0, 0, 3, 3, 3, 3,
    // 131-140: Lapras, Ditto, Eevee, Vaporeon, Jolteon, Flareon, Porygon, Omanyte, Omastar, Kabuto
    3, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 141-151: Kabutops, Aerodactyl, Snorlax, Articuno, Zapdos, Moltres, Dratini, Dragonair, Dragonite, Mewtwo, Mew
    0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2
};

// ── Base stats for stat recalculation (HP, Atk, Def, Spd, Spc) ────────────
// Indexed by Pokedex number (1-151). 5 bytes each.

struct BaseStats {
    uint8_t hp, atk, def, spd, spc;
};

static const BaseStats speciesBaseStats[152] = {
    {0,0,0,0,0},           // 0 = unused
    {45,49,49,45,65},      // 1 Bulbasaur
    {60,62,63,60,80},      // 2 Ivysaur
    {80,82,83,80,100},     // 3 Venusaur
    {39,52,43,65,50},      // 4 Charmander
    {58,64,58,80,65},      // 5 Charmeleon
    {78,84,78,100,85},     // 6 Charizard
    {44,48,65,43,50},      // 7 Squirtle
    {59,63,80,58,65},      // 8 Wartortle
    {79,83,100,78,85},     // 9 Blastoise
    {45,30,35,45,20},      // 10 Caterpie
    {50,20,55,30,25},      // 11 Metapod
    {60,45,50,70,80},      // 12 Butterfree
    {40,35,30,50,20},      // 13 Weedle
    {45,25,50,35,25},      // 14 Kakuna
    {65,80,40,75,45},      // 15 Beedrill
    {40,45,40,56,35},      // 16 Pidgey
    {63,60,55,71,50},      // 17 Pidgeotto
    {83,80,75,91,70},      // 18 Pidgeot
    {30,56,35,72,25},      // 19 Rattata
    {55,81,60,97,50},      // 20 Raticate
    {40,60,30,70,31},      // 21 Spearow
    {65,90,65,100,61},     // 22 Fearow
    {35,60,44,55,40},      // 23 Ekans
    {60,85,69,80,65},      // 24 Arbok
    {35,55,30,90,50},      // 25 Pikachu
    {60,90,55,100,90},     // 26 Raichu
    {50,75,85,40,30},      // 27 Sandshrew
    {75,100,110,65,55},    // 28 Sandslash
    {55,47,52,41,40},      // 29 NidoranF
    {70,62,67,56,55},      // 30 Nidorina
    {90,82,87,76,75},      // 31 Nidoqueen
    {46,57,40,50,40},      // 32 NidoranM
    {61,72,57,65,55},      // 33 Nidorino
    {81,92,77,85,75},      // 34 Nidoking
    {70,45,48,35,60},      // 35 Clefairy
    {95,70,73,60,85},      // 36 Clefable
    {38,41,40,65,65},      // 37 Vulpix
    {73,76,75,100,100},    // 38 Ninetales
    {115,45,20,20,25},     // 39 Jigglypuff
    {140,70,45,45,50},     // 40 Wigglytuff
    {40,45,35,55,40},      // 41 Zubat
    {75,80,70,90,75},      // 42 Golbat
    {45,50,55,30,75},      // 43 Oddish
    {60,65,70,40,85},      // 44 Gloom
    {75,80,85,50,100},     // 45 Vileplume
    {35,70,55,25,55},      // 46 Paras
    {60,95,80,30,80},      // 47 Parasect
    {60,55,50,45,40},      // 48 Venonat
    {70,65,60,90,90},      // 49 Venomoth
    {10,55,25,95,45},      // 50 Diglett
    {35,80,50,120,70},     // 51 Dugtrio
    {40,45,35,90,40},      // 52 Meowth
    {65,70,60,115,65},     // 53 Persian
    {50,52,48,55,50},      // 54 Psyduck
    {80,82,78,85,80},      // 55 Golduck
    {40,80,35,70,35},      // 56 Mankey
    {65,105,60,95,60},     // 57 Primeape
    {55,70,45,60,50},      // 58 Growlithe
    {90,110,80,95,80},     // 59 Arcanine
    {40,50,40,90,40},      // 60 Poliwag
    {65,65,65,90,50},      // 61 Poliwhirl
    {90,85,95,70,70},      // 62 Poliwrath
    {25,20,15,90,105},     // 63 Abra
    {40,35,30,105,120},    // 64 Kadabra
    {55,50,45,120,135},    // 65 Alakazam
    {70,80,50,35,35},      // 66 Machop
    {80,100,70,45,50},     // 67 Machoke
    {90,130,80,55,65},     // 68 Machamp
    {50,75,35,40,70},      // 69 Bellsprout
    {65,90,50,55,85},      // 70 Weepinbell
    {80,105,65,70,100},    // 71 Victreebel
    {40,40,35,70,100},     // 72 Tentacool
    {80,70,65,100,120},    // 73 Tentacruel
    {40,80,100,20,30},     // 74 Geodude
    {55,95,115,35,45},     // 75 Graveler
    {80,110,130,45,55},    // 76 Golem
    {50,85,55,90,65},      // 77 Ponyta
    {65,100,70,105,80},    // 78 Rapidash
    {90,65,65,15,40},      // 79 Slowpoke
    {95,75,110,30,80},     // 80 Slowbro
    {25,35,70,45,95},      // 81 Magnemite
    {50,60,95,70,120},     // 82 Magneton
    {52,65,55,60,58},      // 83 Farfetch'd
    {35,85,45,75,35},      // 84 Doduo
    {60,110,70,100,60},    // 85 Dodrio
    {65,45,55,45,70},      // 86 Seel
    {90,70,80,70,95},      // 87 Dewgong
    {80,80,50,25,40},      // 88 Grimer
    {105,105,75,50,65},    // 89 Muk
    {30,65,100,40,45},     // 90 Shellder
    {50,95,180,70,85},     // 91 Cloyster
    {30,35,30,80,100},     // 92 Gastly
    {45,50,45,95,115},     // 93 Haunter
    {60,65,60,110,130},    // 94 Gengar
    {35,45,160,70,30},     // 95 Onix
    {60,48,45,42,90},      // 96 Drowzee
    {85,73,70,67,115},     // 97 Hypno
    {30,105,90,50,25},     // 98 Krabby
    {55,130,115,75,50},    // 99 Kingler
    {40,30,50,100,55},     // 100 Voltorb
    {60,50,70,140,80},     // 101 Electrode
    {60,40,80,40,60},      // 102 Exeggcute
    {95,95,85,55,125},     // 103 Exeggutor
    {50,50,95,35,40},      // 104 Cubone
    {60,80,110,45,50},     // 105 Marowak
    {50,120,53,87,35},     // 106 Hitmonlee
    {50,105,79,76,35},     // 107 Hitmonchan
    {90,55,75,30,60},      // 108 Lickitung
    {40,65,95,35,60},      // 109 Koffing
    {65,90,120,60,85},     // 110 Weezing
    {80,85,95,25,30},      // 111 Rhyhorn
    {105,130,120,40,45},   // 112 Rhydon
    {250,5,5,50,105},      // 113 Chansey
    {65,55,115,60,100},    // 114 Tangela
    {105,95,80,90,40},     // 115 Kangaskhan
    {30,40,70,60,70},      // 116 Horsea
    {55,65,95,85,95},      // 117 Seadra
    {45,67,60,63,50},      // 118 Goldeen
    {80,92,65,68,80},      // 119 Seaking
    {30,45,55,85,70},      // 120 Staryu
    {60,75,85,115,100},    // 121 Starmie
    {40,45,65,90,100},     // 122 Mr. Mime
    {70,110,80,105,55},    // 123 Scyther
    {65,50,35,95,95},      // 124 Jynx
    {65,83,57,105,85},     // 125 Electabuzz
    {65,95,57,93,85},      // 126 Magmar
    {65,125,100,85,55},    // 127 Pinsir
    {75,100,95,110,70},    // 128 Tauros
    {20,10,55,80,20},      // 129 Magikarp
    {95,125,79,81,100},    // 130 Gyarados
    {130,85,80,60,95},     // 131 Lapras
    {48,48,48,48,48},      // 132 Ditto
    {55,55,50,55,65},      // 133 Eevee
    {130,65,60,65,110},    // 134 Vaporeon
    {65,65,60,130,110},    // 135 Jolteon
    {65,130,60,65,110},    // 136 Flareon
    {65,60,70,40,75},      // 137 Porygon
    {35,40,100,35,90},     // 138 Omanyte
    {70,60,125,55,115},    // 139 Omastar
    {30,80,90,55,45},      // 140 Kabuto
    {60,115,105,80,70},    // 141 Kabutops
    {80,105,65,130,60},    // 142 Aerodactyl
    {160,110,65,30,65},    // 143 Snorlax
    {90,85,100,85,125},    // 144 Articuno
    {90,90,85,100,125},    // 145 Zapdos
    {90,100,90,90,125},    // 146 Moltres
    {41,64,45,50,50},      // 147 Dratini
    {61,84,65,70,70},      // 148 Dragonair
    {91,134,95,80,100},    // 149 Dragonite
    {106,110,90,130,154},  // 150 Mewtwo
    {100,100,100,100,100}, // 151 Mew
};

// ── Internal index ↔ Pokedex number mapping ────────────────────────────────
// Gen 1 uses an internal species index that differs from Pokedex number.
// Index 0 = Missingno, we only need 1-190 range.

// Source: pret/pokered constants/pokemon_constants.asm
// 0 = MissingNo / unused gap. Only 0x01-0xBE have real Pokemon (with gaps).
static const uint8_t internalToDex[256] = {
//   x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xA  xB  xC  xD  xE  xF
      0,112,115, 32, 35, 21,100, 34, 80,  2,103,108,102, 88, 94, 29, // 0x00
     31,104,111,131, 59,151,130, 90, 72, 92,123,120,  9,127,114,  0, // 0x10
      0, 58, 95, 22, 16, 79, 64, 75,113, 67,122,106,107, 24, 47, 54, // 0x20
     96, 76,  0,126,  0,125, 82,109,  0, 56, 86, 50,128,  0,  0,  0, // 0x30
     83, 48,149,  0,  0,  0, 84, 60,124,146,144,145,132, 52, 98,  0, // 0x40
      0,  0, 37, 38, 25, 26,  0,  0,147,148,140,141,116,117,  0,  0, // 0x50
     27, 28,138,139, 39, 40,133,136,135,134, 66, 41, 23, 46, 61, 62, // 0x60
     13, 14, 15,  0, 85, 57, 51, 49, 87,  0,  0, 10, 11, 12, 68,  0, // 0x70
     55, 97, 42,150,143,129,  0,  0, 89,  0, 99, 91,  0,101, 36,110, // 0x80
     53,105,  0, 93, 63, 65, 17, 18,121,  1,  3, 73,  0,118,119,  0, // 0x90
      0,  0,  0, 77, 78, 19, 20, 33, 30, 74,137,142,  0, 81,  0,  0, // 0xA0
      4,  7,  5,  8,  6,  0,  0,  0,  0, 43, 44, 45, 69, 70, 71,  0, // 0xB0
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 0xC0
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 0xD0
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 0xE0
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 0xF0
};

// Reverse: Pokedex number -> internal index
// Source: pret/pokered constants/pokemon_constants.asm
static const uint8_t dexToInternal[152] = {
    0x00,  // 0 = unused
    0x99,  // 1 Bulbasaur
    0x09,  // 2 Ivysaur
    0x9A,  // 3 Venusaur
    0xB0,  // 4 Charmander
    0xB2,  // 5 Charmeleon
    0xB4,  // 6 Charizard
    0xB1,  // 7 Squirtle
    0xB3,  // 8 Wartortle
    0x1C,  // 9 Blastoise
    0x7B,  // 10 Caterpie
    0x7C,  // 11 Metapod
    0x7D,  // 12 Butterfree
    0x70,  // 13 Weedle
    0x71,  // 14 Kakuna
    0x72,  // 15 Beedrill
    0x24,  // 16 Pidgey
    0x96,  // 17 Pidgeotto
    0x97,  // 18 Pidgeot
    0xA5,  // 19 Rattata
    0xA6,  // 20 Raticate
    0x05,  // 21 Spearow
    0x23,  // 22 Fearow
    0x6C,  // 23 Ekans
    0x2D,  // 24 Arbok
    0x54,  // 25 Pikachu
    0x55,  // 26 Raichu
    0x60,  // 27 Sandshrew
    0x61,  // 28 Sandslash
    0x0F,  // 29 NidoranF
    0xA8,  // 30 Nidorina
    0x10,  // 31 Nidoqueen
    0x03,  // 32 NidoranM
    0xA7,  // 33 Nidorino
    0x07,  // 34 Nidoking
    0x04,  // 35 Clefairy
    0x8E,  // 36 Clefable
    0x52,  // 37 Vulpix
    0x53,  // 38 Ninetales
    0x64,  // 39 Jigglypuff
    0x65,  // 40 Wigglytuff
    0x6B,  // 41 Zubat
    0x82,  // 42 Golbat
    0xB9,  // 43 Oddish
    0xBA,  // 44 Gloom
    0xBB,  // 45 Vileplume
    0x6D,  // 46 Paras
    0x2E,  // 47 Parasect
    0x41,  // 48 Venonat
    0x77,  // 49 Venomoth
    0x3B,  // 50 Diglett
    0x76,  // 51 Dugtrio
    0x4D,  // 52 Meowth
    0x90,  // 53 Persian
    0x2F,  // 54 Psyduck
    0x80,  // 55 Golduck
    0x39,  // 56 Mankey
    0x75,  // 57 Primeape
    0x21,  // 58 Growlithe
    0x14,  // 59 Arcanine
    0x47,  // 60 Poliwag
    0x6E,  // 61 Poliwhirl
    0x6F,  // 62 Poliwrath
    0x94,  // 63 Abra
    0x26,  // 64 Kadabra
    0x95,  // 65 Alakazam
    0x6A,  // 66 Machop
    0x29,  // 67 Machoke
    0x7E,  // 68 Machamp
    0xBC,  // 69 Bellsprout
    0xBD,  // 70 Weepinbell
    0xBE,  // 71 Victreebel
    0x18,  // 72 Tentacool
    0x9B,  // 73 Tentacruel
    0xA9,  // 74 Geodude
    0x27,  // 75 Graveler
    0x31,  // 76 Golem
    0xA3,  // 77 Ponyta
    0xA4,  // 78 Rapidash
    0x25,  // 79 Slowpoke
    0x08,  // 80 Slowbro
    0xAD,  // 81 Magnemite
    0x36,  // 82 Magneton
    0x40,  // 83 Farfetch'd
    0x46,  // 84 Doduo
    0x74,  // 85 Dodrio
    0x3A,  // 86 Seel
    0x78,  // 87 Dewgong
    0x0D,  // 88 Grimer
    0x88,  // 89 Muk
    0x17,  // 90 Shellder
    0x8B,  // 91 Cloyster
    0x19,  // 92 Gastly
    0x93,  // 93 Haunter
    0x0E,  // 94 Gengar
    0x22,  // 95 Onix
    0x30,  // 96 Drowzee
    0x81,  // 97 Hypno
    0x4E,  // 98 Krabby
    0x8A,  // 99 Kingler
    0x06,  // 100 Voltorb
    0x8D,  // 101 Electrode
    0x0C,  // 102 Exeggcute
    0x0A,  // 103 Exeggutor
    0x11,  // 104 Cubone
    0x91,  // 105 Marowak
    0x2B,  // 106 Hitmonlee
    0x2C,  // 107 Hitmonchan
    0x0B,  // 108 Lickitung
    0x37,  // 109 Koffing
    0x8F,  // 110 Weezing
    0x12,  // 111 Rhyhorn
    0x01,  // 112 Rhydon
    0x28,  // 113 Chansey
    0x1E,  // 114 Tangela
    0x02,  // 115 Kangaskhan
    0x5C,  // 116 Horsea
    0x5D,  // 117 Seadra
    0x9D,  // 118 Goldeen
    0x9E,  // 119 Seaking
    0x1B,  // 120 Staryu
    0x98,  // 121 Starmie
    0x2A,  // 122 Mr. Mime
    0x1A,  // 123 Scyther
    0x48,  // 124 Jynx
    0x35,  // 125 Electabuzz
    0x33,  // 126 Magmar
    0x1D,  // 127 Pinsir
    0x3C,  // 128 Tauros
    0x85,  // 129 Magikarp
    0x16,  // 130 Gyarados
    0x13,  // 131 Lapras
    0x4C,  // 132 Ditto
    0x66,  // 133 Eevee
    0x69,  // 134 Vaporeon
    0x68,  // 135 Jolteon
    0x67,  // 136 Flareon
    0xAA,  // 137 Porygon
    0x62,  // 138 Omanyte
    0x63,  // 139 Omastar
    0x5A,  // 140 Kabuto
    0x5B,  // 141 Kabutops
    0xAB,  // 142 Aerodactyl
    0x84,  // 143 Snorlax
    0x4A,  // 144 Articuno
    0x4B,  // 145 Zapdos
    0x49,  // 146 Moltres
    0x58,  // 147 Dratini
    0x59,  // 148 Dragonair
    0x42,  // 149 Dragonite
    0x83,  // 150 Mewtwo
    0x15,  // 151 Mew
};

// ── Gen 1 text encoding ↔ ASCII ────────────────────────────────────────────
// gen1CharToAscii is defined in Gen1Species.h — use that version.
// This extended version adds punctuation the base version doesn't cover.

#ifndef GEN1_CHAR_TO_ASCII_DEFINED
#define GEN1_CHAR_TO_ASCII_DEFINED
inline char gen1CharToAscii(uint8_t c) {
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
#endif

__attribute__((unused))
static uint8_t asciiToGen1Char(char c) {
    if (c >= 'A' && c <= 'Z') return 0x80 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 0xA0 + (c - 'a');
    if (c >= '0' && c <= '9') return 0xF6 + (c - '0');
    if (c == ' ') return 0x7F;
    if (c == '.') return 0xE8;
    if (c == '-') return 0xE3;
    if (c == '\'') return 0xEF;
    if (c == ',') return 0xF4;
    if (c == '/') return 0xF3;
    if (c == '\0') return 0x50;
    return 0x50;  // unknown → terminator
}

// ── Helper: read/write big-endian values ───────────────────────────────────

static inline uint16_t readBE16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static inline void writeBE16(uint8_t *p, uint16_t val) {
    p[0] = (val >> 8) & 0xFF;
    p[1] = val & 0xFF;
}

static inline uint32_t readBE24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static inline void writeBE24(uint8_t *p, uint32_t val) {
    p[0] = (val >> 16) & 0xFF;
    p[1] = (val >> 8) & 0xFF;
    p[2] = val & 0xFF;
}

// ── Experience calculation ─────────────────────────────────────────────────

static uint32_t expForLevel(uint8_t dexNum, uint8_t level) {
    if (dexNum == 0 || dexNum > 151 || level <= 1) return 0;

    uint32_t n = level;
    uint32_t n3 = n * n * n;

    switch (speciesGrowthRate[dexNum]) {
        case GROWTH_FAST:
            return (4 * n3) / 5;
        case GROWTH_MEDIUM_FAST:
            return n3;
        case GROWTH_MEDIUM_SLOW: {
            // 1.2*n^3 - 15*n^2 + 100*n - 140
            // Use integer math: (6*n^3)/5 - 15*n^2 + 100*n - 140
            int32_t exp = (int32_t)((6 * n3) / 5) - (int32_t)(15 * n * n)
                        + (int32_t)(100 * n) - 140;
            return (exp < 0) ? 0 : (uint32_t)exp;
        }
        case GROWTH_SLOW:
            return (5 * n3) / 4;
        default:
            return n3;  // fallback to medium fast
    }
}

// Given current total EXP, find what level that corresponds to
static uint8_t levelForExp(uint8_t dexNum, uint32_t totalExp) {
    if (dexNum == 0 || dexNum > 151) return 1;

    uint8_t level = 1;
    for (uint8_t l = 2; l <= 100; l++) {
        if (expForLevel(dexNum, l) > totalExp) break;
        level = l;
    }
    return level;
}

// ── Stat calculation (Gen 1 formula) ───────────────────────────────────────

static inline uint16_t isqrt16(uint16_t n) {
    // Integer square root for stat exp calculation
    if (n == 0) return 0;
    uint16_t x = n;
    uint16_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

static uint16_t calcStatHP(uint8_t base, uint8_t dv, uint16_t statExp, uint8_t level) {
    uint16_t sqrtExp = isqrt16(statExp);
    // ceil(sqrt(statExp)) — isqrt gives floor, so check
    if ((uint32_t)sqrtExp * sqrtExp < statExp) sqrtExp++;
    uint16_t evBonus = sqrtExp / 4;
    return ((uint16_t)((base + dv) * 2 + evBonus) * level / 100) + level + 10;
}

static uint16_t calcStat(uint8_t base, uint8_t dv, uint16_t statExp, uint8_t level) {
    uint16_t sqrtExp = isqrt16(statExp);
    if ((uint32_t)sqrtExp * sqrtExp < statExp) sqrtExp++;
    uint16_t evBonus = sqrtExp / 4;
    return ((uint16_t)((base + dv) * 2 + evBonus) * level / 100) + 5;
}

// ── The Patcher ────────────────────────────────────────────────────────────

struct DaycarePartyInfo {
    uint8_t  dexNum;         // Pokedex number (1-151)
    uint8_t  level;          // from SAV
    uint32_t totalExp;       // from SAV (3-byte BE)
    char     nickname[11];   // decoded to ASCII
    char     otName[11];     // decoded to ASCII
    uint8_t  moves[4];       // 4 move IDs from SAV
};

class DaycareSavPatcher {
public:
    // ── Read party from SRAM ───────────────────────────────────────────
    // sram: pointer to the full 32KB SRAM buffer
    // out: array of 6 DaycarePartyInfo to fill
    // Returns party count (0-6)
    static uint8_t readParty(const uint8_t *sram, DaycarePartyInfo *out) {
        uint8_t count = sram[SAV_PARTY_COUNT];
        if (count > 6) count = 6;

        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *pkm = &sram[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE];

            // Species: internal index → Pokedex number
            uint8_t internalIdx = pkm[PKM_SPECIES];
            out[i].dexNum = internalToDex[internalIdx];

            // Level (party authoritative)
            out[i].level = pkm[PKM_LEVEL_PARTY];

            // Experience (3 bytes big-endian)
            out[i].totalExp = readBE24(&pkm[PKM_EXP]);

            // Nickname (Gen 1 encoding → ASCII)
            const uint8_t *nickRaw = &sram[SAV_NICKNAMES + i * SAV_NAME_SIZE];
            for (int j = 0; j < 10; j++) {
                if (nickRaw[j] == SAV_STRING_TERMINATOR) {
                    out[i].nickname[j] = '\0';
                    break;
                }
                out[i].nickname[j] = gen1CharToAscii(nickRaw[j]);
                if (j == 9) out[i].nickname[10] = '\0';
            }

            // OT name
            const uint8_t *otRaw = &sram[SAV_OT_NAMES + i * SAV_NAME_SIZE];
            for (int j = 0; j < 10; j++) {
                if (otRaw[j] == SAV_STRING_TERMINATOR) {
                    out[i].otName[j] = '\0';
                    break;
                }
                out[i].otName[j] = gen1CharToAscii(otRaw[j]);
                if (j == 9) out[i].otName[10] = '\0';
            }

            // Moves (bytes 0x08-0x0B in the party Pokemon struct)
            for (int m = 0; m < 4; m++) out[i].moves[m] = pkm[0x08 + m];
        }
        return count;
    }

    // ── Write daycare XP/levels back to SRAM ───────────────────────────
    // sram: writable 32KB SRAM buffer
    // partyIdx: which party slot (0-5)
    // dexNum: Pokedex number of the Pokemon
    // xpGained: total XP gained during daycare
    // Returns new level (or 0 on error)
    static uint8_t patchPokemon(uint8_t *sram, uint8_t partyIdx, uint8_t dexNum,
                                 uint32_t xpGained) {
        if (partyIdx >= 6 || dexNum == 0 || dexNum > 151) return 0;

        uint8_t *pkm = &sram[SAV_POKEMON_DATA + partyIdx * SAV_POKEMON_SIZE];

        // Read current EXP
        uint32_t currentExp = readBE24(&pkm[PKM_EXP]);

        // Add daycare XP
        uint32_t newExp = currentExp + xpGained;

        // Cap at level 100's EXP for this species' growth rate
        uint32_t maxExp = expForLevel(dexNum, 100);
        if (newExp > maxExp) newExp = maxExp;

        // Calculate new level
        uint8_t newLevel = levelForExp(dexNum, newExp);
        if (newLevel > 100) newLevel = 100;

        // Write new EXP (3 bytes BE)
        writeBE24(&pkm[PKM_EXP], newExp);

        // Write new level (both box and party)
        pkm[PKM_LEVEL_PC] = newLevel;
        pkm[PKM_LEVEL_PARTY] = newLevel;

        // Recalculate stats
        recalcStats(pkm, dexNum, newLevel);

        return newLevel;
    }

    // ── Recalculate and fix the save checksum ──────────────────────────
    static void fixChecksum(uint8_t *sram) {
        uint8_t sum = 0;
        for (uint32_t i = SAV_CHECKSUM_START; i <= SAV_CHECKSUM_END; i++) {
            sum += sram[i];
        }
        sram[SAV_CHECKSUM_OFFSET] = ~sum;
    }

    // ── Full checkout: patch all party Pokemon and fix checksum ─────────
    // xpGained: array of XP gained per party slot
    // dexNums: array of Pokedex numbers per party slot
    // partyCount: number of Pokemon
    // Returns true if any Pokemon was patched
    static bool checkout(uint8_t *sram, const uint8_t *dexNums,
                         const uint32_t *xpGained, uint8_t partyCount) {
        bool patched = false;
        for (uint8_t i = 0; i < partyCount && i < 6; i++) {
            if (xpGained[i] > 0) {
                patchPokemon(sram, i, dexNums[i], xpGained[i]);
                patched = true;
            }
        }
        if (patched) {
            fixChecksum(sram);
        }
        return patched;
    }

private:
    // ── Recalculate all 5 stats for a party Pokemon ────────────────────
    static void recalcStats(uint8_t *pkm, uint8_t dexNum, uint8_t level) {
        if (dexNum == 0 || dexNum > 151) return;
        const BaseStats &base = speciesBaseStats[dexNum];

        // Read DVs
        uint8_t atkDV = (pkm[PKM_DVS] >> 4) & 0x0F;
        uint8_t defDV = pkm[PKM_DVS] & 0x0F;
        uint8_t spdDV = (pkm[PKM_DVS + 1] >> 4) & 0x0F;
        uint8_t spcDV = pkm[PKM_DVS + 1] & 0x0F;
        uint8_t hpDV  = ((atkDV & 1) << 3) | ((defDV & 1) << 2) |
                        ((spdDV & 1) << 1) | (spcDV & 1);

        // Read stat experience
        uint16_t hpEV  = readBE16(&pkm[PKM_HP_EV]);
        uint16_t atkEV = readBE16(&pkm[PKM_ATK_EV]);
        uint16_t defEV = readBE16(&pkm[PKM_DEF_EV]);
        uint16_t spdEV = readBE16(&pkm[PKM_SPD_EV]);
        uint16_t spcEV = readBE16(&pkm[PKM_SPC_EV]);

        // Calculate new stats
        uint16_t maxHP  = calcStatHP(base.hp, hpDV, hpEV, level);
        uint16_t attack = calcStat(base.atk, atkDV, atkEV, level);
        uint16_t defense = calcStat(base.def, defDV, defEV, level);
        uint16_t speed   = calcStat(base.spd, spdDV, spdEV, level);
        uint16_t special = calcStat(base.spc, spcDV, spcEV, level);

        // Read old max HP and current HP to scale proportionally
        uint16_t oldMaxHP = readBE16(&pkm[PKM_MAX_HP]);
        uint16_t oldCurHP = readBE16(&pkm[PKM_CURRENT_HP]);

        // Scale current HP proportionally to new max HP
        uint16_t newCurHP;
        if (oldMaxHP > 0 && oldCurHP > 0) {
            newCurHP = (uint16_t)((uint32_t)oldCurHP * maxHP / oldMaxHP);
            if (newCurHP == 0) newCurHP = 1;  // don't kill the Pokemon
            if (newCurHP > maxHP) newCurHP = maxHP;
        } else {
            newCurHP = maxHP;  // full heal if data was zeroed
        }

        // Write stats
        writeBE16(&pkm[PKM_CURRENT_HP], newCurHP);
        writeBE16(&pkm[PKM_MAX_HP], maxHP);
        writeBE16(&pkm[PKM_ATTACK], attack);
        writeBE16(&pkm[PKM_DEFENSE], defense);
        writeBE16(&pkm[PKM_SPEED], speed);
        writeBE16(&pkm[PKM_SPECIAL], special);
    }
};
