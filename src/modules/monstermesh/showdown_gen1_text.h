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

// Gen 1 battle messages — Showdown style.
// Substitution placeholders: [POKEMON] [MOVE] [TARGET] [NUM]
namespace Gen1Text {
    static constexpr const char *CRIT = "A critical hit!";
    static constexpr const char *DRAIN = "[POKEMON] had its energy drained!";
    static constexpr const char *FAINT = "[POKEMON] fainted!";
    static constexpr const char *FOE_SEND = "Foe sent out [POKEMON]!";
    static constexpr const char *FORFEIT = "[POKEMON] forfeited the match.";
    static constexpr const char *IMMUNE = "It doesn't affect [POKEMON]…";
    static constexpr const char *MISS = "[POKEMON]'s attack missed!";
    static constexpr const char *OHKO = "It's a one-hit KO!";
    static constexpr const char *RECOIL = "[POKEMON] is hit with recoil!";
    static constexpr const char *RESIST = "It's not very effective…";
    static constexpr const char *STATUS_BRN = "[POKEMON] was burned!";
    static constexpr const char *STATUS_CONFUSE = "[POKEMON] became confused!";
    static constexpr const char *STATUS_FRZ = "[POKEMON] was frozen solid!";
    static constexpr const char *STATUS_PAR = "[POKEMON] is paralyzed! It may be unable to move!";
    static constexpr const char *STATUS_PSN = "[POKEMON] was poisoned!";
    static constexpr const char *STATUS_SLP = "[POKEMON] fell asleep!";
    static constexpr const char *SUPER = "It's super effective!";
    static constexpr const char *SWITCH_IN = "Go! [POKEMON]!";
    static constexpr const char *SWITCH_OUT = "Come back, [POKEMON]!";
    static constexpr const char *USE = "[POKEMON] used [MOVE]!";
    static constexpr const char *VICTORY = "[POKEMON] won the battle!";
    static constexpr const char *WAIT = "Waiting for opponent…";
}  // namespace Gen1Text
