#pragma once
#include <Arduino.h>

// ── Gen 1 species name table ────────────────────────────────────────────────
// Internal index → English name. Covers 0x00–0xBE (191 species).

inline const char *gen1SpeciesName(uint8_t id) {
    static const char *const NAMES[] = {
        "???",        "RHYDON",     "KANGASKHAN", "NIDORAN M",  // 0x00-0x03
        "CLEFAIRY",   "SPEAROW",    "VOLTORB",    "NIDOKING",   // 0x04-0x07
        "SLOWBRO",    "IVYSAUR",    "EXEGGUTOR",  "LICKITUNG",  // 0x08-0x0B
        "EXEGGCUTE",  "GRIMER",     "GENGAR",     "NIDORAN F",  // 0x0C-0x0F
        "NIDOQUEEN",  "CUBONE",     "RHYHORN",    "LAPRAS",     // 0x10-0x13
        "ARCANINE",   "MEW",        "GYARADOS",   "SHELLDER",   // 0x14-0x17
        "TENTACOOL",  "GASTLY",     "SCYTHER",    "STARYU",     // 0x18-0x1B
        "BLASTOISE",  "PINSIR",     "TANGELA",    "???",        // 0x1C-0x1F
        "???",        "GROWLITHE",  "ONIX",       "FEAROW",     // 0x20-0x23
        "PIDGEY",     "SLOWPOKE",   "KADABRA",    "GRAVELER",   // 0x24-0x27
        "CHANSEY",    "MACHOKE",    "MR. MIME",   "HITMONLEE",  // 0x28-0x2B
        "HITMONCHAN", "ARBOK",      "PARASECT",   "PSYDUCK",    // 0x2C-0x2F
        "DROWZEE",    "GOLEM",      "???",        "MAGMAR",     // 0x30-0x33
        "???",        "ELECTABUZZ", "MAGNETON",   "KOFFING",    // 0x34-0x37
        "???",        "MANKEY",     "SEEL",       "DIGLETT",    // 0x38-0x3B
        "TAUROS",     "???",        "???",        "???",        // 0x3C-0x3F
        "FARFETCH'D", "VENONAT",    "DRAGONITE",  "???",        // 0x40-0x43
        "???",        "???",        "DODUO",      "POLIWAG",    // 0x44-0x47
        "JYNX",       "MOLTRES",    "ARTICUNO",   "ZAPDOS",     // 0x48-0x4B
        "DITTO",      "MEOWTH",     "KRABBY",     "???",        // 0x4C-0x4F
        "???",        "???",        "VULPIX",     "NINETALES",  // 0x50-0x53
        "PIKACHU",    "RAICHU",     "???",        "???",        // 0x54-0x57
        "DRATINI",    "DRAGONAIR",  "KABUTO",     "KABUTOPS",   // 0x58-0x5B
        "HORSEA",     "SEADRA",     "???",        "???",        // 0x5C-0x5F
        "SANDSHREW",  "SANDSLASH",  "OMANYTE",    "OMASTAR",    // 0x60-0x63
        "JIGGLYPUFF", "WIGGLYTUFF", "EEVEE",      "FLAREON",    // 0x64-0x67
        "JOLTEON",    "VAPOREON",   "MACHOP",     "ZUBAT",      // 0x68-0x6B
        "EKANS",      "PARAS",      "POLIWHIRL",  "POLIWRATH",  // 0x6C-0x6F
        "WEEDLE",     "KAKUNA",     "BEEDRILL",   "???",        // 0x70-0x73
        "DODRIO",     "PRIMEAPE",   "DUGTRIO",    "VENOMOTH",   // 0x74-0x77
        "DEWGONG",    "???",        "???",        "CATERPIE",   // 0x78-0x7B
        "METAPOD",    "BUTTERFREE", "MACHAMP",    "???",        // 0x7C-0x7F
        "GOLDUCK",    "HYPNO",      "GOLBAT",     "MEWTWO",     // 0x80-0x83
        "SNORLAX",    "MAGIKARP",   "???",        "???",        // 0x84-0x87
        "MUK",        "???",        "KINGLER",    "CLOYSTER",   // 0x88-0x8B
        "???",        "ELECTRODE",  "CLEFABLE",   "WEEZING",    // 0x8C-0x8F
        "PERSIAN",    "MAROWAK",    "???",        "HAUNTER",    // 0x90-0x93
        "ABRA",       "ALAKAZAM",   "PIDGEOTTO",  "PIDGEOT",    // 0x94-0x97
        "STARMIE",    "BULBASAUR",  "VENUSAUR",   "TENTACRUEL", // 0x98-0x9B
        "???",        "GOLDEEN",    "SEAKING",    "???",        // 0x9C-0x9F
        "???",        "???",        "???",        "PONYTA",     // 0xA0-0xA3
        "RAPIDASH",   "RATTATA",    "RATICATE",   "NIDORINO",   // 0xA4-0xA7
        "NIDORINA",   "GEODUDE",    "PORYGON",    "AERODACTYL", // 0xA8-0xAB
        "???",        "MAGNEMITE",  "???",        "???",        // 0xAC-0xAF
        "CHARMANDER", "SQUIRTLE",   "CHARMELEON", "WARTORTLE",  // 0xB0-0xB3
        "CHARIZARD",  "???",        "???",        "???",        // 0xB4-0xB7
        "???",        "ODDISH",     "GLOOM",      "VILEPLUME",  // 0xB8-0xBB
        "BELLSPROUT", "WEEPINBELL", "VICTREEBEL",               // 0xBC-0xBE
    };
    if (id > 0xBE) return "???";
    return NAMES[id];
}

// Gen1 charset → ASCII conversion
#ifndef GEN1_CHAR_TO_ASCII_DEFINED
#define GEN1_CHAR_TO_ASCII_DEFINED
inline char gen1CharToAscii(uint8_t c) {
    if (c >= 0x80 && c <= 0x99) return 'A' + (c - 0x80);
    if (c >= 0xA0 && c <= 0xB9) return 'a' + (c - 0xA0);
    if (c >= 0xF6 && c <= 0xFF) return '0' + (c - 0xF6);
    if (c == 0x7F) return ' ';
    if (c == 0x50) return '\0';
    if (c == 0xE8) return '.';
    return '?';
}
#endif  // GEN1_CHAR_TO_ASCII_DEFINED

inline void gen1NameToAscii(const uint8_t *src, uint8_t srcLen,
                            char *dst, uint8_t dstLen) {
    uint8_t j = 0;
    for (uint8_t i = 0; i < srcLen && j < dstLen - 1; i++) {
        if (src[i] == 0x50) break;
        dst[j++] = gen1CharToAscii(src[i]);
    }
    dst[j] = '\0';
}
