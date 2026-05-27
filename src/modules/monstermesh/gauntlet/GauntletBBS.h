// SPDX-License-Identifier: MIT
//
// GauntletBBS — TinyBBS-friendly event log + (future) door-game hook.
//
// TinyBBS isn't (yet) integrated into this firmware tree. Until it is, this
// layer writes JSONL records to LittleFS so that:
//   1. A future TinyBBS integration can ingest the files into bulletin boards.
//   2. A central script can `scp /monstermesh/gauntlet_log/*.jsonl` for
//      external aggregation right now.
//
// Files:
//   /monstermesh/gauntlet_log/records.jsonl  — leader changes, roster shifts
//   /monstermesh/gauntlet_log/messages.jsonl — player-posted gym messages
//
// Each line is a self-contained JSON object terminated by '\n'. Files rotate
// when they exceed GAUNTLET_LOG_MAX_BYTES (oldest data is dropped — we don't
// need full history for this use-case; MQTT carries the durable feed).
//
// Door-game integration plan: when TinyBBS lands, route TinyBBS games-menu
// 'Y' selection into a new BBS_STATE_GAUNTLET; that handler can call
// gauntletBBSStartChallenge() and gauntletBBSHandleStep() below — they
// return reply strings sized to fit a single Meshtastic packet.

#pragma once
#include "GauntletData.h"

#define GAUNTLET_BBS_BOARD_RECORDS  4   // maps to TinyBBS BOARD_GYM if integrated
#define GAUNTLET_BBS_BOARD_MESSAGES 5   // maps to TinyBBS BOARD_GYM_MSG
#define GAUNTLET_LOG_MAX_BYTES      (32 * 1024)

// Append a "leader_change" record (JSONL).
void gauntletBBSLogLeader(const GauntletState &s,
                           uint32_t nodeNum, const char *name,
                           const char *partyCsv);

// Append a "roster" (slot-claimed) record.
void gauntletBBSLogRoster(const GauntletState &s,
                           uint32_t nodeNum, const char *name,
                           uint8_t rank);

// Append a player-posted message (gym message board).
void gauntletBBSLogMessage(uint32_t nodeNum, const char *name, const char *text);

// ── Door-game stubs (forward-compatible API for TinyBBS integration) ─────────
//
// When TinyBBS is integrated, its games-menu handler should route a 'Y'
// selection here. Each call returns a short reply string suitable for a
// single Meshtastic text packet. Set *done=true when the challenge ends.
//
// These are no-ops without a live GauntletModule (nullptr-safe).

// Begin a new challenge from inside a TinyBBS session.
const char *gauntletBBSStartChallenge(uint32_t nodeNum, const char *shortName);

// Receive one BBS-state input ("y", "Pikachu,Charizard,...", any continue
// keypress) and advance the gauntlet one step. Sets *done=true at the end.
const char *gauntletBBSHandleStep(uint32_t nodeNum, const char *text, bool *done);
