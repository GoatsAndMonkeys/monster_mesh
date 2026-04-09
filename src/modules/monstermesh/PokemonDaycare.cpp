// ── Pokemon Daycare — Main Orchestrator ─────────────────────────────────────
// NOT wired into build yet — standalone for validation.

#include "PokemonDaycare.h"
#include "DaycareData.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

// ── Timing constants ────────────────────────────────────────────────────────

static constexpr uint32_t EVENT_INTERVAL_MS     = 300000;    // 5 min (testing)
static constexpr uint32_t BEACON_INTERVAL_MS    = 300000;    // 5 min (testing)
static constexpr uint32_t NEIGHBOR_TIMEOUT_MS   = 7200000;   // 2 hours = neighbor gone (generous for LoRa)
static constexpr uint32_t DECAY_INTERVAL_MS     = 86400000;  // 1 day
static constexpr uint32_t SAVE_INTERVAL_MS      = 300000;    // 5 min autosave
static constexpr uint32_t MOOD_UPDATE_MS        = 60000;     // 1 min mood check

static constexpr uint16_t DAILY_XP_CAP          = 500;
static constexpr uint8_t  XP_PER_LEVEL          = 100;       // simplified level calc

// ── Init ────────────────────────────────────────────────────────────────────

void PokemonDaycare::init() {
    memset(&state_, 0, sizeof(state_));
    state_.magic = DaycareState::MAGIC;
    active_ = false;
    neighborCount_ = 0;
}

// ── Check in from SRAM ─────────────────────────────────────────────────────

void PokemonDaycare::checkIn(const uint8_t *sram,
                              const char *shortName, const char *gameName) {
    // Try to load saved daycare state first
    if (!loadState() || state_.magic != DaycareState::MAGIC) {
        init();
    }

    // Read party directly from the Game Boy SRAM
    DaycarePartyInfo party[6];
    uint8_t count = DaycareSavPatcher::readParty(sram, party);

    state_.partyCount = count;
    for (uint8_t i = 0; i < count; i++) {
        // Only reset species-specific data if the party changed
        if (state_.pokemon[i].speciesDex != party[i].dexNum) {
            state_.pokemon[i] = {};
            state_.pokemon[i].speciesDex = party[i].dexNum;
        }
        // Store SAV-file level and EXP (the real values)
        state_.pokemon[i].savLevel = party[i].level;
        state_.pokemon[i].savExp = party[i].totalExp;

        // Copy nickname from SAV (already decoded to ASCII)
        strncpy(state_.pokemon[i].nickname, party[i].nickname, 10);
        state_.pokemon[i].nickname[10] = '\0';

        state_.pokemon[i].mood = MOOD_CONTENT;
    }

    strncpy(shortName_, shortName, 4);
    shortName_[4] = '\0';
    strncpy(gameName_, gameName, 7);
    gameName_[7] = '\0';

    active_ = true;
    state_.lastEventMs = millis();
    state_.lastBeaconMs = 0;
}

// ── Legacy check-in for tests (no SRAM) ────────────────────────────────────

void PokemonDaycare::checkIn(const uint8_t *partySpeciesDex,
                              const uint8_t *partyLevels,
                              const char nicknames[][11],
                              uint8_t count,
                              const char *shortName,
                              const char *gameName) {
    if (count > 6) count = 6;

    if (!loadState() || state_.magic != DaycareState::MAGIC) {
        init();
    }

    state_.partyCount = count;
    for (uint8_t i = 0; i < count; i++) {
        if (state_.pokemon[i].speciesDex != partySpeciesDex[i]) {
            state_.pokemon[i] = {};
            state_.pokemon[i].speciesDex = partySpeciesDex[i];
        }
        state_.pokemon[i].savLevel = partyLevels ? partyLevels[i] : 0;
        state_.pokemon[i].savExp = partyLevels
            ? expForLevel(partySpeciesDex[i], partyLevels[i]) : 0;

        if (nicknames) {
            strncpy(state_.pokemon[i].nickname, nicknames[i], 10);
            state_.pokemon[i].nickname[10] = '\0';
        } else {
            state_.pokemon[i].nickname[0] = '\0';
        }
        state_.pokemon[i].mood = MOOD_CONTENT;
    }

    strncpy(shortName_, shortName, 4);
    shortName_[4] = '\0';
    strncpy(gameName_, gameName, 7);
    gameName_[7] = '\0';

    active_ = true;
    state_.lastEventMs = millis();
    state_.lastBeaconMs = 0;
}

// ── Check out — write XP back to SRAM ──────────────────────────────────────

void PokemonDaycare::checkOut(uint8_t *sram) {
    active_ = false;

    if (sram) {
        // Collect XP gained and dex numbers for the patcher
        uint8_t dexNums[6] = {};
        uint32_t xpGained[6] = {};
        for (uint8_t i = 0; i < state_.partyCount; i++) {
            dexNums[i] = state_.pokemon[i].speciesDex;
            xpGained[i] = state_.pokemon[i].totalXpGained;
        }

        // Patch SRAM: add XP, update levels, recalc stats, fix checksum
        DaycareSavPatcher::checkout(sram, dexNums, xpGained, state_.partyCount);
    }

    saveState();
}

// ── Main tick ───────────────────────────────────────────────────────────────

void PokemonDaycare::tick(uint32_t nowMs) {
    if (!active_ || state_.partyCount == 0) return;

    // Broadcast beacon
    if (nowMs - state_.lastBeaconMs >= BEACON_INTERVAL_MS) {
        broadcastBeacon(nowMs);
        state_.lastBeaconMs = nowMs;
    }

    // Mood update (every minute)
    static uint32_t lastMoodMs = 0;
    if (nowMs - lastMoodMs >= MOOD_UPDATE_MS) {
        updateMood(nowMs);
        lastMoodMs = nowMs;
    }

    // Friendship decay (every 24h)
    if (nowMs - state_.lastFriendshipDecayMs >= DECAY_INTERVAL_MS) {
        decayFriendships(nowMs);
        state_.lastFriendshipDecayMs = nowMs;
    }

    // Hourly event — run BEFORE expiring neighbors so current neighbors participate
    if (nowMs - state_.lastEventMs >= EVENT_INTERVAL_MS) {
        runEventCycle(nowMs);
        state_.lastEventMs = nowMs;
    }

    // Expire stale neighbors (after events, so they get one last interaction)
    expireNeighbors(nowMs);

    // Autosave
    static uint32_t lastSaveMs = 0;
    if (nowMs - lastSaveMs >= SAVE_INTERVAL_MS) {
        saveState();
        lastSaveMs = nowMs;
    }
}

// ── Event cycle ─────────────────────────────────────────────────────────────

void PokemonDaycare::runEventCycle(uint32_t nowMs) {
    // Increment hours for all party Pokemon
    for (uint8_t i = 0; i < state_.partyCount; i++) {
        state_.pokemon[i].totalHours++;
    }

    // Generate event
    DaycareEvent evt = DaycareEventGen::generate(
        state_.pokemon, state_.partyCount,
        neighbors_, neighborCount_,
        state_,
        weather_.type,
        isNight(),
        lastNewNodeId_
    );
    lastNewNodeId_ = 0;  // consumed

    // Apply XP — uses real Gen 1 EXP curve from the SAV file
    if (evt.xp > 0 && evt.targetSpeciesIdx < state_.partyCount) {
        auto &pkmn = state_.pokemon[evt.targetSpeciesIdx];

        uint16_t xpToAdd = evt.xp;
        if (xpToAdd > 200) xpToAdd = 200;

        // Cap at level 100's total EXP
        uint32_t maxExp = expForLevel(pkmn.speciesDex, 100);
        uint32_t currentTotal = pkmn.savExp + pkmn.totalXpGained;
        if (currentTotal + xpToAdd > maxExp) {
            xpToAdd = (currentTotal >= maxExp) ? 0 : (uint16_t)(maxExp - currentTotal);
        }

        pkmn.totalXpGained += xpToAdd;

        // Calculate effective level from real EXP curve
        uint8_t oldLevel = pkmn.savLevel + pkmn.totalLevelsGained;
        uint8_t newLevel = levelForExp(pkmn.speciesDex, pkmn.savExp + pkmn.totalXpGained);
        if (newLevel > 100) newLevel = 100;
        if (newLevel > oldLevel) {
            pkmn.totalLevelsGained = newLevel - pkmn.savLevel;
        }
    }

    // Update per-pokemon event counters
    updateEventCounters(evt, evt.targetSpeciesIdx);

    // Increment total events
    state_.totalEvents++;

    // Check achievements
    DaycareAchievement newAchs[4];
    uint8_t newCount = checkAchievements(state_, neighborCount_, newAchs, 4);

    // Store last event for display
    lastEvent_ = evt;
    lastEventTimeMs_ = nowMs;

    // Note: DM sending for events is handled by MonsterMeshModule after tick()
    // to avoid double-sends and packet allocation issues

    // Broadcast achievement announcements
    for (uint8_t i = 0; i < newCount; i++) {
        if (achievementDefs[newAchs[i]].broadcast && broadcast_) {
            char achMsg[200];
            char tag[14];
            if (gameName_[0]) snprintf(tag, sizeof(tag), "%s-%s", shortName_, gameName_);
            else snprintf(tag, sizeof(tag), "%s", shortName_);
            snprintf(achMsg, sizeof(achMsg), "\xE2\x9C\xA8 %s earned \"%s\"! %s",
                     tag,
                     achievementDefs[newAchs[i]].name,
                     achievementDefs[newAchs[i]].description);
            broadcast_(achMsg, broadcastCtx_);
        }
    }
}

// ── Update per-pokemon event counters ───────────────────────────────────────

void PokemonDaycare::updateEventCounters(const DaycareEvent &evt, uint8_t targetIdx) {
    if (targetIdx >= state_.partyCount) return;
    auto &pkmn = state_.pokemon[targetIdx];

    // Parse event category from the message content (simple heuristic)
    // The event generator doesn't tag category explicitly, so we check keywords
    if (strstr(evt.message, "dream") || strstr(evt.message, "Dream") ||
        strstr(evt.message, "slept") || strstr(evt.message, "sleep")) {
        pkmn.dreamCount++;
    }
    if (strstr(evt.message, "discover") || strstr(evt.message, "explore") ||
        strstr(evt.message, "found") || strstr(evt.message, "trail") ||
        strstr(evt.message, "dug up") || strstr(evt.message, "investigated")) {
        pkmn.exploreCount++;
    }
    if (strstr(evt.message, "escape") || strstr(evt.message, "Escape") ||
        strstr(evt.message, "broke free") || strstr(evt.message, "ran off")) {
        pkmn.escapeCount++;
    }
    if (pkmn.speciesDex == 129 && strstr(evt.message, "Splash")) {
        pkmn.splashCount++;
    }
}

// ── Mood system ─────────────────────────────────────────────────────────────

void PokemonDaycare::updateMood(uint32_t nowMs) {
    for (uint8_t i = 0; i < state_.partyCount; i++) {
        auto &pkmn = state_.pokemon[i];

        if (neighborCount_ > 0) {
            // Neighbors present
            if (pkmn.mood == MOOD_LONELY) {
                pkmn.mood = MOOD_EXCITED;  // reunited!
            } else if (pkmn.mood != MOOD_EXCITED) {
                pkmn.mood = MOOD_HAPPY;
            }
            // Excited fades to happy after ~5 min
            if (pkmn.mood == MOOD_EXCITED) {
                // Simple: excited only lasts a few ticks
                static uint8_t excitedTicks = 0;
                excitedTicks++;
                if (excitedTicks >= 5) {
                    pkmn.mood = MOOD_HAPPY;
                    excitedTicks = 0;
                }
            }
        } else {
            // Solo
            if (pkmn.mood == MOOD_HAPPY || pkmn.mood == MOOD_EXCITED) {
                // Just lost neighbors — brief lonely
                pkmn.mood = MOOD_LONELY;
            } else if (pkmn.mood == MOOD_LONELY) {
                // Lonely recovers to content after ~10 min
                static uint8_t lonelyTicks = 0;
                lonelyTicks++;
                if (lonelyTicks >= 10) {
                    pkmn.mood = MOOD_CONTENT;
                    lonelyTicks = 0;
                }
            }
            // MOOD_CONTENT stays content — solo is fine
        }
    }
}

// ── Friendship / rivalry decay ──────────────────────────────────────────────

void PokemonDaycare::decayFriendships(uint32_t nowMs) {
    for (uint8_t i = 0; i < state_.relationshipCount; i++) {
        auto &r = state_.relationships[i];

        // Check if this neighbor is currently absent
        bool present = false;
        for (uint8_t n = 0; n < neighborCount_; n++) {
            if (neighbors_[n].nodeId == r.nodeId) {
                present = true;
                break;
            }
        }

        if (!present) {
            // Friendship: -1 per day offline
            if (r.friendship > 0) r.friendship--;

            // Rivalry: -1 per 2 days (slower decay)
            r.daysMissing++;
            if (r.daysMissing % 2 == 0 && r.rivalry > 0) {
                r.rivalry--;
            }
        } else {
            r.daysMissing = 0;
            r.lastSeenMs = nowMs;
        }
    }
}

// ── Neighbor management ─────────────────────────────────────────────────────

void PokemonDaycare::handleBeacon(const DaycareBeacon &beacon) {
    // Check if this is a new node
    bool isNew = !isKnownNode(state_, beacon.nodeId);
    if (isNew) {
        addKnownNode(state_, beacon.nodeId);
        lastNewNodeId_ = beacon.nodeId;
    }

    // Update or add neighbor
    int8_t slot = -1;
    for (uint8_t i = 0; i < neighborCount_; i++) {
        if (neighbors_[i].nodeId == beacon.nodeId) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        if (neighborCount_ < MAX_NEIGHBORS) {
            slot = neighborCount_++;
        } else {
            // Evict oldest
            uint32_t oldest = UINT32_MAX;
            slot = 0;
            for (uint8_t i = 0; i < MAX_NEIGHBORS; i++) {
                if (neighborLastSeen_[i] < oldest) {
                    oldest = neighborLastSeen_[i];
                    slot = i;
                }
            }
        }
    }

    neighbors_[slot].nodeId = beacon.nodeId;
    strncpy(neighbors_[slot].shortName, beacon.shortName, 4);
    neighbors_[slot].shortName[4] = '\0';
    strncpy(neighbors_[slot].gameName, beacon.gameName, 7);
    neighbors_[slot].gameName[7] = '\0';
    // Use first Pokemon from beacon party
    if (beacon.partyCount > 0) {
        neighbors_[slot].speciesDex = beacon.pokemon[0].species;
        neighbors_[slot].level = beacon.pokemon[0].level;
        strncpy(neighbors_[slot].nickname, beacon.pokemon[0].nickname, 10);
        neighbors_[slot].nickname[10] = '\0';
    }
    neighborLastSeen_[slot] = millis();
}

void PokemonDaycare::expireNeighbors(uint32_t nowMs) {
    for (uint8_t i = 0; i < neighborCount_;) {
        if (nowMs - neighborLastSeen_[i] > NEIGHBOR_TIMEOUT_MS) {
            // Shift down
            for (uint8_t j = i; j < neighborCount_ - 1; j++) {
                neighbors_[j] = neighbors_[j + 1];
                neighborLastSeen_[j] = neighborLastSeen_[j + 1];
            }
            neighborCount_--;
        } else {
            i++;
        }
    }
}

// ── Beacon broadcast ────────────────────────────────────────────────────────

void PokemonDaycare::broadcastBeacon(uint32_t nowMs) {
    if (!sendBeacon_) return;

    DaycareBeacon beacon = {};
    beacon.type = 0x60;  // DAYCARE_BEACON packet type (0x10 conflicts with BATTLE_REQUEST)
    beacon.nodeId = 0;   // filled by send callback (from nodeDB)
    strncpy(beacon.shortName, shortName_, 4);
    strncpy(beacon.gameName, gameName_, 7);
    beacon.partyCount = state_.partyCount;
    for (uint8_t i = 0; i < state_.partyCount && i < 6; i++) {
        beacon.pokemon[i].species = state_.pokemon[i].speciesDex;
        beacon.pokemon[i].level = state_.pokemon[i].savLevel + state_.pokemon[i].totalLevelsGained;
        strncpy(beacon.pokemon[i].nickname, state_.pokemon[i].nickname, 10);
    }

    sendBeacon_(beacon, sendBeaconCtx_);
}

// ── Dog park arrival event ──────────────────────────────────────────────────

bool PokemonDaycare::triggerArrivalEvent(const DaycareBeacon &newcomer) {
    if (!active_ || state_.partyCount == 0) return false;
    if (newcomer.partyCount == 0) return false;

    DaycareEvent evt = {};
    DaycareEventGen::generateArrivalEvent(
        evt, state_.pokemon, state_.partyCount, newcomer, state_);

    // Apply XP
    if (evt.xp > 0 && evt.targetSpeciesIdx < state_.partyCount) {
        auto &pkmn = state_.pokemon[evt.targetSpeciesIdx];
        uint16_t xpToAdd = evt.xp;
        if (xpToAdd > 200) xpToAdd = 200;
        pkmn.totalXpGained += xpToAdd;

        uint8_t newLevel = levelForExp(pkmn.speciesDex, pkmn.savExp + pkmn.totalXpGained);
        if (newLevel > 100) newLevel = 100;
        if (newLevel > pkmn.savLevel + pkmn.totalLevelsGained) {
            pkmn.totalLevelsGained = newLevel - pkmn.savLevel;
        }
    }

    // Store as last event
    lastEvent_ = evt;
    lastEventTimeMs_ = millis();
    state_.totalEvents++;

    return true;
}

// ── Weather ─────────────────────────────────────────────────────────────────

void PokemonDaycare::setWeather(DaycareWeatherType type, int8_t tempC, uint8_t windMps) {
    weather_.type = type;
    weather_.tempC = tempC;
    weather_.windSpeedMps = windMps;
    weather_.lastFetchMs = millis();

    updateWeatherCounters(type);
}

void PokemonDaycare::updateWeatherCounters(DaycareWeatherType type) {
    if (type == WEATHER_NONE) return;

    // Track weather types seen (for Weathered achievement)
    state_.weatherTypesSeen |= (1 << type);

    switch (type) {
        case WEATHER_THUNDERSTORM:
            state_.thunderstormCount++;
            // Lightning Rod: electric types absorb
            for (uint8_t i = 0; i < state_.partyCount; i++) {
                const DaycareSpecies *sp = daycareGetSpecies(state_.pokemon[i].speciesDex);
                if (sp && (sp->type1 == TYPE_ELECTRIC || sp->type2 == TYPE_ELECTRIC)) {
                    state_.lightningAbsorbs++;
                }
            }
            break;
        case WEATHER_SNOW:
        case WEATHER_COLD:
            state_.snowCount++;
            break;
        case WEATHER_FOG:
            state_.fogCount++;
            break;
        default:
            break;
    }
}

// ── Location setter ─────────────────────────────────────────────────────────

void PokemonDaycare::setLocation(float latDeg, uint8_t hourOfDay, uint16_t dayOfYear) {
    latDeg_ = latDeg;
    hourOfDay_ = hourOfDay;
    dayOfYear_ = dayOfYear;
    hasLocation_ = true;
}

// ── Night check (sunrise/sunset based) ─────────────────────────────────────
// Uses simplified NOAA solar calculation. Accurate to ~15 min for most latitudes.
// Only needs latitude + day-of-year + current hour.

bool PokemonDaycare::isNight() const {
    if (!hasLocation_) {
        // No GPS — fallback to fixed 10pm-6am
        return (hourOfDay_ >= 22 || hourOfDay_ < 6);
    }

    // Solar declination (radians) — simplified equation of time
    float dayAngle = 2.0f * 3.14159f * (dayOfYear_ - 1) / 365.0f;
    float declination = 0.006918f - 0.399912f * cosf(dayAngle) + 0.070257f * sinf(dayAngle)
                      - 0.006758f * cosf(2 * dayAngle) + 0.000907f * sinf(2 * dayAngle);

    // Hour angle at sunrise/sunset (cos(ha) = -tan(lat)*tan(decl))
    float latRad = latDeg_ * 3.14159f / 180.0f;
    float cosHa = -tanf(latRad) * tanf(declination);

    // Clamp for polar regions (midnight sun / polar night)
    if (cosHa < -1.0f) return false;   // midnight sun — never night
    if (cosHa > 1.0f)  return true;    // polar night — always night

    float haRad = acosf(cosHa);
    float haDeg = haRad * 180.0f / 3.14159f;

    // Convert hour angle to sunrise/sunset hours (solar noon = 12:00)
    float sunriseHour = 12.0f - haDeg / 15.0f;
    float sunsetHour  = 12.0f + haDeg / 15.0f;

    // Night = before sunrise or after sunset
    float h = (float)hourOfDay_;
    return (h < sunriseHour || h >= sunsetHour);
}

// ── Save / Load (LittleFS stubs) ────────────────────────────────────────────
// Real implementation will use FSCom.open("/daycare/state.dat", FILE_O_WRITE)
// For now, stubs that return true.

bool PokemonDaycare::saveState() {
    // TODO: FSCom.open("/daycare/state.dat", FILE_O_WRITE)
    //       f.write((uint8_t*)&state_, sizeof(state_))
    //       f.close()
    return true;
}

bool PokemonDaycare::loadState() {
    // TODO: FSCom.open("/daycare/state.dat", FILE_O_READ)
    //       f.read((uint8_t*)&state_, sizeof(state_))
    //       f.close()
    //       return state_.magic == DaycareState::MAGIC
    return false;  // no saved state yet
}
