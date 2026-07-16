// ── Pokemon Daycare — Main Orchestrator ─────────────────────────────────────
// NOT wired into build yet — standalone for validation.

#include "PokemonDaycare.h"
#include "AtomicSdFile.h"
#include "DaycareData.h"
#include "FSCommon.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

// ── Timing constants ────────────────────────────────────────────────────────

static constexpr uint32_t EVENT_INTERVAL_MS     = 3600000;   // 1 hour
static constexpr uint32_t BEACON_INTERVAL_MS    = 900000;    // 15 min — light on the airwaves
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
    runtimeStateLoaded_ = false;
    savOwner_ = {};
    savOwnerLoaded_ = false;
    savOwnerLegacy_ = false;
    clearPendingFlush();
    neighborCount_ = 0;
}

void PokemonDaycare::clearPendingFlush() {
    // Avoid materializing a large zeroed aggregate on the ESP32 loopTask
    // stack. PendingFlush is a POD persistence snapshot.
    memset(&pendingFlush_, 0, sizeof(pendingFlush_));
}

bool PokemonDaycare::hasPendingXp() const {
    // Scan every persisted slot, not only the current party count. A failed
    // re-check-in must never hide XP merely because the observed party shrank.
    for (uint8_t i = 0; i < 6; i++) {
        if (state_.pokemon[i].totalXpGained != 0) return true;
    }
    return false;
}

bool PokemonDaycare::eventDue(uint32_t nowMs) const {
    return active_ && state_.partyCount != 0 &&
           nowMs - state_.lastEventMs >= EVENT_INTERVAL_MS;
}

uint8_t PokemonDaycare::effectiveLevel(uint8_t partyIdx) const {
    if (partyIdx >= state_.partyCount || partyIdx >= 6) return 0;
    const auto &pkmn = state_.pokemon[partyIdx];
    uint32_t totalExp = pkmn.savExp;
    if (pkmn.totalXpGained > UINT32_MAX - totalExp) {
        totalExp = UINT32_MAX;
    } else {
        totalExp += pkmn.totalXpGained;
    }
    return levelForExp(pkmn.speciesDex, totalExp);
}

void PokemonDaycare::addPendingXp(uint8_t partyIdx, uint16_t requestedXp) {
    if (partyIdx >= state_.partyCount || partyIdx >= 6 || requestedXp == 0) return;

    auto &pkmn = state_.pokemon[partyIdx];
    if (pkmn.speciesDex == 0 || pkmn.speciesDex > 151) return;

    uint16_t xpToAdd = requestedXp > 200 ? 200 : requestedXp;
    uint8_t oldLevel = effectiveLevel(partyIdx);

    uint32_t maxExp = expForLevel(pkmn.speciesDex, 100);
    uint32_t currentTotal = pkmn.savExp;
    if (pkmn.totalXpGained > UINT32_MAX - currentTotal) {
        currentTotal = UINT32_MAX;
    } else {
        currentTotal += pkmn.totalXpGained;
    }
    if (currentTotal >= maxExp) {
        xpToAdd = 0;
    } else if (xpToAdd > maxExp - currentTotal) {
        xpToAdd = static_cast<uint16_t>(maxExp - currentTotal);
    }

    pkmn.totalXpGained += xpToAdd;
    uint8_t newLevel = effectiveLevel(partyIdx);
    if (newLevel > oldLevel) {
        uint16_t gained = static_cast<uint16_t>(newLevel - oldLevel);
        if (gained > static_cast<uint16_t>(UINT16_MAX - pkmn.totalLevelsGained)) {
            pkmn.totalLevelsGained = UINT16_MAX;
        } else {
            pkmn.totalLevelsGained += gained;
        }
    }
}

// ── Check in from SRAM ─────────────────────────────────────────────────────

__attribute__((noinline)) bool PokemonDaycare::checkIn(
    const uint8_t *sram, const char *shortName, const char *gameName,
    const char *savPath) {
    if (!sram || !savPath) return false;
    // Load persisted counters once per runtime. Subsequent check-ins must keep
    // the current in-memory flush baseline even if the auxiliary LittleFS file
    // was stale or its most recent best-effort write failed.
    if (!runtimeStateLoaded_) {
        if (!loadState() || state_.magic != DaycareState::MAGIC) init();
        runtimeStateLoaded_ = true;
    }
    // Read party directly from the Game Boy SRAM
    DaycarePartyInfo party[6];
    uint8_t count = DaycareSavPatcher::readParty(sram, party);
    if (count == 0 || count > 6 ||
        !ensureSavBinding(savPath, party, count)) {
        active_ = false;
        return false;
    }
    clearPendingFlush();
    if (!reconcileFlushJournal(savOwner_.savPath, party, count)) {
        active_ = false;
        return false;
    }

    state_.partyCount = count;
    for (uint8_t i = 0; i < 6; i++) travelerIdx_[i] = NO_TRAVELER;
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

        // Attach this individual Pokemon to its own Kanto journey. Keyed by
        // identity (species + nickname + OT), so a mon swapped out and back in
        // resumes its trip instead of restarting — even though the slot data
        // above was wiped on a species change.
        attachJourney(i, party[i].dexNum, party[i].nickname, party[i].otName);
    }

    strncpy(shortName_, shortName, 4);
    shortName_[4] = '\0';
    strncpy(gameName_, gameName, 7);
    gameName_[7] = '\0';

    active_ = true;
    state_.lastEventMs = millis();
    state_.lastBeaconMs = 0;
    return true;
}

// ── Legacy check-in for tests (no SRAM) ────────────────────────────────────

__attribute__((noinline)) bool PokemonDaycare::checkIn(
    const uint8_t *partySpeciesDex, const uint8_t *partyLevels,
    const char nicknames[][11], uint8_t count, const char *shortName,
    const char *gameName, const uint32_t *partyExp,
    const uint8_t partyIdentity[][4], const char *savPath) {
    if (!partySpeciesDex || !partyIdentity || !savPath ||
        count == 0 || count > 6) return false;

    if (!runtimeStateLoaded_) {
        if (!loadState() || state_.magic != DaycareState::MAGIC) init();
        runtimeStateLoaded_ = true;
    }
    DaycarePartyInfo observed[6] = {};
    for (uint8_t i = 0; i < count; ++i) {
        observed[i].dexNum = partySpeciesDex[i];
        observed[i].level = partyLevels ? partyLevels[i] : 0;
        observed[i].totalExp = partyExp ? partyExp[i]
            : (partyLevels ? expForLevel(partySpeciesDex[i], partyLevels[i]) : 0);
        memcpy(observed[i].identity, partyIdentity[i],
               sizeof(observed[i].identity));
    }
    if (!ensureSavBinding(savPath, observed, count)) {
        active_ = false;
        return false;
    }
    clearPendingFlush();
    if (!reconcileFlushJournal(savOwner_.savPath, observed, count)) {
        active_ = false;
        return false;
    }

    state_.partyCount = count;
    for (uint8_t i = 0; i < 6; i++) travelerIdx_[i] = NO_TRAVELER;
    for (uint8_t i = 0; i < count; i++) {
        if (state_.pokemon[i].speciesDex != partySpeciesDex[i]) {
            state_.pokemon[i] = {};
            state_.pokemon[i].speciesDex = partySpeciesDex[i];
        }
        state_.pokemon[i].savLevel = partyLevels ? partyLevels[i] : 0;
        state_.pokemon[i].savExp = partyExp ? partyExp[i]
            : (partyLevels ? expForLevel(partySpeciesDex[i], partyLevels[i]) : 0);

        if (nicknames) {
            strncpy(state_.pokemon[i].nickname, nicknames[i], 10);
            state_.pokemon[i].nickname[10] = '\0';
        } else {
            state_.pokemon[i].nickname[0] = '\0';
        }
        state_.pokemon[i].mood = MOOD_CONTENT;

        attachJourney(i, partySpeciesDex[i], state_.pokemon[i].nickname, "");
    }

    strncpy(shortName_, shortName, 4);
    shortName_[4] = '\0';
    strncpy(gameName_, gameName, 7);
    gameName_[7] = '\0';

    active_ = true;
    state_.lastEventMs = millis();
    state_.lastBeaconMs = 0;
    return true;
}

// ── Transactional XP flush / terminal checkout ─────────────────────────────

bool PokemonDaycare::applyPendingXp(const char *savPath, uint8_t *sram) {
    if (!sram || !hasPendingXp() || !isBoundToSav(savPath)) return false;

    uint8_t partyCount = state_.partyCount < 6 ? state_.partyCount : 6;
    DaycarePartyInfo before[6] = {};
    if (DaycareSavPatcher::readParty(sram, before) != partyCount ||
        !partyMatchesSavOwner(before, partyCount)) return false;
    for (uint8_t i = 0; i < partyCount; i++) {
        if (state_.pokemon[i].speciesDex == 0 ||
            state_.pokemon[i].speciesDex != before[i].dexNum) {
            return false;
        }
    }

    // A failed persistence attempt retains its original snapshot. New XP may
    // accrue while background daycare remains active, but it belongs to the
    // next transaction and must not be cleared by this one.
    if (!pendingFlush_.staged) {
        pendingFlush_.partyCount = partyCount;
        for (uint8_t i = 0; i < partyCount; i++) {
            pendingFlush_.xp[i] = state_.pokemon[i].totalXpGained;
            pendingFlush_.species[i] = state_.pokemon[i].speciesDex;
            memcpy(pendingFlush_.identity[i], before[i].identity,
                   sizeof(pendingFlush_.identity[i]));
            pendingFlush_.baseExp[i] = before[i].totalExp;
        }
        memcpy(pendingFlush_.savPath, savOwner_.savPath,
               sizeof(pendingFlush_.savPath));
        pendingFlush_.staged = true;
    }

    pendingFlush_.applied = false;
    if (pendingFlush_.partyCount != partyCount ||
        strcmp(pendingFlush_.savPath, savOwner_.savPath) != 0) return false;

    bool stagedAny = false;
    for (uint8_t i = 0; i < partyCount; i++) {
        if (pendingFlush_.species[i] != state_.pokemon[i].speciesDex ||
            memcmp(pendingFlush_.identity[i], savOwner_.identity[i],
                   sizeof(pendingFlush_.identity[i])) != 0 ||
            pendingFlush_.xp[i] > state_.pokemon[i].totalXpGained) {
            return false;
        }
        stagedAny |= pendingFlush_.xp[i] != 0;
    }
    if (!stagedAny) return false;

    // If a prior post-commit rollback failed, the real path may already expose
    // this exact prepared image.  Recognize it rather than adding the frozen
    // snapshot a second time.  Any other baseline drift is ambiguous and must
    // be rejected for a later check-in/reconciliation.
    bool haveWrittenSnapshot = false;
    bool alreadyWritten = true;
    for (uint8_t i = 0; i < partyCount; ++i) {
        if (pendingFlush_.xp[i] != 0 && pendingFlush_.writtenExp[i] != 0)
            haveWrittenSnapshot = true;
        if (before[i].totalExp != pendingFlush_.writtenExp[i])
            alreadyWritten = false;
    }
    if (haveWrittenSnapshot && alreadyWritten) {
        pendingFlush_.applied = true;
        return saveFlushJournal(FlushJournalPhase::PREPARED) &&
               saveFlushJournal(FlushJournalPhase::PREPARED);
    }
    for (uint8_t i = 0; i < partyCount; ++i) {
        if (before[i].totalExp != pendingFlush_.baseExp[i]) return false;
    }

    if (!DaycareSavPatcher::checkout(sram, pendingFlush_.species,
                                     pendingFlush_.xp, partyCount)) {
        return false;
    }

    // Direct SRAM patching bypasses the game's normal level-up move flow.
    // Fill empty slots, then apply the existing non-interactive weakest-move
    // policy for any remaining moves.
    bool taughtAny = false;
    for (uint8_t i = 0; i < partyCount; i++) {
        if (pendingFlush_.xp[i] == 0) continue;
        uint8_t newLevel = sram[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE
                                + PKM_LEVEL_PARTY];
        DaycareSavPatcher::MoveLearnResult res;
        DaycareSavPatcher::learnMoves(sram, i, pendingFlush_.species[i],
                                      before[i].level, newLevel, res);
        if (res.learnedCount) taughtAny = true;
        for (uint8_t k = 0; k < res.pendingCount; k++) {
            uint8_t slot = DaycareSavPatcher::weakestMoveSlot(sram, i);
            DaycareSavPatcher::setMove(sram, i, slot, res.pending[k]);
            taughtAny = true;
        }
    }
    if (taughtAny) DaycareSavPatcher::fixChecksum(sram);

    DaycarePartyInfo after[6] = {};
    if (DaycareSavPatcher::readParty(sram, after) != partyCount) return false;
    for (uint8_t i = 0; i < partyCount; i++) {
        if (after[i].dexNum != pendingFlush_.species[i] ||
            memcmp(after[i].identity, pendingFlush_.identity[i],
                   sizeof(after[i].identity)) != 0) return false;
        pendingFlush_.writtenExp[i] = after[i].totalExp;
        pendingFlush_.writtenLevel[i] = after[i].level;
    }
    pendingFlush_.applied = true;
    // Persist the counters that the PREPARED journal describes before the
    // caller can promote the SAV.  Without this ordering, a reset after the
    // journal write but before the next periodic daycare autosave could load
    // an older state with no pending XP.  The journal recovery path below is
    // still able to reconstruct the snapshot if this primary later corrupts
    // and LittleFS falls back to its older .bak.
    if (!saveState() || !saveState()) {
        pendingFlush_.applied = false;
        return false;
    }
    // Persist the exact prepared snapshot before the caller can promote the
    // patched SAV.  This journal closes the reset window between SD rename and
    // daycare.dat commit and is idempotently reconciled on the next check-in.
    if (!saveFlushJournal(FlushJournalPhase::PREPARED) ||
        !saveFlushJournal(FlushJournalPhase::PREPARED)) {
        pendingFlush_.applied = false;
        return false;
    }
    return true;
}

bool PokemonDaycare::commitXpFlush(const char *savPath,
                                   const uint8_t *writtenSram) {
    if (!writtenSram || !pendingFlush_.staged || !pendingFlush_.applied ||
        !isBoundToSav(savPath) ||
        strcmp(pendingFlush_.savPath, savOwner_.savPath) != 0) return false;

    uint8_t partyCount = state_.partyCount < 6 ? state_.partyCount : 6;
    if (pendingFlush_.partyCount != partyCount) return false;

    DaycarePartyInfo written[6] = {};
    if (DaycareSavPatcher::readParty(writtenSram, written) != partyCount ||
        !partyMatchesSavOwner(written, partyCount)) return false;

    // Validate the whole checked-in party before mutating any daycare state.
    // This makes commit all-or-nothing even if the wrong buffer is supplied.
    for (uint8_t i = 0; i < partyCount; i++) {
        if (state_.pokemon[i].speciesDex != pendingFlush_.species[i] ||
            state_.pokemon[i].totalXpGained < pendingFlush_.xp[i] ||
            written[i].dexNum != pendingFlush_.species[i] ||
            memcmp(written[i].identity, pendingFlush_.identity[i],
                   sizeof(written[i].identity)) != 0 ||
            written[i].totalExp != pendingFlush_.writtenExp[i] ||
            written[i].level != pendingFlush_.writtenLevel[i]) {
            return false;
        }
    }

    // Once the caller has verified the promoted SD image, mark that fact
    // durably before changing in-memory counters.  A reboot can then finish
    // this exact commit without guessing from unrelated gameplay EXP.
    if (!saveFlushJournal(FlushJournalPhase::PROMOTED)) return false;

    // Only these three per-party fields change below. Keeping a compact
    // rollback snapshot avoids placing the 1096-byte DaycareState on the
    // small ESP32 loopTask stack.
    struct PartyProgress {
        uint32_t savExp;
        uint32_t totalXpGained;
        uint8_t savLevel;
    } previous[6] = {};
    for (uint8_t i = 0; i < partyCount; ++i) {
        previous[i].savExp = state_.pokemon[i].savExp;
        previous[i].totalXpGained = state_.pokemon[i].totalXpGained;
        previous[i].savLevel = state_.pokemon[i].savLevel;
    }

    for (uint8_t i = 0; i < partyCount; i++) {
        auto &pkmn = state_.pokemon[i];
        pkmn.savExp = written[i].totalExp;
        pkmn.savLevel = written[i].level;
        pkmn.totalXpGained -= pendingFlush_.xp[i];
    }

    // daycare.dat is part of the logical transaction.  If its verified atomic
    // update fails, restore the old in-memory state and leave the staged XP
    // eligible; the caller will roll the SD file back to its retained .bak.
    if (!saveState()) {
        for (uint8_t i = 0; i < partyCount; ++i) {
            state_.pokemon[i].savExp = previous[i].savExp;
            state_.pokemon[i].totalXpGained = previous[i].totalXpGained;
            state_.pokemon[i].savLevel = previous[i].savLevel;
        }
        (void)saveFlushJournal(FlushJournalPhase::PREPARED);
        return false;
    }

    clearPendingFlush();

    // atomicWriteFile intentionally retains the previous state as .bak.  The
    // first write above therefore leaves a pre-commit backup that still
    // contains the staged XP. Rotate the identical committed state once more
    // so both primary and backup are post-commit before deleting the only
    // reconciliation evidence. If this redundancy write fails, the primary
    // commit is still verified; retain the PROMOTED journal so a later
    // check-in can safely repair either copy.
    if (saveState()) clearFlushJournal();
    return true;
}

bool PokemonDaycare::refreshSavBaseline(const char *savPath,
                                        const DaycarePartyInfo *party,
                                        uint8_t count) {
    if (!party || count == 0 || count > 6 || count != state_.partyCount ||
        pendingFlush_.staged || !isBoundToSav(savPath) ||
        !partyMatchesSavOwner(party, count)) {
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (party[i].dexNum == 0 ||
            party[i].dexNum != state_.pokemon[i].speciesDex) {
            return false;
        }
    }

    for (uint8_t i = 0; i < count; ++i) {
        state_.pokemon[i].savExp = party[i].totalExp;
        state_.pokemon[i].savLevel = party[i].level;
    }

    // Refresh both the primary and retained backup. Even if the second
    // rotation fails, the in-memory baseline and verified primary remain
    // usable; the next check-in also derives this baseline from the SAV.
    if (!saveState()) return false;
    return saveState();
}

void PokemonDaycare::checkOut(uint8_t *sram) {
    if (sram && savOwnerLoaded_)
        (void)applyPendingXp(savOwner_.savPath, sram);
    active_ = false;
    (void)saveState();
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
    // Increment hours for all party Pokemon, and advance each one's personal
    // Kanto journey a base step. Every boarding mon keeps walking its own trip.
    static constexpr uint8_t KANTO_BASE_STEP = 10;   // progress pts / cycle
    for (uint8_t i = 0; i < state_.partyCount; i++) {
        state_.pokemon[i].totalHours++;
        if (travelerIdx_[i] != NO_TRAVELER)
            journeys_.advance(travelerIdx_[i], KANTO_BASE_STEP);
    }

    // Propagate the local trainer identity into the event generator each cycle
    // (restored from the pre-audit current line — the audit reconciliation
    // dropped this call, which left generated events without the player's
    // short/game name). Must run before generate().
    DaycareEventGen::setLocalTrainer(shortName_, gameName_);

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
        addPendingXp(evt.targetSpeciesIdx, evt.xp);
    }

    // The Pokemon featured in this cycle's event travelled the most — give its
    // journey an extra push on top of the base step above.
    static constexpr uint8_t KANTO_EVENT_STEP = 14;
    if (evt.targetSpeciesIdx < state_.partyCount &&
        travelerIdx_[evt.targetSpeciesIdx] != NO_TRAVELER)
        journeys_.advance(travelerIdx_[evt.targetSpeciesIdx], KANTO_EVENT_STEP);

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
    // Store full party from beacon
    uint8_t cnt = beacon.partyCount < 6 ? beacon.partyCount : 6;
    neighbors_[slot].partyCount = cnt;
    for (uint8_t i = 0; i < cnt; ++i) {
        neighbors_[slot].party[i].species = beacon.pokemon[i].species;
        neighbors_[slot].party[i].level   = beacon.pokemon[i].level;
        strncpy(neighbors_[slot].party[i].nickname, beacon.pokemon[i].nickname, 10);
        neighbors_[slot].party[i].nickname[10] = '\0';
    }
    if (cnt > 0) {
        neighbors_[slot].speciesDex = beacon.pokemon[0].species;
        neighbors_[slot].level      = beacon.pokemon[0].level;
        strncpy(neighbors_[slot].nickname, beacon.pokemon[0].nickname, 10);
        neighbors_[slot].nickname[10] = '\0';
    }
    neighbors_[slot].ngPlusTier = beacon.ngPlusTier;
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
        beacon.pokemon[i].level = effectiveLevel(i);
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
        evt, state_.pokemon, state_.partyCount, newcomer, state_,
        shortName_, gameName_);

    // Apply XP
    if (evt.xp > 0 && evt.targetSpeciesIdx < state_.partyCount) {
        addPendingXp(evt.targetSpeciesIdx, evt.xp);
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

// ── Save / Load (LittleFS via FSCom) ────────────────────────────────────────

static constexpr const char *DAYCARE_STATE_PATH = "/monstermesh/daycare.dat";
static constexpr const char *DAYCARE_FLUSH_PATH = "/monstermesh/daycare.flush";
static constexpr const char *DAYCARE_OWNER_PATH = "/monstermesh/daycare.owner";

static bool canonicalizeSavPath(const char *path, char out[256]) {
    if (!path || !out) return false;
    if (strncmp(path, "/sd/", 4) == 0) path += 3;
    if (path[0] != '/') return false;
    size_t len = strnlen(path, 256);
    if (len == 0 || len >= 256) return false;

    const char *segment = path + 1;
    for (const char *p = segment;; ++p) {
        if (*p == '/' || *p == '\0') {
            const size_t segmentLen = static_cast<size_t>(p - segment);
            if (segmentLen == 0 ||
                (segmentLen == 1 && segment[0] == '.') ||
                (segmentLen == 2 && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            if (*p == '\0') break;
            segment = p + 1;
        }
    }
    memcpy(out, path, len + 1);
    return true;
}

template <typename T>
static bool readExactFsObject(const char *path, T &object) {
    auto f = FSCom.open(path, FILE_READ);
    if (!f) return false;
    if (f.size() != sizeof(T)) {
        f.close();
        return false;
    }
    // Callers provide zero-initialized scratch and only inspect it on success.
    // Reading directly avoids a second object (1096 bytes for DaycareState)
    // on the constrained loopTask stack.
    memset(&object, 0, sizeof(object));
    size_t read = f.read(reinterpret_cast<uint8_t *>(&object), sizeof(object));
    f.close();
    return read == sizeof(object);
}

bool PokemonDaycare::saveState() {
    FSCom.mkdir("/monstermesh");
    bool ok = monstermesh::atomic_sd_detail::atomicWriteFile(
        FSCom, DAYCARE_STATE_PATH,
        reinterpret_cast<const uint8_t *>(&state_), sizeof(state_));
    // Persist per-Pokemon Kanto journeys to their own file when changed.
    if (journeys_.dirty()) journeys_.save();
    return ok;
}

bool PokemonDaycare::saveSavOwner() {
    if (!savOwnerLoaded_ || savOwnerLegacy_ ||
        savOwner_.magic != SavOwner::MAGIC ||
        savOwner_.partyCount == 0 || savOwner_.partyCount > 6) return false;
    FSCom.mkdir("/monstermesh");
    return monstermesh::atomic_sd_detail::atomicWriteFile(
        FSCom, DAYCARE_OWNER_PATH,
        reinterpret_cast<const uint8_t *>(&savOwner_), sizeof(savOwner_));
}

__attribute__((noinline)) bool PokemonDaycare::loadSavOwner() {
    (void)monstermesh::atomic_sd_detail::recoverFile(FSCom, DAYCARE_OWNER_PATH);
    auto validOwner = [](const SavOwner &owner) {
        char canonical[256] = {};
        if (owner.magic != SavOwner::MAGIC || owner.partyCount == 0 ||
            owner.partyCount > 6 ||
            !canonicalizeSavPath(owner.savPath, canonical) ||
            strcmp(owner.savPath, canonical) != 0) return false;
        for (uint8_t i = 0; i < owner.partyCount; ++i)
            if (owner.species[i] == 0 || owner.species[i] > 151) return false;
        return true;
    };
    auto validLegacyOwner = [](const LegacySavOwner &owner) {
        char canonical[256] = {};
        return owner.magic == LegacySavOwner::MAGIC &&
               canonicalizeSavPath(owner.savPath, canonical) &&
               strcmp(owner.savPath, canonical) == 0;
    };
    auto readCandidate = [&]() {
        SavOwner candidate = {};
        if (readExactFsObject(DAYCARE_OWNER_PATH, candidate) &&
            validOwner(candidate)) {
            savOwner_ = candidate;
            savOwnerLoaded_ = true;
            savOwnerLegacy_ = false;
            return true;
        }
        LegacySavOwner legacy = {};
        if (readExactFsObject(DAYCARE_OWNER_PATH, legacy) &&
            validLegacyOwner(legacy)) {
            savOwner_ = {};
            memcpy(savOwner_.savPath, legacy.savPath,
                   sizeof(savOwner_.savPath));
            savOwnerLoaded_ = true;
            savOwnerLegacy_ = true;
            return true;
        }
        return false;
    };

    bool valid = readCandidate();
    if (!valid &&
        monstermesh::atomic_sd_detail::restorePreviousFile(
            FSCom, DAYCARE_OWNER_PATH)) {
        valid = readCandidate();
    }
    return valid;
}

void PokemonDaycare::setSavBinding(const char *canonicalPath,
                                   const DaycarePartyInfo *party,
                                   uint8_t count) {
    // SavOwner is POD; avoid a 292-byte aggregate temporary on loopTask.
    memset(&savOwner_, 0, sizeof(savOwner_));
    savOwner_.magic = SavOwner::MAGIC;
    memcpy(savOwner_.savPath, canonicalPath, strlen(canonicalPath) + 1);
    savOwner_.partyCount = count;
    for (uint8_t i = 0; i < count; ++i) {
        savOwner_.species[i] = party[i].dexNum;
        memcpy(savOwner_.identity[i], party[i].identity,
               sizeof(savOwner_.identity[i]));
    }
    savOwnerLoaded_ = true;
    savOwnerLegacy_ = false;
}

bool PokemonDaycare::partyMatchesSavOwner(const DaycarePartyInfo *party,
                                          uint8_t count) const {
    if (!party || !savOwnerLoaded_ || savOwnerLegacy_ ||
        count == 0 || count > 6 || count != savOwner_.partyCount) return false;
    for (uint8_t i = 0; i < count; ++i) {
        if (party[i].dexNum == 0 || party[i].dexNum > 151 ||
            party[i].dexNum != savOwner_.species[i] ||
            memcmp(party[i].identity, savOwner_.identity[i],
                   sizeof(party[i].identity)) != 0) return false;
    }
    return true;
}

__attribute__((noinline)) bool PokemonDaycare::ensureSavBinding(
    const char *savPath, const DaycarePartyInfo *party, uint8_t count) {
    char canonical[256] = {};
    if (!canonicalizeSavPath(savPath, canonical) || !party ||
        count == 0 || count > 6) return false;
    for (uint8_t i = 0; i < count; ++i)
        if (party[i].dexNum == 0 || party[i].dexNum > 151) return false;

    FlushJournal journal = {};
    LegacyFlushJournal legacyJournal = {};
    const bool haveJournal = loadFlushJournal(journal);
    const bool haveLegacyJournal = loadLegacyFlushJournal(legacyJournal);
    char journalBackupPath[monstermesh::ATOMIC_SD_SIBLING_PATH_CAPACITY] = {};
    const bool haveJournalBackupPath =
        monstermesh::atomic_sd_detail::makeSiblingPath(
            DAYCARE_FLUSH_PATH, ".bak", journalBackupPath,
            sizeof(journalBackupPath));
    const bool haveUnknownJournalEvidence =
        !haveJournal && !haveLegacyJournal &&
        (FSCom.exists(DAYCARE_FLUSH_PATH) ||
         (haveJournalBackupPath && FSCom.exists(journalBackupPath)));
    if (haveUnknownJournalEvidence &&
        (hasPendingXp() || pendingFlush_.staged)) return false;
    if (haveUnknownJournalEvidence) clearFlushJournal();
    if (hasPendingXp()) {
        if (state_.partyCount != count) return false;
        for (uint8_t i = 0; i < count; ++i)
            if (state_.pokemon[i].speciesDex != party[i].dexNum) return false;
        for (uint8_t i = count; i < 6; ++i)
            if (state_.pokemon[i].totalXpGained != 0) return false;
    }
    // DCF2 has no OT ID/DV binding. Never infer which same-species Pokemon its
    // pending XP belonged to.
    if (haveLegacyJournal) return false;

    auto journalMatches = [&]() {
        if (!haveJournal || strcmp(journal.savPath, canonical) != 0 ||
            journal.partyCount != count) return false;
        for (uint8_t i = 0; i < count; ++i) {
            if (journal.species[i] != party[i].dexNum ||
                memcmp(journal.identity[i], party[i].identity,
                       sizeof(journal.identity[i])) != 0) return false;
        }
        return true;
    };
    if (haveJournal && !journalMatches()) return false;

    if (!savOwnerLoaded_ && !loadSavOwner()) {
        // A DCF3 journal contains its exact owner and can recover a missing
        // sidecar without guessing. Unjournaled pending XP is deliberately
        // left inactive rather than being attached to whichever ROM loaded.
        if (!haveJournal && (hasPendingXp() || pendingFlush_.staged)) {
            return false;
        }
        setSavBinding(canonical, party, count);
        if (!saveSavOwner() || !saveSavOwner()) {
            savOwnerLoaded_ = false;
            return false;
        }
        return true;
    }

    // DCO1 has only a path. A DCF3 journal can safely supply its missing party
    // identity; otherwise only a state with no pending work may be upgraded.
    if (savOwnerLegacy_) {
        if ((haveJournal && strcmp(savOwner_.savPath, canonical) != 0) ||
            (!haveJournal && (hasPendingXp() || pendingFlush_.staged))) {
            return false;
        }
        setSavBinding(canonical, party, count);
        if (!saveSavOwner() || !saveSavOwner()) {
            savOwnerLoaded_ = false;
            return false;
        }
        return true;
    }

    if (strcmp(savOwner_.savPath, canonical) == 0 &&
        partyMatchesSavOwner(party, count)) return true;

    if (hasPendingXp() || pendingFlush_.staged || haveJournal) {
        // Preserve both the old path and exact Pokemon binding for retry.
        return false;
    }
    // Invalid/unbound stale journals carry no useful evidence once the old
    // owner has no pending XP.
    clearFlushJournal();

    setSavBinding(canonical, party, count);
    if (!saveSavOwner() || !saveSavOwner()) {
        savOwnerLoaded_ = false;
        return false;
    }
    return true;
}

bool PokemonDaycare::isBoundToSav(const char *savPath) const {
    char canonical[256] = {};
    return savOwnerLoaded_ && canonicalizeSavPath(savPath, canonical) &&
           strcmp(savOwner_.savPath, canonical) == 0;
}

__attribute__((noinline)) bool PokemonDaycare::loadState() {
    (void)monstermesh::atomic_sd_detail::recoverFile(FSCom, DAYCARE_STATE_PATH);
    DaycareState candidate = {};
    auto validMagic = [](const DaycareState &s) {
        return s.magic == DaycareState::MAGIC ||
               s.magic == DaycareState::LEGACY_MAGIC ||
               s.magic == DaycareState::LEGACY_MAGIC_V2;
    };
    bool validFile = readExactFsObject(DAYCARE_STATE_PATH, candidate) &&
                     validMagic(candidate);
    if (!validFile &&
        monstermesh::atomic_sd_detail::restorePreviousFile(
            FSCom, DAYCARE_STATE_PATH)) {
        validFile = readExactFsObject(DAYCARE_STATE_PATH, candidate) &&
                    validMagic(candidate);
    }
    if (!validFile) return false;
    state_ = candidate;

    // ── Legacy DACA0002 (audit b58) 8-bit-speciesDex remap ──────────────────
    // b58 stored speciesDex as a single byte, so within each 44-byte per-Pokemon
    // record the nickname and savLevel sit ONE byte earlier than this 16-bit
    // (DACA0003) layout. Because of struct padding, savExp and every field after
    // it land at identical offsets in both layouts — so `state_ = candidate`
    // already loaded those correctly, and only speciesDex/nickname/savLevel need
    // to be re-read from their raw 8-bit positions. (Both layouts are the same
    // total size, which is exactly why the magic word — not a size check — must
    // discriminate them.)
    if (candidate.magic == DaycareState::LEGACY_MAGIC_V2) {
        for (uint8_t i = 0; i < 6; ++i) {
            const uint8_t *rec =
                reinterpret_cast<const uint8_t *>(&candidate.pokemon[i]);
            state_.pokemon[i].speciesDex = rec[0];                 // 8-bit dex
            memcpy(state_.pokemon[i].nickname, rec + 1,
                   sizeof(state_.pokemon[i].nickname));            // nick @ +1
            state_.pokemon[i].savLevel = rec[12];                 // savLevel @ +12
        }
    }

    // Legacy checkout applied totalXpGained to the SAV without clearing it, and
    // the b58 value additionally cannot be proven un-replayed across the
    // 8-bit->16-bit change. It is therefore ambiguous on upgrade and must never
    // be reinterpreted as pending XP. Preserve lifetime achievements/counters,
    // discard only that ambiguous delta, and stamp the transactional format.
    if (state_.magic == DaycareState::LEGACY_MAGIC ||
        state_.magic == DaycareState::LEGACY_MAGIC_V2) {
        for (uint8_t i = 0; i < 6; ++i) state_.pokemon[i].totalXpGained = 0;
        state_.magic = DaycareState::MAGIC;
    }

    bool valid = state_.magic == DaycareState::MAGIC;
    if (valid) {
        // Persisted counters are used as loop bounds throughout daycare. Clamp
        // them at the trust boundary before any caller can observe the state.
        if (state_.partyCount > 6) state_.partyCount = 6;
        if (state_.relationshipCount > MAX_RELATIONSHIPS)
            state_.relationshipCount = MAX_RELATIONSHIPS;
        if (state_.knownNodeCount > MAX_KNOWN_NODES)
            state_.knownNodeCount = MAX_KNOWN_NODES;
        for (uint8_t i = 0; i < 6; ++i)
            state_.pokemon[i].nickname[10] = '\0';
    }
    if (valid) runtimeStateLoaded_ = true;
    return valid;
}

bool PokemonDaycare::saveFlushJournal(FlushJournalPhase phase) {
    if (!pendingFlush_.staged || !savOwnerLoaded_ || savOwnerLegacy_ ||
        pendingFlush_.partyCount != savOwner_.partyCount ||
        strcmp(pendingFlush_.savPath, savOwner_.savPath) != 0) return false;
    for (uint8_t i = 0; i < pendingFlush_.partyCount; ++i) {
        if (pendingFlush_.species[i] != savOwner_.species[i] ||
            memcmp(pendingFlush_.identity[i], savOwner_.identity[i],
                   sizeof(pendingFlush_.identity[i])) != 0) return false;
    }
    FlushJournal journal = {};
    journal.magic = FlushJournal::MAGIC;
    journal.phase = static_cast<uint8_t>(phase);
    journal.partyCount = pendingFlush_.partyCount;
    for (uint8_t i = 0; i < 6; ++i) {
        journal.species[i] = pendingFlush_.species[i];
        memcpy(journal.identity[i], pendingFlush_.identity[i],
               sizeof(journal.identity[i]));
        journal.writtenLevel[i] = pendingFlush_.writtenLevel[i];
        journal.baseExp[i] = pendingFlush_.baseExp[i];
        journal.xp[i] = pendingFlush_.xp[i];
        journal.writtenExp[i] = pendingFlush_.writtenExp[i];
    }
    memcpy(journal.savPath, pendingFlush_.savPath,
           sizeof(journal.savPath));
    FSCom.mkdir("/monstermesh");
    return monstermesh::atomic_sd_detail::atomicWriteFile(
        FSCom, DAYCARE_FLUSH_PATH,
        reinterpret_cast<const uint8_t *>(&journal), sizeof(journal));
}

__attribute__((noinline)) bool PokemonDaycare::loadFlushJournal(
    FlushJournal &journal) const {
    (void)monstermesh::atomic_sd_detail::recoverFile(FSCom, DAYCARE_FLUSH_PATH);
    auto isValid = [](const FlushJournal &j) {
        char canonical[256] = {};
        if (j.magic != FlushJournal::MAGIC ||
            (j.phase != static_cast<uint8_t>(FlushJournalPhase::PREPARED) &&
             j.phase != static_cast<uint8_t>(FlushJournalPhase::PROMOTED)) ||
            j.partyCount == 0 || j.partyCount > 6 ||
            !canonicalizeSavPath(j.savPath, canonical) ||
            strcmp(j.savPath, canonical) != 0) {
            return false;
        }
        bool any = false;
        for (uint8_t i = 0; i < j.partyCount; ++i) {
            if (j.species[i] == 0 || j.species[i] > 151 ||
                j.writtenExp[i] < j.baseExp[i]) return false;
            any |= j.xp[i] != 0;
        }
        return any;
    };
    auto isValidLegacy = [](const LegacyFlushJournal &j) {
        char canonical[256] = {};
        if (j.magic != LegacyFlushJournal::MAGIC ||
            (j.phase != static_cast<uint8_t>(FlushJournalPhase::PREPARED) &&
             j.phase != static_cast<uint8_t>(FlushJournalPhase::PROMOTED)) ||
            j.partyCount == 0 || j.partyCount > 6 ||
            !canonicalizeSavPath(j.savPath, canonical) ||
            strcmp(j.savPath, canonical) != 0) return false;
        bool any = false;
        for (uint8_t i = 0; i < j.partyCount; ++i) {
            if (j.species[i] == 0 || j.species[i] > 151 ||
                j.writtenExp[i] < j.baseExp[i]) return false;
            any |= j.xp[i] != 0;
        }
        return any;
    };

    FlushJournal candidate = {};
    bool valid = readExactFsObject(DAYCARE_FLUSH_PATH, candidate) &&
                 isValid(candidate);
    LegacyFlushJournal legacy = {};
    if (!valid && readExactFsObject(DAYCARE_FLUSH_PATH, legacy) &&
        isValidLegacy(legacy)) {
        // A valid older record is authoritative but cannot be returned through
        // the identity-bearing DCF3 type. Preserve it for the fail-closed path.
        return false;
    }
    if (!valid &&
        monstermesh::atomic_sd_detail::restorePreviousFile(
            FSCom, DAYCARE_FLUSH_PATH)) {
        valid = readExactFsObject(DAYCARE_FLUSH_PATH, candidate) &&
                isValid(candidate);
        if (!valid && readExactFsObject(DAYCARE_FLUSH_PATH, legacy) &&
            isValidLegacy(legacy)) return false;
    }
    if (!valid) return false;
    journal = candidate;
    return true;
}

__attribute__((noinline)) bool PokemonDaycare::loadLegacyFlushJournal(
    LegacyFlushJournal &journal) const {
    (void)monstermesh::atomic_sd_detail::recoverFile(FSCom,
                                                      DAYCARE_FLUSH_PATH);
    auto isValid = [](const LegacyFlushJournal &j) {
        char canonical[256] = {};
        if (j.magic != LegacyFlushJournal::MAGIC ||
            (j.phase != static_cast<uint8_t>(FlushJournalPhase::PREPARED) &&
             j.phase != static_cast<uint8_t>(FlushJournalPhase::PROMOTED)) ||
            j.partyCount == 0 || j.partyCount > 6 ||
            !canonicalizeSavPath(j.savPath, canonical) ||
            strcmp(j.savPath, canonical) != 0) return false;
        bool any = false;
        for (uint8_t i = 0; i < j.partyCount; ++i) {
            if (j.species[i] == 0 || j.species[i] > 151 ||
                j.writtenExp[i] < j.baseExp[i]) return false;
            any |= j.xp[i] != 0;
        }
        return any;
    };
    auto currentFormatPresent = []() {
        FlushJournal current = {};
        return readExactFsObject(DAYCARE_FLUSH_PATH, current) &&
               current.magic == FlushJournal::MAGIC;
    };

    LegacyFlushJournal candidate = {};
    bool valid = readExactFsObject(DAYCARE_FLUSH_PATH, candidate) &&
                 isValid(candidate);
    if (!valid && currentFormatPresent()) return false;
    if (!valid &&
        monstermesh::atomic_sd_detail::restorePreviousFile(
            FSCom, DAYCARE_FLUSH_PATH)) {
        valid = readExactFsObject(DAYCARE_FLUSH_PATH, candidate) &&
                isValid(candidate);
        if (!valid && currentFormatPresent()) return false;
    }
    if (!valid) return false;
    journal = candidate;
    return true;
}

void PokemonDaycare::clearFlushJournal() {
    char tempPath[monstermesh::ATOMIC_SD_SIBLING_PATH_CAPACITY] = {};
    char backupPath[monstermesh::ATOMIC_SD_SIBLING_PATH_CAPACITY] = {};
    if (monstermesh::atomic_sd_detail::makeSiblingPath(
            DAYCARE_FLUSH_PATH, ".tmp", tempPath, sizeof(tempPath))) {
        if (FSCom.exists(tempPath)) (void)FSCom.remove(tempPath);
    }
    if (monstermesh::atomic_sd_detail::makeSiblingPath(
            DAYCARE_FLUSH_PATH, ".bak", backupPath, sizeof(backupPath))) {
        if (FSCom.exists(backupPath)) (void)FSCom.remove(backupPath);
    }
    if (FSCom.exists(DAYCARE_FLUSH_PATH))
        (void)FSCom.remove(DAYCARE_FLUSH_PATH);
}

__attribute__((noinline)) bool PokemonDaycare::reconcileFlushJournal(
    const char *savPath, const DaycarePartyInfo *party, uint8_t count) {
    if (!party || count == 0 || count > 6) return false;
    FlushJournal journal = {};
    if (!loadFlushJournal(journal)) return true;
    if (!isBoundToSav(savPath) ||
        strcmp(journal.savPath, savOwner_.savPath) != 0 ||
        journal.partyCount != count ||
        !partyMatchesSavOwner(party, count)) return false;

    const bool prepared =
        journal.phase == static_cast<uint8_t>(FlushJournalPhase::PREPARED);
    bool savShowsPromotedImage = true;
    bool savShowsBaseImage = prepared;
    for (uint8_t i = 0; i < count; ++i) {
        if (party[i].dexNum != journal.species[i] ||
            state_.pokemon[i].speciesDex != journal.species[i] ||
            memcmp(party[i].identity, journal.identity[i],
                   sizeof(party[i].identity)) != 0) {
            return false;
        }
        if (prepared) {
            // PREPARED can mean either "not yet promoted" or a reset in the
            // narrow SD-promote → journal-promote window. Exact equality is
            // the only unambiguous evidence in that phase.
            if (party[i].totalExp != journal.writtenExp[i])
                savShowsPromotedImage = false;
            if (party[i].totalExp != journal.baseExp[i])
                savShowsBaseImage = false;
        } else if (party[i].totalExp < journal.writtenExp[i]) {
            savShowsPromotedImage = false;
        }
    }

    // Only these fields are changed during reconciliation. A compact rollback
    // snapshot is enough to preserve all-or-nothing behavior and is more than
    // two kilobytes smaller than the former full-state copies.
    struct PartyProgress {
        uint32_t savExp;
        uint32_t totalXpGained;
        uint8_t savLevel;
    } previous[6] = {};
    for (uint8_t i = 0; i < count; ++i) {
        previous[i].savExp = state_.pokemon[i].savExp;
        previous[i].totalXpGained = state_.pokemon[i].totalXpGained;
        previous[i].savLevel = state_.pokemon[i].savLevel;
    }

    // PREPARED + exact base means the SD promotion never happened (or was
    // rolled back), but the state primary may have fallen back to a backup
    // written before these counters were autosaved. Reconstruct at least the
    // frozen journal XP, retain any newer persisted XP, and advance neither
    // the SAV nor the flush baseline.
    if (!savShowsPromotedImage) {
        if (!savShowsBaseImage) return false;

        for (uint8_t i = 0; i < count; ++i) {
            auto &pkmn = state_.pokemon[i];
            if (pkmn.totalXpGained < journal.xp[i])
                pkmn.totalXpGained = journal.xp[i];
            pkmn.savExp = party[i].totalExp;
            pkmn.savLevel = party[i].level;
        }
    } else {
        for (uint8_t i = 0; i < count; ++i) {
            auto &pkmn = state_.pokemon[i];
            if (pkmn.savExp < journal.writtenExp[i]) {
                // A stale state backup may predate the pending-XP autosave. In
                // that case the promoted SAV is the durable proof that the
                // journal snapshot committed, so there is nothing to subtract
                // from the older zero/smaller counter.
                if (pkmn.totalXpGained >= journal.xp[i])
                    pkmn.totalXpGained -= journal.xp[i];
                else
                    pkmn.totalXpGained = 0;
            }
            pkmn.savExp = party[i].totalExp;
            pkmn.savLevel = party[i].level;
        }
    }

    if (!saveState()) {
        for (uint8_t i = 0; i < count; ++i) {
            state_.pokemon[i].savExp = previous[i].savExp;
            state_.pokemon[i].totalXpGained = previous[i].totalXpGained;
            state_.pokemon[i].savLevel = previous[i].savLevel;
        }
        return false;
    }
    // Only clear the journal after the reconciled state has also become the
    // retained backup. A failed second rotation leaves the journal available
    // for another idempotent repair, while the primary remains usable.
    if (saveState()) clearFlushJournal();
    return true;
}
