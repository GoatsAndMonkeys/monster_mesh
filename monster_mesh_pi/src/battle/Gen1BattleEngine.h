// SPDX-License-Identifier: MIT
//
// Gen1BattleEngine — deterministic Pokemon Gen 1 battle engine used for
// MonsterMesh text-battles over LoRa and the local roguelike crawler.
//
// Both sides of a networked battle run the same engine with the same RNG seed
// and exchange only their inputs (move/switch/forfeit). Output (HP, messages,
// turn order) must therefore be a pure function of (state, inputs).
//
// Stub points for future Gen 2 support are marked with `// gen2:`.
//
// Move/type/stat data: Pokemon Showdown (https://github.com/smogon/Pokemon-Showdown).
// Pokemon trademarks © Nintendo / Game Freak / Creatures.

#pragma once

#include "../shared/platform.h"
#include "../shared/PokemonData.h"
#include "showdown_gen1_moves.h"
#include "showdown_gen1_typechart.h"
#include "showdown_gen1_basestats.h"
#include "showdown_gen1_text.h"

class Gen1BattleEngine {
public:
    static constexpr uint8_t MAX_PARTY     = 6;
    static constexpr uint8_t LOG_LINE_MAX  = 64;

    enum class Result : uint8_t { ONGOING, P1_WIN, P2_WIN, DRAW };

    // Status condition bits (Gen 1 byte layout — same as Gen1Pokemon::status).
    enum Status : uint8_t {
        ST_NONE = 0x00, ST_SLP = 0x07, ST_PSN = 0x08,
        ST_BRN  = 0x10, ST_FRZ = 0x20, ST_PAR = 0x40,
    };

    struct BattlePoke {
        uint16_t species   = 0;   // national dex (1-386)
        char     nickname[11] = {};
        uint8_t  level     = 0;
        uint8_t  type1     = 0, type2 = 0;
        uint16_t hp        = 0, maxHp = 0;
        uint16_t atk = 0, def = 0, spd = 0, spc = 0;
        // Gen-3 mode only: Special is split into Sp.Atk / Sp.Def. Unused (0) in
        // Gen-1 mode, which keeps using spc for both. See setGen3().
        uint16_t spaG3 = 0, spdG3 = 0;
        uint8_t  moves[4]  = {};
        uint8_t  pp[4]     = {};
        // Live state (cleared on switch-out except status)
        uint8_t  status    = ST_NONE;
        uint8_t  sleepTurns   = 0;
        uint8_t  confuseTurns = 0;
        int8_t   atkBoost = 0, defBoost = 0, spdBoost = 0, spcBoost = 0;
        int8_t   accBoost = 0, evaBoost = 0;
        uint8_t  toxicCounter  = 0;
        bool     mustRecharge  = false;
        bool     thrashing     = false;
        uint8_t  thrashTurns   = 0;
        uint8_t  lastMoveIdx   = 0xFF;     // last move slot used (Counter / Mimic)
        uint8_t  disabledSlot  = 0xFF;
        uint8_t  disabledTurns = 0;
        bool     flinched      = false;    // skip next move if hit by EFF_FLINCH
        uint8_t  chargingSlot  = 0xFF;     // EFF_CHARGE_TURN: slot being charged
        uint8_t  trapTurns     = 0;        // EFF_TRAPPING: target turns left
        uint8_t  thrashSlot    = 0xFF;     // EFF_THRASH: locked-in move slot
        uint16_t bideTurns     = 0;        // EFF_BIDE turn counter (0 = inactive)
        uint16_t bideDamage    = 0;        // accumulated damage to release
        bool     rageActive    = false;    // EFF_RAGE
        uint16_t substituteHp  = 0;        // EFF_SUBSTITUTE: 0 = no sub
        uint16_t lastDamageTaken = 0;      // physical-only, for Counter
        // EFF_MIMIC: a single slot temporarily holds a copied move; the
        // original is restored on switch-out.
        uint8_t  mimicSlot     = 0xFF;
        uint8_t  mimicOrigMove = 0;
        uint8_t  mimicOrigPp   = 0;
        // EFF_TRANSFORM: full backup of the fields Transform overwrites,
        // restored on switch-out.
        bool     transformed   = false;
        uint16_t origSpecies   = 0;
        uint8_t  origType1     = 0, origType2 = 0;
        uint16_t origAtk = 0, origDef = 0, origSpd = 0, origSpc = 0;
        uint16_t origSpaG3 = 0, origSpdG3 = 0;
        uint8_t  origMoves[4]  = {};
        uint8_t  origPp[4]     = {};
    };

    struct BattleParty {
        BattlePoke mons[MAX_PARTY];
        uint8_t    count   = 0;
        uint8_t    active  = 0;
        // Field effects on this side
        bool       reflect = false, lightScreen = false, mist = false, focused = false;
        uint8_t    reflectTurns = 0, lightScreenTurns = 0;
    };

    // Caller-provided log sink. Engine may emit several messages per turn.
    using LogSink = void (*)(const char *line, void *ctx);

    // Initialise from two Gen1Party save records. Both sides MUST pass the
    // same `rngSeed` for deterministic execution.
    void start(const Gen1Party &p1, const Gen1Party &p2,
               uint32_t rngSeed, uint8_t gen = 1);

    // Battle-mechanics generation: 1 = classic Gen-1, 3 = Gen 2/3-style
    // (Special split into Sp.Atk/Sp.Def, staged 1/16 crits, Dark/Steel + modern
    // type chart, re-typed moves). Set before start(), or start() takes it too.
    void    setGen(uint8_t g) { gen_ = g; }
    uint8_t gen() const { return gen_; }

    // Gym gauntlet: swap in a fresh opponent without resetting our side.
    // Player keeps their current HP, PP, status, boosts. Opponent is
    // re-initialized from `p` and result_ goes back to ONGOING.
    void replaceOpponent(const Gen1Party &p);

    // Single-side submission. `side` is 0 (us) or 1 (opponent).
    // Returns true once both sides have submitted and the turn can be executed.
    bool submitAction(uint8_t side, uint8_t actionType, uint8_t index);

    // Execute the pending turn. Caller must have submitted both sides first.
    // After the call, both action slots are cleared.
    void executeTurn(LogSink log, void *ctx);

    // Auto-replace fainted active mon with the first alive party member.
    // Returns false if `side` has no living mons (i.e., that side has lost).
    bool autoReplaceIfFainted(uint8_t side, LogSink log, void *ctx);

    Result   result()  const { return result_; }
    uint16_t turn()    const { return turn_; }
    uint8_t  pendAction(uint8_t side) const { return pendAction_[side & 1]; }

    const BattleParty &party(uint8_t side) const { return p_[side & 1]; }
    BattleParty       &party(uint8_t side)       { return p_[side & 1]; }

    // 8-byte hash of the entire battle state, exchanged every
    // TEXT_BATTLE_HASH_INTERVAL turns to detect desync between peers.
    void hashState(uint8_t out[8]) const;

    // Single-side forfeit; immediately ends the match.
    void forfeit(uint8_t side, LogSink log, void *ctx);

    // Local-only convenience: roll a CPU action (move + index) for `side`.
    // Used by the roguelike crawler.
    void cpuPickAction(uint8_t side, uint8_t &outAction, uint8_t &outIndex);

    // Live-stat init from save struct. Public so the roguelike crawler can
    // build wild encounters from base stats without going through Gen1Party.
    // `gen` selects the mechanics generation (1 or 3) for stat derivation, so
    // these stay static (no instance needed).
    static void initBattlePokeFromSave(BattlePoke &dst,
                                       const Gen1Pokemon &src,
                                       const uint8_t nick[11], uint8_t gen = 1);
    static void initBattlePokeFromBase(BattlePoke &dst,
                                       uint16_t species, uint8_t level,
                                       const uint8_t moves[4], uint8_t gen = 1);

private:
    BattleParty p_[2];
    uint32_t    rng_     = 0;
    uint16_t    turn_    = 0;
    uint8_t     gen_     = 1;
    Result      result_  = Result::ONGOING;

    // Pending submitted actions for next turn.
    uint8_t pendAction_[2] = {0xFF, 0xFF};
    uint8_t pendIndex_[2]  = {0, 0};

    // Deterministic RNG (xoshiro128+ — fast, tiny, well-distributed).
    uint32_t s_[4] = {};
    uint32_t rand32();
    uint8_t  rand8();           // 0..255
    bool     percent(uint8_t p); // true with probability p%

    // Battle math.
    bool     isGen3() const { return gen_ >= 3; }
    // Move data for the active mechanics generation (gen3 re-types/re-powers
    // the same 165 move ids; effects + PP are identical, so this is safe to use
    // everywhere the engine looks a move up).
    const Gen1MoveData *mdata(uint8_t id) const;
    uint16_t calcDamage(uint8_t side, uint8_t targetSide,
                        const Gen1MoveData &mv, bool &outCrit);
    uint8_t  effectiveness(uint8_t atkType, uint8_t defType) const;
    bool     rollHit(uint8_t side, uint8_t targetSide,
                     const Gen1MoveData &mv);

    // Turn step helpers.
    bool     pokeActsFirst(uint8_t side, uint8_t action, uint8_t targetAction);
    void     stepAction(uint8_t side, LogSink log, void *ctx);
    void     useMove(uint8_t side, uint8_t moveSlot, LogSink log, void *ctx);
    void     applyMove(uint8_t side, uint8_t targetSide,
                       const Gen1MoveData &mv, LogSink log, void *ctx);
    void     applyEndOfTurn(uint8_t side, LogSink log, void *ctx);

    // Logging helper — substitutes [POKEMON]/[MOVE]/[NUM]/[TARGET] in template.
    void emit(LogSink log, void *ctx, const char *tmpl,
              const char *poke = nullptr, const char *move = nullptr,
              const char *target = nullptr, int num = 0);

    static int16_t boostMult(int8_t stage);  // x100 modifier (e.g., +1 = 150)
};
