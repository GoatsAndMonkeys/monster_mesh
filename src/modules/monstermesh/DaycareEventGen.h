#pragma once
// ── Pokemon Daycare Event Generator ──────────────────────────────────────────
// Dual-layer system: flat templates (Layer 1) + compositional generation (Layer 2)
// NOT wired into build yet — standalone for validation.

#include "DaycareTypes.h"

// Forward declaration — DaycareData.h is auto-generated
struct DaycareSpecies;
struct TypeBehavior;

// ── Neighbor info (from beacon) ──────────────────────────────────────────────

struct DaycareNeighborPokemon {
    uint32_t nodeId;
    char     shortName[5];      // Meshtastic short name (4 + null)
    char     gameName[8];       // Pokemon in-game trainer name (7 + null)
    uint8_t  speciesDex;        // = party[0].species (compat)
    uint8_t  level;             // = party[0].level   (compat)
    char     nickname[11];      // = party[0].nickname (compat)
    uint8_t  partyCount;
    struct { uint8_t species; uint8_t level; char nickname[11]; uint8_t moves[4]; } party[6];
};

// ── Event generator ──────────────────────────────────────────────────────────

class DaycareEventGen {
public:
    // Generate one event. Call once per hour.
    // localParty: array of 6 DaycarePokemonState (from DaycareState)
    // localPartyCount: how many are valid (1-6)
    // neighbors: array of Pokemon from nearby mesh nodes
    // neighborCount: how many neighbors
    // state: mutable reference to DaycareState for friendship/rivalry/achievement updates
    // weather: current weather (WEATHER_NONE if no WiFi)
    // isNight: true if 10pm-6am
    // newNodeId: non-zero if a new node just appeared (for visitor event)
    static DaycareEvent generate(
        const DaycarePokemonState *localParty, uint8_t localPartyCount,
        const DaycareNeighborPokemon *neighbors, uint8_t neighborCount,
        DaycareState &state,
        DaycareWeatherType weather,
        bool isNight,
        uint32_t newNodeId
    );

private:
    // Layer 1: flat templates — species-specific signature moments
    static bool tryFlatTemplate(
        DaycareEvent &out,
        const DaycarePokemonState *localParty, uint8_t localPartyCount,
        const DaycareNeighborPokemon *neighbors, uint8_t neighborCount,
        DaycareState &state, DaycareWeatherType weather, bool isNight
    );

    // Layer 2: compositional generation
    static void generateCompositional(
        DaycareEvent &out,
        const DaycarePokemonState *localParty, uint8_t localPartyCount,
        const DaycareNeighborPokemon *neighbors, uint8_t neighborCount,
        DaycareState &state, DaycareWeatherType weather, bool isNight
    );

    // Special events
    static void generateDream(DaycareEvent &out,
        const DaycarePokemonState *localParty, uint8_t localPartyCount);
    static void generateVisitor(DaycareEvent &out, uint32_t newNodeId,
        const DaycarePokemonState *localParty, uint8_t localPartyCount);

public:
    // Generate a "dog park" arrival event when a new trainer comes online
    // Matches local Pokemon with newcomer's Pokemon by type affinity or rivalry
    // localShortName/localGameName: the local trainer identity so the remote
    //   perspective message can be assembled from the newcomer's POV.
    static void generateArrivalEvent(
        DaycareEvent &out,
        const DaycarePokemonState *localParty, uint8_t localPartyCount,
        const DaycareBeacon &newcomer,
        DaycareState &state,
        const char *localShortName = "",
        const char *localGameName  = "");

private:
    static bool tryEscape(DaycareEvent &out,
        const DaycarePokemonState *localParty, uint8_t localPartyCount);
    static void generateWeatherEvent(DaycareEvent &out,
        const DaycarePokemonState *localParty, uint8_t localPartyCount,
        DaycareWeatherType weather);

    // Helpers
    static uint8_t pickPokemon(uint8_t partyCount);
    static uint16_t calcXp(uint8_t baseXp, uint8_t friendship, uint8_t rivalry,
                           uint8_t weatherMult100, bool isMentor);
    static void formatMessage(char *buf, size_t bufSize, const char *fmt, ...);
};
