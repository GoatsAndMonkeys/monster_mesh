#include "DungeonBattle.h"
#include <string.h>
#include <ctype.h>

// D&D spell slot table: [classLevel-1][slotLevel-1] = slots per long rest
// Full-caster progression (PHB). Martial classes get zeros; half-casters halved below.
static constexpr uint8_t SPELL_SLOTS_BY_LEVEL[20][9] = {
//  L1 L2 L3 L4 L5 L6 L7 L8 L9
    { 2, 0, 0, 0, 0, 0, 0, 0, 0 }, // class lv 1
    { 3, 0, 0, 0, 0, 0, 0, 0, 0 }, // class lv 2
    { 4, 2, 0, 0, 0, 0, 0, 0, 0 }, // class lv 3
    { 4, 3, 0, 0, 0, 0, 0, 0, 0 }, // class lv 4
    { 4, 3, 2, 0, 0, 0, 0, 0, 0 }, // class lv 5
    { 4, 3, 3, 0, 0, 0, 0, 0, 0 }, // class lv 6
    { 4, 3, 3, 1, 0, 0, 0, 0, 0 }, // class lv 7
    { 4, 3, 3, 2, 0, 0, 0, 0, 0 }, // class lv 8
    { 4, 3, 3, 3, 1, 0, 0, 0, 0 }, // class lv 9
    { 4, 3, 3, 3, 2, 0, 0, 0, 0 }, // class lv 10
    { 4, 3, 3, 3, 2, 1, 0, 0, 0 }, // class lv 11
    { 4, 3, 3, 3, 2, 1, 0, 0, 0 }, // class lv 12
    { 4, 3, 3, 3, 2, 1, 1, 0, 0 }, // class lv 13
    { 4, 3, 3, 3, 2, 1, 1, 0, 0 }, // class lv 14
    { 4, 3, 3, 3, 2, 1, 1, 1, 0 }, // class lv 15
    { 4, 3, 3, 3, 2, 1, 1, 1, 0 }, // class lv 16
    { 4, 3, 3, 3, 2, 1, 1, 1, 1 }, // class lv 17
    { 4, 3, 3, 3, 3, 1, 1, 1, 1 }, // class lv 18
    { 4, 3, 3, 3, 3, 2, 1, 1, 1 }, // class lv 19
    { 4, 3, 3, 3, 3, 2, 2, 1, 1 }, // class lv 20
};

// ── LCG RNG ────────────────────────────────────────────────────────────────────
uint32_t DungeonBattle::lcg(uint32_t &state) {
    state = state * 1664525u + 1013904223u;
    return state >> 16;
}

// ── buildWildEnemy ─────────────────────────────────────────────────────────────
EnemyActor DungeonBattle::buildWildEnemy(uint8_t depth, uint8_t dexNum,
                                          DnDClass cls, uint32_t seed) {
    EnemyActor e = {};
    e.dexNum   = (dexNum == 0 || dexNum > 151) ? 4 : dexNum;
    e.dndClass = cls;
    // Class level scales 1:1 with depth, capped at 15
    e.classLevel = (depth > 15) ? 15 : (depth > 0 ? depth : 1);

    const Gen1Stats& base = getSpeciesStats(e.dexNum);
    e.type1 = base.type1;
    e.type2 = base.type2;

    // HP scales more aggressively than other stats so fights take multiple turns
    uint16_t hpBonus = (uint16_t)((e.classLevel - 1) * 5 + depth * 3);
    e.maxHp  = (uint16_t)(base.hp + hpBonus);
    e.hp     = e.maxHp;
    e.atk    = (uint16_t)(base.atk  + e.classLevel * 2);
    e.def    = (uint16_t)(base.def  + e.classLevel);
    e.spd    = base.spd;
    e.spc    = (uint16_t)(base.spc  + e.classLevel);

    // Spell slot assignment from PHB full-caster table
    uint8_t lvIdx = (e.classLevel > 0) ? (uint8_t)(e.classLevel - 1) : 0u;
    if (lvIdx >= 20) lvIdx = 19;
    for (int i = 0; i < 9; i++)
        e.spellSlots[i] = SPELL_SLOTS_BY_LEVEL[lvIdx][i];

    // Martials get no slots (abilities are cantrips with slotLevel=0, always free)
    if (cls == DnDClass::Fighter || cls == DnDClass::Barbarian) {
        for (int i = 0; i < 9; i++) e.spellSlots[i] = 0;
    } else if (cls == DnDClass::Ranger || cls == DnDClass::Rogue || cls == DnDClass::Monk) {
        // Half-casters: halve all slot counts (integer floor)
        for (int i = 0; i < 9; i++) e.spellSlots[i] /= 2;
    }

    e.statusFlags = 0;
    (void)seed;
    return e;
}

// ── buildNpcTrainer ────────────────────────────────────────────────────────────
void DungeonBattle::buildNpcTrainer(uint8_t depth, uint8_t partySize,
                                     EnemyActor out[], uint8_t &count,
                                     uint32_t seed) {
    count = (partySize > MAX_ENEMIES) ? MAX_ENEMIES : partySize;
    uint32_t rng = seed;

    // Higher depths draw from higher Pokédex ranges
    uint8_t minDex = (uint8_t)(1 + (depth / 3) * 20);
    if (minDex > 130) minDex = 130;
    uint8_t range = 20;

    for (uint8_t i = 0; i < count; i++) {
        uint8_t dex = (uint8_t)(minDex + (lcg(rng) % range) + 1);
        if (dex > 151) dex = 151;
        DnDClass cls = (DnDClass)((uint8_t)(lcg(rng) % (uint8_t)DnDClass::COUNT));
        out[i] = buildWildEnemy(depth, dex, cls, rng);
    }
}

// ── applyTrainerBonuses ────────────────────────────────────────────────────────
void DungeonBattle::applyTrainerBonuses(PlayerActor &actor,
                                          const AbilityScores &scores) {
    // STR mod scales attack ±1% per modifier point
    int8_t strMod = abilityMod(scores.str);
    if (strMod != 0)
        actor.atk = (uint16_t)(actor.atk * (100 + strMod) / 100);

    // CON mod scales HP pool ±1% per modifier point
    int8_t conMod = abilityMod(scores.con);
    if (conMod != 0) {
        actor.maxHp = (uint16_t)(actor.maxHp * (100 + conMod) / 100);
        if (actor.hp > actor.maxHp) actor.hp = actor.maxHp;
    }
}

// ── playerAttack ──────────────────────────────────────────────────────────────
uint16_t DungeonBattle::playerAttack(PlayerActor &attacker, EnemyActor &target,
                                      const char *moveName) {
    // Phase 2: flat base power 40; infer move type from name keywords
    uint8_t power    = 40;
    PokeType moveType = PokeType::Normal;

    if (moveName && *moveName) {
        char low[16] = {};
        for (int i = 0; moveName[i] && i < 15; i++)
            low[i] = (char)tolower((uint8_t)moveName[i]);

        if      (strstr(low,"fire")   || strstr(low,"ember")  || strstr(low,"flame"))   moveType = PokeType::Fire;
        else if (strstr(low,"water")  || strstr(low,"surf")   || strstr(low,"bubble"))  moveType = PokeType::Water;
        else if (strstr(low,"thunder")|| strstr(low,"bolt")   || strstr(low,"spark"))   moveType = PokeType::Electric;
        else if (strstr(low,"leaf")   || strstr(low,"vine")   || strstr(low,"grass"))   moveType = PokeType::Grass;
        else if (strstr(low,"psychic")|| strstr(low,"psych"))                            moveType = PokeType::Psychic;
        else if (strstr(low,"rock")   || strstr(low,"stone")  || strstr(low,"slide"))   moveType = PokeType::Rock;
        else if (strstr(low,"ice")    || strstr(low,"blizzard")|| strstr(low,"frost"))  moveType = PokeType::Ice;
        else if (strstr(low,"poison") || strstr(low,"acid")   || strstr(low,"sludge"))  moveType = PokeType::Poison;
        else if (strstr(low,"shadow") || strstr(low,"ghost")  || strstr(low,"lick"))    moveType = PokeType::Ghost;
        else if (strstr(low,"kick")   || strstr(low,"punch")  || strstr(low,"karate"))  moveType = PokeType::Fighting;
        else if (strstr(low,"wing")   || strstr(low,"gust")   || strstr(low,"fly"))     moveType = PokeType::Flying;
    }

    bool stab = ((uint8_t)moveType == attacker.type1 ||
                 (uint8_t)moveType == attacker.type2);

    uint16_t typeEff = typeEffectivenessDual(moveType,
                                              (PokeType)target.type1,
                                              (PokeType)target.type2);
    uint8_t effCapped = (typeEff > 200) ? 200u : (uint8_t)typeEff;

    uint16_t dmg = gen1Damage(attacker.level, attacker.atk, power,
                               target.def, effCapped, stab);

    if (dmg >= target.hp) target.hp = 0;
    else                   target.hp = (uint16_t)(target.hp - dmg);
    return dmg;
}

// ── enemyAttack ───────────────────────────────────────────────────────────────
uint16_t DungeonBattle::enemyAttack(EnemyActor &attacker, PlayerActor &target,
                                     uint32_t &rng) {
    // Paralysis: skip 1-in-4 turns
    if (attacker.statusFlags & (uint32_t)StatusFlag::Paralysis) {
        if (lcg(rng) % 4 == 0) return 0;
    }
    // Stun: lose this turn, then clear
    if (attacker.statusFlags & (uint32_t)StatusFlag::Stunned) {
        attacker.statusFlags &= ~(uint32_t)StatusFlag::Stunned;
        return 0;
    }

    uint8_t actionIdx = pickEnemyAction(attacker, target, rng);
    uint16_t dmg = 0;

    if (actionIdx == 0xFF) {
        // Physical Pokemon move — STAB, type from species primary type
        PokeType moveType = (PokeType)attacker.type1;
        uint16_t typeEff  = typeEffectivenessDual(moveType,
                                                   (PokeType)target.type1,
                                                   (PokeType)target.type2);
        uint8_t effCapped = (typeEff > 200) ? 200u : (uint8_t)typeEff;
        dmg = gen1Damage((uint8_t)(attacker.classLevel + 3), attacker.atk, 40,
                          target.def, effCapped, /*stab=*/true);
    } else {
        const DnDSpell& spell = getClassSpells(attacker.dndClass)[actionIdx];

        if (spell.basePower > 0) {
            PokeType moveType = (PokeType)spell.pokeType;
            bool stab = (spell.pokeType == attacker.type1 ||
                         spell.pokeType == attacker.type2);
            uint16_t typeEff  = typeEffectivenessDual(moveType,
                                                       (PokeType)target.type1,
                                                       (PokeType)target.type2);
            uint8_t effCapped = (typeEff > 200) ? 200u : (uint8_t)typeEff;
            dmg = gen1Damage((uint8_t)(attacker.classLevel + 3), attacker.atk,
                              spell.basePower, target.def, effCapped, stab);

            // Consume spell slot if leveled
            if (spell.slotLevel > 0 && spell.slotLevel <= 9) {
                uint8_t si = (uint8_t)(spell.slotLevel - 1);
                if (attacker.spellSlots[si] > 0) attacker.spellSlots[si]--;
            }
        } else if (spell.hpEffect > 0) {
            // Self-heal — no damage to player this action
            uint16_t healed = (uint16_t)spell.hpEffect;
            uint16_t newHp  = (uint16_t)(attacker.hp + healed);
            attacker.hp = (newHp > attacker.maxHp) ? attacker.maxHp : newHp;
            dmg = 0;
        }

        // Apply secondary effect to target
        if (dmg > 0 && spell.effectCode > 0 && spell.effectChance > 0) {
            if ((uint32_t)(lcg(rng) % 100) < spell.effectChance) {
                switch (spell.effectCode) {
                    case 1:  target.statusFlags |= (uint32_t)StatusFlag::Burn;       break;
                    case 2:  target.statusFlags |= (uint32_t)StatusFlag::Paralysis;  break;
                    case 3:  target.statusFlags |= (uint32_t)StatusFlag::Sleep;      break;
                    case 4:  target.statusFlags |= (uint32_t)StatusFlag::Poison;     break;
                    case 5:  target.statusFlags |= (uint32_t)StatusFlag::Freeze;     break;
                    case 6:  target.statusFlags |= (uint32_t)StatusFlag::Confusion;  break;
                    case 14: target.statusFlags |= (uint32_t)StatusFlag::Stunned;    break;
                    default: break;
                }
            }
        }
    }

    if (dmg >= target.hp) target.hp = 0;
    else                   target.hp = (uint16_t)(target.hp - dmg);
    return dmg;
}

// ── pickEnemyAction ────────────────────────────────────────────────────────────
uint8_t DungeonBattle::pickEnemyAction(const EnemyActor &enemy,
                                        const PlayerActor &player,
                                        uint32_t &rng) {
    const DnDSpell* spells = getClassSpells(enemy.dndClass);

    auto slotAvailable = [&](const DnDSpell& s) -> bool {
        if (s.slotLevel == 0) return true;  // cantrip / class feature
        if (s.slotLevel > 9)  return false;
        return enemy.spellSlots[s.slotLevel - 1] > 0;
    };

    // Priority 1: heal when below 30% HP
    if (enemy.hp < enemy.maxHp * 3 / 10) {
        for (uint8_t i = 0; i < 6; i++) {
            if (spells[i].hpEffect > 0 && slotAvailable(spells[i]))
                return i;
        }
    }

    // Priority 2: use highest base-power available damage spell
    uint8_t bestIdx   = 0xFF;
    uint8_t bestPower = 0;
    for (uint8_t i = 0; i < 6; i++) {
        if (spells[i].basePower > bestPower && slotAvailable(spells[i])) {
            bestPower = spells[i].basePower;
            bestIdx   = i;
        }
    }
    if (bestIdx != 0xFF) return bestIdx;

    // Priority 3: use any available utility/buff spell (30% chance), else physical
    for (uint8_t i = 0; i < 6; i++) {
        if (slotAvailable(spells[i]) && (lcg(rng) % 10) < 3)
            return i;
    }

    (void)player;
    return 0xFF;  // physical Pokemon move
}

// ── tickStatus ────────────────────────────────────────────────────────────────
// Returns HP lost to status this turn. Floors at 1 HP so status can't kill
// outright (remove floor once the switch/cure mechanics exist).
uint8_t DungeonBattle::tickStatus(uint32_t &flags, uint16_t &hp) {
    uint8_t damage = 0;

    if (flags & (uint32_t)StatusFlag::Burn) {
        damage = (uint8_t)(hp > 8 ? hp / 8 : 1);
    } else if (flags & (uint32_t)StatusFlag::BadPoison) {
        damage = (uint8_t)(hp > 6 ? hp / 6 : 1);
    } else if (flags & (uint32_t)StatusFlag::Poison) {
        damage = (uint8_t)(hp > 16 ? hp / 16 : 1);
    }

    if (damage >= hp) {
        damage = (uint16_t)(hp - 1);  // floor at 1 HP
        hp     = 1;
    } else {
        hp = (uint16_t)(hp - damage);
    }
    return damage;
}

// ── clearBattleEndStatus ──────────────────────────────────────────────────────
void DungeonBattle::clearBattleEndStatus(uint32_t &flags) {
    static constexpr uint32_t IN_BATTLE =
        (uint32_t)StatusFlag::Confusion  |
        (uint32_t)StatusFlag::Bound      |
        (uint32_t)StatusFlag::Charmed    |
        (uint32_t)StatusFlag::Frightened |
        (uint32_t)StatusFlag::Stunned    |
        (uint32_t)StatusFlag::Restrained |
        (uint32_t)StatusFlag::Blinded    |
        (uint32_t)StatusFlag::Deafened;
    flags &= ~IN_BATTLE;
}
