#pragma once
// ── Pokemon Daycare Types & Persistence ──────────────────────────────────────
// Core data structures for the mesh daycare system.
// NOT wired into the build yet — standalone for validation.

#include <Arduino.h>

// ── Weather ──────────────────────────────────────────────────────────────────

enum DaycareWeatherType : uint8_t {
    WEATHER_NONE = 0,       // no WiFi = no weather
    WEATHER_CLEAR,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_THUNDERSTORM,
    WEATHER_FOG,
    WEATHER_WINDY,
    WEATHER_HOT,
    WEATHER_COLD,
    WEATHER_COUNT
};

struct DaycareWeather {
    DaycareWeatherType type = WEATHER_NONE;
    int8_t  tempC = 0;
    uint8_t windSpeedMps = 0;
    uint32_t lastFetchMs = 0;
};

// ── Mood ─────────────────────────────────────────────────────────────────────

enum DaycareMood : uint8_t {
    MOOD_CONTENT = 0,   // default solo state — not sad
    MOOD_HAPPY,         // mesh neighbors present, social events
    MOOD_LONELY,        // brief state after friend leaves, recovers
    MOOD_EXCITED,       // new node arrived, burst of energy
};

// ── Event category ───────────────────────────────────────────────────────────

enum DaycareEventCategory : uint8_t {
    EVCAT_SOCIAL = 0,
    EVCAT_COMBAT,
    EVCAT_EXPLORE,
    EVCAT_REST,
    EVCAT_MISCHIEF,
    EVCAT_DREAM,        // night only
    EVCAT_VISITOR,      // new node arrival
    EVCAT_ESCAPE,       // very rare
    EVCAT_WEATHER,      // only with WiFi
    EVCAT_COUNT
};

// ── Event rarity ─────────────────────────────────────────────────────────────

enum DaycareEventRarity : uint8_t {
    RARITY_COMMON = 0,      // multiple per day
    RARITY_UNCOMMON,        // daily
    RARITY_RARE,            // weekly
    RARITY_LEGENDARY,       // monthly
};

// ── Generated event ──────────────────────────────────────────────────────────

struct DaycareEvent {
    char message[200];          // assembled message text (local-trainer perspective)
    char remoteMessage[200];    // partner-trainer perspective (empty = use message)
    uint16_t xp;                // 0 if flavor-only
    uint8_t  targetSpeciesIdx;  // which of the 6 party pokemon gets XP (0-5)
    uint32_t targetNodeId;      // remote node to DM (0 = solo event, local only)
    bool     isBroadcast;       // achievement broadcast to MONSTERMESH_CHANNEL
    DaycareEventRarity rarity;
};

// ── Achievement flags ────────────────────────────────────────────────────────

enum DaycareAchievement : uint8_t {
    // Time
    ACH_FIRST_DAY = 0,
    ACH_SETTLED_IN,         // 100 hours
    ACH_DAYCARE_VETERAN,    // 1,000 hours
    ACH_LIFETIME_RESIDENT,  // 10,000 hours
    ACH_OLD_TIMER,          // 50,000 hours

    // Friendship
    ACH_FIRST_FRIEND,
    ACH_GOOD_FRIENDS,
    ACH_BEST_FRIENDS_FOREVER,
    ACH_SOCIAL_BUTTERFLY,   // Friends+ with 10
    ACH_BELOVED,            // Best Friends with 5

    // Rivalry
    ACH_FIRST_RIVAL,
    ACH_HEATED_RIVALRY,     // Fierce Rivals tier
    ACH_ARCH_NEMESIS,       // Arch-Rivals tier
    ACH_FRIENDLY_RIVAL,     // max friendship AND max rivalry
    ACH_FIGHT_CLUB,         // 5+ active rivalries
    ACH_UNSTOPPABLE_FORCE,  // win 50 spars
    ACH_RESPECT,            // rivalry evolves into Best Friends

    // XP / Level
    ACH_FIRST_LEVEL_UP,
    ACH_DAYCARE_GRADUATE,   // 10 levels
    ACH_DAYCARE_SCHOLAR,    // 25 levels

    // Events
    ACH_EXPLORER,           // 50 explore events
    ACH_DREAMER,            // 20 dream events
    ACH_ESCAPE_ARTIST,
    ACH_FULL_MOON,
    ACH_SPLASH_CHAMPION,    // Magikarp splashes 100 times
    ACH_THE_LONELY_ONE,     // 7 days missing friend then reunite

    // Weather
    ACH_STORM_CHASER,       // 10 thunderstorms
    ACH_SNOWBOUND,          // 10 snow events
    ACH_WEATHERED,          // all 7 weather types
    ACH_LIGHTNING_ROD,      // Electric absorbs lightning 5x
    ACH_FOG_WALKER,         // Ghost in 10 fogs

    // Mesh
    ACH_WELCOME_COMMITTEE,  // greet 5 new nodes
    ACH_MESH_ELDER,         // greet 25 new nodes
    ACH_NEIGHBORHOOD,       // 5+ nodes simultaneous
    ACH_COMMUNITY,          // 10+ nodes simultaneous

    ACH_COUNT               // must be <= 64
};
static_assert(ACH_COUNT <= 64, "Achievement count exceeds 64-bit bitfield");

// ── Achievement metadata ─────────────────────────────────────────────────────

struct AchievementDef {
    const char *name;
    const char *description;
    bool broadcast;  // announce on MONSTERMESH_CHANNEL?
};

static const AchievementDef achievementDefs[] = {
    // Time
    {"First Day",           "24 hours in daycare",                  false},
    {"Settled In",          "100 hours in daycare",                 false},
    {"Daycare Veteran",     "1,000 hours in daycare",               true},
    {"Lifetime Resident",   "10,000 hours in daycare",              true},
    {"Old Timer",           "50,000 hours in daycare",              true},
    // Friendship
    {"First Friend",        "Reach Acquaintance tier",              false},
    {"Good Friends",        "Reach Friends tier",                   false},
    {"Best Friends Forever","Reach Best Friends tier",              true},
    {"Social Butterfly",    "Friends+ with 10 different Pokemon",   true},
    {"Beloved",             "Best Friends with 5 Pokemon",          true},
    // Rivalry
    {"First Rival",         "Reach Competitors tier",               false},
    {"Heated Rivalry",      "Reach Fierce Rivals tier",             true},
    {"Arch-Nemesis",        "Reach Arch-Rivals tier",               true},
    {"Friendly Rival",      "Max friendship AND rivalry",           true},
    {"Fight Club",          "5+ active rivalries",                  true},
    {"Unstoppable Force",   "Win 50 rival spars",                   true},
    {"Respect",             "Rivalry becomes Best Friends",         true},
    // XP
    {"First Level Up",      "Gain a level in daycare",              false},
    {"Daycare Graduate",    "Gain 10 levels in daycare",            true},
    {"Daycare Scholar",     "Gain 25 levels in daycare",            true},
    // Events
    {"Explorer",            "50 exploration events",                false},
    {"Dreamer",             "20 dream events",                      false},
    {"Escape Artist",       "Experience a rare escape",             true},
    {"Full Moon",           "Witness a full moon event",            true},
    {"Splash Champion",     "Magikarp splashes 100 times",          true},
    {"The Lonely One",      "Miss a friend 7 days then reunite",    true},
    // Weather
    {"Storm Chaser",        "Active in 10 thunderstorms",           false},
    {"Snowbound",           "Active in 10 snow events",             false},
    {"Weathered",           "Experience all 7 weather types",       true},
    {"Lightning Rod",       "Electric absorbs lightning 5x",        true},
    {"Fog Walker",          "Ghost thrives in 10 fogs",             false},
    // Mesh
    {"Welcome Committee",   "Greet 5 new nodes",                    false},
    {"Mesh Elder",          "Greet 25 new nodes",                   true},
    {"Neighborhood",        "5+ nodes in daycare",                  true},
    {"Community",           "10+ nodes in daycare",                 true},
};
static_assert(sizeof(achievementDefs) / sizeof(achievementDefs[0]) == ACH_COUNT,
              "achievementDefs must match ACH_COUNT");

// ── Relationship (friendship + rivalry per pair) ─────────────────────────────

struct DaycareRelationship {
    uint32_t nodeId;
    uint8_t  localSpeciesIdx;   // index into local party (0-5)
    uint8_t  remoteSpeciesDex;  // Pokedex number of remote Pokemon
    uint8_t  friendship;        // 0-255
    uint8_t  rivalry;           // 0-255, independent of friendship
    uint16_t sparCount;         // for achievements
    uint32_t lastSeenMs;        // for decay calculation
    uint16_t daysMissing;       // for The Lonely One achievement
};

// Friendship tiers
static constexpr uint8_t FRIEND_TIER_STRANGER     = 0;    // 0-20
static constexpr uint8_t FRIEND_TIER_ACQUAINTANCE  = 21;
static constexpr uint8_t FRIEND_TIER_FRIEND        = 61;
static constexpr uint8_t FRIEND_TIER_CLOSE_FRIEND  = 121;
static constexpr uint8_t FRIEND_TIER_BEST_FRIEND   = 201;

// Rivalry tiers
static constexpr uint8_t RIVAL_TIER_NONE           = 0;    // 0-15
static constexpr uint8_t RIVAL_TIER_COMPETITOR     = 16;
static constexpr uint8_t RIVAL_TIER_RIVAL          = 51;
static constexpr uint8_t RIVAL_TIER_FIERCE         = 121;
static constexpr uint8_t RIVAL_TIER_ARCH           = 201;

// Friendship XP multipliers (x100 for integer math)
inline uint8_t friendshipXpMultiplier100(uint8_t friendship) {
    if (friendship >= FRIEND_TIER_BEST_FRIEND)   return 200;  // 2.0x
    if (friendship >= FRIEND_TIER_CLOSE_FRIEND)  return 150;  // 1.5x
    if (friendship >= FRIEND_TIER_FRIEND)        return 125;  // 1.25x
    if (friendship >= FRIEND_TIER_ACQUAINTANCE)  return 110;  // 1.1x
    return 100;  // 1.0x
}

// Rivalry XP bonus (flat, added to base)
inline uint8_t rivalryXpBonus(uint8_t rivalry) {
    if (rivalry >= RIVAL_TIER_ARCH)   return 30;
    if (rivalry >= RIVAL_TIER_FIERCE) return 20;
    if (rivalry >= RIVAL_TIER_RIVAL)  return 10;
    if (rivalry >= RIVAL_TIER_COMPETITOR) return 5;
    return 0;
}

inline const char *friendshipTierName(uint8_t friendship) {
    if (friendship >= FRIEND_TIER_BEST_FRIEND)   return "Best Friends";
    if (friendship >= FRIEND_TIER_CLOSE_FRIEND)  return "Close Friends";
    if (friendship >= FRIEND_TIER_FRIEND)        return "Friends";
    if (friendship >= FRIEND_TIER_ACQUAINTANCE)  return "Acquaintances";
    return "Strangers";
}

inline const char *rivalryTierName(uint8_t rivalry) {
    if (rivalry >= RIVAL_TIER_ARCH)   return "Arch-Rivals";
    if (rivalry >= RIVAL_TIER_FIERCE) return "Fierce Rivals";
    if (rivalry >= RIVAL_TIER_RIVAL)  return "Rivals";
    if (rivalry >= RIVAL_TIER_COMPETITOR) return "Competitors";
    return nullptr;  // no rivalry
}

// ── Per-Pokemon daycare state ────────────────────────────────────────────────

struct DaycarePokemonState {
    uint16_t speciesDex;          // national dex (1-386)
    char     nickname[11];        // Player-set nickname (10 + null), empty = use species name
    uint8_t  savLevel;            // Level read from SAV file at check-in
    uint32_t savExp;              // Total EXP read from SAV file at check-in
    uint32_t totalHours;
    uint32_t totalXpGained;       // Uncommitted daycare XP pending a durable SAV flush
    uint16_t totalLevelsGained;   // Lifetime levels gained (display/achievements; never a level offset)
    DaycareMood mood;
    uint8_t  escapeCount;
    uint16_t dreamCount;
    uint16_t exploreCount;
    uint16_t splashCount;         // Magikarp only
    uint8_t  moves[4];            // SAV moves at check-in
};

// ── Persistence structure ────────────────────────────────────────────────────

static constexpr uint8_t MAX_RELATIONSHIPS = 32;
static constexpr uint8_t MAX_KNOWN_NODES   = 32;

struct DaycareState {
    uint32_t magic;               // persistence format/version marker
    // Format history (all three are sizeof()==identical, so ONLY the magic word
    // discriminates them — a size check cannot):
    //   DACA0001  pre-audit current line — 16-bit speciesDex (this exact layout)
    //   DACA0002  audit b58 line — 8-bit speciesDex, so nickname/savLevel sit one
    //             byte earlier inside each per-Pokemon record
    //   DACA0003  current — 16-bit speciesDex + transactional flush + dual-legacy
    //             migration that fails closed on ambiguous legacy pending XP
    static constexpr uint32_t LEGACY_MAGIC    = 0xDACA0001;  // 16-bit, direct read
    static constexpr uint32_t LEGACY_MAGIC_V2 = 0xDACA0002;  // 8-bit, needs field remap
    static constexpr uint32_t MAGIC           = 0xDACA0003;

    // Per-Pokemon (6 party slots)
    DaycarePokemonState pokemon[6];
    uint8_t partyCount;

    // Relationships (friendship + rivalry per cross-mesh pair)
    DaycareRelationship relationships[MAX_RELATIONSHIPS];
    uint8_t relationshipCount;

    // Achievements
    uint64_t achievementFlags;

    // Weather tracking
    uint8_t weatherTypesSeen;     // bitfield for Weathered achievement
    uint8_t thunderstormCount;
    uint8_t snowCount;
    uint8_t fogCount;
    uint8_t lightningAbsorbs;

    // Mesh tracking
    uint32_t knownNodes[MAX_KNOWN_NODES];
    uint8_t  knownNodeCount;
    uint8_t  uniqueTrainersGreeted;

    // Counters
    uint32_t totalEvents;
    uint16_t totalSparWins;

    // Timing
    uint32_t lastEventMs;         // for 1/hour rate limit
    uint32_t lastBeaconMs;
    uint32_t lastWeatherFetchMs;
    uint32_t lastFriendshipDecayMs;

    // Current weather (not persisted — re-fetched)
    // (stored in RAM only, not saved to flash)
};

// ── Achievement helpers ──────────────────────────────────────────────────────

inline bool hasAchievement(const DaycareState &state, DaycareAchievement ach) {
    return (state.achievementFlags & (1ULL << ach)) != 0;
}

inline bool grantAchievement(DaycareState &state, DaycareAchievement ach) {
    if (hasAchievement(state, ach)) return false;  // already have it
    state.achievementFlags |= (1ULL << ach);
    return true;  // newly granted
}

// ── Relationship helpers ─────────────────────────────────────────────────────

inline DaycareRelationship *findRelationship(DaycareState &state,
                                              uint32_t nodeId,
                                              uint8_t localIdx,
                                              uint8_t remoteDex) {
    for (uint8_t i = 0; i < state.relationshipCount; i++) {
        auto &r = state.relationships[i];
        if (r.nodeId == nodeId && r.localSpeciesIdx == localIdx &&
            r.remoteSpeciesDex == remoteDex) {
            return &r;
        }
    }
    return nullptr;
}

inline DaycareRelationship *getOrCreateRelationship(DaycareState &state,
                                                     uint32_t nodeId,
                                                     uint8_t localIdx,
                                                     uint8_t remoteDex) {
    auto *existing = findRelationship(state, nodeId, localIdx, remoteDex);
    if (existing) return existing;

    if (state.relationshipCount >= MAX_RELATIONSHIPS) {
        // Evict oldest (by lastSeenMs)
        uint8_t oldest = 0;
        uint32_t oldestMs = UINT32_MAX;
        for (uint8_t i = 0; i < state.relationshipCount; i++) {
            if (state.relationships[i].lastSeenMs < oldestMs) {
                oldestMs = state.relationships[i].lastSeenMs;
                oldest = i;
            }
        }
        state.relationships[oldest] = {};
        auto &r = state.relationships[oldest];
        r.nodeId = nodeId;
        r.localSpeciesIdx = localIdx;
        r.remoteSpeciesDex = remoteDex;
        r.lastSeenMs = millis();
        return &r;
    }

    auto &r = state.relationships[state.relationshipCount++];
    r = {};
    r.nodeId = nodeId;
    r.localSpeciesIdx = localIdx;
    r.remoteSpeciesDex = remoteDex;
    r.lastSeenMs = millis();
    return &r;
}

// ── Known node helpers ───────────────────────────────────────────────────────

inline bool isKnownNode(const DaycareState &state, uint32_t nodeId) {
    for (uint8_t i = 0; i < state.knownNodeCount; i++) {
        if (state.knownNodes[i] == nodeId) return true;
    }
    return false;
}

inline bool addKnownNode(DaycareState &state, uint32_t nodeId) {
    if (isKnownNode(state, nodeId)) return false;
    if (state.knownNodeCount < MAX_KNOWN_NODES) {
        state.knownNodes[state.knownNodeCount++] = nodeId;
    } else {
        // Overwrite oldest (slot 0), shift down
        for (uint8_t i = 0; i < MAX_KNOWN_NODES - 1; i++) {
            state.knownNodes[i] = state.knownNodes[i + 1];
        }
        state.knownNodes[MAX_KNOWN_NODES - 1] = nodeId;
    }
    state.uniqueTrainersGreeted++;
    return true;  // new node
}

// ── Beacon format ────────────────────────────────────────────────────────────

#pragma pack(push, 1)
struct DaycareBeacon {
    uint8_t  type;          // PktType value for DAYCARE_BEACON
    uint32_t nodeId;
    char     shortName[5];  // Meshtastic short name (4 + null)
    char     gameName[8];   // Pokemon in-game trainer name (7 + null)
    uint8_t  partyCount;
    struct {
        uint8_t species;    // Pokedex number
        uint8_t level;
        char    nickname[11]; // Player nickname (10 + null)
        uint8_t moves[4];
    } pokemon[6];
    uint8_t  ngPlusTier;    // 0 = base game; 1..5 = Legend of Charizard NG+
    // Set on boot beacons and manual `beacon` / `bc` commands. When a peer
    // sees this true, they respond MQTT-only with their own beacon +
    // NodeInfo so the requester populates their neighbor list without
    // every deck slamming the LoRa airwaves. Periodic auto-beacons leave
    // this zero so they don't trigger fan-out.
    uint8_t  requestResponse;
};
#pragma pack(pop)

// ── Weather type XP multiplier per Pokemon type ──────────────────────────────
// Returns multiplier x100 (150 = 1.5x boost, 75 = 0.75x penalty, 100 = neutral)
// Returns 0 if no weather (WEATHER_NONE) — caller should skip weather logic

inline uint8_t weatherTypeMultiplier100(DaycareWeatherType weather, uint8_t pkmnType) {
    if (weather == WEATHER_NONE) return 100;  // no weather system active

    // Import type indices from DaycareData.h (these match the PkmnType enum)
    // TYPE_NORMAL=0 TYPE_FIGHTING=1 TYPE_FLYING=2 TYPE_POISON=3 TYPE_GROUND=4
    // TYPE_ROCK=5 TYPE_BUG=6 TYPE_GHOST=7 TYPE_FIRE=8 TYPE_WATER=9
    // TYPE_GRASS=10 TYPE_ELECTRIC=11 TYPE_PSYCHIC=12 TYPE_ICE=13 TYPE_DRAGON=14

    switch (weather) {
        case WEATHER_RAIN:
            if (pkmnType == 9 || pkmnType == 10) return 150;  // Water, Grass boosted
            if (pkmnType == 8 || pkmnType == 5 || pkmnType == 4) return 75;  // Fire, Rock, Ground penalized
            if (pkmnType == 11) return 75;  // Electric penalized (conductivity danger)
            break;
        case WEATHER_THUNDERSTORM:
            if (pkmnType == 11) return 200;  // Electric ENERGIZED
            if (pkmnType == 2) return 75;    // Flying grounded
            if (pkmnType == 9) return 75;    // Water hides
            break;
        case WEATHER_CLEAR:
        case WEATHER_HOT:
            if (pkmnType == 8) return 150;   // Fire boosted
            if (pkmnType == 10) return 150;  // Grass photosynthesis
            if (pkmnType == 13) return 75;   // Ice suffering
            if (weather == WEATHER_HOT && pkmnType == 6) return 75;  // Bug sluggish
            break;
        case WEATHER_SNOW:
        case WEATHER_COLD:
            if (pkmnType == 13) return 150;  // Ice thriving
            if (pkmnType == 8) return 75;    // Fire struggles
            if (pkmnType == 10) return 75;   // Grass dormant
            if (pkmnType == 6) return 75;    // Bug hibernating
            break;
        case WEATHER_WINDY:
            if (pkmnType == 2 || pkmnType == 14) return 150;  // Flying, Dragon soaring
            if (pkmnType == 6) return 75;    // Bug blown around
            if (pkmnType == 3) return 75;    // Poison gas dispersed
            break;
        case WEATHER_FOG:
            if (pkmnType == 7) return 150;   // Ghost thriving
            if (pkmnType == 2) return 75;    // Flying can't see
            if (pkmnType == 0) return 75;    // Normal nervous
            break;
        default:
            break;
    }
    return 100;  // neutral
}
