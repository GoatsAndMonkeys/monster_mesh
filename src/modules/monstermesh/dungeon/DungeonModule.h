#pragma once
#include "configuration.h"

// Dungeons and MonstersMesh — co-op roguelike door game
// 2-5 trainers descend an infinite dungeon; Pokemon battle engine + D&D classes/mechanics

// ── Forward declarations ──────────────────────────────────────────────────────
struct DungeonTrainer;
struct DungeonParty;
struct DungeonRun;
struct DungeonFloor;
struct DungeonEnemy;

// ── Ability scores ────────────────────────────────────────────────────────────
struct AbilityScores {
    uint8_t str; // buffs party Pokemon attack %
    uint8_t dex; // double-move threshold; accuracy
    uint8_t con; // buffs party Pokemon HP pool
    uint8_t intel; // Vault-Tec hacking difficulty
    uint8_t wis; // free Wordle hints (wis/2 = 1, near-max = 2)
    uint8_t cha; // NPC reactions, alignment encounters
};

// ── Alignment ─────────────────────────────────────────────────────────────────
enum class Alignment : uint8_t {
    LawfulGood, NeutralGood, ChaoticGood,
    LawfulNeutral, TrueNeutral, ChaoticNeutral,
    LawfulEvil, NeutralEvil, ChaoticEvil
};

// ── D&D class for enemies ─────────────────────────────────────────────────────
enum class DnDClass : uint8_t {
    Fighter, Barbarian, Ranger, Rogue,
    Wizard, Sorcerer, Warlock,
    Cleric, Paladin, Druid,
    Bard, Monk,
    COUNT
};

// ── Pokemon type (for spell type-matching) ────────────────────────────────────
enum class PokeType : uint8_t {
    Normal, Fire, Water, Electric, Grass, Ice,
    Fighting, Poison, Ground, Flying, Psychic,
    Bug, Rock, Ghost, Dragon, Dark, Steel, Fairy,
    COUNT
};

// ── Status conditions ─────────────────────────────────────────────────────────
// Pokemon-convention persistence: major status persists after battle;
// in-battle status clears at battle end.
enum class StatusFlag : uint32_t {
    // Pokemon major (persists)
    Poison      = 1 << 0,
    BadPoison   = 1 << 1,
    Burn        = 1 << 2,
    Paralysis   = 1 << 3,
    Freeze      = 1 << 4,
    Sleep       = 1 << 5,
    // Pokemon in-battle (clears at end)
    Confusion   = 1 << 6,
    Bound       = 1 << 7,
    // D&D conditions (stack with Pokemon; in-battle)
    Charmed     = 1 << 8,
    Frightened  = 1 << 9,
    Stunned     = 1 << 10,
    Restrained  = 1 << 11,
    Blinded     = 1 << 12,
    Deafened    = 1 << 13,
};

// ── Trainer (player or NPC) ───────────────────────────────────────────────────
// Trainer is ROGUELIKE (ephemeral): resets to level 1 each run with a new
// class+background drawn from the card deck. The persistent assets are the
// Pokemon XP (syncs to main save) and Trivial Pursuit category bonuses.
struct DungeonTrainer {
    uint32_t    nodeId;
    char        name[16];
    AbilityScores stats;
    Alignment   alignment;
    DnDClass    dndClassId;    // class drawn for this run (determines spells vs class abilities)
    uint8_t     backgroundId;  // background drawn for this run (paired with class on card)
    uint8_t     feats[4];      // up to 4 feats, 0xFF = empty; earned at fixed milestones
    uint8_t     level;         // trainer level (per-run, fast-leveling, resets each run)
    uint32_t    xp;            // per-run XP
    uint8_t     cardXpBonusPct; // 0 = normal card; 5 or 10 = golden card XP bonus for this run

    // D&D spell slots — indexed by spell level 1-9 (index 0 unused; [1] = 1st-level slots).
    // Non-casting classes (Barbarian, Fighter, Monk, Rogue) use [0] for class resource
    // uses (rage, ki points, etc.); all other indices unused for those classes.
    // Recharges at Pokemon Centers alongside full Pokemon healing.
    uint8_t     spellSlotsRemaining[10]; // [0]=class resource uses, [1-9]=spell levels 1-9

    // Pokemon slots (indices into trainer's save file party)
    uint8_t     activeSlot;     // currently active pokemon (0-5)
    bool        slotFainted[6];
    uint16_t    slotHp[6];      // current HP in-run (may be modified by damage/healing)
    // Co-op level equalization: run_level = (actual_level + party_avg_level) / 2
    // Stats scale to runLevel[slot]; move pools use actual level from save.
    uint8_t     runLevel[6];    // equalized run level per pokemon slot (0 = not yet set)

    bool isAlive() const {
        for (int i = 0; i < 6; i++) if (!slotFainted[i]) return true;
        return false;
    }

    // True for spellcasting classes (Wizard, Cleric, Druid, Sorcerer, Warlock, Bard,
    // Paladin, Ranger). False for martial classes (Barbarian, Fighter, Monk, Rogue).
    bool isSpellcaster() const {
        switch (dndClassId) {
            case DnDClass::Wizard:
            case DnDClass::Cleric:
            case DnDClass::Druid:
            case DnDClass::Sorcerer:
            case DnDClass::Warlock:
            case DnDClass::Bard:
            case DnDClass::Paladin:
            case DnDClass::Ranger:
                return true;
            default:
                return false;
        }
    }
};

// ── Enemy pokemon ─────────────────────────────────────────────────────────────
struct DungeonEnemy {
    uint16_t    speciesId;      // Pokemon dex number
    DnDClass    dndClass;
    uint8_t     classLevel;
    uint16_t    currentHp;
    uint16_t    maxHp;
    uint32_t    statusFlags;    // bitfield of StatusFlag
    uint8_t     spellSlotsRemaining[9]; // indexed by spell level 1-9
};

// ── Encounter types ───────────────────────────────────────────────────────────
enum class EncounterType : uint8_t {
    WildPokemon,    // single enemy
    NpcTrainer,     // trainer with up to 6 pokemon
    Trivia,         // embedded in combat
    Trap,           // skill check minigame
    HackTerminal,   // Vault-Tec hacking
    PokemonCenter,  // full heal
    BossTrainer,
};

// ── Plane / floor / biome ─────────────────────────────────────────────────────
enum class Plane : uint8_t {
    PlaneFire, Feywild, Shadowfell, PlaneWater,
    PlaneEarth, PlaneAir, AstralPlane, EtherealPlane,
    COUNT
};

struct DungeonFloor {
    Plane       plane;
    uint8_t     floorInPlane;   // 0-2
    uint8_t     biomeId;
    uint8_t     depth;          // absolute depth (1-based)
    PokeType    typeBias[3];    // types more likely to appear
    DnDClass    classBias[2];   // classes more likely to appear
};

// ── Run state ─────────────────────────────────────────────────────────────────
enum class RunPhase : uint8_t {
    Lobby,
    Exploring,
    InCombat,
    Trivia,
    WordleRecovery,
    HackMinigame,
    TrapMinigame,
    FloorComplete,
    RunOver,
};

struct DungeonRun {
    DungeonFloor    currentFloor;
    RunPhase        phase;
    uint8_t         partySize;
    DungeonTrainer  party[5];
    uint8_t         triviaCategories; // bitmask: which of 5 categories answered this run
    uint32_t        runSeed;          // for deterministic generation
    // Co-op level equalization: computed at run start from all party members' Pokemon.
    // Each trainer's runLevel[slot] = (actual_level + partyAvgLevel) / 2.
    uint16_t        partyAvgLevel;    // weighted average across all party members' 6-slot levels
};

// DungeonModule class removed — use DungeonGame (DungeonGame.h) instead.
// DungeonModule.h is now the shared struct/enum header only.
