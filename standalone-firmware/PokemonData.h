#pragma once
#include <Arduino.h>

// ── ROM version enum (shared by EmulatorApp, Lobby, etc.) ────────────────────
enum class RomVersion : uint8_t {
    RED     = 0,
    BLUE    = 1,
    YELLOW  = 2,
    GOLD    = 3,
    SILVER  = 4,
    CRYSTAL = 5,
    UNKNOWN = 0xFF
};

// ── Gen 1 (Red/Blue) WRAM addresses ──────────────────────────────────────────
// Source: pret/pokered wram.asm — verified against community documentation.
// All values confirmed for Red/Blue (US). Yellow addresses differ slightly;
// Crystal (Gen 2) requires a separate address table — use RomVersion enum below.

namespace Gen1 {

// ── Player party ──────────────────────────────────────────────────────────────
static constexpr uint16_t wPartyCount       = 0xD163;  // 1 byte,   0–6
static constexpr uint16_t wPartySpecies     = 0xD164;  // 7 bytes,  species IDs + 0xFF terminator
static constexpr uint16_t wPartyMons        = 0xD16B;  // 264 bytes, 6 × 44-byte structs
static constexpr uint16_t wPartyMonOT       = 0xD273;  // 66 bytes,  6 × 11-byte OT names
static constexpr uint16_t wPartyMonNicks    = 0xD2B5;  // 66 bytes,  6 × 11-byte nicknames

// ── Enemy party (written by shim before battle) ──────────────────────────────
static constexpr uint16_t wEnemyPartyCount  = 0xD89C;  // 1 byte
static constexpr uint16_t wEnemyPartySpec   = 0xD89D;  // 7 bytes, species + 0xFF
static constexpr uint16_t wEnemyMons        = 0xD8A4;  // 264 bytes, 6 × 44

// ── Player info ─────────────────────────────────────────────────────────────
static constexpr uint16_t wPlayerName        = 0xD158;  // 11 bytes, Gen1-encoded

// ── Battle state ──────────────────────────────────────────────────────────────
static constexpr uint16_t wIsInBattle       = 0xD057;  // 0=no, 1=wild, 2=trainer/link
static constexpr uint16_t wBattleType       = 0xD05A;  // 0=wild/trainer, 1=link
static constexpr uint16_t wLinkState        = 0xC4F2;  // link protocol state byte
static constexpr uint16_t wLinkPlayerNumber = 0xC4F3;  // 1=master, 2=slave

// Active battle mon copies (populated when battle starts)
static constexpr uint16_t wBattleMon        = 0xD014;  // player's active mon (44 bytes)
static constexpr uint16_t wEnemyMon         = 0xCFEB;  // enemy's  active mon (44 bytes)

// ── Stat stage modifiers (−6..+6, stored as 1–13 with 7 = neutral) ───────────
static constexpr uint16_t wPlayerAtkMod     = 0xCD1A;
static constexpr uint16_t wPlayerDefMod     = 0xCD1B;
static constexpr uint16_t wPlayerSpdMod     = 0xCD1C;
static constexpr uint16_t wPlayerSpcMod     = 0xCD1D;
static constexpr uint16_t wEnemyAtkMod      = 0xCD2E;
static constexpr uint16_t wEnemyDefMod      = 0xCD2F;
static constexpr uint16_t wEnemySpdMod      = 0xCD30;
static constexpr uint16_t wEnemySpcMod      = 0xCD31;

// ── Move selection ────────────────────────────────────────────────────────────
// wCurrentMenuItem: 0–3 while on move select screen
static constexpr uint16_t wCurrentMenuItem  = 0xCC26;
// wPlayerSelectedMove: set when player confirms a move (0–3)
static constexpr uint16_t wPlayerSelectedMove = 0xCC28;

// ── RNG (HRAM region — use hram_io[addr - 0xFF00]) ───────────────────────────
static constexpr uint16_t hRandomAdd        = 0xFFD3;  // HRAM — RNG accumulator
static constexpr uint16_t hRandomSub        = 0xFFD4;  // HRAM — RNG subtractor

// ── Useful constants ──────────────────────────────────────────────────────────
static constexpr uint16_t PARTY_MON_SIZE    = 44;
static constexpr uint8_t  PARTY_MAX         = 6;
static constexpr uint8_t  NAME_LEN          = 11;  // max name bytes (incl. 0x50 terminator)
static constexpr uint8_t  PARTY_SPEC_SIZE   = 7;   // 6 species + 0xFF terminator

// ── Link cable protocol bytes ─────────────────────────────────────────────────
static constexpr uint8_t  SERIAL_IDLE       = 0x00;
static constexpr uint8_t  SERIAL_MASTER     = 0x01;
static constexpr uint8_t  SERIAL_SLAVE      = 0x02;
static constexpr uint8_t  SERIAL_CABLE_CLUB = 0x60;
static constexpr uint8_t  LINKED_POKE       = 0x61;
static constexpr uint8_t  SERIAL_TRADE      = 0xD4;
static constexpr uint8_t  SERIAL_BATTLE     = 0xD5;
static constexpr uint8_t  SERIAL_CANCEL     = 0xD6;
static constexpr uint8_t  SERIAL_PREAMBLE   = 0xFD;
static constexpr uint8_t  SERIAL_NO_DATA    = 0xFE;
static constexpr uint8_t  SERIAL_TERMINATE  = 0xFF;

} // namespace Gen1

// ── Gen 1 Pokémon data structure (44 bytes, matches wPartyMons layout) ────────
// Stored in WRAM exactly as-is. Can be memcpy'd to/from WRAM directly.
#pragma pack(push, 1)
struct Gen1Pokemon {
    uint8_t  species;       // +0x00  Internal species index
    uint8_t  hp[2];         // +0x01  Current HP (big-endian)
    uint8_t  boxLevel;      // +0x03  Level in box
    uint8_t  status;        // +0x04  Status condition bitmask
    uint8_t  type1;         // +0x05
    uint8_t  type2;         // +0x06
    uint8_t  catchRate;     // +0x07  Catch rate / held item
    uint8_t  moves[4];      // +0x08  Move IDs (0 = empty)
    uint8_t  otId[2];       // +0x0C  OT trainer ID (big-endian)
    uint8_t  exp[3];        // +0x0E  Experience (24-bit big-endian)
    uint8_t  hpExp[2];      // +0x11  HP stat EV
    uint8_t  atkExp[2];     // +0x13  Attack EV
    uint8_t  defExp[2];     // +0x15  Defense EV
    uint8_t  spdExp[2];     // +0x17  Speed EV
    uint8_t  spcExp[2];     // +0x19  Special EV
    uint8_t  dvs[2];        // +0x1B  DVs packed: [AtkDef][SpdSpc]
    uint8_t  pp[4];         // +0x1D  PP for moves 0–3 (bits 6-7 = PP ups)
    uint8_t  level;         // +0x21  Current level
    uint8_t  maxHp[2];      // +0x22  Max HP (big-endian)
    uint8_t  atk[2];        // +0x24
    uint8_t  def[2];        // +0x26
    uint8_t  spd[2];        // +0x28
    uint8_t  spc[2];        // +0x2A
    // Total: 0x2C = 44 bytes
};
static_assert(sizeof(Gen1Pokemon) == 44, "Gen1Pokemon must be exactly 44 bytes");
#pragma pack(pop)

// ── Full party (what we read from WRAM and send over LoRa) ───────────────────
struct Gen1Party {
    uint8_t     count;
    uint8_t     species[7];           // 6 IDs + 0xFF terminator
    Gen1Pokemon mons[6];              // 264 bytes
    uint8_t     otNames[6][11];       // OT names
    uint8_t     nicknames[6][11];     // Pokémon nicknames
    // Total: 1 + 7 + 264 + 66 + 66 = 404 bytes
};

// ── Status bitmask constants ──────────────────────────────────────────────────
static constexpr uint8_t STATUS_SLEEP   = 0x07;  // bits 0-2: sleep turn count
static constexpr uint8_t STATUS_POISON  = 0x08;
static constexpr uint8_t STATUS_BURNED  = 0x10;
static constexpr uint8_t STATUS_FROZEN  = 0x20;
static constexpr uint8_t STATUS_PARALYZ = 0x40;
static constexpr uint8_t STATUS_TOXIC   = 0x80;  // badly poisoned

// ── Helpers — read a big-endian uint16 from a 2-byte array ───────────────────
inline uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
inline void setBe16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}
