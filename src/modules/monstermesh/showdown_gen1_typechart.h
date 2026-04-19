// SPDX-License-Identifier: MIT
//
// Auto-generated from Pokemon Showdown data — DO NOT EDIT BY HAND.
// Regenerate with: python3 scripts/gen_showdown_data.py
//
// Source: https://github.com/smogon/Pokemon-Showdown
// Copyright (c) Smogon University / Pokemon Showdown contributors (MIT).
//
// Pokemon and all related marks are trademarks of Nintendo / Game Freak / Creatures.
// This file contains only mechanical/statistical data, used for fan-game research.

#pragma once
#include <stdint.h>

// Gen 1 type effectiveness (Showdown rules — Psychic immune to Ghost, etc.)
// Encoding: 0 = immune, 1 = resist (0.5x), 2 = neutral (1x), 4 = super (2x).
// Use as: GEN1_TYPECHART[attackerType][defenderType]

static constexpr const char *GEN1_TYPE_NAMES[] = {
    "NORMAL",
    "FIGHTING",
    "FLYING",
    "POISON",
    "GROUND",
    "ROCK",
    "BIRD",
    "BUG",
    "GHOST",
    "FIRE",
    "WATER",
    "GRASS",
    "ELECTRIC",
    "PSYCHIC",
    "ICE",
    "DRAGON",
};
static constexpr uint8_t GEN1_TYPE_COUNT = 16;

static constexpr uint8_t GEN1_TYPECHART[16][16] = {
    { 2, 2, 2, 2, 2, 1, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2 },  //    normal
    { 4, 2, 1, 1, 2, 4, 2, 1, 0, 2, 2, 2, 2, 1, 4, 2 },  //  fighting
    { 2, 4, 2, 2, 2, 1, 2, 4, 2, 2, 2, 4, 1, 2, 2, 2 },  //    flying
    { 2, 2, 2, 1, 1, 1, 2, 4, 1, 2, 2, 4, 2, 2, 2, 2 },  //    poison
    { 2, 2, 0, 4, 2, 4, 2, 1, 2, 4, 2, 1, 4, 2, 2, 2 },  //    ground
    { 2, 1, 4, 2, 1, 2, 2, 4, 2, 4, 2, 2, 2, 2, 4, 2 },  //      rock
    { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },  //      bird
    { 2, 1, 1, 4, 2, 2, 2, 2, 1, 1, 2, 4, 2, 4, 2, 2 },  //       bug
    { 0, 2, 2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 0, 2, 2 },  //     ghost
    { 2, 2, 2, 2, 2, 1, 2, 4, 2, 1, 1, 4, 2, 2, 4, 1 },  //      fire
    { 2, 2, 2, 2, 4, 4, 2, 2, 2, 4, 1, 1, 2, 2, 2, 1 },  //     water
    { 2, 2, 1, 1, 4, 4, 2, 1, 2, 1, 4, 1, 2, 2, 2, 1 },  //     grass
    { 2, 2, 4, 2, 0, 2, 2, 2, 2, 2, 4, 1, 1, 2, 2, 1 },  //  electric
    { 2, 4, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 2 },  //   psychic
    { 2, 2, 4, 2, 4, 2, 2, 2, 2, 2, 1, 4, 2, 2, 1, 4 },  //       ice
    { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4 },  //    dragon
};
