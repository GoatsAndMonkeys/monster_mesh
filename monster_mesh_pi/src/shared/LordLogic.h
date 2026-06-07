// SPDX-License-Identifier: MIT
//
// NG+ scaling + coverage-move overlay for the Legend of Charizard door game.
// Ported verbatim from the T-Deck MonsterMesh firmware (LordLogic.cpp) so the
// Pi and T-Deck play identically.

#pragma once

#include "platform.h"

// Scale a roster level for the given NG+ tier.  Tier 0 = base game (unchanged).
// Gyms scale to L60/70/80/90/100 for tiers 1..5; Elite Four / Champion get +10
// on top (capped at 100).  Never scales a mon *down* below its base level.
uint8_t lordScaleLevel(uint8_t baseLevel, uint8_t ngPlusTier, bool isE4);

// Session-global NG+ tier, so party builders don't have to thread it through
// every call.  Set this from LordSave.ngPlusTier at load time.
void    lordSetCurrentNgPlusTier(uint8_t tier);
uint8_t lordCurrentNgPlusTier();

// Overlay NG+ coverage moves onto a 4-move set in place (tier-dependent).
// No-op at tier 0.  dex is the national dex number (1..151).
void    lordApplyNgPlusMoves(uint8_t dex, uint8_t tier, uint8_t moves[4]);
