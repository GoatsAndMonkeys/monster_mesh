#pragma once
// ── Pokemon Daycare — Main Orchestrator (Pi daemon port) ─────────────────────
// Manages the hourly event loop, check-in/out, beacon exchange, mood,
// friendship decay, weather fetching, XP write-back, and DM sending.

#include "../shared/platform.h"
#include "../shared/DaycareTypes.h"
#include "../shared/DaycareEventGen.h"
#include "../shared/DaycareAchievements.h"
#include "../shared/DaycareSavPatcher.h"

// ── Daycare manager ──────────────────────────────────────────────────────────

class PokemonDaycare {
public:
    // Call once after module init
    void init();

    // Call every frame from the daemon tick — internally rate-limits
    void tick(uint32_t nowMs);

    // Check in from SRAM — reads party species, levels, nicknames, EXP directly
    // sram: pointer to the 32KB Game Boy SRAM buffer
    // shortName: Meshtastic short name (4 chars), gameName: Pokemon trainer name (7 chars)
    void checkIn(const uint8_t *sram, const char *shortName, const char *gameName);

    // Legacy check-in for tests (no SRAM)
    void checkIn(const uint8_t *partySpeciesDex, const uint8_t *partyLevels,
                 const char nicknames[][11], uint8_t count,
                 const char *shortName, const char *gameName);

    // Check out — writes XP/levels back to SRAM and fixes checksum
    // sram: writable 32KB SRAM buffer (nullptr = don't patch, just stop daycare)
    void checkOut(uint8_t *sram = nullptr);

    // Process incoming daycare beacon from another node
    void handleBeacon(const DaycareBeacon &beacon);

    // Get current state (for UI display)
    const DaycareState &getState() const { return state_; }
    bool isActive() const { return active_; }
    uint8_t getNeighborCount() const { return neighborCount_; }
    const DaycareNeighborPokemon *getNeighbors() const { return neighbors_; }
    const uint32_t *getNeighborLastSeen() const { return neighborLastSeen_; }

    // Save/load state to POSIX file
    bool saveState();
    bool loadState();

    // Get last event (for display)
    const DaycareEvent &getLastEvent() const { return lastEvent_; }
    uint32_t getLastEventTime() const { return lastEventTimeMs_; }

    // Force an immediate event cycle (for testing)
    void forceEvent() { if (active_) { runEventCycle(millis()); state_.lastEventMs = millis(); } }

    // Force an immediate beacon broadcast.
    void forceBeacon() { broadcastBeacon(millis()); state_.lastBeaconMs = millis(); }

    // Credit XP from a finished battle into the same totalXpGained counter
    // a daycare event would feed.  Drained by clearTotalXpGained() after a
    // successful SAV writeback so we don't double-apply.
    void creditBattleXp(uint8_t slot, uint32_t xp) {
        if (slot < 6 && slot < state_.partyCount)
            state_.pokemon[slot].totalXpGained += xp;
    }

    void clearTotalXpGained() {
        for (uint8_t i = 0; i < 6; i++) state_.pokemon[i].totalXpGained = 0;
    }

    // Trigger a "dog park" arrival event when a new trainer comes online
    // Returns true if an event was generated (and DM sent)
    bool triggerArrivalEvent(const DaycareBeacon &newcomer);

    // Set weather (called from WiFi fetch or from another node's report)
    void setWeather(DaycareWeatherType type, int8_t tempC, uint8_t windMps);

    // Set location + time for sunrise/sunset night detection
    // latDeg: latitude in degrees (-90 to 90), hourOfDay: 0-23, dayOfYear: 1-365
    void setLocation(float latDeg, uint8_t hourOfDay, uint16_t dayOfYear);

    // Callback: set this to send a DM packet
    typedef void (*SendDmFunc)(uint32_t destNodeId, const char *msg, void *ctx);
    void setSendDm(SendDmFunc func, void *ctx) { sendDm_ = func; sendDmCtx_ = ctx; }

    // Callback: set this to broadcast on MONSTERMESH_CHANNEL
    typedef void (*BroadcastFunc)(const char *msg, void *ctx);
    void setBroadcast(BroadcastFunc func, void *ctx) { broadcast_ = func; broadcastCtx_ = ctx; }

    // Callback: set this to send a beacon packet
    typedef void (*SendBeaconFunc)(const DaycareBeacon &beacon, void *ctx);
    void setSendBeacon(SendBeaconFunc func, void *ctx) { sendBeacon_ = func; sendBeaconCtx_ = ctx; }

    // Callback: fired after each event (for IPC push)
    typedef void (*EventCallback)(const DaycareEvent &evt, void *ctx);
    void setEventCallback(EventCallback func, void *ctx) { eventCb_ = func; eventCbCtx_ = ctx; }

private:
    DaycareState state_ = {};
    bool active_ = false;

    // Neighbors from beacons
    static constexpr uint8_t MAX_NEIGHBORS = 16;
    DaycareNeighborPokemon neighbors_[MAX_NEIGHBORS] = {};
    uint8_t neighborCount_ = 0;
    uint32_t neighborLastSeen_[MAX_NEIGHBORS] = {};

    // Local trainer info
    char shortName_[5] = {};
    char gameName_[8] = {};

    // Weather
    DaycareWeather weather_ = {};

    // Location / time for sunrise/sunset
    float latDeg_ = 0;
    uint8_t hourOfDay_ = 12;
    uint16_t dayOfYear_ = 172;
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
    EventCallback eventCb_ = nullptr;
    void *eventCbCtx_ = nullptr;

    // Internal
    void runEventCycle(uint32_t nowMs);
    void updateMood(uint32_t nowMs);
    void decayFriendships(uint32_t nowMs);
    void expireNeighbors(uint32_t nowMs);
    void broadcastBeacon(uint32_t nowMs);
    void updateWeatherCounters(DaycareWeatherType type);
    void updateEventCounters(const DaycareEvent &evt, uint8_t targetIdx);
    bool isNight() const;
};
