#pragma once
#include <Arduino.h>

// ── Gen 1 internal species ID → name lookup ──────────────────────────────────
// Internal IDs are NOT Pokédex numbers. Source: pret/pokered.
// Highest valid index: 0xBE (VICTREEBEL). Empty strings = MISSINGNO.
// Uses C++17 inline to share one copy across all TUs.

inline const char GEN1_SPECIES[][11] = {
    "",           // 0x00 (none)
    "RHYDON",     // 0x01
    "KANGASKHAN", // 0x02
    "NIDORAN M",  // 0x03
    "CLEFAIRY",   // 0x04
    "SPEAROW",    // 0x05
    "VOLTORB",    // 0x06
    "NIDOKING",   // 0x07
    "SLOWBRO",    // 0x08
    "IVYSAUR",    // 0x09
    "EXEGGUTOR",  // 0x0A
    "LICKITUNG",  // 0x0B
    "EXEGGCUTE",  // 0x0C
    "GRIMER",     // 0x0D
    "GENGAR",     // 0x0E
    "NIDORAN F",  // 0x0F
    "NIDOQUEEN",  // 0x10
    "CUBONE",     // 0x11
    "RHYHORN",    // 0x12
    "LAPRAS",     // 0x13
    "ARCANINE",   // 0x14
    "MEW",        // 0x15
    "GYARADOS",   // 0x16
    "SHELLDER",   // 0x17
    "TENTACOOL",  // 0x18
    "GASTLY",     // 0x19
    "SCYTHER",    // 0x1A
    "STARYU",     // 0x1B
    "BLASTOISE",  // 0x1C
    "PINSIR",     // 0x1D
    "TANGELA",    // 0x1E
    "",           // 0x1F
    "",           // 0x20
    "GROWLITHE",  // 0x21
    "ONIX",       // 0x22
    "FEAROW",     // 0x23
    "PIDGEY",     // 0x24
    "SLOWPOKE",   // 0x25
    "KADABRA",    // 0x26
    "GRAVELER",   // 0x27
    "CHANSEY",    // 0x28
    "MACHOKE",    // 0x29
    "MR.MIME",    // 0x2A
    "HITMONLEE",  // 0x2B
    "HITMONCHAN", // 0x2C
    "ARBOK",      // 0x2D
    "PARASECT",   // 0x2E
    "PSYDUCK",    // 0x2F
    "DROWZEE",    // 0x30
    "GOLEM",      // 0x31
    "",           // 0x32
    "MAGMAR",     // 0x33
    "",           // 0x34
    "ELECTABUZZ", // 0x35
    "MAGNETON",   // 0x36
    "KOFFING",    // 0x37
    "",           // 0x38
    "MANKEY",     // 0x39
    "SEEL",       // 0x3A
    "DIGLETT",    // 0x3B
    "TAUROS",     // 0x3C
    "",           // 0x3D
    "",           // 0x3E
    "",           // 0x3F
    "FARFETCHD",  // 0x40
    "VENONAT",    // 0x41
    "DRAGONITE",  // 0x42
    "",           // 0x43
    "",           // 0x44
    "",           // 0x45
    "DODUO",      // 0x46
    "POLIWAG",    // 0x47
    "JYNX",       // 0x48
    "MOLTRES",    // 0x49
    "ARTICUNO",   // 0x4A
    "ZAPDOS",     // 0x4B
    "DITTO",      // 0x4C
    "MEOWTH",     // 0x4D
    "KRABBY",     // 0x4E
    "",           // 0x4F
    "",           // 0x50
    "",           // 0x51
    "VULPIX",     // 0x52
    "NINETALES",  // 0x53
    "PIKACHU",    // 0x54
    "RAICHU",     // 0x55
    "",           // 0x56
    "",           // 0x57
    "DRATINI",    // 0x58
    "DRAGONAIR",  // 0x59
    "KABUTO",     // 0x5A
    "KABUTOPS",   // 0x5B
    "HORSEA",     // 0x5C
    "SEADRA",     // 0x5D
    "",           // 0x5E
    "",           // 0x5F
    "SANDSHREW",  // 0x60
    "SANDSLASH",  // 0x61
    "OMANYTE",    // 0x62
    "OMASTAR",    // 0x63
    "JIGGLYPUFF", // 0x64
    "WIGGLYTUFF", // 0x65
    "EEVEE",      // 0x66
    "FLAREON",    // 0x67
    "JOLTEON",    // 0x68
    "VAPOREON",   // 0x69
    "MACHOP",     // 0x6A
    "ZUBAT",      // 0x6B
    "EKANS",      // 0x6C
    "PARAS",      // 0x6D
    "POLIWHIRL",  // 0x6E
    "POLIWRATH",  // 0x6F
    "WEEDLE",     // 0x70
    "KAKUNA",     // 0x71
    "BEEDRILL",   // 0x72
    "",           // 0x73
    "DODRIO",     // 0x74
    "PRIMEAPE",   // 0x75
    "DUGTRIO",    // 0x76
    "VENOMOTH",   // 0x77
    "DEWGONG",    // 0x78
    "",           // 0x79
    "",           // 0x7A
    "CATERPIE",   // 0x7B
    "METAPOD",    // 0x7C
    "BUTTERFREE", // 0x7D
    "MACHAMP",    // 0x7E
    "",           // 0x7F
    "GOLDUCK",    // 0x80
    "HYPNO",      // 0x81
    "GOLBAT",     // 0x82
    "MEWTWO",     // 0x83
    "SNORLAX",    // 0x84
    "MAGIKARP",   // 0x85
    "",           // 0x86
    "",           // 0x87
    "MUK",        // 0x88
    "",           // 0x89
    "KINGLER",    // 0x8A
    "CLOYSTER",   // 0x8B
    "",           // 0x8C
    "ELECTRODE",  // 0x8D
    "CLEFABLE",   // 0x8E
    "WEEZING",    // 0x8F
    "PERSIAN",    // 0x90
    "MAROWAK",    // 0x91
    "",           // 0x92
    "HAUNTER",    // 0x93
    "ABRA",       // 0x94
    "ALAKAZAM",   // 0x95
    "PIDGEOTTO",  // 0x96
    "PIDGEOT",    // 0x97
    "STARMIE",    // 0x98
    "BULBASAUR",  // 0x99
    "VENUSAUR",   // 0x9A
    "TENTACRUEL", // 0x9B
    "",           // 0x9C
    "GOLDEEN",    // 0x9D
    "SEAKING",    // 0x9E
    "",           // 0x9F
    "",           // 0xA0
    "",           // 0xA1
    "",           // 0xA2
    "PONYTA",     // 0xA3
    "RAPIDASH",   // 0xA4
    "RATTATA",    // 0xA5
    "RATICATE",   // 0xA6
    "NIDORINO",   // 0xA7
    "NIDORINA",   // 0xA8
    "GEODUDE",    // 0xA9
    "PORYGON",    // 0xAA
    "AERODACTYL", // 0xAB
    "",           // 0xAC
    "MAGNEMITE",  // 0xAD
    "",           // 0xAE
    "",           // 0xAF
    "CHARMANDER", // 0xB0
    "SQUIRTLE",   // 0xB1
    "CHARMELEON", // 0xB2
    "WARTORTLE",  // 0xB3
    "CHARIZARD",  // 0xB4
    "",           // 0xB5
    "",           // 0xB6
    "",           // 0xB7
    "",           // 0xB8
    "ODDISH",     // 0xB9
    "GLOOM",      // 0xBA
    "VILEPLUME",  // 0xBB
    "BELLSPROUT", // 0xBC
    "WEEPINBELL", // 0xBD
    "VICTREEBEL", // 0xBE
};

static constexpr uint8_t GEN1_SPECIES_MAX = 0xBF;

inline const char *gen1SpeciesName(uint8_t id) {
    if (id >= GEN1_SPECIES_MAX) return "???";
    if (GEN1_SPECIES[id][0] == '\0') return "???";
    return GEN1_SPECIES[id];
}

// ── Gen 1 character encoding → ASCII ─────────────────────────────────────────
// Pokémon Red/Blue uses a custom charset. Key ranges:
//   0x50       = string terminator
//   0x7F       = space
//   0x80–0x99  = A–Z
//   0xA0–0xB9  = a–z
//   0xF6–0xFF  = 0–9

inline char gen1CharToAscii(uint8_t c) {
    if (c >= 0x80 && c <= 0x99) return 'A' + (c - 0x80);
    if (c >= 0xA0 && c <= 0xB9) return 'a' + (c - 0xA0);
    if (c >= 0xF6)              return '0' + (c - 0xF6);
    if (c == 0x7F)              return ' ';
    if (c == 0x50)              return '\0';
    return '?';
}

// Convert a Gen1-encoded name from WRAM into a null-terminated ASCII string.
inline void gen1NameToAscii(const uint8_t *src, size_t srcLen,
                            char *dst, size_t dstLen) {
    size_t j = 0;
    for (size_t i = 0; i < srcLen && j < dstLen - 1; i++) {
        if (src[i] == 0x50) break;
        char c = gen1CharToAscii(src[i]);
        if (c != '\0') dst[j++] = c;
    }
    dst[j] = '\0';
}
