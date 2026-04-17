// SPDX-License-Identifier: MIT
//
// MonsterMeshTerminal — LVGL text terminal for Gen 1 Pokemon battles.
//
// Text-based multiplayer Pokemon battles over LoRa mesh. Uses the 6 Pokemon
// from the player's Game Boy SAV file (same as PokemonDaycare). Player picks
// one Pokemon at a time and battles opponents.
//
// Commands:
//   help             — list commands
//   party            — show your 6 Pokemon from the SAV
//   pick 1..6        — choose Pokemon for battle
//   fight            — battle a random CPU opponent
//   1..4             — use move 1-4
//   status           — show battle status
//   quit             — forfeit current battle

#pragma once

#include <Arduino.h>
#include "Gen1BattleEngine.h"
#include "PokemonData.h"
#include "DaycareEventGen.h"
#include "showdown_gen1_basestats.h"
#include "showdown_gen1_moves.h"

// Forward-declare LVGL types so we don't pull in lvgl.h in the header.
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

class MonsterMeshTerminal {
public:
    MonsterMeshTerminal() = default;

    void init(lv_obj_t *outputPanel, lv_obj_t *inputTextarea);
    void submitCommand();
    bool ready() const { return outputPanel_ != nullptr; }

    // Called by MonsterMeshModule after reading .sav from SD card.
    // Copies the full party (up to 6 Pokemon with decoded ASCII nicknames).
    void loadParty(const Gen1Party &party);
    bool hasParty() const { return savParty_.count > 0; }

    // Set true when init fires and party isn't loaded yet; module checks this.
    bool needsPartyLoad() const { return needsLoad_; }

    // Point to daycare's live neighbor list — called from runOnce() each tick.
    // Pointer must remain valid (it points into PokemonDaycare's internal array).
    void setMeshPeers(const DaycareNeighborPokemon *peers, uint8_t count) {
        meshPeers_      = peers;
        meshPeerCount_  = count;
    }

private:
    enum class State : uint8_t {
        IDLE,           // no party loaded or no Pokemon picked
        READY,          // Pokemon picked, waiting to fight
        IN_BATTLE,      // in a battle
        IN_RUN,         // roguelike run active, between waves
        IN_RUN_BATTLE,  // roguelike run, in a wave battle
    };

    static constexpr uint16_t MAX_OUTPUT_LINES = 200;

    State    state_      = State::IDLE;
    uint32_t rng_        = 0;
    Gen1BattleEngine engine_;
    Gen1Party        savParty_    = {};   // full 6-mon party from SAV
    Gen1Party        battleParty_ = {};   // single chosen Pokemon
    Gen1Party        oppParty_    = {};
    uint8_t          chosenSlot_  = 0xFF; // which of the 6 is active
    bool             needsLoad_   = false;

    // Roguelike run state
    bool      runActive_  = false;
    uint8_t   waveNum_    = 0;
    Gen1Party runParty_   = {};   // full party with HP persisted across waves

    // Mesh peer list — pointer into PokemonDaycare::neighbors_ (not owned)
    const DaycareNeighborPokemon *meshPeers_     = nullptr;
    uint8_t                       meshPeerCount_ = 0;
    char                          lastFoeSource_[12] = {};  // trainer short name of last opponent

    // LVGL objects (not owned)
    lv_obj_t *outputPanel_    = nullptr;
    lv_obj_t *inputTextarea_  = nullptr;
    uint16_t  lineCount_      = 0;

    // Output
    void print(const char *text);
    void printSep();
    static void engineLogCb(const char *line, void *ctx);

    // Commands
    void handleCommand(const char *cmd);

    // Game logic
    void showParty();
    void pickPokemon(uint8_t slot);
    void startBattle();
    void resolvePlayerAction(uint8_t actionType, uint8_t index);
    void describeBattleStatus();
    void startRun();
    void startRunWave();
    void syncRunPartyHpFromEngine();
    uint8_t runAvgLevel() const;

    // Wild opponent generation
    void buildWildOpponent(Gen1Party &out, uint8_t level);
    static void pickMovesForSpecies(uint8_t species, uint8_t outMoves[4]);
    void writeBattlePokeToSave(Gen1Party &out, uint8_t slot,
                               uint8_t species, uint8_t lvl,
                               const uint8_t moves[4],
                               const Gen1BattleEngine::BattlePoke &tmp,
                               uint8_t dvByte, const char *nick);

    // RNG
    uint32_t rand32();
};
