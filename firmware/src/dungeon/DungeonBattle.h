#pragma once
#include <Arduino.h>
#include "DungeonModule.h"

// ── Gen 1 type chart ──────────────────────────────────────────────────────────
// 15 types: Normal=0 Fire=1 Water=2 Electric=3 Grass=4 Ice=5 Fighting=6
//           Poison=7 Ground=8 Flying=9 Psychic=10 Bug=11 Rock=12 Ghost=13 Dragon=14
// Values: 0=immune  50=½x  100=1x  200=2x
// Implements Gen 1 bugs: Ghost→Psychic=0 (not 2x), Bug→Poison=2x, Poison→Bug=2x

static constexpr uint8_t GEN1_TYPE_CHART[15][15] = {
//         Nrm  Fir  Wat  Elc  Grs  Ice  Fgt  Psn  Gnd  Fly  Psy  Bug  Rck  Gst  Drg
/* Nrm */{ 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,  50,   0, 100},
/* Fir */{ 100,  50,  50, 100, 200, 200, 100, 100, 100, 100, 100, 200,  50, 100,  50},
/* Wat */{ 100, 200,  50, 100,  50, 100, 100, 100, 200, 100, 100, 100, 200, 100,  50},
/* Elc */{ 100, 100, 200,  50,  50, 100, 100, 100,   0, 200, 100, 100, 100, 100,  50},
/* Grs */{ 100,  50, 200, 100,  50, 100, 100,  50, 200,  50, 100,  50, 200, 100,  50},
/* Ice */{ 100,  50,  50, 100, 200,  50, 100, 100, 200, 200, 100, 100, 100, 100, 200},
/* Fgt */{ 200, 100, 100, 100, 100, 200, 100,  50, 100, 100,  50,  50, 200,   0, 100},
/* Psn */{ 100, 100, 100, 100, 200, 100, 100,  50,  50, 100, 100, 200,  50,  50, 100},
/* Gnd */{ 100, 200, 100, 200,  50, 100, 100, 200, 100,   0, 100,  50, 200, 100, 100},
/* Fly */{ 100, 100, 100,  50, 200, 100, 200, 100, 100, 100, 100, 200,  50, 100, 100},
/* Psy */{ 100, 100, 100, 100, 100, 100, 200, 200, 100, 100,  50, 100, 100,   0, 100},
/* Bug */{ 100,  50, 100, 100, 200, 100,  50, 200, 100,  50, 200, 100, 100, 100, 100},
/* Rck */{ 100, 200, 100, 100, 100, 200,  50, 100,  50, 200, 100, 200, 100, 100, 100},
/* Gst */{   0, 100, 100, 100, 100, 100, 100, 100, 100, 100,   0, 100, 100, 200, 100},
/* Drg */{ 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 200},
};

// Type effectiveness lookup — returns 0/50/100/200
inline uint8_t typeEffectiveness(PokeType atk, PokeType def) {
    uint8_t a = (uint8_t)atk;
    uint8_t d = (uint8_t)def;
    if (a >= 15 || d >= 15) return 100;  // Fairy and beyond → treat as Normal
    return GEN1_TYPE_CHART[a][d];
}

// Combined effectiveness for dual-type defender
inline uint16_t typeEffectivenessDual(PokeType atk, PokeType t1, PokeType t2) {
    uint16_t e = (uint16_t)typeEffectiveness(atk, t1) * typeEffectiveness(atk, t2) / 100;
    return e;  // e.g. 200*200/100 = 400 for 4x
}

// ── D&D Spell type mapping ────────────────────────────────────────────────────
// Maps D&D spell school/element keywords to PokeType for matchup purposes.
// Non-elemental spells default to Psychic (magic force).

inline PokeType spellNameToType(const char *name) {
    if (!name) return PokeType::Psychic;
    // Check lowercase substrings
    struct { const char *keyword; PokeType type; } MAP[] = {
        {"fire",      PokeType::Fire},
        {"flame",     PokeType::Fire},
        {"burn",      PokeType::Fire},
        {"blaze",     PokeType::Fire},
        {"lightning", PokeType::Electric},
        {"thunder",   PokeType::Electric},
        {"shock",     PokeType::Electric},
        {"ice",       PokeType::Ice},
        {"frost",     PokeType::Ice},
        {"cold",      PokeType::Ice},
        {"snow",      PokeType::Ice},
        {"poison",    PokeType::Poison},
        {"acid",      PokeType::Poison},
        {"venom",     PokeType::Poison},
        {"necrotic",  PokeType::Ghost},
        {"shadow",    PokeType::Ghost},
        {"radiant",   PokeType::Normal},
        {"water",     PokeType::Water},
        {"flood",     PokeType::Water},
        {"earth",     PokeType::Ground},
        {"stone",     PokeType::Rock},
        {"rock",      PokeType::Rock},
        {"wind",      PokeType::Flying},
        {"air",       PokeType::Flying},
        {"gust",      PokeType::Flying},
        {"psychic",   PokeType::Psychic},
        {"mind",      PokeType::Psychic},
        {"heal",      PokeType::Normal},
        {"cure",      PokeType::Normal},
        {nullptr,     PokeType::Psychic},
    };
    // Lowercase copy for comparison
    char lower[32] = {};
    for (int i = 0; name[i] && i < 31; i++) lower[i] = tolower((uint8_t)name[i]);
    for (int i = 0; MAP[i].keyword; i++) {
        if (strstr(lower, MAP[i].keyword)) return MAP[i].type;
    }
    return PokeType::Psychic;  // default: Psychic (magic force)
}

// ── Gen 1 base stats + types (indexed by Pokédex number 1–151) ───────────────
struct Gen1Stats {
    uint8_t hp, atk, def, spd, spc;
    uint8_t type1, type2;  // PokeType indices
};

// Index 0 = placeholder (unused). Index 1-151 = Pokédex number.
static constexpr Gen1Stats GEN1_BASE_STATS[152] = {
/*  0 */ { 45,  45,  45,  45,  45,  0,  0 },  // placeholder
/*  1 */ { 45,  49,  49,  45,  65,  4,  7 },  // Bulbasaur  Grass/Poison
/*  2 */ { 60,  62,  63,  60,  80,  4,  7 },  // Ivysaur
/*  3 */ { 80,  82,  83,  80, 100,  4,  7 },  // Venusaur
/*  4 */ { 39,  52,  43,  65,  50,  1,  1 },  // Charmander Fire
/*  5 */ { 58,  64,  58,  80,  65,  1,  1 },  // Charmeleon
/*  6 */ { 78,  84,  78, 100,  85,  1,  9 },  // Charizard  Fire/Flying
/*  7 */ { 44,  48,  65,  43,  50,  2,  2 },  // Squirtle   Water
/*  8 */ { 59,  63,  80,  58,  65,  2,  2 },  // Wartortle
/*  9 */ { 79,  83, 100,  78,  85,  2,  2 },  // Blastoise
/* 10 */ { 45,  30,  35,  45,  20, 11, 11 },  // Caterpie   Bug
/* 11 */ { 50,  20,  55,  30,  25, 11, 11 },  // Metapod
/* 12 */ { 60,  45,  50,  70,  90, 11,  9 },  // Butterfree Bug/Flying
/* 13 */ { 40,  35,  30,  50,  20, 11,  7 },  // Weedle     Bug/Poison
/* 14 */ { 45,  25,  50,  35,  25, 11,  7 },  // Kakuna
/* 15 */ { 65,  90,  40,  75,  45, 11,  7 },  // Beedrill
/* 16 */ { 40,  45,  40,  56,  35,  0,  9 },  // Pidgey     Normal/Flying
/* 17 */ { 63,  60,  55,  71,  50,  0,  9 },  // Pidgeotto
/* 18 */ { 83,  80,  75, 101,  70,  0,  9 },  // Pidgeot
/* 19 */ { 30,  56,  35,  72,  25,  0,  0 },  // Rattata    Normal
/* 20 */ { 55,  81,  60,  97,  50,  0,  0 },  // Raticate
/* 21 */ { 40,  60,  30,  70,  31,  0,  9 },  // Spearow
/* 22 */ { 65,  90,  65, 100,  61,  0,  9 },  // Fearow
/* 23 */ { 35,  60,  44,  55,  40,  7,  7 },  // Ekans      Poison
/* 24 */ { 60,  85,  69,  80,  65,  7,  7 },  // Arbok
/* 25 */ { 35,  55,  30,  90,  50,  3,  3 },  // Pikachu    Electric
/* 26 */ { 60,  90,  55, 110,  90,  3,  3 },  // Raichu
/* 27 */ { 50,  75,  85,  40,  30,  8,  8 },  // Sandshrew  Ground
/* 28 */ { 75, 100, 110,  65,  55,  8,  8 },  // Sandslash
/* 29 */ { 55,  47,  52,  41,  40,  7,  7 },  // Nidoran F
/* 30 */ { 70,  62,  67,  56,  55,  7,  7 },  // Nidorina
/* 31 */ { 90,  92,  87,  76,  75,  7,  8 },  // Nidoqueen  Poison/Ground
/* 32 */ { 46,  57,  40,  50,  40,  7,  7 },  // Nidoran M
/* 33 */ { 61,  72,  57,  65,  55,  7,  7 },  // Nidorino
/* 34 */ { 81, 102,  77,  85,  75,  7,  8 },  // Nidoking
/* 35 */ { 70,  45,  48,  35,  60,  0,  0 },  // Clefairy   Normal
/* 36 */ { 95,  70,  73,  60,  85,  0,  0 },  // Clefable
/* 37 */ { 38,  41,  40,  65,  65,  1,  1 },  // Vulpix     Fire
/* 38 */ { 73,  76,  75, 100, 100,  1,  1 },  // Ninetales
/* 39 */ {115,  45,  20,  20,  25,  0,  0 },  // Jigglypuff
/* 40 */ {140,  70,  45,  45,  50,  0,  0 },  // Wigglytuff
/* 41 */ { 40,  45,  35,  55,  40,  7,  9 },  // Zubat      Poison/Flying
/* 42 */ { 75,  80,  70,  90,  75,  7,  9 },  // Golbat
/* 43 */ { 45,  50,  55,  30,  75,  4,  7 },  // Oddish     Grass/Poison
/* 44 */ { 60,  65,  70,  40,  85,  4,  7 },  // Gloom
/* 45 */ { 75,  80,  85,  50, 100,  4,  7 },  // Vileplume
/* 46 */ { 35,  70,  55,  25,  55, 11,  4 },  // Paras      Bug/Grass
/* 47 */ { 60,  95,  80,  30,  80, 11,  4 },  // Parasect
/* 48 */ { 60,  55,  50,  45,  40, 11,  7 },  // Venonat
/* 49 */ { 70,  65,  60,  90,  90, 11,  7 },  // Venomoth
/* 50 */ { 10,  55,  25,  95,  45,  8,  8 },  // Diglett    Ground
/* 51 */ { 35,  80,  50, 120,  70,  8,  8 },  // Dugtrio
/* 52 */ { 40,  45,  35,  90,  40,  0,  0 },  // Meowth     Normal
/* 53 */ { 65,  70,  60, 115,  65,  0,  0 },  // Persian
/* 54 */ { 50,  52,  48,  55,  50,  2,  2 },  // Psyduck    Water
/* 55 */ { 80,  82,  78,  85,  80,  2,  2 },  // Golduck
/* 56 */ { 40,  80,  35,  70,  35,  6,  6 },  // Mankey     Fighting
/* 57 */ { 65, 105,  60,  95,  60,  6,  6 },  // Primeape
/* 58 */ { 55,  70,  45,  60,  50,  1,  1 },  // Growlithe  Fire
/* 59 */ { 90, 110,  80,  95,  80,  1,  1 },  // Arcanine
/* 60 */ { 40,  50,  40,  90,  40,  2,  2 },  // Poliwag    Water
/* 61 */ { 65,  65,  65,  90,  50,  2,  2 },  // Poliwhirl
/* 62 */ { 90,  95,  95,  70,  70,  2,  6 },  // Poliwrath  Water/Fighting
/* 63 */ { 25,  20,  15,  90, 105, 10, 10 },  // Abra       Psychic
/* 64 */ { 40,  35,  30, 105, 120, 10, 10 },  // Kadabra
/* 65 */ { 55,  50,  45, 120, 135, 10, 10 },  // Alakazam
/* 66 */ { 70,  80,  50,  35,  35,  6,  6 },  // Machop     Fighting
/* 67 */ { 80, 100,  70,  45,  50,  6,  6 },  // Machoke
/* 68 */ { 90, 130,  80,  55,  65,  6,  6 },  // Machamp
/* 69 */ { 50,  75,  35,  40,  70,  4,  7 },  // Bellsprout Grass/Poison
/* 70 */ { 65,  90,  50,  55,  85,  4,  7 },  // Weepinbell
/* 71 */ { 80, 105,  65,  70, 100,  4,  7 },  // Victreebel
/* 72 */ { 40,  40,  35,  70, 100,  2,  7 },  // Tentacool  Water/Poison
/* 73 */ { 80,  70,  65, 100, 120,  2,  7 },  // Tentacruel
/* 74 */ { 40,  80, 100,  20,  30, 12,  8 },  // Geodude    Rock/Ground
/* 75 */ { 55,  95, 115,  35,  45, 12,  8 },  // Graveler
/* 76 */ { 80, 120, 130,  45,  55, 12,  8 },  // Golem
/* 77 */ { 50,  85,  55,  90,  65,  1,  1 },  // Ponyta     Fire
/* 78 */ { 65, 100,  70, 105,  80,  1,  1 },  // Rapidash
/* 79 */ { 90,  65,  65,  15,  40,  2, 10 },  // Slowpoke   Water/Psychic
/* 80 */ { 95,  75, 110,  30,  80,  2, 10 },  // Slowbro
/* 81 */ { 25,  35,  70,  45,  95,  3,  3 },  // Magnemite  Electric
/* 82 */ { 50,  60,  95,  70, 120,  3,  3 },  // Magneton
/* 83 */ { 52,  65,  55,  60,  58,  0,  9 },  // Farfetch'd Normal/Flying
/* 84 */ { 35,  85,  45,  75,  35,  0,  9 },  // Doduo
/* 85 */ { 60, 110,  70, 100,  60,  0,  9 },  // Dodrio
/* 86 */ { 65,  45,  55,  45,  70,  2,  2 },  // Seel       Water
/* 87 */ { 90,  70,  80,  70,  95,  2,  5 },  // Dewgong    Water/Ice
/* 88 */ { 80,  80,  50,  25,  40,  7,  7 },  // Grimer     Poison
/* 89 */ {105, 105,  75,  50,  65,  7,  7 },  // Muk
/* 90 */ { 30,  65, 100,  40,  45,  2,  2 },  // Shellder   Water
/* 91 */ { 50,  95, 180,  70,  85,  2,  5 },  // Cloyster   Water/Ice
/* 92 */ { 30,  35,  30,  80, 100, 13,  7 },  // Gastly     Ghost/Poison
/* 93 */ { 45,  50,  45,  95, 115, 13,  7 },  // Haunter
/* 94 */ { 60,  65,  60, 110, 130, 13,  7 },  // Gengar
/* 95 */ { 35,  45, 160,  70,  30, 12,  8 },  // Onix       Rock/Ground
/* 96 */ { 60,  48,  45,  42,  90, 10, 10 },  // Drowzee    Psychic
/* 97 */ { 85,  73,  70,  67, 115, 10, 10 },  // Hypno
/* 98 */ { 30, 105,  90,  50,  25,  2,  2 },  // Krabby     Water
/* 99 */ { 55, 130, 115,  75,  50,  2,  2 },  // Kingler
/*100 */ { 40,  30,  50, 100,  55,  3,  3 },  // Voltorb    Electric
/*101 */ { 60,  50,  70, 140,  80,  3,  3 },  // Electrode
/*102 */ { 60,  40,  80,  40,  60,  4, 10 },  // Exeggcute  Grass/Psychic
/*103 */ { 95,  95,  85,  55, 125,  4, 10 },  // Exeggutor
/*104 */ { 50,  50,  95,  35,  40,  8,  8 },  // Cubone     Ground
/*105 */ { 60,  80, 110,  45,  50,  8,  8 },  // Marowak
/*106 */ { 50, 120,  53,  87,  35,  6,  6 },  // Hitmonlee  Fighting
/*107 */ { 50, 105,  79,  76,  35,  6,  6 },  // Hitmonchan
/*108 */ { 90,  55,  75,  30,  60,  0,  0 },  // Lickitung  Normal
/*109 */ { 40,  65,  95,  35,  60,  7,  7 },  // Koffing    Poison
/*110 */ { 65,  90, 120,  60,  85,  7,  7 },  // Weezing
/*111 */ { 80,  85,  95,  25,  30,  8, 12 },  // Rhyhorn    Ground/Rock
/*112 */ {105, 130, 120,  40,  45,  8, 12 },  // Rhydon
/*113 */ {250,   5,   5,  50, 105,  0,  0 },  // Chansey    Normal
/*114 */ { 65,  55, 115,  60, 100,  4,  4 },  // Tangela    Grass
/*115 */ {105,  95,  80,  90,  40,  0,  0 },  // Kangaskhan
/*116 */ { 30,  40,  70,  60,  70,  2,  2 },  // Horsea     Water
/*117 */ { 55,  65,  95,  85,  95,  2,  2 },  // Seadra
/*118 */ { 45,  67,  60,  63,  50,  2,  2 },  // Goldeen
/*119 */ { 80,  92,  65,  68,  80,  2,  2 },  // Seaking
/*120 */ { 30,  45,  55,  85,  70,  2,  2 },  // Staryu     Water
/*121 */ { 60,  75,  85, 115, 100,  2, 10 },  // Starmie    Water/Psychic
/*122 */ { 40,  45,  65,  90, 100, 10, 10 },  // Mr. Mime   Psychic
/*123 */ { 70, 110,  80, 105,  55, 11,  9 },  // Scyther    Bug/Flying
/*124 */ { 65,  50,  35,  95,  95,  5, 10 },  // Jynx       Ice/Psychic
/*125 */ { 65,  83,  57, 105,  85,  3,  3 },  // Electabuzz Electric
/*126 */ { 65,  95,  57,  93,  85,  1,  1 },  // Magmar     Fire
/*127 */ { 65, 125, 100,  85,  55, 11, 11 },  // Pinsir     Bug
/*128 */ { 75, 100,  95, 110,  70,  0,  0 },  // Tauros     Normal
/*129 */ { 20,  10,  55,  80,  20,  2,  2 },  // Magikarp   Water
/*130 */ { 95, 125,  79,  81, 100,  2,  9 },  // Gyarados   Water/Flying
/*131 */ {130,  85,  80,  60,  95,  2,  5 },  // Lapras     Water/Ice
/*132 */ { 48,  48,  48,  48,  48,  0,  0 },  // Ditto      Normal
/*133 */ { 55,  55,  50,  55,  65,  0,  0 },  // Eevee      Normal
/*134 */ {130,  65,  60,  65, 110,  2,  2 },  // Vaporeon   Water
/*135 */ { 65,  65,  60, 130, 110,  3,  3 },  // Jolteon    Electric
/*136 */ { 65, 130,  60,  65, 110,  1,  1 },  // Flareon    Fire
/*137 */ { 65,  60,  70,  40,  75,  0,  0 },  // Porygon    Normal
/*138 */ { 35,  40, 100,  35,  90, 12,  2 },  // Omanyte    Rock/Water
/*139 */ { 70,  60, 125,  55, 115, 12,  2 },  // Omastar
/*140 */ { 30,  80,  90,  55,  45, 12,  2 },  // Kabuto     Rock/Water
/*141 */ { 60, 115, 105,  80,  70, 12,  2 },  // Kabutops
/*142 */ { 80, 105,  65, 130,  60, 12,  9 },  // Aerodactyl Rock/Flying
/*143 */ {160, 110,  65,  30,  65,  0,  0 },  // Snorlax    Normal
/*144 */ { 90,  85, 100,  85, 125,  5,  9 },  // Articuno   Ice/Flying
/*145 */ { 90,  90,  85, 100, 125,  3,  9 },  // Zapdos     Electric/Flying
/*146 */ { 90, 100,  90,  90, 125,  1,  9 },  // Moltres    Fire/Flying
/*147 */ { 41,  64,  45,  50,  50, 14, 14 },  // Dratini    Dragon
/*148 */ { 61,  84,  65,  70,  70, 14, 14 },  // Dragonair
/*149 */ { 91, 134,  95,  80, 100, 14,  9 },  // Dragonite  Dragon/Flying
/*150 */ {106, 110,  90, 130, 154, 10, 10 },  // Mewtwo     Psychic
/*151 */ {100, 100, 100, 100, 100, 10, 10 },  // Mew        Psychic
};

inline const Gen1Stats& getSpeciesStats(uint8_t dexNum) {
    if (dexNum == 0 || dexNum > 151) return GEN1_BASE_STATS[0];
    return GEN1_BASE_STATS[dexNum];
}

// ── Species → name (by Pokédex number) ───────────────────────────────────────
static constexpr const char* POKEDEX_NAMES[152] = {
    "???",        "Bulbasaur",  "Ivysaur",    "Venusaur",   "Charmander",
    "Charmeleon", "Charizard",  "Squirtle",   "Wartortle",  "Blastoise",
    "Caterpie",   "Metapod",    "Butterfree", "Weedle",     "Kakuna",
    "Beedrill",   "Pidgey",     "Pidgeotto",  "Pidgeot",    "Rattata",
    "Raticate",   "Spearow",    "Fearow",     "Ekans",      "Arbok",
    "Pikachu",    "Raichu",     "Sandshrew",  "Sandslash",  "Nidoran-F",
    "Nidorina",   "Nidoqueen",  "Nidoran-M",  "Nidorino",   "Nidoking",
    "Clefairy",   "Clefable",   "Vulpix",     "Ninetales",  "Jigglypuff",
    "Wigglytuff", "Zubat",      "Golbat",     "Oddish",     "Gloom",
    "Vileplume",  "Paras",      "Parasect",   "Venonat",    "Venomoth",
    "Diglett",    "Dugtrio",    "Meowth",     "Persian",    "Psyduck",
    "Golduck",    "Mankey",     "Primeape",   "Growlithe",  "Arcanine",
    "Poliwag",    "Poliwhirl",  "Poliwrath",  "Abra",       "Kadabra",
    "Alakazam",   "Machop",     "Machoke",    "Machamp",    "Bellsprout",
    "Weepinbell", "Victreebel", "Tentacool",  "Tentacruel", "Geodude",
    "Graveler",   "Golem",      "Ponyta",     "Rapidash",   "Slowpoke",
    "Slowbro",    "Magnemite",  "Magneton",   "Farfetch'd", "Doduo",
    "Dodrio",     "Seel",       "Dewgong",    "Grimer",     "Muk",
    "Shellder",   "Cloyster",   "Gastly",     "Haunter",    "Gengar",
    "Onix",       "Drowzee",    "Hypno",      "Krabby",     "Kingler",
    "Voltorb",    "Electrode",  "Exeggcute",  "Exeggutor",  "Cubone",
    "Marowak",    "Hitmonlee",  "Hitmonchan", "Lickitung",  "Koffing",
    "Weezing",    "Rhyhorn",    "Rhydon",     "Chansey",    "Tangela",
    "Kangaskhan", "Horsea",     "Seadra",     "Goldeen",    "Seaking",
    "Staryu",     "Starmie",    "Mr.Mime",    "Scyther",    "Jynx",
    "Electabuzz", "Magmar",     "Pinsir",     "Tauros",     "Magikarp",
    "Gyarados",   "Lapras",     "Ditto",      "Eevee",      "Vaporeon",
    "Jolteon",    "Flareon",    "Porygon",    "Omanyte",    "Omastar",
    "Kabuto",     "Kabutops",   "Aerodactyl", "Snorlax",    "Articuno",
    "Zapdos",     "Moltres",    "Dratini",    "Dragonair",  "Dragonite",
    "Mewtwo",     "Mew",
};

// ── D&D Spell records for enemy use ──────────────────────────────────────────
// Base powers are Pokemon-scale, not D&D dice.
// Effect codes: 0=none, 1=burn, 2=paralyze, 3=sleep, 4=poison, 5=freeze,
//               6=confuse, 7=flinch, 8=leech, 9=heal, 10=buff_atk, 11=buff_def,
//               12=stat_down_atk, 13=stat_down_def, 14=stun(miss next)

struct DnDSpell {
    const char* name;
    uint8_t     slotLevel;  // 0=cantrip, 1-9=slot
    uint8_t     pokeType;   // PokeType index
    uint8_t     basePower;  // 0 if healing/utility
    int8_t      hpEffect;   // positive=heal, negative=extra damage (as fixed HP)
    uint8_t     effectCode; // secondary effect (0=none)
    uint8_t     effectChance; // % chance of secondary effect (0-100)
    bool        isAoE;
};

// Spell list per D&D class — enemies draw from their class list.
// Indexed by DnDClass enum.
static constexpr DnDSpell DND_CLASS_SPELLS[][6] = {
/*  Fighter [0] */    {
        {"Action Surge",   0, 6 /*Fighting*/,  0,   0, 10, 100, false}, // buff: extra action = buff_atk
        {"Second Wind",    0, 0 /*Normal*/,    0,  20,  0,   0, false}, // heal 20 HP
        {"Trip Attack",    0, 6 /*Fighting*/,  50,  0, 13,  50, false}, // def down 50%
        {"Disarming Strike",0,6 /*Fighting*/,  55,  0, 12,  50, false}, // atk down 50%
        {"Sweeping Attack",0, 6 /*Fighting*/,  65,  0,  0,   0, true }, // AoE
        {"Push Attack",    0, 6 /*Fighting*/,  45,  0,  7,  30, false}, // flinch
    },
/*  Barbarian [1] */ {
        {"Rage",           0, 6 /*Fighting*/,  0,   0, 10, 100, false}, // rage = buff_atk
        {"Reckless Attack",0, 6 /*Fighting*/,  80,  0,  0,   0, false}, // high power, no effect
        {"Frenzy Throw",   0, 12/*Rock*/,      60,  0,  7,  20, false},
        {"Intimidate",     0, 0 /*Normal*/,    0,   0, 12, 100, false}, // atk down
        {"Bear Totem",     0, 0 /*Normal*/,    0,   0, 11, 100, false}, // def buff self
        {"Brutal Critical",0, 6 /*Fighting*/,  95,  0,  0,   0, false},
    },
/*  Ranger [2] */    {
        {"Hunter's Mark",  1, 0 /*Normal*/,    0,   0, 10, 100, false}, // atk buff
        {"Ensnaring Strike",1,4 /*Grass*/,     50,  0, 14, 50, false}, // stun
        {"Hail of Arrows", 1, 0 /*Normal*/,    55,  0,  0,   0, true }, // AoE
        {"Lightning Arrow",3, 3 /*Electric*/,  80,  0,  2,  20, false},
        {"Entangle",       1, 4 /*Grass*/,     0,   0, 14, 70, true }, // AoE stun
        {"Volley",         5, 0 /*Normal*/,    75,  0,  0,   0, true },
    },
/*  Rogue [3] */     {
        {"Sneak Attack",   0, 0 /*Normal*/,    75,  0,  0,   0, false},
        {"Cunning Action", 0, 0 /*Normal*/,    0,   0, 11, 100, false}, // def buff (dodge)
        {"Psychic Blade",  0,10 /*Psychic*/,   60,  0,  6,  30, false}, // confuse 30%
        {"Poison Dagger",  0, 7 /*Poison*/,    45,  0,  4,  40, false}, // poison
        {"Uncanny Dodge",  0, 0 /*Normal*/,    0,   0, 11, 100, false},
        {"Assassinate",    0, 0 /*Normal*/,    90,  0,  0,   0, false},
    },
/*  Wizard [4] */    {
        {"Fireball",       3, 1 /*Fire*/,      80,  0,  1,  10, true },
        {"Magic Missile",  1,10 /*Psychic*/,   45,  0,  0,   0, false}, // always hits
        {"Thunderwave",    1, 3 /*Electric*/,  50,  0,  2,  60, false}, // paralyze
        {"Ray of Frost",   0, 2 /*Water*/,     40,  0,  2,  10, false}, // slow
        {"Cone of Cold",   5, 5 /*Ice*/,       85,  0,  5,  20, true },
        {"Disintegrate",   6,10 /*Psychic*/,  120,  0,  0,   0, false},
    },
/*  Sorcerer [5] */  {
        {"Burning Hands",  1, 1 /*Fire*/,      50,  0,  1,  10, true },
        {"Chromatic Orb",  1, 0 /*Normal*/,    60,  0,  0,   0, false}, // picks type at random
        {"Lightning Bolt", 3, 3 /*Electric*/,  80,  0,  0,   0, false},
        {"Wild Surge",     0, 0 /*Normal*/,    40,  0,  6,  50, false}, // confuse self or enemy
        {"Chaos Bolt",     1, 0 /*Normal*/,    65,  0,  0,   0, false},
        {"Twinned Spell",  0, 0 /*Normal*/,    70,  0,  0,   0, false}, // hits twice
    },
/*  Warlock [6] */   {
        {"Eldritch Blast", 0,10 /*Psychic*/,   60,  0,  0,   0, false},
        {"Hex",            1, 7 /*Poison*/,    30,  0,  4, 100, false}, // curse/poison
        {"Hunger of Hadar",3,13 /*Ghost*/,     70,  0,  3,  20, true },
        {"Blight",         4, 7 /*Poison*/,    90,  0,  4,  50, false},
        {"Banishment",     4,10 /*Psychic*/,   0,   0, 14, 100, false}, // stun one turn
        {"Dark One's Blessing",0,13/*Ghost*/,  0,  15,  0,   0, false}, // heal on kill (passive here = small heal)
    },
/*  Cleric [7] */    {
        {"Sacred Flame",   0,10 /*Psychic*/,   40,  0,  0,   0, false},
        {"Guiding Bolt",   1,10 /*Psychic*/,   70,  0, 10,  100,false}, // buff next attack
        {"Cure Wounds",    1, 0 /*Normal*/,    0,  25,  0,   0, false},
        {"Spiritual Weapon",2,6 /*Fighting*/,  55,  0,  0,   0, false},
        {"Inflict Wounds", 1,13 /*Ghost*/,     75,  0,  0,   0, false},
        {"Harm",           6,13 /*Ghost*/,    100,  0,  0,   0, false},
    },
/*  Paladin [8] */   {
        {"Divine Smite",   1, 6 /*Fighting*/,  65,  0,  0,   0, false},
        {"Lay on Hands",   0, 0 /*Normal*/,    0,  30,  0,   0, false},
        {"Bless",          1, 0 /*Normal*/,    0,   0, 10, 100, false}, // atk buff
        {"Thunderous Smite",1,3 /*Electric*/,  70,  0,  7,  20, false},
        {"Searing Smite",  1, 1 /*Fire*/,      60,  0,  1,  100,false},
        {"Aura of Courage",3, 0 /*Normal*/,    0,   0, 11, 100, false}, // def buff
    },
/*  Druid [9] */     {
        {"Shillelagh",     0, 4 /*Grass*/,     50,  0, 10, 100, false}, // atk buff + attack
        {"Healing Word",   1, 4 /*Grass*/,     0,  20,  0,   0, false},
        {"Entangle",       1, 4 /*Grass*/,     0,   0, 14,  80, true }, // AoE stun
        {"Call Lightning", 3, 3 /*Electric*/,  80,  0,  2,  10, false},
        {"Erupting Earth", 3, 8 /*Ground*/,    75,  0, 13,  50, true },
        {"Sunbeam",        6, 1 /*Fire*/,      90,  0,  1,  30, false},
    },
/*  Bard [10] */     {
        {"Vicious Mockery",0,10 /*Psychic*/,   30,  0,  6,  100,false}, // confuse
        {"Cutting Words",  0, 0 /*Normal*/,    0,   0, 12, 100, false}, // atk down
        {"Hypnotic Pattern",3,10/*Psychic*/,   70,  0,  3,  70, true }, // sleep AoE
        {"Shatter",        2, 0 /*Normal*/,    60,  0,  0,   0, true },
        {"Power Word Pain",7,10 /*Psychic*/,  100,  0,  0,   0, false},
        {"Song of Rest",   0, 0 /*Normal*/,    0,  15,  0,   0, false}, // heal
    },
/*  Monk [11] */     {
        {"Flurry of Blows",0, 6 /*Fighting*/,  30,  0,  0,   0, false}, // hits twice
        {"Stunning Strike",0, 6 /*Fighting*/,  50,  0, 14,  40, false}, // stun
        {"Ki-Empowered",   0, 6 /*Fighting*/,  55,  0,  0,   0, false},
        {"Shadow Step",    0,13 /*Ghost*/,     45,  0, 10, 100, false}, // teleport + atk buff
        {"Empty Body",     0,10 /*Psychic*/,   0,   0, 11, 100, false}, // defense up
        {"Quivering Palm", 9, 6 /*Fighting*/,  90,  0, 14, 100, false}, // stun
    },
};
static_assert((uint8_t)DnDClass::COUNT == 12, "DND_CLASS_SPELLS size mismatch");

inline const DnDSpell* getClassSpells(DnDClass cls) {
    return DND_CLASS_SPELLS[(uint8_t)cls];
}

// ── Gen 1 damage formula ──────────────────────────────────────────────────────
// Returns raw damage before applying type effectiveness.
// levelBonus = floor(2*level/5) + 2
// raw = floor(levelBonus * attack * power / defense / 50) + 2
// Final = raw * typeEff / 100   (typeEff from typeEffectiveness())
// No random factor — deterministic for mesh play.
inline uint16_t gen1Damage(uint8_t level, uint16_t attack, uint8_t power,
                            uint16_t defense, uint8_t typeEff, bool stab) {
    if (power == 0) return 0;
    uint32_t lvlBonus = (uint32_t)(level * 2 / 5) + 2;
    uint32_t raw = lvlBonus * (uint32_t)attack * power / (defense > 0 ? defense : 1) / 50 + 2;
    if (stab) raw = raw * 150 / 100;  // 1.5x STAB
    uint32_t final = raw * typeEff / 100;
    return (uint16_t)(final > 65535 ? 65535 : final);
}

// ── Enemy actor ───────────────────────────────────────────────────────────────
// A dungeon enemy: Pokemon species + D&D class grafted on.
struct EnemyActor {
    uint8_t  dexNum;          // Pokédex number (1-151)
    DnDClass dndClass;
    uint8_t  classLevel;      // 1-20
    uint16_t hp;              // current HP
    uint16_t maxHp;
    uint16_t atk;             // computed stats (scaled from base + class level)
    uint16_t def;
    uint16_t spd;
    uint16_t spc;
    uint8_t  type1, type2;    // PokeType indices
    uint8_t  spellSlots[9];   // remaining slots per level (index 0 = level 1)
    uint32_t statusFlags;     // StatusFlag bitfield

    const char* speciesName() const {
        return (dexNum <= 151) ? POKEDEX_NAMES[dexNum] : "???";
    }
    const char* className() const {
        static const char* NAMES[] = {
            "Fighter","Barbarian","Ranger","Rogue","Wizard","Sorcerer",
            "Warlock","Cleric","Paladin","Druid","Bard","Monk"
        };
        uint8_t i = (uint8_t)dndClass;
        return (i < 12) ? NAMES[i] : "???";
    }
};

// ── Player actor snapshot ─────────────────────────────────────────────────────
// Filled from save data at battle start. Attack uses STR modifier from trainer.
struct PlayerActor {
    uint8_t  dexNum;
    uint16_t hp;
    uint16_t maxHp;
    uint16_t atk;   // from save data + trainer STR modifier
    uint16_t def;   // from save data + trainer CON modifier
    uint16_t spd;   // from save data
    uint16_t spc;   // from save data
    uint8_t  level;
    uint8_t  type1, type2;
    uint32_t statusFlags;
    char     moveName[4][11]; // up to 4 move names (placeholder — actual moves from save)
};

// ── Ability score → stat modifier (D&D convention, +/- percentage) ───────────
// STR boosts party attack by 1% per point above 10
// CON boosts party HP pool by 1% per point above 10
inline int8_t abilityMod(uint8_t score) { return (int8_t)(score / 2) - 5; }

// ── DungeonBattle ─────────────────────────────────────────────────────────────
// Manages one encounter: player(s) vs one or more enemies.
// Phase 2: one wild Pokemon encounter or one NPC trainer's lead Pokemon.
// Initiative: use Speed stat (Pokemon convention).

class DungeonBattle {
public:
    static constexpr uint8_t MAX_ENEMIES = 6;
    static constexpr uint8_t MAX_PLAYERS = 5;

    // Build a wild Pokemon encounter for a given floor depth and party size.
    static EnemyActor buildWildEnemy(uint8_t depth, uint8_t dexNum,
                                      DnDClass cls, uint32_t seed);

    // Build an NPC trainer encounter (party of up to 6 enemies).
    static void buildNpcTrainer(uint8_t depth, uint8_t partySize,
                                 EnemyActor out[], uint8_t &count,
                                 uint32_t seed);

    // Apply trainer ability score bonuses to a PlayerActor
    static void applyTrainerBonuses(PlayerActor &actor,
                                     const AbilityScores &scores);

    // Compute damage from player attacking enemy with a move.
    // moveName → look up in move table (Phase 2: use flat 40-base-power moves)
    // Returns damage dealt. Updates enemy HP and status.
    static uint16_t playerAttack(PlayerActor &attacker, EnemyActor &target,
                                  const char *moveName);

    // Compute damage from enemy attacking player.
    // Returns damage dealt. Updates player HP and status.
    static uint16_t enemyAttack(EnemyActor &attacker, PlayerActor &target,
                                 uint32_t &rng);

    // Tick status conditions at end of turn.
    // Returns HP lost to burn/poison this turn.
    static uint8_t tickStatus(uint32_t &flags, uint16_t &hp);

    // Check if a status effect clears at end of battle (in-battle conditions only).
    static void clearBattleEndStatus(uint32_t &flags);

    // Enemy AI: pick best action given current state.
    // Returns spell index (0-5 for spell, 0xFF for physical Pokemon move).
    static uint8_t pickEnemyAction(const EnemyActor &enemy,
                                    const PlayerActor &player,
                                    uint32_t &rng);

private:
    static uint32_t lcg(uint32_t &state);
};
