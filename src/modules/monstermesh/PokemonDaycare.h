#pragma once
// ── Pokemon Daycare — Main Orchestrator ─────────────────────────────────────
// Manages the hourly event loop, check-in/out, beacon exchange, mood,
// friendship decay, weather fetching, XP write-back, and DM sending.
// NOT wired into build yet — standalone for validation.

#include "DaycareTypes.h"
#include "DaycareEventGen.h"
#include "DaycareAchievements.h"
#include "DaycareSavPatcher.h"
#include "KantoJourney.h"

// Forward decl — the emulator module provides party data
class MonsterMeshModule;

// ── Daycare manager ─────────────────────────────────────────────────────────

class PokemonDaycare {
public:
    // Call once after module init
    void init();

    // Call every frame from MonsterMeshModule::runOnce() — internally rate-limits
    void tick(uint32_t nowMs);

    // Check in from SRAM — reads party species, levels, nicknames, EXP directly
    // sram: pointer to the 32KB Game Boy SRAM buffer
    // shortName: Meshtastic short name (4 chars), gameName: Pokemon trainer name (7 chars)
    bool checkIn(const uint8_t *sram, const char *shortName,
                 const char *gameName, const char *savPath);

    // Party-structure check-in used by the terminal path. partyExp is optional
    // for legacy tests, but production callers supply the exact 24-bit SAV EXP
    // so effective levels are never reconstructed from a level threshold.
    // partyIdentity is required and stores OT ID[2] + packed DVs[2] per slot;
    // this prevents same-species replacements from inheriting pending XP.
    bool checkIn(const uint8_t *partySpeciesDex, const uint8_t *partyLevels,
                 const char nicknames[][11], uint8_t count,
                 const char *shortName, const char *gameName,
                 const uint32_t *partyExp,
                 const uint8_t partyIdentity[][4], const char *savPath);

    // True while at least one party member has XP that has not been committed
    // to a durable SAV image.
    bool hasPendingXp() const;

    // True when the next tick would enter runEventCycle(). Exposing the same
    // predicate lets callers invalidate any published SAV-derived party before
    // tick creates pending XP, without duplicating the private interval.
    bool eventDue(uint32_t nowMs) const;

    // Apply a frozen snapshot of pending XP to a caller-owned 32KB SRAM image.
    // This is the prepare phase of the flush transaction: it does not clear XP,
    // update the SAV baseline, persist daycare state, or deactivate daycare.
    // On retry, the same staged snapshot is reused so XP earned after the first
    // attempt cannot be cleared by the older write. The caller must supply a
    // freshly read, unpatched SAV image on each attempt.
    // Returns false if there is no pending XP or the SRAM party is invalid or
    // does not match the checked-in party.
    bool applyPendingXp(const char *savPath, uint8_t *sram);
    bool applyPendingXp(uint8_t *sram) {
        return savOwnerLoaded_ && applyPendingXp(savOwner_.savPath, sram);
    }

    // Commit the most recently applied snapshot after the caller has durably
    // written and verified that SRAM image. Validates the written party, EXP,
    // and levels before changing daycare state; a mismatch leaves all pending
    // XP staged for retry. On success, advances savExp/savLevel and subtracts
    // only the staged XP. Lifetime achievement counters are preserved.
    bool commitXpFlush(const char *savPath, const uint8_t *writtenSram);
    bool commitXpFlush(const uint8_t *writtenSram) {
        return savOwnerLoaded_ && commitXpFlush(savOwner_.savPath, writtenSram);
    }

    // Advance only the SAV-derived baseline after another serialized writer
    // (currently terminal battle XP) has committed a fresh party image.
    // Pending daycare XP and lifetime achievement counters are preserved.
    bool refreshSavBaseline(const char *savPath,
                            const DaycarePartyInfo *party, uint8_t count);
    bool refreshSavBaseline(const DaycarePartyInfo *party, uint8_t count) {
        return savOwnerLoaded_ &&
               refreshSavBaseline(savOwner_.savPath, party, count);
    }

    // A pending flush is owned by one canonical SD-relative SAV path. This
    // guard prevents failed or reboot-recovered XP from migrating when a
    // different ROM happens to have the same party species.
    bool isBoundToSav(const char *savPath) const;

    // Effective current level is always derived from SAV EXP + pending XP;
    // totalLevelsGained is a lifetime achievement counter, not a level offset.
    uint8_t effectiveLevel(uint8_t partyIdx) const;

    // Terminal checkout. If SRAM is supplied, stage/apply pending XP to it but
    // do not commit it; the caller must durably write/verify the image and then
    // call commitXpFlush(). Checkout always deactivates daycare, including when
    // SRAM is null or preparation fails.
    void checkOut(uint8_t *sram = nullptr);

    // Process incoming daycare beacon from another node
    void handleBeacon(const DaycareBeacon &beacon);

    // Get current state (for UI display)
    const DaycareState &getState() const { return state_; }
    bool isActive() const { return active_; }
    uint8_t getNeighborCount() const { return neighborCount_; }
    const DaycareNeighborPokemon *getNeighbors() const { return neighbors_; }
    const uint32_t *getNeighborLastSeen() const { return neighborLastSeen_; }

    // Save/load state to LittleFS
    bool saveState();
    bool loadState();

    // Get last event (for display)
    const DaycareEvent &getLastEvent() const { return lastEvent_; }
    uint32_t getLastEventTime() const { return lastEventTimeMs_; }

    // Per-Pokemon Kanto journey — the individual traveller boarding in party
    // slot `slot` (0-5), or nullptr if that slot has no active journey. The
    // journey follows the Pokemon by identity, not by slot, so it survives
    // swaps. See KantoJourney.h for the waypoint map.
    const KantoTraveler *slotTraveler(uint8_t slot) const {
        if (slot >= 6 || travelerIdx_[slot] == NO_TRAVELER) return nullptr;
        return journeys_.get(travelerIdx_[slot]);
    }

    // Force an immediate event cycle (for testing)
    void forceEvent() { if (active_) { runEventCycle(millis()); state_.lastEventMs = millis(); } }

    // Force an immediate beacon broadcast. Always fires — even before
    // checkIn() flipped active_, so the user-typed `beacon` command can
    // reach peers right after boot. With no party loaded, the beacon
    // carries short_name + gameName but partyCount=0; receivers still
    // register us as a neighbor.
    void forceBeacon() { broadcastBeacon(millis()); state_.lastBeaconMs = millis(); }

    // Trigger a "dog park" arrival event when a new trainer comes online
    // Returns true if an event was generated (and DM sent)
    bool triggerArrivalEvent(const DaycareBeacon &newcomer);

    // Set weather (called from WiFi fetch or from another node's report)
    void setWeather(DaycareWeatherType type, int8_t tempC, uint8_t windMps);

    // Set location + time for sunrise/sunset night detection
    // latDeg: latitude in degrees (-90 to 90), hourOfDay: 0-23, dayOfYear: 1-365
    void setLocation(float latDeg, uint8_t hourOfDay, uint16_t dayOfYear);

    // Callback: set this to send a DM packet (implemented by MonsterMeshModule)
    typedef void (*SendDmFunc)(uint32_t destNodeId, const char *msg, void *ctx);
    void setSendDm(SendDmFunc func, void *ctx) { sendDm_ = func; sendDmCtx_ = ctx; }

    // Callback: set this to broadcast on MONSTERMESH_CHANNEL
    typedef void (*BroadcastFunc)(const char *msg, void *ctx);
    void setBroadcast(BroadcastFunc func, void *ctx) { broadcast_ = func; broadcastCtx_ = ctx; }

    // Callback: set this to send a beacon packet
    typedef void (*SendBeaconFunc)(const DaycareBeacon &beacon, void *ctx);
    void setSendBeacon(SendBeaconFunc func, void *ctx) { sendBeacon_ = func; sendBeaconCtx_ = ctx; }

private:
#ifdef PIO_UNIT_TESTING
    friend struct PokemonDaycareTestAccess;
#endif

    struct PendingFlush {
        bool staged = false;
        bool applied = false;
        uint8_t partyCount = 0;
        uint32_t xp[6] = {};
        uint8_t species[6] = {};
        uint8_t identity[6][4] = {};
        uint32_t baseExp[6] = {};
        uint32_t writtenExp[6] = {};
        uint8_t writtenLevel[6] = {};
        char savPath[256] = {};
    };

    enum class FlushJournalPhase : uint8_t {
        PREPARED = 1,
        PROMOTED = 2,
    };

    struct FlushJournal {
        static constexpr uint32_t MAGIC = 0x44434633; // "DCF3"
        uint32_t magic = MAGIC;
        uint8_t phase = 0;
        uint8_t partyCount = 0;
        uint8_t species[6] = {};
        uint8_t identity[6][4] = {};
        uint8_t writtenLevel[6] = {};
        uint32_t baseExp[6] = {};
        uint32_t xp[6] = {};
        uint32_t writtenExp[6] = {};
        char savPath[256] = {};
    };

    // DCF2 did not bind individual same-species Pokemon. It is recognized only
    // as unresolved evidence so upgrade paths fail closed instead of guessing.
    struct LegacyFlushJournal {
        static constexpr uint32_t MAGIC = 0x44434632; // "DCF2"
        uint32_t magic = MAGIC;
        uint8_t phase = 0;
        uint8_t partyCount = 0;
        uint8_t species[6] = {};
        uint8_t writtenLevel[6] = {};
        uint32_t baseExp[6] = {};
        uint32_t xp[6] = {};
        uint32_t writtenExp[6] = {};
        char savPath[256] = {};
    };

    struct SavOwner {
        static constexpr uint32_t MAGIC = 0x44434F32; // "DCO2"
        uint32_t magic = MAGIC;
        char savPath[256] = {};
        uint8_t partyCount = 0;
        uint8_t species[6] = {};
        uint8_t identity[6][4] = {};
    };

    struct LegacySavOwner {
        static constexpr uint32_t MAGIC = 0x44434F31; // "DCO1"
        uint32_t magic = MAGIC;
        char savPath[256] = {};
    };

    static_assert(sizeof(FlushJournal) == 372,
                  "DCF3 persistence layout changed");
    static_assert(sizeof(LegacyFlushJournal) == 348,
                  "DCF2 compatibility layout changed");
    static_assert(sizeof(SavOwner) == 292,
                  "DCO2 persistence layout changed");
    static_assert(sizeof(LegacySavOwner) == 260,
                  "DCO1 compatibility layout changed");

    DaycareState state_ = {};
    bool active_ = false;
    bool runtimeStateLoaded_ = false;
    PendingFlush pendingFlush_ = {};
    SavOwner savOwner_ = {};
    bool savOwnerLoaded_ = false;
    bool savOwnerLegacy_ = false;

    // Per-Pokemon Kanto journeys (identity-keyed roster, own kanto.dat file).
    // travelerIdx_ maps each party slot to a roster entry; rebuilt on checkIn.
    static constexpr uint8_t NO_TRAVELER = 0xFF;
    KantoJourney journeys_ = {};
    uint8_t travelerIdx_[6] = {NO_TRAVELER, NO_TRAVELER, NO_TRAVELER,
                               NO_TRAVELER, NO_TRAVELER, NO_TRAVELER};
    bool journeysLoaded_ = false;

    // Attach party slot `i` to its individual traveller and start/continue its
    // Kanto trip. Called from both checkIn paths.
    void attachJourney(uint8_t slot, uint16_t dex, const char *nick, const char *ot) {
        if (slot >= 6) return;
        if (!journeysLoaded_) { journeys_.load(); journeysLoaded_ = true; }
        travelerIdx_[slot] = journeys_.findOrCreate(dex, nick, ot);
    }

    // Neighbors from beacons
    static constexpr uint8_t MAX_NEIGHBORS = 16;
    DaycareNeighborPokemon neighbors_[MAX_NEIGHBORS] = {};
    uint8_t neighborCount_ = 0;
    uint32_t neighborLastSeen_[MAX_NEIGHBORS] = {};

    // Local trainer info
    char shortName_[5] = {};   // Meshtastic short name
    char gameName_[8] = {};    // Pokemon in-game trainer name

    // Weather
    DaycareWeather weather_ = {};

    // Location / time for sunrise/sunset
    float latDeg_ = 0;
    uint8_t hourOfDay_ = 12;
    uint16_t dayOfYear_ = 172;   // default: summer solstice
    bool hasLocation_ = false;

    // Last event for display
    DaycareEvent lastEvent_ = {};
    uint32_t lastEventTimeMs_ = 0;

    // Track new arrivals for visitor events
    uint32_t lastNewNodeId_ = 0;

    // Callbacks
    SendDmFunc sendDm_ = nullptr;
    void *sendDmCtx_ = nullptr;
    BroadcastFunc broadcast_ = nullptr;
    void *broadcastCtx_ = nullptr;
    SendBeaconFunc sendBeacon_ = nullptr;
    void *sendBeaconCtx_ = nullptr;

    // Internal
    void runEventCycle(uint32_t nowMs);
    void updateMood(uint32_t nowMs);
    void decayFriendships(uint32_t nowMs);
    void expireNeighbors(uint32_t nowMs);
    void broadcastBeacon(uint32_t nowMs);
    void updateWeatherCounters(DaycareWeatherType type);
    void updateEventCounters(const DaycareEvent &evt, uint8_t targetIdx);
    void addPendingXp(uint8_t partyIdx, uint16_t requestedXp);
    void clearPendingFlush();
    bool saveFlushJournal(FlushJournalPhase phase);
    bool loadFlushJournal(FlushJournal &journal) const;
    bool loadLegacyFlushJournal(LegacyFlushJournal &journal) const;
    void clearFlushJournal();
    bool reconcileFlushJournal(const char *savPath,
                               const DaycarePartyInfo *party, uint8_t count);
    bool ensureSavBinding(const char *savPath,
                          const DaycarePartyInfo *party, uint8_t count);
    bool partyMatchesSavOwner(const DaycarePartyInfo *party,
                              uint8_t count) const;
    void setSavBinding(const char *canonicalPath,
                       const DaycarePartyInfo *party, uint8_t count);
    bool saveSavOwner();
    bool loadSavOwner();
    bool isNight() const;
};
