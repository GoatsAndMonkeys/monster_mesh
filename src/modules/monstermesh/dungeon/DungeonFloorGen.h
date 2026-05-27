#pragma once
#include "DungeonModule.h"

// Floor generator — deterministic given run seed + depth.
// Plane/biome bias types and classes but does NOT lock them out.
// Encounter count scales to party size.

struct EncounterSlot {
    EncounterType   type;
    uint16_t        enemySpeciesId; // valid for WildPokemon / NpcTrainer
    uint8_t         enemyClassId;
    uint8_t         enemyClassLevel;
    bool            completed;
};

struct GeneratedFloor {
    DungeonFloor    meta;
    EncounterSlot   encounters[30];
    uint8_t         encounterCount;
    bool            hasPokemonCenter;   // always true at floor exit
    bool            hasBoss;
    uint8_t         hackTerminalCount;
    uint8_t         trapCount;
};

class DungeonFloorGen {
public:
    // Generate a floor given absolute depth (1-based) and party size (2-5).
    // Uses run seed + depth as RNG seed for reproducibility.
    static GeneratedFloor generate(uint8_t depth, uint8_t partySize, uint32_t seed);

private:
    // Map depth to plane + floor-in-plane
    static DungeonFloor makeMeta(uint8_t depth);

    // Determine type/class bias for this biome
    static void setBias(DungeonFloor& floor);

    // Pick encounter count: 15 + (partySize - 2) * 3, capped at 30
    static uint8_t encounterCount(uint8_t partySize);

    // Weight wild vs NPC trainer split (e.g., 70% wild / 30% trainer on early floors)
    static EncounterType pickEncounterType(uint8_t depth, uint32_t& rng);

    // Pick a species biased toward floor types
    static uint16_t pickSpecies(const DungeonFloor& floor, uint32_t& rng);

    // Pick a class biased toward floor classes
    static uint8_t pickClass(const DungeonFloor& floor, uint32_t& rng);

    static uint32_t lcg(uint32_t& state);
};
