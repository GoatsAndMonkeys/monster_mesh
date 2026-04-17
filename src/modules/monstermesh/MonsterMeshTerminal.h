// SPDX-License-Identifier: MIT
//
// MonsterMeshTerminal — LVGL text terminal for Gen 1 Pokemon battles.
//
// Runs entirely on the local T-Deck — no DMs, no peer node required. Provides
// a command-line-style interface rendered in an LVGL panel inside the device-ui
// tools menu. The player types commands on the physical keyboard; results are
// appended as text lines to a scrollable output area.
//
// Supported commands (typed into the input bar, Enter to submit):
//   help             — list commands
//   rogue            — start a roguelike run
//   ok               — advance to next encounter
//   1..4             — use move 1-4
//   s 1..6           — switch to party slot
//   status           — show party / battle
//   quit             — abandon run
//   queue / queue r   — join matchmaking (casual / rated)
//   peers            — list nearby trainers
//   cancel           — leave queue
//   forfeit          — forfeit PvP battle

#pragma once

#include <Arduino.h>
#include "Gen1BattleEngine.h"
#include "PokemonData.h"
#include "showdown_gen1_basestats.h"
#include "showdown_gen1_moves.h"

// Forward-declare LVGL types so we don't pull in lvgl.h in the header.
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

class MonsterMeshTerminal {
public:
    MonsterMeshTerminal() = default;

    // ── LVGL wiring ────────────────────────────────────────────────────────
    // Called once after the device-ui is created. `outputPanel` is the
    // scrollable flex-column container; `inputTextarea` is the one-line
    // textarea for commands.
    void init(lv_obj_t *outputPanel, lv_obj_t *inputTextarea);

    // Called when the user presses Enter in the input textarea.
    void submitCommand();

    // Returns true if the terminal has been initialised.
    bool ready() const { return outputPanel_ != nullptr; }

private:
    // ── Battle state (mirrors PokeBattleNRF logic) ─────────────────────────
    enum class State : uint8_t {
        IDLE,
        BETWEEN_BATTLES,
        IN_BATTLE,
        RUN_OVER,
    };

    static constexpr uint8_t FLOORS_PER_BOSS      = 5;
    static constexpr uint8_t ENCOUNTERS_PER_FLOOR  = 3;
    static constexpr uint8_t MAX_OUTPUT_LINES      = 200;

    State    state_      = State::IDLE;
    uint8_t  floor_      = 0;
    uint8_t  encIdx_     = 0;
    uint32_t rng_        = 0;
    Gen1BattleEngine engine_;
    Gen1Party        playerParty_ = {};

    // ── LVGL objects (not owned — lifetime managed by device-ui) ───────────
    lv_obj_t *outputPanel_    = nullptr;
    lv_obj_t *inputTextarea_  = nullptr;
    uint16_t  lineCount_      = 0;

    // ── Output ─────────────────────────────────────────────────────────────
    void print(const char *text);          // one line to output
    void printSep();                       // "────────" separator
    static void engineLogCb(const char *line, void *ctx);

    // ── Command processing ─────────────────────────────────────────────────
    void handleCommand(const char *cmd);

    // ── Game logic ─────────────────────────────────────────────────────────
    void startRun();
    void prepareNextEncounter();
    void resolvePlayerAction(uint8_t actionType, uint8_t index);
    void describeBattleStatus();
    void describePartyStatus();

    // ── Party building ─────────────────────────────────────────────────────
    void buildDemoParty(Gen1Party &out);
    void buildWildOpponent(Gen1Party &out, uint8_t avgLvl);
    void buildBossOpponent(Gen1Party &out, uint8_t floorNum);
    void healFullParty();
    static void pickMovesForSpecies(uint8_t species, uint8_t outMoves[4]);
    static void writeBattlePokeToSave(Gen1Party &out, uint8_t slot,
                                      uint8_t species, uint8_t lvl,
                                      const uint8_t moves[4],
                                      const Gen1BattleEngine::BattlePoke &tmp,
                                      uint8_t dvByte, const char *nick);

    // ── RNG ────────────────────────────────────────────────────────────────
    uint32_t rand32();
};
