// SPDX-License-Identifier: MIT
// See Gen1BattleEngine.h for description and credits.

#include "../shared/platform.h"
#include "Gen1BattleEngine.h"
#include "../shared/Gen1Species.h"          // gen1CharToAscii — nicknames in Gen 1 charset
#include "../shared/DaycareSavPatcher.h"    // internalToDex[] — SAV uses internal hex
#include "showdown_gen3_basestats.h"        // GEN3_BASE_STATS (Special split + modern types)
#include "showdown_gen3_typechart.h"        // GEN3_TYPECHART / GEN3_TYPE_IS_SPECIAL
#include "showdown_gen3_moves.h"            // GEN3_MOVES / gen3Move()
#include <string.h>
#include <stdio.h>

// Move data for the active mechanics generation. gen3Move re-types/re-powers
// the same 165 ids with identical effects+PP, so callers don't care which gen.
const Gen1MoveData *Gen1BattleEngine::mdata(uint8_t id) const {
    return isGen3() ? gen3Move(id) : gen1Move(id);
}

// ── xoshiro128+ deterministic RNG ───────────────────────────────────────────

static inline uint32_t rotl32(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

uint32_t Gen1BattleEngine::rand32()
{
    const uint32_t r  = s_[0] + s_[3];
    const uint32_t t  = s_[1] << 9;
    s_[2] ^= s_[0]; s_[3] ^= s_[1];
    s_[1] ^= s_[2]; s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3]  = rotl32(s_[3], 11);
    return r;
}

uint8_t Gen1BattleEngine::rand8()        { return rand32() & 0xFF; }
bool    Gen1BattleEngine::percent(uint8_t p) { return (rand32() % 100) < p; }

// ── Stat init from Gen1Pokemon save struct ──────────────────────────────────
//
// Gen 1 stat formula: floor(((Base + IV) * 2 + ceil(sqrt(StatExp)) / 4) * Level / 100) + 5
// HP adds + Level + 5 instead of +5.
// IVs: 4 bits each (HP IV is parity bits of Atk/Def/Spd/Spc IVs).

static uint16_t isqrt16(uint32_t v) {
    uint32_t r = 0, b = 1u << 15;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else            {              r >>= 1;          }
        b >>= 2;
    }
    return (uint16_t)r;
}

static uint16_t calcStat(uint8_t base, uint8_t iv, uint16_t expVal,
                          uint8_t level, bool isHp)
{
    uint32_t expBonus = (isqrt16(expVal) + 3) >> 2;  // ceil(sqrt/4) approx
    uint32_t v = ((uint32_t)(base + iv) * 2 + expBonus) * level / 100;
    return (uint16_t)(isHp ? v + level + 10 : v + 5);
}

// Re-establish the 0xFF sentinel fields after a memset(...,0,...) zeroes
// them, otherwise every battle thinks it's mid-charge in slot 0 and every
// move pick gets clobbered to "use the first move".
static inline void initSentinelFields(Gen1BattleEngine::BattlePoke &p)
{
    p.lastMoveIdx   = 0xFF;
    p.disabledSlot  = 0xFF;
    p.chargingSlot  = 0xFF;
    p.thrashSlot    = 0xFF;
    p.mimicSlot     = 0xFF;
}

void Gen1BattleEngine::initBattlePokeFromSave(BattlePoke &dst,
                                              const Gen1Pokemon &src,
                                              const uint8_t nick[11], uint8_t gen)
{
    memset(&dst, 0, sizeof(dst));
    initSentinelFields(dst);
    // Gen 1 SAV stores species as the internal hex code (0x01-0xBE), NOT the
    // pokedex index. The base-stats table is dex-indexed, so without this
    // conversion we'd look up garbage stats and the pokemon would be wrong.
    dst.species = internalToDex[src.species];
    if (nick && nick[0] != 0 && nick[0] != 0x50) {
        // Decode Gen 1 charset → ASCII so the battle UI shows readable names
        // instead of raw 0x80+ bytes. Skip if the source has no nickname
        // (zero-filled, e.g. LoC gym roster) — fall through to species name.
        for (uint8_t i = 0; i < 10; ++i) {
            char a = gen1CharToAscii(nick[i]);
            if (a == '\0') { dst.nickname[i] = '\0'; break; }
            dst.nickname[i] = a;
        }
    }
    dst.nickname[10] = 0;
    if (dst.nickname[0] == 0 || dst.nickname[0] == '?') {
        // No nickname provided — use the species name. dst.species is the
        // pokedex index (1..151); gen1SpeciesName takes the internal hex,
        // so route through dexToInternal first.
        uint8_t internal = (dst.species < 152) ? dexToInternal[dst.species] : 0;
        const char *sp = gen1SpeciesName(internal);
        if (sp) {
            uint8_t w = 0;
            while (sp[w] && w < 10) { dst.nickname[w] = sp[w]; ++w; }
            dst.nickname[w] = 0;
        }
    }
    dst.level = src.level ? src.level : src.boxLevel;

    // DVs: high nibble of dvs[0] = Atk, low = Def, high of dvs[1] = Spd, low = Spc.
    uint8_t atkDV = (src.dvs[0] >> 4) & 0xF;
    uint8_t defDV =  src.dvs[0]       & 0xF;
    uint8_t spdDV = (src.dvs[1] >> 4) & 0xF;
    uint8_t spcDV =  src.dvs[1]       & 0xF;
    uint8_t hpDV  = ((atkDV & 1) << 3) | ((defDV & 1) << 2) |
                    ((spdDV & 1) << 1) | (spcDV & 1);

    // GEN1_BASE_STATS is dex-indexed. src.species is the SAV's internal hex
    // code (0x01-0xBE), not the dex number — looking up b[src.species]
    // directly returns a different pokemon's base stats. Mewtwo (internal
    // 0x83) was being read as Lapras (dex 131), giving HP 301 instead of
    // ~265. Use dst.species (already converted to dex on line above).
    const Gen1BaseStats &b = GEN1_BASE_STATS[dst.species < 152 ? dst.species : 0];

    uint16_t hpExp  = be16(src.hpExp);
    uint16_t atkExp = be16(src.atkExp);
    uint16_t defExp = be16(src.defExp);
    uint16_t spdExp = be16(src.spdExp);
    uint16_t spcExp = be16(src.spcExp);

    if (gen >= 3) {
        // Gen 2/3: modern types + full stat block, with Special SPLIT. Both
        // Sp.Atk and Sp.Def are derived from the save's single Special DV/exp
        // (Gen-1 saves only store one), so they differ purely by base stat.
        const Gen3BaseStats &g = GEN3_BASE_STATS[dst.species < 387 ? dst.species : 0];
        dst.type1 = g.type1; dst.type2 = g.type2;
        dst.maxHp = calcStat(g.hp,  hpDV,  hpExp,  dst.level, true);
        dst.atk   = calcStat(g.atk, atkDV, atkExp, dst.level, false);
        dst.def   = calcStat(g.def, defDV, defExp, dst.level, false);
        dst.spd   = calcStat(g.spe, spdDV, spdExp, dst.level, false);  // g.spe = Speed
        dst.spaG3 = calcStat(g.spa, spcDV, spcExp, dst.level, false);
        dst.spdG3 = calcStat(g.spd, spcDV, spcExp, dst.level, false);  // g.spd = Sp.Def
        dst.spc   = dst.spaG3;  // legacy stat displays still read .spc
    } else {
        dst.type1 = b.type1; dst.type2 = b.type2;
        dst.maxHp = calcStat(b.hp,  hpDV,  hpExp,  dst.level, true);
        dst.atk   = calcStat(b.atk, atkDV, atkExp, dst.level, false);
        dst.def   = calcStat(b.def, defDV, defExp, dst.level, false);
        dst.spd   = calcStat(b.spd, spdDV, spdExp, dst.level, false);
        dst.spc   = calcStat(b.spc, spcDV, spcExp, dst.level, false);
    }
    dst.hp    = be16(src.hp);
    if (dst.hp == 0 || dst.hp > dst.maxHp) dst.hp = dst.maxHp;

    for (int _k = 0; _k < 4; ++_k) dst.moves[_k] = src.moves[_k];
    memcpy(dst.pp,    src.pp,    4);
    dst.status = src.status;
}

void Gen1BattleEngine::initBattlePokeFromBase(BattlePoke &dst,
                                              uint16_t species, uint8_t level,
                                              const uint8_t moves[4], uint8_t gen)
{
    memset(&dst, 0, sizeof(dst));
    initSentinelFields(dst);
    dst.species = species;
    dst.level   = level;
    // Wild encounters: average DVs = 8, no stat exp.
    if (gen >= 3) {
        const Gen3BaseStats &g = GEN3_BASE_STATS[species < 387 ? species : 0];
        dst.type1 = g.type1; dst.type2 = g.type2;
        dst.maxHp = calcStat(g.hp,  8, 0, level, true);
        dst.atk   = calcStat(g.atk, 8, 0, level, false);
        dst.def   = calcStat(g.def, 8, 0, level, false);
        dst.spd   = calcStat(g.spe, 8, 0, level, false);
        dst.spaG3 = calcStat(g.spa, 8, 0, level, false);
        dst.spdG3 = calcStat(g.spd, 8, 0, level, false);
        dst.spc   = dst.spaG3;
    } else {
        const Gen1BaseStats &b = GEN1_BASE_STATS[species < 152 ? species : 0];
        dst.type1 = b.type1; dst.type2 = b.type2;
        dst.maxHp = calcStat(b.hp,  8, 0, level, true);
        dst.atk   = calcStat(b.atk, 8, 0, level, false);
        dst.def   = calcStat(b.def, 8, 0, level, false);
        dst.spd   = calcStat(b.spd, 8, 0, level, false);
        dst.spc   = calcStat(b.spc, 8, 0, level, false);
    }
    dst.hp    = dst.maxHp;
    for (int _k = 0; _k < 4; ++_k) dst.moves[_k] = moves[_k];
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *m = (gen >= 3 ? gen3Move(moves[i]) : gen1Move(moves[i]));
        dst.pp[i] = m ? m->pp : 0;
    }
    snprintf(dst.nickname, sizeof(dst.nickname), "%s",
             (species < 152) ? "WILD" : "???");
}

// ── start() ─────────────────────────────────────────────────────────────────

void Gen1BattleEngine::start(const Gen1Party &p1, const Gen1Party &p2,
                             uint32_t rngSeed, uint8_t gen)
{
    rng_ = rngSeed; gen_ = gen; turn_ = 0; result_ = Result::ONGOING;
    pendAction_[0] = pendAction_[1] = 0xFF;

    // Seed xoshiro: spread the 32-bit input across all four state words.
    s_[0] = rngSeed ? rngSeed : 0xDEADBEEFu;
    s_[1] = rotl32(s_[0], 7)  ^ 0x9E3779B9u;
    s_[2] = rotl32(s_[1], 13) ^ 0x85EBCA77u;
    s_[3] = rotl32(s_[2], 17) ^ 0xC2B2AE3Du;

    auto initSide = [&](uint8_t side, const Gen1Party &pty) {
        BattleParty &bp = p_[side];
        memset(&bp, 0, sizeof(bp));
        bp.count = pty.count > MAX_PARTY ? MAX_PARTY : pty.count;
        for (uint8_t i = 0; i < bp.count; ++i) {
            initBattlePokeFromSave(bp.mons[i], pty.mons[i], pty.nicknames[i], gen_);
        }
        bp.active = 0;
    };
    initSide(0, p1);
    initSide(1, p2);
}

void Gen1BattleEngine::start(const WireParty &p1, const WireParty &p2,
                             uint32_t rngSeed, uint8_t gen)
{
    rng_ = rngSeed; gen_ = gen; turn_ = 0; result_ = Result::ONGOING;
    pendAction_[0] = pendAction_[1] = 0xFF;

    s_[0] = rngSeed ? rngSeed : 0xDEADBEEFu;
    s_[1] = rotl32(s_[0], 7)  ^ 0x9E3779B9u;
    s_[2] = rotl32(s_[1], 13) ^ 0x85EBCA77u;
    s_[3] = rotl32(s_[2], 17) ^ 0xC2B2AE3Du;

    auto initSide = [&](uint8_t side, const WireParty &pty) {
        BattleParty &bp = p_[side];
        memset(&bp, 0, sizeof(bp));
        bp.count = pty.count > MAX_PARTY ? MAX_PARTY : pty.count;
        for (uint8_t i = 0; i < bp.count; ++i)
            initBattlePokeFromWire(bp.mons[i], pty.mons[i], gen_);
        bp.active = 0;
    };
    initSide(0, p1);
    initSide(1, p2);
}

void Gen1BattleEngine::initBattlePokeFromWire(BattlePoke &dst, const WireMon &src,
                                              uint8_t gen)
{
    memset(&dst, 0, sizeof(dst));
    initSentinelFields(dst);
    dst.species = src.species;
    dst.level   = src.level;
    // Final stats verbatim — each save reader computed them for its own gen.
    dst.maxHp = src.maxHp;
    dst.hp    = src.maxHp;
    dst.atk = src.atk; dst.def = src.def; dst.spd = src.spe;
    dst.spaG3 = src.spa; dst.spdG3 = src.spd;
    dst.spc   = src.spa;   // gen-1 mode uses spc for both specials
    if (gen >= 3) {
        const Gen3BaseStats &g = GEN3_BASE_STATS[src.species < 387 ? src.species : 0];
        dst.type1 = g.type1; dst.type2 = g.type2;
    } else {
        const Gen1BaseStats &b = GEN1_BASE_STATS[src.species < 152 ? src.species : 0];
        dst.type1 = b.type1; dst.type2 = b.type2;
    }
    for (int i = 0; i < 4; ++i) {
        dst.moves[i] = src.moves[i];
        const Gen1MoveData *m = (gen >= 3 ? gen3Move(src.moves[i]) : gen1Move(src.moves[i]));
        dst.pp[i] = m ? m->pp : 0;
    }
    // Nickname = species name (Gen-1 table covers 1-151; else "#dex").
    uint8_t internal = (dst.species < 152) ? dexToInternal[dst.species] : 0;
    const char *sp = internal ? gen1SpeciesName(internal) : nullptr;
    if (sp) {
        uint8_t w = 0;
        while (sp[w] && w < 10) { dst.nickname[w] = sp[w]; ++w; }
        dst.nickname[w] = 0;
    } else {
        snprintf(dst.nickname, sizeof(dst.nickname), "#%u", (unsigned)dst.species);
    }
}

void Gen1BattleEngine::battlePokeToWireMon(const BattlePoke &src, WireMon &out)
{
    out.species = src.species;
    out.level   = src.level;
    out.maxHp = src.maxHp;
    out.atk = src.atk; out.def = src.def; out.spe = src.spd;  // engine spd = Speed
    out.spa = src.spaG3 ? src.spaG3 : src.spc;
    out.spd = src.spdG3 ? src.spdG3 : src.spc;
    for (int i = 0; i < 4; ++i) out.moves[i] = src.moves[i];
}

void Gen1BattleEngine::replaceOpponent(const Gen1Party &p)
{
    BattleParty &bp = p_[1];
    memset(&bp, 0, sizeof(bp));
    bp.count = p.count > MAX_PARTY ? MAX_PARTY : p.count;
    for (uint8_t i = 0; i < bp.count; ++i) {
        initBattlePokeFromSave(bp.mons[i], p.mons[i], p.nicknames[i], gen_);
    }
    bp.active = 0;
    // Battle is alive again — clear result & pending actions so the next
    // executeTurn cycle picks up.
    result_ = Result::ONGOING;
    pendAction_[0] = 0xFF; pendAction_[1] = 0xFF;
    pendIndex_[0]  = 0;    pendIndex_[1]  = 0;
}

// ── Action submission ───────────────────────────────────────────────────────

bool Gen1BattleEngine::submitAction(uint8_t side, uint8_t actionType, uint8_t index)
{
    side &= 1;
    pendAction_[side] = actionType;
    pendIndex_[side]  = index;
    return pendAction_[0] != 0xFF && pendAction_[1] != 0xFF;
}

// ── Stat-stage multiplier (Gen 1 table) ─────────────────────────────────────
// Returns x100 multiplier. Stages -6..+6 → {25,28,33,40,50,66,100,150,200,250,300,350,400}

int16_t Gen1BattleEngine::boostMult(int8_t stage)
{
    static const int16_t T[13] = {25,28,33,40,50,66,100,150,200,250,300,350,400};
    int s = stage + 6;
    if (s < 0)  s = 0;
    if (s > 12) s = 12;
    return T[s];
}

// ── Type effectiveness lookup ───────────────────────────────────────────────

uint8_t Gen1BattleEngine::effectiveness(uint8_t atkType, uint8_t defType) const
{
    if (isGen3()) {
        if (atkType >= GEN3_TYPE_COUNT || defType >= GEN3_TYPE_COUNT) return 2;
        return GEN3_TYPECHART[atkType][defType];
    }
    if (atkType >= GEN1_TYPE_COUNT || defType >= GEN1_TYPE_COUNT) return 2;
    return GEN1_TYPECHART[atkType][defType];
}

// ── Hit roll ────────────────────────────────────────────────────────────────

bool Gen1BattleEngine::rollHit(uint8_t side, uint8_t targetSide,
                                const Gen1MoveData &mv)
{
    if (mv.accuracy == 0) return true;  // never-miss moves (Swift)
    const BattlePoke &atk = p_[side].mons[p_[side].active];
    const BattlePoke &def = p_[targetSide].mons[p_[targetSide].active];
    int16_t accMult = boostMult(atk.accBoost);
    int16_t evaMult = boostMult(-def.evaBoost);  // higher evasion → lower hit
    uint32_t threshold = (uint32_t)mv.accuracy * accMult * evaMult / 10000;
    if (threshold > 100) threshold = 100;
    return (rand32() % 100) < threshold;
}

// ── Damage calc (Gen 1 formula) ─────────────────────────────────────────────

uint16_t Gen1BattleEngine::calcDamage(uint8_t side, uint8_t targetSide,
                                       const Gen1MoveData &mv, bool &outCrit)
{
    const BattlePoke &atk = p_[side].mons[p_[side].active];
    const BattlePoke &def = p_[targetSide].mons[p_[targetSide].active];
    outCrit = false;
    if (mv.power == 0) return 0;

    // Physical/special is by attack TYPE in both gens. Gen 1: a fixed 9..15
    // range is special. Gen 2/3: table lookup (adds Dark = special, Steel =
    // physical), and Special uses the SPLIT Sp.Atk / Sp.Def stats.
    const bool g3 = isGen3();
    bool isSpecial = g3 ? (mv.type < GEN3_TYPE_COUNT && GEN3_TYPE_IS_SPECIAL[mv.type])
                        : (mv.type >= 9 && mv.type <= 15);

    // Special attack/defence stat: gen3 splits them; gen1 uses the single spc.
    auto spAtk = [&](const BattlePoke &m) -> int32_t { return g3 ? m.spaG3 : m.spc; };
    auto spDef = [&](const BattlePoke &m) -> int32_t { return g3 ? m.spdG3 : m.spc; };

    int32_t A = isSpecial ? spAtk(atk) : atk.atk;
    int32_t D = isSpecial ? spDef(def) : def.def;
    A = (A * boostMult(isSpecial ? atk.spcBoost : atk.atkBoost)) / 100;
    D = (D * boostMult(isSpecial ? def.spcBoost : def.defBoost)) / 100;
    if (atk.status & ST_BRN) A = A / 2;

    // Light Screen / Reflect halve the matching def stat.
    if (isSpecial && p_[targetSide].lightScreen) D *= 2;
    if (!isSpecial && p_[targetSide].reflect)    D *= 2;

    if (g3) {
        // Gen 2/3: fixed base crit ~1/16, NOT tied to Speed (Focus Energy raises
        // the crit stage to ~1/4). One rand32() consumed, matching Gen 1.
        uint8_t r = rand32() & 0xFF;
        outCrit = p_[side].focused ? ((r & 0x3) == 0) : ((r & 0xF) == 0);
    } else {
        // Gen 1: base Speed / 2 (Focus Energy *quartered* it — the classic bug).
        const Gen1BaseStats &bs = GEN1_BASE_STATS[atk.species < 152 ? atk.species : 0];
        uint16_t critRate = bs.spd / 2;
        if (p_[side].focused) critRate /= 4;
        if (critRate > 255) critRate = 255;
        outCrit = (rand32() & 0xFF) < critRate;
    }
    if (outCrit) {
        // Crits ignore stat stages (use unboosted Atk/Def).
        A = isSpecial ? spAtk(atk) : atk.atk;
        D = isSpecial ? spDef(def) : def.def;
        if (atk.status & ST_BRN) A = A / 2;
    }

    // Gen 1 crit = double the level; Gen 2/3 crit = x2 the final damage.
    int32_t L = (outCrit && !g3) ? atk.level * 2 : atk.level;
    int32_t dmg = (((2 * L / 5 + 2) * mv.power * A) / D) / 50 + 2;
    if (outCrit && g3) dmg *= 2;

    // STAB
    if (mv.type == atk.type1 || mv.type == atk.type2) dmg = dmg * 3 / 2;

    // Type effectiveness (multiplicative across both defender types).
    uint8_t e1 = effectiveness(mv.type, def.type1);
    uint8_t e2 = (def.type1 != def.type2) ? effectiveness(mv.type, def.type2) : 2;
    dmg = dmg * e1 / 2;
    dmg = dmg * e2 / 2;
    if (dmg == 0) return 0;

    // Damage variance: 217..255 / 255  (≈85-100%).
    uint8_t roll = 217 + (rand32() % 39);
    dmg = dmg * roll / 255;
    if (dmg < 1) dmg = 1;
    if (dmg > 65535) dmg = 65535;
    return (uint16_t)dmg;
}

// ── Speed comparison ────────────────────────────────────────────────────────
// In Gen 1, only move PRIORITY then SPEED matters. Switch is priority +6.

bool Gen1BattleEngine::pokeActsFirst(uint8_t side, uint8_t action, uint8_t targetAction)
{
    int p1pri = 0, p2pri = 0;
    if (action       == 1 /*SWITCH*/) p1pri = 6;
    if (targetAction == 1 /*SWITCH*/) p2pri = 6;
    if (action == 0) {
        const BattlePoke &m = p_[side].mons[p_[side].active];
        if (pendIndex_[side] < 4) {
            const Gen1MoveData *mv = mdata(m.moves[pendIndex_[side]]);
            if (mv) p1pri = mv->priority;
        }
    }
    uint8_t other = side ^ 1;
    if (targetAction == 0) {
        const BattlePoke &m = p_[other].mons[p_[other].active];
        if (pendIndex_[other] < 4) {
            const Gen1MoveData *mv = mdata(m.moves[pendIndex_[other]]);
            if (mv) p2pri = mv->priority;
        }
    }
    if (p1pri != p2pri) return p1pri > p2pri;

    int32_t s1 = p_[side].mons[p_[side].active].spd;
    int32_t s2 = p_[other].mons[p_[other].active].spd;
    s1 = (s1 * boostMult(p_[side].mons[p_[side].active].spdBoost)) / 100;
    s2 = (s2 * boostMult(p_[other].mons[p_[other].active].spdBoost)) / 100;
    if (p_[side].mons[p_[side].active].status & ST_PAR)   s1 /= 4;
    if (p_[other].mons[p_[other].active].status & ST_PAR) s2 /= 4;
    if (s1 != s2) return s1 > s2;
    return (rand32() & 1) == 0;  // speed tie → coin flip
}

// ── Logging helper ──────────────────────────────────────────────────────────

// Emits a stat-change message and mutates `stage` by `delta` (+1 or -1).
static void emitStatChange(Gen1BattleEngine::LogSink log, void *ctx,
                           const char *nick, const char *statName,
                           int8_t &stage, int8_t delta)
{
    if (!log) return;
    char buf[64];
    if (delta > 0) {
        if (stage >= 6) {
            // Short form fits the 32-byte log buffer.
            snprintf(buf, sizeof(buf), "%.10s's %s maxed!", nick, statName);
        } else {
            stage++;
            snprintf(buf, sizeof(buf), "%.10s's %s rose!", nick, statName);
        }
    } else {
        if (stage <= -6) {
            snprintf(buf, sizeof(buf), "%.10s's %s bottomed!", nick, statName);
        } else {
            stage--;
            snprintf(buf, sizeof(buf), "%.10s's %s fell!", nick, statName);
        }
    }
    log(buf, ctx);
}

void Gen1BattleEngine::emit(LogSink log, void *ctx, const char *tmpl,
                            const char *poke, const char *move,
                            const char *target, int num)
{
    if (!log || !tmpl) return;
    char buf[LOG_LINE_MAX];
    size_t o = 0;
    for (size_t i = 0; tmpl[i] && o + 1 < sizeof(buf); ) {
        if (tmpl[i] == '[') {
            const char *sub = nullptr;
            char numbuf[8];
            if      (!strncmp(tmpl + i, "[POKEMON]", 9)) { sub = poke; i += 9; }
            else if (!strncmp(tmpl + i, "[MOVE]",    6)) { sub = move; i += 6; }
            else if (!strncmp(tmpl + i, "[TARGET]",  8)) { sub = target ? target : poke; i += 8; }
            else if (!strncmp(tmpl + i, "[NUM]",     5)) { snprintf(numbuf, sizeof(numbuf), "%d", num); sub = numbuf; i += 5; }
            if (sub) {
                while (*sub && o + 1 < sizeof(buf)) buf[o++] = *sub++;
                continue;
            }
        }
        buf[o++] = tmpl[i++];
    }
    buf[o] = 0;
    log(buf, ctx);
}

// ── Move execution ──────────────────────────────────────────────────────────

void Gen1BattleEngine::useMove(uint8_t side, uint8_t moveSlot, LogSink log, void *ctx)
{
    BattlePoke &user = p_[side].mons[p_[side].active];
    // Struggle sentinel: caller submitted moveSlot==0xFE because every real
    // slot is at 0 PP. Bypass slot/PP/charging/thrash/bide/disabled checks
    // since none of them apply — Gen 1 status checks (sleep/freeze/par/etc.)
    // do still apply because they're properties of the user, not the move.
    bool isStruggle = (moveSlot == 0xFE);
    uint8_t mvId;
    const Gen1MoveData *mv;
    if (isStruggle) {
        mvId = 165;  // Struggle
        mv = mdata(mvId);
        if (!mv) return;
    } else {
        if (moveSlot >= 4) return;
        mvId = user.moves[moveSlot];
        mv = mdata(mvId);
        if (!mv || user.pp[moveSlot] == 0) return;
    }

    // Pre-move status checks.
    if (user.status & ST_FRZ) {
        emit(log, ctx, "[POKEMON] is frozen solid!", user.nickname);
        return;
    }
    if (user.status & ST_SLP) {
        if (user.sleepTurns > 0) {
            user.sleepTurns--;
            emit(log, ctx, "[POKEMON] is fast asleep.", user.nickname);
            return;
        } else {
            user.status &= ~ST_SLP;
            emit(log, ctx, "[POKEMON] woke up!", user.nickname);
        }
    }
    if (user.status & ST_PAR && percent(25)) {
        emit(log, ctx, "[POKEMON] is fully paralyzed!", user.nickname);
        return;
    }
    if (user.mustRecharge) {
        user.mustRecharge = false;
        emit(log, ctx, "[POKEMON] must recharge!", user.nickname);
        return;
    }
    if (user.confuseTurns > 0) {
        user.confuseTurns--;
        if (percent(50)) {
            emit(log, ctx, "[POKEMON] hurt itself in confusion!", user.nickname);
            // Self-hit: 40-power typeless physical attack.
            uint16_t self = ((2 * user.level / 5 + 2) * 40 * user.atk / user.def) / 50 + 2;
            if (self > user.hp) user.hp = 0; else user.hp -= self;
            return;
        }
    }

    // Trapped — target loses turn while EFF_TRAPPING is active. Residual
    // damage is applied in the residual-damage block below.
    if (user.trapTurns > 0) {
        emit(log, ctx, "[POKEMON] can't move!", user.nickname);
        return;
    }

    // Flinch — overrides any chosen action.
    if (user.flinched) {
        user.flinched = false;
        emit(log, ctx, "[POKEMON] flinched and couldn't move!", user.nickname);
        return;
    }

    if (!isStruggle) {
        // Charging mid-move (Solar Beam, Fly, Dig, Sky Attack, Skull Bash, Razor
        // Wind): on turn 2 we force the same slot and skip the charge branch.
        if (user.chargingSlot != 0xFF) {
            moveSlot = user.chargingSlot;
            user.chargingSlot = 0xFF;
            mvId = user.moves[moveSlot];
            mv   = mdata(mvId);
            if (!mv) return;
        }

        // Thrash / Petal Dance: while locked, override the slot and force the
        // thrash move every turn.
        if (user.thrashSlot != 0xFF) {
            moveSlot = user.thrashSlot;
            mvId = user.moves[moveSlot];
            mv   = mdata(mvId);
            if (!mv) return;
        }
    }

    // Bide: while charging, store damage taken and skip our turn. After 2
    // turns, fire 2× accumulated damage at the foe.
    if (!isStruggle && user.bideTurns > 0) {
        user.bideTurns--;
        if (user.bideTurns == 0) {
            BattlePoke &foe = p_[side ^ 1].mons[p_[side ^ 1].active];
            uint16_t dmg = user.bideDamage * 2;
            user.bideDamage = 0;
            if (foe.hp <= dmg) foe.hp = 0; else foe.hp -= dmg;
            emit(log, ctx, "[POKEMON] unleashed energy!", user.nickname);
            if (foe.hp == 0) emit(log, ctx, Gen1Text::FAINT, foe.nickname);
        } else {
            emit(log, ctx, "[POKEMON] is storing energy.", user.nickname);
        }
        return;
    }

    // Disabled slot blocks selection (only matters if the user picked the
    // disabled slot — engine.submitAction allows it; here we no-op).
    if (!isStruggle && user.disabledSlot == moveSlot && user.disabledTurns > 0) {
        emit(log, ctx, "[POKEMON]'s move is disabled!", user.nickname);
        if (user.disabledTurns > 0 && --user.disabledTurns == 0) {
            user.disabledSlot = 0xFF;
        }
        return;
    }

    if (!isStruggle) {
        user.pp[moveSlot]--;
        user.lastMoveIdx = moveSlot;
    }
    emit(log, ctx, Gen1Text::USE, user.nickname, mv->name);

    // Charge-turn first half: emit charge text, set chargingSlot, return.
    if (!isStruggle && mv->effect == EFF_CHARGE_TURN && user.chargingSlot == 0xFF) {
        user.chargingSlot = moveSlot;
        const char *charge = "[POKEMON] is charging!";
        if      (!strcmp(mv->name, "Solar Beam"))  charge = "[POKEMON] absorbed light!";
        else if (!strcmp(mv->name, "Sky Attack"))  charge = "[POKEMON] is glowing!";
        else if (!strcmp(mv->name, "Skull Bash"))  charge = "[POKEMON] lowered its head!";
        else if (!strcmp(mv->name, "Razor Wind"))  charge = "[POKEMON] whipped up a whirlwind!";
        else if (!strcmp(mv->name, "Fly"))         charge = "[POKEMON] flew up high!";
        else if (!strcmp(mv->name, "Dig"))         charge = "[POKEMON] dug a hole!";
        emit(log, ctx, charge, user.nickname);
        return;
    }

    // Bide setup — only triggers when bide is fresh.
    if (!isStruggle && mv->effect == EFF_BIDE && user.bideTurns == 0) {
        user.bideTurns = 2;
        user.bideDamage = 0;
        emit(log, ctx, "[POKEMON] is storing energy.", user.nickname);
        return;
    }

    // Thrash setup — pick lock duration on first use.
    if (!isStruggle && mv->effect == EFF_THRASH && user.thrashSlot == 0xFF) {
        user.thrashSlot = moveSlot;
        user.thrashTurns = 2 + (rand32() & 1);  // 2-3 turn lock
    }

    applyMove(side, side ^ 1, *mv, log, ctx);

    // Thrash bookkeeping after damage.
    if (mv->effect == EFF_THRASH && user.thrashSlot != 0xFF) {
        if (user.thrashTurns > 0) user.thrashTurns--;
        if (user.thrashTurns == 0) {
            user.thrashSlot = 0xFF;
            if (user.confuseTurns == 0) {
                user.confuseTurns = 2 + (rand32() % 4);
                emit(log, ctx, "[POKEMON] became confused!", user.nickname);
            }
        }
    }

    // Counter: reflect 2× the user's last physical damage taken at the foe.
    // Counter has 1 base power so it goes through the damage path normally;
    // we override with the stored damage if it exceeds the rolled value.
    if (mv->effect == EFF_COUNTER && user.lastDamageTaken > 0) {
        BattlePoke &foe = p_[side ^ 1].mons[p_[side ^ 1].active];
        uint16_t d = user.lastDamageTaken * 2;
        user.lastDamageTaken = 0;
        if (foe.hp <= d) foe.hp = 0; else foe.hp -= d;
        emit(log, ctx, "[POKEMON] returned the hit!", user.nickname);
        if (foe.hp == 0) emit(log, ctx, Gen1Text::FAINT, foe.nickname);
    }

    // Substitute: spend 25% maxHP to spawn a sub. Only if no sub exists.
    if (mv->effect == EFF_SUBSTITUTE && user.substituteHp == 0) {
        uint16_t cost = user.maxHp / 4;
        if (cost == 0) cost = 1;
        if (user.hp > cost) {
            user.hp -= cost;
            user.substituteHp = cost;
            emit(log, ctx, "[POKEMON] made a substitute!", user.nickname);
        } else {
            emit(log, ctx, "[POKEMON] is too weak for a substitute!", user.nickname);
        }
    }

    // Disable: lock a random non-empty slot of the target for 4-7 turns.
    if (mv->effect == EFF_DISABLE) {
        BattlePoke &t = p_[side ^ 1].mons[p_[side ^ 1].active];
        uint8_t valid[4] = {}; uint8_t n = 0;
        for (uint8_t i = 0; i < 4; ++i) if (t.moves[i] != 0 && t.pp[i] > 0) valid[n++] = i;
        if (n > 0) {
            t.disabledSlot  = valid[rand32() % n];
            t.disabledTurns = 4 + (rand32() % 4);
            emit(log, ctx, "[POKEMON]'s move was disabled!", t.nickname);
        }
    }

    // Metronome — pick a random other move ID and fire it. Doesn't recurse
    // on Metronome itself, mostly to avoid log spam.
    if (mv->effect == EFF_METRONOME) {
        uint8_t pickId = 1 + (rand32() % 165);
        const Gen1MoveData *picked = mdata(pickId);
        if (picked && picked->effect != EFF_METRONOME) {
            emit(log, ctx, "Metronome rolled [MOVE]!", nullptr, picked->name);
            applyMove(side, side ^ 1, *picked, log, ctx);
        }
    }

    // Mimic — copy the target's last-used move into this slot. PP resets to
    // 5 per Gen 1. Slot is restored on switch-out via clearVolatile.
    if (mv->effect == EFF_MIMIC && user.mimicSlot == 0xFF) {
        BattlePoke &t = p_[side ^ 1].mons[p_[side ^ 1].active];
        if (t.lastMoveIdx < 4 && t.moves[t.lastMoveIdx] != 0) {
            user.mimicSlot     = moveSlot;
            user.mimicOrigMove = user.moves[moveSlot];
            user.mimicOrigPp   = user.pp[moveSlot];
            user.moves[moveSlot] = t.moves[t.lastMoveIdx];
            user.pp[moveSlot]    = 5;
            const Gen1MoveData *copied = mdata(user.moves[moveSlot]);
            emit(log, ctx, "[POKEMON] copied [MOVE]!",
                 user.nickname, copied ? copied->name : "?");
        }
    }

    // Transform — full Gen 1 copy: species, types, base battle stats, all
    // four moves with 5 PP each, nickname display. HP and level stay as
    // the user's. Backup is saved for restore on switch-out.
    if (mv->effect == EFF_TRANSFORM && !user.transformed) {
        BattlePoke &t = p_[side ^ 1].mons[p_[side ^ 1].active];
        // Snapshot.
        user.transformed = true;
        user.origSpecies = user.species;
        user.origType1   = user.type1;
        user.origType2   = user.type2;
        user.origAtk = user.atk; user.origDef = user.def;
        user.origSpd = user.spd; user.origSpc = user.spc;
        memcpy(user.origMoves, user.moves, sizeof(user.moves));
        memcpy(user.origPp,    user.pp,    4);
        // Copy.
        user.species = t.species;
        user.type1   = t.type1;
        user.type2   = t.type2;
        user.atk = t.atk; user.def = t.def; user.spd = t.spd; user.spc = t.spc;
        memcpy(user.moves, t.moves, sizeof(user.moves));
        for (uint8_t i = 0; i < 4; ++i) user.pp[i] = 5;
        emit(log, ctx, "[POKEMON] transformed!", user.nickname);
    }
}

void Gen1BattleEngine::applyMove(uint8_t side, uint8_t targetSide,
                                  const Gen1MoveData &mv, LogSink log, void *ctx)
{
    BattlePoke &user = p_[side].mons[p_[side].active];
    BattlePoke &target = p_[targetSide].mons[p_[targetSide].active];

    // Hit check (skip for self-targeting boost moves).
    bool selfTarget = (mv.effect >= EFF_BOOST_ATK && mv.effect <= EFF_BOOST_EVA) ||
                       mv.effect == EFF_FOCUS_ENERGY ||
                       mv.effect == EFF_MIST ||
                       mv.effect == EFF_LIGHT_SCREEN ||
                       mv.effect == EFF_REFLECT ||
                       mv.effect == EFF_REST ||
                       mv.effect == EFF_HEAL_HALF ||
                       mv.effect == EFF_HAZE;

    // Swift never misses — bypass the hit roll entirely.
    bool neverMiss = (mv.effect == EFF_SWIFT_NEVERMISS);

    // Dream Eater only works on a sleeping target — count "wake-up failure"
    // as a miss otherwise.
    if (mv.effect == EFF_DREAM_EATER && !(target.status & ST_SLP)) {
        emit(log, ctx, Gen1Text::MISS, user.nickname);
        return;
    }

    if (!selfTarget && !neverMiss && !rollHit(side, targetSide, mv)) {
        emit(log, ctx, Gen1Text::MISS, user.nickname);
        return;
    }

    // Pay Day: cosmetic flavor before damage.
    if (mv.effect == EFF_PAY_DAY) {
        emit(log, ctx, "Coins scattered everywhere!", nullptr);
    }

    // If the user is flinched coming into useMove (set by an opponent's
    // flinch-effect move on this same turn), they lose their action and the
    // flag clears.
    if (user.flinched) {
        emit(log, ctx, "[POKEMON] flinched and couldn't move!", user.nickname);
        user.flinched = false;
        return;
    }

    // Damage
    if (mv.power > 0 && mv.effect != EFF_FIXED_DMG &&
        mv.effect != EFF_LEVEL_DMG && mv.effect != EFF_OHKO) {
        bool crit = false;
        uint16_t dmg = calcDamage(side, targetSide, mv, crit);
        uint8_t e1 = effectiveness(mv.type, target.type1);
        uint8_t e2 = (target.type1 != target.type2) ? effectiveness(mv.type, target.type2) : 2;
        uint16_t eff = (uint16_t)e1 * e2;  // 0/2/4/8/16
        if (eff == 0) {
            emit(log, ctx, Gen1Text::IMMUNE, target.nickname);
            return;
        }
        uint8_t hits = 1;
        if (mv.effect == EFF_MULTI_HIT)  hits = (rand32() % 4 < 2) ? ((rand32() & 1) ? 2 : 3) : ((rand32() & 1) ? 4 : 5);
        if (mv.effect == EFF_DOUBLE_HIT) hits = 2;
        for (uint8_t h = 0; h < hits; ++h) {
            if (target.hp <= dmg) { target.hp = 0; }
            else                   { target.hp -= dmg; }
            if (target.hp == 0) break;
        }
        if (crit) emit(log, ctx, Gen1Text::CRIT);
        if (eff > 4) emit(log, ctx, Gen1Text::SUPER);
        else if (eff < 4) emit(log, ctx, Gen1Text::RESIST);
        if (hits > 1) {
            char msg[40];
            snprintf(msg, sizeof(msg), "Hit %u times!", hits);
            log(msg, ctx);
        }
        // Recoil
        if (mv.effect == EFF_RECOIL) {
            uint16_t r = dmg / 4;
            if (r > user.hp) user.hp = 0; else user.hp -= r;
            emit(log, ctx, Gen1Text::RECOIL, user.nickname);
        }
        // Drain (Mega Drain, Absorb)
        if (mv.effect == EFF_DRAIN_HP) {
            uint16_t heal = dmg / 2;
            user.hp += heal;
            if (user.hp > user.maxHp) user.hp = user.maxHp;
            emit(log, ctx, Gen1Text::DRAIN, target.nickname);
        }
        // Dream Eater drains half the damage as HP (target was already
        // confirmed asleep at the top of the function).
        if (mv.effect == EFF_DREAM_EATER) {
            uint16_t heal = dmg / 2;
            user.hp += heal;
            if (user.hp > user.maxHp) user.hp = user.maxHp;
            emit(log, ctx, "[POKEMON]'s dream was eaten!", target.nickname);
        }
        // Self-Destruct / Explosion: user faints after dealing damage. Done
        // unconditionally even if the move missed-but-still-applied (Gen 1
        // explode-on-hit behavior).
        if (mv.effect == EFF_EXPLODE) {
            user.hp = 0;
            emit(log, ctx, Gen1Text::FAINT, user.nickname);
        }
        // Flinch — only matters if the target hasn't moved yet this turn.
        // executeTurn orders moves by speed; if user moved first the target
        // sees flinched at the top of its useMove call and skips. If user
        // moved second, the flag is set but cleared at the next pre-move
        // check, which is a small Gen-1-accurate quirk.
        if (mv.effect == EFF_FLINCH && target.hp > 0 &&
            (mv.effectChance == 0 || percent(mv.effectChance))) {
            target.flinched = true;
        }
        // Trap (Wrap, Bind, Fire Spin, Clamp): lock target for 2-5 turns of
        // residual damage applied in applyEndOfTurn.
        if (mv.effect == EFF_TRAPPING && target.hp > 0 && target.trapTurns == 0) {
            target.trapTurns = 2 + (rand32() % 4);
            emit(log, ctx, "[POKEMON] was trapped!", target.nickname);
        }
        // Bide accumulator: any damage dealt to the user's foe gets stored
        // for release. Done here so non-bide moves still feed the counter
        // when a bide-using mon is hit.
        if (target.bideTurns > 0) target.bideDamage += dmg;
        // Counter setup: track raw physical damage taken by the target.
        // (Type-restricted Counter would need a category check; Gen 1 just
        // requires the last incoming move was Normal/Fighting — we
        // approximate by storing every hit.)
        target.lastDamageTaken = dmg;
        // Rage payoff: if the target is rage-active, getting hit boosts atk.
        if (target.rageActive && target.hp > 0) {
            emitStatChange(log, ctx, target.nickname, "Attack", target.atkBoost, 1);
        }
    }

    // Fixed damage families
    switch (mv.effect) {
        case EFF_FIXED_DMG: {
            uint16_t d = (strstr(mv.name, "Sonic") || strstr(mv.name, "Sonicboom")) ? 20 : 40;
            if (target.hp <= d) target.hp = 0; else target.hp -= d;
            break;
        }
        case EFF_LEVEL_DMG: {
            uint16_t d = user.level;
            if (target.hp <= d) target.hp = 0; else target.hp -= d;
            break;
        }
        case EFF_PSYWAVE: {
            uint16_t d = 1 + (rand32() % (user.level * 3 / 2));
            if (target.hp <= d) target.hp = 0; else target.hp -= d;
            break;
        }
        case EFF_SUPER_FANG: {
            uint16_t d = target.hp / 2; if (d == 0) d = 1;
            target.hp -= d; break;
        }
        case EFF_OHKO: {
            if (user.level < target.level) {
                emit(log, ctx, Gen1Text::MISS, user.nickname); break;
            }
            target.hp = 0;
            emit(log, ctx, Gen1Text::OHKO);
            break;
        }
        default: break;
    }

    // Secondary / status effects
    auto roll = [&](uint8_t chance) { return chance == 0 || percent(chance); };
    switch (mv.effect) {
        case EFF_STATUS_PSN:
            if (target.status == ST_NONE && roll(mv.effectChance)) {
                target.status = ST_PSN;
                emit(log, ctx, Gen1Text::STATUS_PSN, target.nickname);
            } break;
        case EFF_STATUS_BRN:
            if (target.status == ST_NONE && roll(mv.effectChance)) {
                target.status = ST_BRN;
                emit(log, ctx, Gen1Text::STATUS_BRN, target.nickname);
            } break;
        case EFF_STATUS_PAR:
            if (target.status == ST_NONE && roll(mv.effectChance)) {
                target.status = ST_PAR;
                emit(log, ctx, Gen1Text::STATUS_PAR, target.nickname);
            } break;
        case EFF_STATUS_SLP:
            if (target.status == ST_NONE && roll(mv.effectChance)) {
                target.status = ST_SLP;
                target.sleepTurns = 1 + (rand32() % 7);
                emit(log, ctx, Gen1Text::STATUS_SLP, target.nickname);
            } break;
        case EFF_STATUS_FRZ:
            if (target.status == ST_NONE && roll(mv.effectChance)) {
                target.status = ST_FRZ;
                emit(log, ctx, Gen1Text::STATUS_FRZ, target.nickname);
            } break;
        case EFF_STATUS_CONFUSE:
            if (target.confuseTurns == 0 && roll(mv.effectChance)) {
                target.confuseTurns = 2 + (rand32() % 4);
                emit(log, ctx, Gen1Text::STATUS_CONFUSE, target.nickname);
            } break;
        case EFF_BOOST_ATK: emitStatChange(log, ctx, user.nickname, "Attack",   user.atkBoost,  1); break;
        case EFF_BOOST_DEF: emitStatChange(log, ctx, user.nickname, "Defense",  user.defBoost,  1); break;
        case EFF_BOOST_SPD: emitStatChange(log, ctx, user.nickname, "Speed",    user.spdBoost,  1); break;
        case EFF_BOOST_SPC: emitStatChange(log, ctx, user.nickname, "Special",  user.spcBoost,  1); break;
        case EFF_BOOST_ACC: emitStatChange(log, ctx, user.nickname, "accuracy", user.accBoost,  1); break;
        case EFF_BOOST_EVA: emitStatChange(log, ctx, user.nickname, "evasion",  user.evaBoost,  1); break;
        case EFF_DROP_ATK:  emitStatChange(log, ctx, target.nickname, "Attack",   target.atkBoost, -1); break;
        case EFF_DROP_DEF:  emitStatChange(log, ctx, target.nickname, "Defense",  target.defBoost, -1); break;
        case EFF_DROP_SPD:  emitStatChange(log, ctx, target.nickname, "Speed",    target.spdBoost, -1); break;
        case EFF_DROP_SPC:  emitStatChange(log, ctx, target.nickname, "Special",  target.spcBoost, -1); break;
        case EFF_DROP_ACC:  emitStatChange(log, ctx, target.nickname, "accuracy", target.accBoost, -1); break;
        case EFF_DROP_EVA:  emitStatChange(log, ctx, target.nickname, "evasion",  target.evaBoost, -1); break;
        case EFF_FOCUS_ENERGY: p_[side].focused = true; break;
        case EFF_MIST:         p_[side].mist = true; break;
        case EFF_LIGHT_SCREEN: p_[side].lightScreen = true; p_[side].lightScreenTurns = 5; break;
        case EFF_REFLECT:      p_[side].reflect = true; p_[side].reflectTurns = 5; break;
        case EFF_HAZE:
            for (int s = 0; s < 2; ++s) {
                BattlePoke &m = p_[s].mons[p_[s].active];
                m.atkBoost = m.defBoost = m.spdBoost = m.spcBoost = 0;
                m.accBoost = m.evaBoost = 0;
                p_[s].focused = false;
                p_[s].lightScreen = p_[s].reflect = p_[s].mist = false;
            }
            break;
        case EFF_REST:
            user.hp = user.maxHp;
            user.status = ST_SLP;
            user.sleepTurns = 2;
            break;
        case EFF_HEAL_HALF: {
            // Recover / Soft-Boiled — heal 50% of maxHP, no sleep, no
            // status side-effect. No-op if already at full HP.
            if (user.hp < user.maxHp) {
                uint16_t heal = user.maxHp / 2;
                if (heal == 0) heal = 1;
                user.hp += heal;
                if (user.hp > user.maxHp) user.hp = user.maxHp;
                emit(log, ctx, "[POKEMON] regained health!", user.nickname);
            }
            break;
        }
        case EFF_CONVERSION: {
            // Gen 1 quirk: Conversion copies the target's typing onto the
            // user (Gen 2+ uses the user's last move's type instead).
            user.type1 = target.type1;
            user.type2 = target.type2;
            emit(log, ctx, "[POKEMON] copied the foe's type!", user.nickname);
            break;
        }
        case EFF_RAGE: {
            // Activate rage: subsequent damage taken bumps user's Attack.
            user.rageActive = true;
            break;
        }
        case EFF_HYPER_BEAM:
            if (target.hp > 0) user.mustRecharge = true;
            break;
        case EFF_FLINCH:
            // Flinch only matters if target hasn't moved yet — we approximate
            // by always granting the flinch chance and letting the next turn's
            // pre-move check ignore it (Gen 1: flinch wears off immediately).
            // No per-turn flag wired up yet; tracked under thrashTurns hack.
            break;
        default: break;
    }

    // "But nothing happened!" — fallback for status moves whose effect we
    // haven't wired yet (move table maps them to EFF_NONE with 0 power).
    // Mirrors Gen 1's response to Splash and to no-op situations.
    if (mv.power == 0 && mv.effect == EFF_NONE) {
        emit(log, ctx, "But nothing happened!", nullptr);
    }

    // Faint message (the engine doesn't auto-replace; caller checks).
    if (target.hp == 0) emit(log, ctx, Gen1Text::FAINT, target.nickname);
}

// ── Switch handling ─────────────────────────────────────────────────────────

static void clearVolatile(Gen1BattleEngine::BattlePoke &m)
{
    m.atkBoost = m.defBoost = m.spdBoost = m.spcBoost = 0;
    m.accBoost = m.evaBoost = 0;
    m.confuseTurns = 0;
    m.mustRecharge = false;
    m.thrashing    = false;
    m.thrashTurns  = 0;
    m.disabledSlot = 0xFF;
    m.disabledTurns = 0;
    m.flinched     = false;
    m.chargingSlot = 0xFF;
    m.trapTurns    = 0;
    m.thrashSlot   = 0xFF;
    m.bideTurns    = 0;
    m.bideDamage   = 0;
    m.rageActive   = false;
    m.substituteHp = 0;
    m.lastDamageTaken = 0;
    // Restore Mimic'd slot to its original move + PP, then clear the marker.
    if (m.mimicSlot < 4) {
        m.moves[m.mimicSlot] = m.mimicOrigMove;
        m.pp[m.mimicSlot]    = m.mimicOrigPp;
        m.mimicSlot     = 0xFF;
        m.mimicOrigMove = 0;
        m.mimicOrigPp   = 0;
    }
    // Restore Transform: copy back species/type/stats/moves/PP.
    if (m.transformed) {
        m.species = m.origSpecies;
        m.type1   = m.origType1;
        m.type2   = m.origType2;
        m.atk = m.origAtk; m.def = m.origDef; m.spd = m.origSpd; m.spc = m.origSpc;
        memcpy(m.moves, m.origMoves, sizeof(m.moves));
        memcpy(m.pp,    m.origPp,    4);
        m.transformed = false;
    }
}

void Gen1BattleEngine::stepAction(uint8_t side, LogSink log, void *ctx)
{
    if (result_ != Result::ONGOING) return;
    BattleParty &bp = p_[side];
    if (bp.mons[bp.active].hp == 0) return;

    uint8_t act = pendAction_[side];
    uint8_t idx = pendIndex_[side];

    if (act == 1 /*SWITCH*/) {
        if (idx >= bp.count || idx == bp.active || bp.mons[idx].hp == 0) return;
        emit(log, ctx, Gen1Text::SWITCH_OUT, bp.mons[bp.active].nickname);
        clearVolatile(bp.mons[bp.active]);
        bp.active = idx;
        emit(log, ctx, Gen1Text::SWITCH_IN, bp.mons[bp.active].nickname);
        return;
    }
    if (act == 0 /*USE_MOVE*/) {
        useMove(side, idx, log, ctx);
    }
}

// ── End-of-turn (poison/burn tick, screen countdowns) ───────────────────────

void Gen1BattleEngine::applyEndOfTurn(uint8_t side, LogSink log, void *ctx)
{
    BattleParty &bp = p_[side];
    BattlePoke  &m  = bp.mons[bp.active];
    if (m.hp == 0) return;
    if (m.status & ST_PSN) {
        uint16_t d = m.maxHp / 8; if (d == 0) d = 1;
        if (m.hp <= d) m.hp = 0; else m.hp -= d;
        emit(log, ctx, "[POKEMON] is hurt by poison!", m.nickname);
    }
    if (m.status & ST_BRN) {
        uint16_t d = m.maxHp / 8; if (d == 0) d = 1;
        if (m.hp <= d) m.hp = 0; else m.hp -= d;
        emit(log, ctx, "[POKEMON] is hurt by its burn!", m.nickname);
    }
    // Trap residual: ~1/16 maxHP per turn while EFF_TRAPPING is active.
    if (m.trapTurns > 0) {
        uint16_t d = m.maxHp / 16; if (d == 0) d = 1;
        if (m.hp <= d) m.hp = 0; else m.hp -= d;
        emit(log, ctx, "[POKEMON] is hurt by trap!", m.nickname);
        m.trapTurns--;
    }
    if (m.hp == 0) emit(log, ctx, Gen1Text::FAINT, m.nickname);

    if (bp.lightScreenTurns && --bp.lightScreenTurns == 0) bp.lightScreen = false;
    if (bp.reflectTurns     && --bp.reflectTurns     == 0) bp.reflect     = false;
}

// ── executeTurn ─────────────────────────────────────────────────────────────

void Gen1BattleEngine::executeTurn(LogSink log, void *ctx)
{
    if (result_ != Result::ONGOING) return;
    if (pendAction_[0] == 0xFF || pendAction_[1] == 0xFF) return;

    bool firstIs0 = pokeActsFirst(0, pendAction_[0], pendAction_[1]);
    uint8_t order[2] = { (uint8_t)(firstIs0 ? 0 : 1), (uint8_t)(firstIs0 ? 1 : 0) };

    for (int i = 0; i < 2; ++i) stepAction(order[i], log, ctx);
    for (int i = 0; i < 2; ++i) applyEndOfTurn(order[i], log, ctx);

    pendAction_[0] = pendAction_[1] = 0xFF;
    turn_++;

    // Check for win (current active fainted AND no replacement available).
    bool a0 = false, a1 = false;
    for (uint8_t i = 0; i < p_[0].count; ++i) if (p_[0].mons[i].hp > 0) { a0 = true; break; }
    for (uint8_t i = 0; i < p_[1].count; ++i) if (p_[1].mons[i].hp > 0) { a1 = true; break; }
    if (!a0 && !a1) result_ = Result::DRAW;
    else if (!a0)   result_ = Result::P2_WIN;
    else if (!a1)   result_ = Result::P1_WIN;
}

bool Gen1BattleEngine::autoReplaceIfFainted(uint8_t side, LogSink log, void *ctx)
{
    BattleParty &bp = p_[side];
    if (bp.mons[bp.active].hp > 0) return true;
    for (uint8_t i = 0; i < bp.count; ++i) {
        if (bp.mons[i].hp > 0) {
            clearVolatile(bp.mons[bp.active]);
            bp.active = i;
            emit(log, ctx, Gen1Text::SWITCH_IN, bp.mons[i].nickname);
            return true;
        }
    }
    return false;
}

void Gen1BattleEngine::forfeit(uint8_t side, LogSink log, void *ctx)
{
    side &= 1;
    emit(log, ctx, Gen1Text::FORFEIT, p_[side].mons[p_[side].active].nickname);
    result_ = (side == 0) ? Result::P2_WIN : Result::P1_WIN;
}

// ── State hash (FNV-1a 64 → low 8 bytes) ────────────────────────────────────

void Gen1BattleEngine::hashState(uint8_t out[8]) const
{
    // Networked PvP runs two engine instances with P0/P1 SWAPPED — the
    // initiator's engine has P0=me=initiator, P1=opp=receiver; the
    // receiver's engine has P0=me=receiver, P1=opp=initiator. Game
    // progression stays in sync because move routing uses `p_[side^1]`
    // (consistent self/foe references), but hashState used to iterate
    // P0 then P1 in fixed order — so the two engines produced different
    // hashes from the same logical state, guaranteeing a "desync"
    // alarm on the first hash compare (every 5 turns).
    //
    // Fix: compute a per-side hash and XOR the two together. XOR is
    // commutative, so the result is identical regardless of which side
    // the engine considers P0 vs P1. The turn counter is mixed in
    // separately (it's the same on both engines).
    auto sideHash = [&](int s) -> uint64_t {
        uint64_t h = 1469598103934665603ull;  // FNV-1a 64 offset
        auto mix = [&](const void *p, size_t n) {
            const uint8_t *b = (const uint8_t *)p;
            for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
        };
        mix(&p_[s].active, 1);
        mix(&p_[s].reflect, 1); mix(&p_[s].lightScreen, 1);
        mix(&p_[s].mist, 1);    mix(&p_[s].focused, 1);
        mix(&p_[s].reflectTurns, 1);
        mix(&p_[s].lightScreenTurns, 1);
        for (int i = 0; i < p_[s].count; ++i) {
            const BattlePoke &m = p_[s].mons[i];
            // Permanent-but-mutable stats (level-up + Transform rewrite these).
            mix(&m.species, 2); mix(&m.level, 1);
            mix(&m.type1, 1);   mix(&m.type2, 1);
            mix(&m.maxHp, 2);
            mix(&m.atk, 2); mix(&m.def, 2); mix(&m.spd, 2); mix(&m.spc, 2);
            mix(m.moves, 8);
            // Live combat state.
            mix(&m.hp, 2); mix(&m.status, 1);
            mix(&m.sleepTurns, 1); mix(&m.confuseTurns, 1);
            int8_t b[6] = {m.atkBoost, m.defBoost, m.spdBoost,
                           m.spcBoost, m.accBoost, m.evaBoost};
            mix(b, 6);
            mix(&m.toxicCounter, 1);
            uint8_t flags = (uint8_t)((m.mustRecharge ? 0x01 : 0) |
                                       (m.thrashing    ? 0x02 : 0) |
                                       (m.flinched     ? 0x04 : 0) |
                                       (m.rageActive   ? 0x08 : 0) |
                                       (m.transformed  ? 0x10 : 0));
            mix(&flags, 1);
            mix(&m.thrashTurns, 1);
            mix(&m.lastMoveIdx, 1);
            mix(&m.disabledSlot, 1);
            mix(&m.disabledTurns, 1);
            mix(&m.chargingSlot, 1);
            mix(&m.trapTurns, 1);
            mix(&m.thrashSlot, 1);
            mix(&m.bideTurns, 2);
            mix(&m.bideDamage, 2);
            mix(&m.substituteHp, 2);
            mix(&m.lastDamageTaken, 2);
            mix(&m.mimicSlot, 1);
            mix(&m.mimicOrigMove, 2);
            mix(&m.mimicOrigPp, 1);
            mix(m.pp, 4);
        }
        return h;
    };
    uint64_t th = 1469598103934665603ull;
    auto thmix = [&](const void *p, size_t n) {
        const uint8_t *b = (const uint8_t *)p;
        for (size_t i = 0; i < n; ++i) { th ^= b[i]; th *= 1099511628211ull; }
    };
    thmix(&turn_, sizeof(turn_));
    // RNG state must be in the hash. If one engine consumes random numbers
    // a different number of times than the other (e.g. divergent code path
    // through a status-immunity short-circuit) the RNG silently desyncs
    // and every subsequent damage roll diverges with no signal — the
    // narrower hash misses it because hp/status look the same on turn N
    // but will differ on turn N+1.
    thmix(s_, sizeof(s_));
    thmix(&rng_, sizeof(rng_));
    uint64_t h = th ^ sideHash(0) ^ sideHash(1);
    for (int i = 0; i < 8; ++i) out[i] = (uint8_t)(h >> (i * 8));
}

// ── CPU action picker (simple heuristic for roguelike) ──────────────────────

void Gen1BattleEngine::cpuPickAction(uint8_t side, uint8_t &outAction, uint8_t &outIndex)
{
    side &= 1;
    BattleParty &bp = p_[side];
    BattlePoke  &m  = bp.mons[bp.active];

    // If active has 0 HP, switch to first alive.
    if (m.hp == 0) {
        outAction = 1;
        for (uint8_t i = 0; i < bp.count; ++i) if (bp.mons[i].hp > 0) { outIndex = i; return; }
        outIndex = 0; return;
    }
    // Pick the legal move with highest expected damage (rough: power * STAB * eff).
    uint8_t bestSlot = 0;
    int bestScore = -1;
    BattlePoke &foe = p_[side ^ 1].mons[p_[side ^ 1].active];
    for (uint8_t i = 0; i < 4; ++i) {
        if (m.moves[i] == 0 || m.pp[i] == 0) continue;
        const Gen1MoveData *mv = mdata(m.moves[i]);
        if (!mv) continue;
        int score = mv->power;
        if (mv->type == m.type1 || mv->type == m.type2) score = score * 3 / 2;
        uint8_t e1 = effectiveness(mv->type, foe.type1);
        uint8_t e2 = (foe.type1 != foe.type2) ? effectiveness(mv->type, foe.type2) : 2;
        score = score * e1 * e2 / 4;
        // Status moves get a small flat bonus when target is unstatused.
        if (mv->power == 0 && foe.status == ST_NONE) score = 30;
        if (score > bestScore) { bestScore = score; bestSlot = i; }
    }
    outAction = 0;
    outIndex  = (bestScore < 0) ? 0xFE /*STRUGGLE*/ : bestSlot;
}
