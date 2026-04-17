// SPDX-License-Identifier: MIT
//
// MonsterMeshRoguelike — solo dungeon crawler that re-uses Gen1BattleEngine
// for combat. Designed to require no peer connection.
//
// Run loop:
//   floor 1 → 3 random encounters → between-floor heal → floor 2 → … →
//   every 5th floor: scripted boss → reward (full party heal + +1 level)
//   full party faint → run ends, return to title.
//
// All state is in-RAM only (intentionally — no save bloat). The player uses
// their current Gen 1 save's party as the run team; XP/levels are virtual
// for the duration of the run.

#pragma once

#include <Arduino.h>
#include "PokemonData.h"
#include "MonsterMeshTextBattle.h"

class MonsterMeshRoguelike {
public:
    static constexpr uint8_t FLOORS_PER_BOSS = 5;
    static constexpr uint8_t ENCOUNTERS_PER_FLOOR = 3;

    enum class State : uint8_t {
        OFF,                // not in a run
        BETWEEN_BATTLES,    // showing floor / encounter banner; press space to continue
        IN_BATTLE,          // delegated to MonsterMeshTextBattle
        RUN_OVER,           // party wiped; press any key to exit
    };

    explicit MonsterMeshRoguelike(MonsterMeshTextBattle &battle)
      : battle_(battle) {}

    // Begin a new run with the player's current party.
    void start(const Gen1Party &playerParty);

    // Called every frame; advances state when the inner battle finishes.
    void tick(uint32_t nowMs);

    // Caller passes the player's keystrokes.  The roguelike consumes them
    // only between battles (banner screens) — during a battle, input goes
    // directly to MonsterMeshTextBattle::handleKey().
    void handleKey(uint8_t c);

    bool   isActive() const { return state_ != State::OFF; }
    State  state()    const { return state_; }
    uint8_t floor()   const { return floor_; }
    uint8_t encounterIndex() const { return encounterIdx_; }

    // For the host module to render the banner overlay.
    const char *bannerLine1() const { return banner1_; }
    const char *bannerLine2() const { return banner2_; }

private:
    MonsterMeshTextBattle &battle_;

    State    state_       = State::OFF;
    Gen1Party playerParty_= {};
    uint8_t  floor_       = 0;
    uint8_t  encounterIdx_= 0;

    char banner1_[40] = {};
    char banner2_[40] = {};

    void prepareNextEncounter();
    void buildWildOpponent(Gen1Party &out, uint8_t playerLevel);
    void buildBossOpponent(Gen1Party &out, uint8_t floorNum);
    void healFullParty();
    void announce(const char *line1, const char *line2);

    // Pseudo-RNG seeded from millis at run start so floors feel different.
    uint32_t rng_ = 0;
    uint32_t rand32();
};
