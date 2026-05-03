#pragma once
// ── Pokemon Daycare Achievement Checker ─────────────────────────────────────
// Runs after each event to check and grant achievements.
// NOT wired into build yet — standalone for validation.

#include "DaycareTypes.h"
#include "DaycareData.h"

// ── Achievement checker — call after every event ────────────────────────────
// Returns the number of NEW achievements granted this tick.
// Fills newAchs[] with up to maxNew newly granted achievement IDs.

inline uint8_t checkAchievements(DaycareState &state,
                                  uint8_t neighborCount,
                                  DaycareAchievement *newAchs,
                                  uint8_t maxNew) {
    uint8_t count = 0;
    auto grant = [&](DaycareAchievement ach) {
        if (count < maxNew && grantAchievement(state, ach)) {
            newAchs[count++] = ach;
        }
    };

    // ── Time achievements ───────────────────────────────────────────────────
    for (uint8_t i = 0; i < state.partyCount; i++) {
        uint32_t h = state.pokemon[i].totalHours;
        if (h >= 24)    grant(ACH_FIRST_DAY);
        if (h >= 100)   grant(ACH_SETTLED_IN);
        if (h >= 1000)  grant(ACH_DAYCARE_VETERAN);
        if (h >= 10000) grant(ACH_LIFETIME_RESIDENT);
        if (h >= 50000) grant(ACH_OLD_TIMER);
    }

    // ── Friendship achievements ─────────────────────────────────────────────
    uint8_t friendsPlusCount = 0;
    uint8_t bestFriendsCount = 0;
    for (uint8_t i = 0; i < state.relationshipCount; i++) {
        auto &r = state.relationships[i];
        if (r.friendship >= FRIEND_TIER_ACQUAINTANCE) {
            grant(ACH_FIRST_FRIEND);
        }
        if (r.friendship >= FRIEND_TIER_FRIEND) {
            friendsPlusCount++;
            grant(ACH_GOOD_FRIENDS);
        }
        if (r.friendship >= FRIEND_TIER_BEST_FRIEND) {
            bestFriendsCount++;
            grant(ACH_BEST_FRIENDS_FOREVER);
        }
        // Friendly Rival: max friendship AND max rivalry
        if (r.friendship >= FRIEND_TIER_BEST_FRIEND && r.rivalry >= RIVAL_TIER_ARCH) {
            grant(ACH_FRIENDLY_RIVAL);
        }
        // Respect: rivalry -> best friends (rivalry exists AND best friends)
        if (r.rivalry >= RIVAL_TIER_RIVAL && r.friendship >= FRIEND_TIER_BEST_FRIEND) {
            grant(ACH_RESPECT);
        }
    }
    if (friendsPlusCount >= 10) grant(ACH_SOCIAL_BUTTERFLY);
    if (bestFriendsCount >= 5)  grant(ACH_BELOVED);

    // ── Rivalry achievements ────────────────────────────────────────────────
    uint8_t activeRivalries = 0;
    for (uint8_t i = 0; i < state.relationshipCount; i++) {
        auto &r = state.relationships[i];
        if (r.rivalry >= RIVAL_TIER_COMPETITOR) {
            grant(ACH_FIRST_RIVAL);
            activeRivalries++;
        }
        if (r.rivalry >= RIVAL_TIER_FIERCE) grant(ACH_HEATED_RIVALRY);
        if (r.rivalry >= RIVAL_TIER_ARCH)   grant(ACH_ARCH_NEMESIS);
    }
    if (activeRivalries >= 5) grant(ACH_FIGHT_CLUB);
    if (state.totalSparWins >= 50) grant(ACH_UNSTOPPABLE_FORCE);

    // ── XP / Level achievements ─────────────────────────────────────────────
    for (uint8_t i = 0; i < state.partyCount; i++) {
        if (state.pokemon[i].totalLevelsGained >= 1)  grant(ACH_FIRST_LEVEL_UP);
        if (state.pokemon[i].totalLevelsGained >= 10) grant(ACH_DAYCARE_GRADUATE);
        if (state.pokemon[i].totalLevelsGained >= 25) grant(ACH_DAYCARE_SCHOLAR);
    }

    // ── Event achievements ──────────────────────────────────────────────────
    for (uint8_t i = 0; i < state.partyCount; i++) {
        if (state.pokemon[i].exploreCount >= 50) grant(ACH_EXPLORER);
        if (state.pokemon[i].dreamCount >= 20)   grant(ACH_DREAMER);
        if (state.pokemon[i].escapeCount >= 1)   grant(ACH_ESCAPE_ARTIST);
        // Splash Champion: Magikarp only (dex 129)
        if (state.pokemon[i].speciesDex == 129 && state.pokemon[i].splashCount >= 100) {
            grant(ACH_SPLASH_CHAMPION);
        }
    }

    // The Lonely One: 7+ days missing then reunite (checked when neighbor reappears)
    for (uint8_t i = 0; i < state.relationshipCount; i++) {
        auto &r = state.relationships[i];
        if (r.daysMissing >= 7 && r.friendship >= FRIEND_TIER_FRIEND) {
            // If they're currently visible (lastSeenMs is recent), it's a reunite
            uint32_t now = millis();
            if (now - r.lastSeenMs < 120000) { // seen in last 2 min
                grant(ACH_THE_LONELY_ONE);
                r.daysMissing = 0; // reset
            }
        }
    }

    // ── Weather achievements ────────────────────────────────────────────────
    if (state.thunderstormCount >= 10) grant(ACH_STORM_CHASER);
    if (state.snowCount >= 10)         grant(ACH_SNOWBOUND);
    if (state.fogCount >= 10) {
        // Check if any Ghost-type in party
        for (uint8_t i = 0; i < state.partyCount; i++) {
            const DaycareSpecies *sp = daycareGetSpecies(state.pokemon[i].speciesDex);
            if (sp && (sp->type1 == TYPE_GHOST || sp->type2 == TYPE_GHOST)) {
                grant(ACH_FOG_WALKER);
                break;
            }
        }
    }
    if (state.lightningAbsorbs >= 5) {
        // Check if any Electric-type in party
        for (uint8_t i = 0; i < state.partyCount; i++) {
            const DaycareSpecies *sp = daycareGetSpecies(state.pokemon[i].speciesDex);
            if (sp && (sp->type1 == TYPE_ELECTRIC || sp->type2 == TYPE_ELECTRIC)) {
                grant(ACH_LIGHTNING_ROD);
                break;
            }
        }
    }
    // Weathered: all 7 non-NONE weather types seen (bits 1-7)
    if ((state.weatherTypesSeen & 0xFE) == 0xFE) {
        grant(ACH_WEATHERED);
    }

    // ── Mesh achievements ───────────────────────────────────────────────────
    if (state.uniqueTrainersGreeted >= 5)  grant(ACH_WELCOME_COMMITTEE);
    if (state.uniqueTrainersGreeted >= 25) grant(ACH_MESH_ELDER);
    if (neighborCount >= 5)  grant(ACH_NEIGHBORHOOD);
    if (neighborCount >= 10) grant(ACH_COMMUNITY);

    return count;
}
