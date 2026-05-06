// SPDX-License-Identifier: MIT
//
// Legend of Charizard (LORD) — pure-function glue between LordSave, LordGyms,
// and the existing roguelike run loop in MonsterMeshTerminal.

#pragma once

#include <Arduino.h>
#include "LordSave.h"

// Per-run snapshot the terminal fills while Explore mode is active.
// Reset with lordResetRunStats() at run start, passed to lordOnRunEnd() at
// the wipe.
struct LordRunStats {
    uint16_t wavesBeaten;         // fully-cleared waves
    uint16_t xpEarned;            // rough approximation
    uint8_t  highestOppLevel;     // max wild-level faced in this run
};

inline void lordResetRunStats(LordRunStats &r) {
    r.wavesBeaten     = 0;
    r.xpEarned        = 0;
    r.highestOppLevel = 0;
}

// If `nowEpoch >= s.nextResetEpoch`, reset daily counters and advance the
// nextResetEpoch to the next 9am-local boundary. Lazy — safe to call on every
// `explore`/`stats`/`news` entry. If `nowEpoch == 0` (RTC unsynced), no-op.
//
// The 9am-local convention matches the FRPG/Wordle code in the sister
// mesh_bbs project; this keeps the daily rollover predictable for the user.
void lordApplyDailyReset(LordSave &s, uint32_t nowEpoch, int8_t tzOffsetHours);

// Merge a completed Explore run into totals, maintain best-run, append news.
void lordOnRunEnd(LordSave &s, const LordRunStats &r);

// Badge set bit, progress bumped to 5 (leader cleared), news event, save-it.
void lordOnGymCleared(LordSave &s, uint8_t gymIdx);

// Has the player earned badge `gymIdx` (0..7)?
bool lordHasBadge(const LordSave &s, uint8_t gymIdx);

// Gym unlocked for challenge? Linear: gym 0 always; gym N requires badges
// 0..N-1. (Gym-cleared gyms also remain visitable.)
bool lordGymUnlocked(const LordSave &s, uint8_t gymIdx);

// NG+ level scaling. Tier 0 = base game (return baseLevel unchanged).
// Tier 1..5 forces every gym leader's pokemon to a uniform level
// (60/70/80/90/100), and the E4 + Champion to that level + 10 (cap 100).
// Returns max(baseLevel, scaledLevel) so a base-game leader at lvl 65 isn't
// scaled DOWN at NG+1 when the table says 60.
uint8_t lordScaleLevel(uint8_t baseLevel, uint8_t ngPlusTier, bool isE4);

// Globally accessible NG+ tier — set by terminal at lord-load time so the
// LordGyms / LordE4 builders can pick it up without threading a parameter
// through the gym-fight callback chain.
void    lordSetCurrentNgPlusTier(uint8_t tier);
uint8_t lordCurrentNgPlusTier();

// NG+ coverage moves: gym/E4 leaders get level-appropriate STAB / coverage
// moves at higher tiers (e.g. Brock's Onix learns Earthquake at NG+1+).
// Tier 0 = no change (orig moves stay). Tier 1+ overlays one type-1
// coverage move into the last slot; tier 2+ adds a type-2 coverage move
// into slot 2; tier 3+ tops the last slot with Hyper Beam.
//
// `dex` is the pokedex number; `moves[4]` is patched in-place. Safe to
// call when tier == 0 (no-op).
void lordApplyNgPlusMoves(uint8_t dex, uint8_t tier, uint8_t moves[4]);
