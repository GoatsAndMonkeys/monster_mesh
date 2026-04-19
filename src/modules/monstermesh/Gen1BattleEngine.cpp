// SPDX-License-Identifier: MIT
// See Gen1BattleEngine.h for description and credits.

#include "Gen1BattleEngine.h"
#include <string.h>
#include <stdio.h>

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

void Gen1BattleEngine::initBattlePokeFromSave(BattlePoke &dst,
                                              const Gen1Pokemon &src,
                                              const uint8_t nick[11])
{
    memset(&dst, 0, sizeof(dst));
    dst.species = src.species;
    if (nick) memcpy(dst.nickname, nick, 10);
    dst.nickname[10] = 0;
    dst.level = src.level ? src.level : src.boxLevel;

    // DVs: high nibble of dvs[0] = Atk, low = Def, high of dvs[1] = Spd, low = Spc.
    uint8_t atkDV = (src.dvs[0] >> 4) & 0xF;
    uint8_t defDV =  src.dvs[0]       & 0xF;
    uint8_t spdDV = (src.dvs[1] >> 4) & 0xF;
    uint8_t spcDV =  src.dvs[1]       & 0xF;
    uint8_t hpDV  = ((atkDV & 1) << 3) | ((defDV & 1) << 2) |
                    ((spdDV & 1) << 1) | (spcDV & 1);

    const Gen1BaseStats &b = GEN1_BASE_STATS[src.species < 152 ? src.species : 0];
    dst.type1 = b.type1; dst.type2 = b.type2;

    uint16_t hpExp  = be16(src.hpExp);
    uint16_t atkExp = be16(src.atkExp);
    uint16_t defExp = be16(src.defExp);
    uint16_t spdExp = be16(src.spdExp);
    uint16_t spcExp = be16(src.spcExp);

    dst.maxHp = calcStat(b.hp,  hpDV,  hpExp,  dst.level, true);
    dst.atk   = calcStat(b.atk, atkDV, atkExp, dst.level, false);
    dst.def   = calcStat(b.def, defDV, defExp, dst.level, false);
    dst.spd   = calcStat(b.spd, spdDV, spdExp, dst.level, false);
    dst.spc   = calcStat(b.spc, spcDV, spcExp, dst.level, false);
    dst.hp    = be16(src.hp);
    if (dst.hp == 0 || dst.hp > dst.maxHp) dst.hp = dst.maxHp;

    memcpy(dst.moves, src.moves, 4);
    memcpy(dst.pp,    src.pp,    4);
    dst.status = src.status;
}

void Gen1BattleEngine::initBattlePokeFromBase(BattlePoke &dst,
                                              uint8_t species, uint8_t level,
                                              const uint8_t moves[4])
{
    memset(&dst, 0, sizeof(dst));
    dst.species = species;
    dst.level   = level;
    const Gen1BaseStats &b = GEN1_BASE_STATS[species < 152 ? species : 0];
    dst.type1 = b.type1; dst.type2 = b.type2;
    // Wild encounters: average DVs = 8, no stat exp.
    dst.maxHp = calcStat(b.hp,  8, 0, level, true);
    dst.atk   = calcStat(b.atk, 8, 0, level, false);
    dst.def   = calcStat(b.def, 8, 0, level, false);
    dst.spd   = calcStat(b.spd, 8, 0, level, false);
    dst.spc   = calcStat(b.spc, 8, 0, level, false);
    dst.hp    = dst.maxHp;
    memcpy(dst.moves, moves, 4);
    for (int i = 0; i < 4; ++i) {
        const Gen1MoveData *m = gen1Move(moves[i]);
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
            initBattlePokeFromSave(bp.mons[i], pty.mons[i], pty.nicknames[i]);
        }
        bp.active = 0;
    };
    initSide(0, p1);
    initSide(1, p2);
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

    // Gen 1 physical/special split: by attack TYPE.
    bool isSpecial = (mv.type == 9 || mv.type == 10 || mv.type == 11 ||
                      mv.type == 12 || mv.type == 13 || mv.type == 14 ||
                      mv.type == 15);

    int32_t A = isSpecial ? atk.spc : atk.atk;
    int32_t D = isSpecial ? def.spc : def.def;
    A = (A * boostMult(isSpecial ? atk.spcBoost : atk.atkBoost)) / 100;
    D = (D * boostMult(isSpecial ? def.spcBoost : def.defBoost)) / 100;
    if (atk.status & ST_BRN) A = A / 2;

    // Light Screen / Reflect halve the matching def stat.
    if (isSpecial && p_[targetSide].lightScreen) D *= 2;
    if (!isSpecial && p_[targetSide].reflect)    D *= 2;

    // Crit: Gen 1 uses base Speed / 2 (focus energy quartered it — bug). Cap 255/256.
    const Gen1BaseStats &bs = GEN1_BASE_STATS[atk.species < 152 ? atk.species : 0];
    uint16_t critRate = bs.spd / 2;
    if (p_[side].focused) critRate /= 4;  // Gen 1 bug
    if (critRate > 255) critRate = 255;
    outCrit = (rand32() & 0xFF) < critRate;
    if (outCrit) {
        // Gen 1 crits use unboosted Atk/Def AND double level.
        A = isSpecial ? atk.spc : atk.atk;
        D = isSpecial ? def.spc : def.def;
        if (atk.status & ST_BRN) A = A / 2;
    }

    int32_t L = outCrit ? atk.level * 2 : atk.level;
    int32_t dmg = (((2 * L / 5 + 2) * mv.power * A) / D) / 50 + 2;

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
            const Gen1MoveData *mv = gen1Move(m.moves[pendIndex_[side]]);
            if (mv) p1pri = mv->priority;
        }
    }
    uint8_t other = side ^ 1;
    if (targetAction == 0) {
        const BattlePoke &m = p_[other].mons[p_[other].active];
        if (pendIndex_[other] < 4) {
            const Gen1MoveData *mv = gen1Move(m.moves[pendIndex_[other]]);
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
            snprintf(buf, sizeof(buf), "%.10s's %s can't go any higher!", nick, statName);
        } else {
            stage++;
            snprintf(buf, sizeof(buf), "%.10s's %s rose!", nick, statName);
        }
    } else {
        if (stage <= -6) {
            snprintf(buf, sizeof(buf), "%.10s's %s can't go any lower!", nick, statName);
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
    if (moveSlot >= 4) return;
    uint8_t mvId = user.moves[moveSlot];
    const Gen1MoveData *mv = gen1Move(mvId);
    if (!mv || user.pp[moveSlot] == 0) return;

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

    user.pp[moveSlot]--;
    user.lastMoveIdx = moveSlot;
    emit(log, ctx, Gen1Text::USE, user.nickname, mv->name);

    applyMove(side, side ^ 1, *mv, log, ctx);
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
                       mv.effect == EFF_HAZE;

    if (!selfTarget && !rollHit(side, targetSide, mv)) {
        emit(log, ctx, Gen1Text::MISS, user.nickname);
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
        // Drain
        if (mv.effect == EFF_DRAIN_HP) {
            uint16_t heal = dmg / 2;
            user.hp += heal;
            if (user.hp > user.maxHp) user.hp = user.maxHp;
            emit(log, ctx, Gen1Text::DRAIN, target.nickname);
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
    uint64_t h = 1469598103934665603ull;  // FNV-1a 64 offset
    auto mix = [&](const void *p, size_t n) {
        const uint8_t *b = (const uint8_t *)p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    mix(&turn_, sizeof(turn_));
    for (int s = 0; s < 2; ++s) {
        mix(&p_[s].active, 1);
        for (int i = 0; i < p_[s].count; ++i) {
            const BattlePoke &m = p_[s].mons[i];
            mix(&m.hp, 2); mix(&m.status, 1);
            int8_t b[6] = {m.atkBoost, m.defBoost, m.spdBoost,
                           m.spcBoost, m.accBoost, m.evaBoost};
            mix(b, 6);
            mix(m.pp, 4);
        }
    }
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
        const Gen1MoveData *mv = gen1Move(m.moves[i]);
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
    outIndex  = bestSlot;
}
