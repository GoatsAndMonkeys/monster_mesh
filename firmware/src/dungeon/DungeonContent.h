#pragma once
#include <stdint.h>

// Static content tables — loaded from SD card at runtime.
// These are populated by the data pipeline scripts (tools/dungeon_data/).
// Baked-in fallbacks exist only for the minimum test encounter.

// ── Spell record (Open5e spells, rebalanced for Pokemon damage scale) ─────────
struct SpellRecord {
    uint16_t    id;
    char        name[24];
    uint8_t     level;          // spell slot level 1-9
    uint8_t     pokeType;       // PokeType index for matchup
    uint16_t    pokeBasePower;  // balanced damage equivalent (not raw dice)
    uint8_t     effectId;       // StatusFlag bit or 0 if none
    bool        isHealing;
    bool        isBuff;
};

// ── D&D class record ──────────────────────────────────────────────────────────
struct ClassRecord {
    uint8_t     id;             // DnDClass index
    char        name[16];
    uint8_t     hitDie;         // d6, d8, d10, d12
    uint8_t     primaryAbility; // 0=STR 1=DEX 2=CON 3=INT 4=WIS 5=CHA
    uint8_t     saveProficiency[2];
    uint8_t     spellSlots[20][9]; // [level][slot_level] — levels 1-20
    uint16_t    spellList[30];  // spell IDs available to this class
};

// ── Background record ─────────────────────────────────────────────────────────
struct BackgroundRecord {
    uint8_t     id;
    char        name[24];       // Pokemon-world name (e.g., "Ace Trainer")
    char        flavorName[24]; // D&D name ("Soldier")
    uint8_t     skillProficiencies[4]; // skill IDs, 0xFF = empty
    uint8_t     itemDropCadence; // 0=per-floor 1=per-plane
};

// ── Feat record ───────────────────────────────────────────────────────────────
struct FeatRecord {
    uint8_t     id;
    char        name[24];
    char        description[80];
    uint8_t     abilityBonus[6]; // +N per ability (usually 0 or 1)
    uint8_t     effectId;
};

// ── Trivia question record ────────────────────────────────────────────────────
enum class TriviaCategory : uint8_t {
    Pokemon = 0, DnD = 1, PopCulture = 2, ScienceNature = 3, History = 4
};

struct TriviaQuestion {
    uint16_t        id;
    TriviaCategory  category;
    uint8_t         minDepth;   // first floor depth this question is appropriate
    char            question[120];
    char            answer[32];
    char            altAnswer[32]; // acceptable alternate, empty if none
};

// ── Content table sizes (populated by data pipeline) ─────────────────────────
// TODO: replace with SD-loaded dynamic tables in Phase 1
static constexpr uint16_t SPELL_TABLE_SIZE      = 0; // placeholder
static constexpr uint16_t CLASS_TABLE_SIZE      = 12;
static constexpr uint16_t BACKGROUND_TABLE_SIZE = 0; // placeholder
static constexpr uint16_t FEAT_TABLE_SIZE       = 0; // placeholder
static constexpr uint16_t TRIVIA_TABLE_SIZE     = 0; // placeholder

// ── Loader (Phase 1) ──────────────────────────────────────────────────────────
class DungeonContent {
public:
    // Load all tables from SD card path
    static bool loadFromSD(const char* basePath);

    // Fallback: load hardcoded test data (Phase 1 only)
    static void loadTestData();

    static const SpellRecord*       getSpell(uint16_t id);
    static const ClassRecord*       getClass(uint8_t dndClassId);
    static const BackgroundRecord*  getBackground(uint8_t id);
    static const FeatRecord*        getFeat(uint8_t id);
    static const TriviaQuestion*    getRandomTrivia(TriviaCategory cat, uint8_t depth);
};
