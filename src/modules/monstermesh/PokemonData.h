#pragma once
#include <Arduino.h>

// ── Gen 1 (Red/Blue) WRAM addresses ──────────────────────────────────────────
namespace Gen1 {

static constexpr uint16_t wPartyCount       = 0xD163;
static constexpr uint16_t wPartySpecies     = 0xD164;
static constexpr uint16_t wPartyMons        = 0xD16B;
static constexpr uint16_t wPartyMonOT       = 0xD273;
static constexpr uint16_t wPartyMonNicks    = 0xD2B5;

static constexpr uint16_t wEnemyPartyCount  = 0xD89C;
static constexpr uint16_t wEnemyPartySpec   = 0xD89D;
static constexpr uint16_t wEnemyMons        = 0xD8A4;

static constexpr uint16_t wPlayerName        = 0xD158;

static constexpr uint16_t wIsInBattle       = 0xD057;
static constexpr uint16_t wBattleType       = 0xD05A;
static constexpr uint16_t wLinkState        = 0xC4F2;
static constexpr uint16_t wLinkPlayerNumber = 0xC4F3;

static constexpr uint16_t wBattleMon        = 0xD014;
static constexpr uint16_t wEnemyMon         = 0xCFEB;

static constexpr uint16_t hRandomAdd        = 0xFFD3;
static constexpr uint16_t hRandomSub        = 0xFFD4;

static constexpr uint16_t PARTY_MON_SIZE    = 44;
static constexpr uint8_t  PARTY_MAX         = 6;
static constexpr uint8_t  NAME_LEN          = 11;
static constexpr uint8_t  PARTY_SPEC_SIZE   = 7;

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

#pragma pack(push, 1)
struct Gen1Pokemon {
    uint8_t  species;
    uint8_t  hp[2];
    uint8_t  boxLevel;
    uint8_t  status;
    uint8_t  type1;
    uint8_t  type2;
    uint8_t  catchRate;
    uint8_t  moves[4];
    uint8_t  otId[2];
    uint8_t  exp[3];
    uint8_t  hpExp[2];
    uint8_t  atkExp[2];
    uint8_t  defExp[2];
    uint8_t  spdExp[2];
    uint8_t  spcExp[2];
    uint8_t  dvs[2];
    uint8_t  pp[4];
    uint8_t  level;
    uint8_t  maxHp[2];
    uint8_t  atk[2];
    uint8_t  def[2];
    uint8_t  spd[2];
    uint8_t  spc[2];
};
static_assert(sizeof(Gen1Pokemon) == 44, "Gen1Pokemon must be exactly 44 bytes");
#pragma pack(pop)

struct Gen1Party {
    uint8_t     count;
    uint8_t     species[7];
    Gen1Pokemon mons[6];
    uint8_t     otNames[6][11];
    uint8_t     nicknames[6][11];
};

inline uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
inline void setBe16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}
