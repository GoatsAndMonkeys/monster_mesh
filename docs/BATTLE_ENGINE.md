# The Gen-1 battle engine

`Gen1BattleEngine` is a from-scratch C++ implementation of the Generation-1 battle system. It is the single battle core used by **every** mode — gyms, the Elite Four, explore, local `fight`, MonsterMesh Gyms (`mmg`), and live PvP (`mmb`). The same code runs a CPU opponent locally or a real opponent over the radio; only where the *actions* come from changes.

## Design goals

1. **Faithful Gen-1 mechanics** — type chart with the Gen-1 quirks, STAB, critical hits, stat stages, accuracy, the Gen-1 speed-tie behavior.
2. **Deterministic** — given the same starting parties, the same RNG seed, and the same sequence of actions, two devices compute byte-for-byte identical battles. This is what makes networked play possible without sending battle state over the slow LoRa link.
3. **Pure-input** — battle *state* never crosses the wire. Both sides hold their own copy and advance it from `(state, myAction, theirAction)`.

## What's modeled

- Stats derived from Gen-1 base stats + level.
- Real movesets and current PP read from your loaded `.sav`.
- Type effectiveness with Gen-1-specific cases, STAB, critical hits, damage variance, stat-stage multipliers, accuracy/miss.
- A simple **CPU action picker** (highest expected damage) used for gym trainers, the Elite Four, wild encounters, MMG gym trainers, and the local `fight` mirror match.
- Per-monster XP awards with in-battle level-ups, written back into your `.sav` after the fight (Gen-1 EXP curve).

### Known simplifications

Some complex Gen-1 moves are intentionally simplified (they fall through to plain damage / no-op) rather than fully simulated — e.g. multi-turn trapping locks, Bide/Counter/Transform/Mimic/Metronome, and the Substitute internals. These are clearly marked in `Gen1BattleEngine.cpp`. Add behavior when a mode needs it; the engine is structured so you can fill these in without reworking the core.

## Determinism & networked play

For a live PvP battle (`mmb`, see [MULTIPLAYER.md](MULTIPLAYER.md)):

1. The initiator picks an RNG seed and sends it in a `TEXT_BATTLE_START` packet, along with the generation, party count, and a hash of the party.
2. Both sides send their full party once, chunked, via `TEXT_BATTLE_PARTY` packets.
3. Each turn, both sides broadcast only their chosen action in a `TEXT_BATTLE_ACTION` packet (`turn`, action type, index).
4. Once both actions for a turn are known, **each device resolves the turn locally** using the shared seed. Because the engine is deterministic, both arrive at the same result.

The RNG is seeded from the shared seed so both peers draw the same random numbers in lockstep.

### Desync detection

LoRa is lossy and bugs happen, so the engine guards against silent divergence:

- Every **5 turns** (`TEXT_BATTLE_HASH_INTERVAL`), each side computes a hash of the full battle state (turn number, both sides' HP/status/boosts/PP) and sends it in a `TEXT_BATTLE_HASH` packet.
- If the peer's hash doesn't match the local one, the battle is **forfeited** rather than continued. A desync means a real bug or dropped packet; the engine doesn't try to silently "repair" state.

## Wire format

The text-battle packet types are defined alongside the battle code (`MonsterMeshTextBattle.h` / `MonsterMeshModule.h`) and carried over the MonsterMesh mesh channel (channel 1):

| Packet | Purpose |
|---|---|
| `TEXT_BATTLE_START` | `rngSeed`, generation, party count, party hash — opens a networked battle |
| `TEXT_BATTLE_PARTY` | chunked full party data, sent once at the start |
| `TEXT_BATTLE_ACTION` | `turn`, action type, index — one player's move for a turn |
| `TEXT_BATTLE_HASH` | `turn` + state hash, every 5 turns, for desync detection |
| `TEXT_BATTLE_FORFEIT` | end the battle |

The MMG gym ladder reuses the same `TEXT_BATTLE_PARTY` transfer plus its own request/result packets (`BBS_LADDER_*`, `BBS_FIGHT_*`) — see [MULTIPLAYER.md](MULTIPLAYER.md#wire-protocol-summary).

## Data pipeline

The Gen-1 battle data — base stats and moves — comes from [Pokémon Showdown](https://github.com/smogon/pokemon-showdown) (MIT), extracted into `showdown_gen1_basestats.h` and `showdown_gen1_moves.h`. These headers are **generated** from the Showdown data set and are **gitignored**, not committed — so they're produced as part of setting up a build. The species name table (`Gen1Species.h`) maps the Game Boy internal species indices to names.

Keep the SPDX-MIT/attribution headers on the generated files intact — see [CREDITS.md](CREDITS.md).
