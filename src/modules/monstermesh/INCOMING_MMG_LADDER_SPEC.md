# INCOMING — MMG gym ladder (5-trainer sequence)

> **For the gym/Gauntlet agent iterating on `gauntlet/GauntletModule.cpp`:**
> The challenger side (`MonsterMeshModule.cpp` on `t-deck-tft`) now drives a
> 5-trainer gym ladder over the existing `BBS_FIGHT_REQUEST` /
> `TEXT_BATTLE_PARTY` / `BBS_FIGHT_RESULT` packets. The gym side currently
> serves the same single party (leader) every time, so the challenger fights
> trainer 1 five times. This note pins down the protocol the gym side needs
> to follow so the ladder feels right end-to-end.

## What the challenger does today (b227, shipped)

In `MonsterMeshModule.cpp`:

1. `mmg fight N` → sends one `BBS_FIGHT_REQUEST` to gym `N`. Sets a per-run
   ladder index `bbsLadderTrainerIdx_ = 0`, `bbsLadderCount_ = 5`.
2. Receives `TEXT_BATTLE_PARTY` chunks → reassembles → starts the local
   battle vs the served party. Header reads `MM Gym - trainer 1/5`.
3. On battle resolve:
   - **Win** + `bbsLadderTrainerIdx_ + 1 < 5` → full-heal the player party,
     `bbsLadderTrainerIdx_++`, send a fresh `BBS_FIGHT_REQUEST` (new
     `sessionId`, same target). Header bumps to `MM Gym - trainer N/5`.
   - **Win** + last trainer (idx 4) → "MM Gym cleared!" → ladder ends.
   - **Loss** → ladder ends. `BBS_FIGHT_RESULT` with `outcome = 0` is sent
     before the index resets.
4. Each win-step also sends `BBS_FIGHT_RESULT` with `outcome = 1` and the
   challenger's short_name. Sessions are independent: each fight gets its
   own `sessionId` (millis-derived); the gym can correlate by source
   `mp.from`, not by sessionId.

## What the gym side needs to change

### Goal

Per-challenger ladder progression. When `BBS_FIGHT_REQUEST` arrives from
node X, serve trainer `members[X.ladderIdx]`. On a win for X, advance
`X.ladderIdx`. On a loss or after 5 wins, reset `X.ladderIdx = 0`.

### State to add

In `GauntletState` (or a sibling struct that doesn't bloat the on-disk
save), add a small ring/dict keyed by `nodeNum` with at minimum:

```c
struct GauntletLadderProgress {
    uint32_t nodeNum;        // challenger
    uint8_t  ladderIdx;      // 0..4 = trainer to serve next
    uint32_t lastSeenMs;     // for stale-entry eviction
};
GauntletLadderProgress ladders[8];   // small cap, evict LRU
```

Doesn't need to persist across reboot. RAM-only is fine.

### `BBS_FIGHT_REQUEST` handler (currently `GauntletModule.cpp` ~line 1485)

Replace the unconditional `buildGymBattleParty(gym)` with a lookup of the
challenger's current ladder index:

```cpp
uint8_t idx = lookupOrInsertLadder(mp.from);   // 0 if new
Gen1Party gym;
buildLadderTrainerParty(gym, idx);             // see below
```

`buildLadderTrainerParty(out, idx)`:
- For idx 0..3: build trainer `members[idx]` from `autoSlots[idx]`.
  Reuse the existing `gauntletBuildPresetTrainerParty` /
  `autoLookupTrainer` pattern. Levels come from
  `state_.memberLevels[idx]`.
- For idx 4 (leader slot): same logic as today's
  `buildGymBattleParty` — player-claimed leader if any, else
  `autoSlots[GAUNTLET_GYM_LEADER_IDX]`, else stored leader, else
  Lorelei fallback.

The existing chunk-and-send loop below the `buildGymBattleParty` call
stays exactly the same — it just ships whichever party `gym` ended up
holding.

### `BBS_FIGHT_RESULT` handler (currently ~line 1530)

After parsing the outcome:

```cpp
if (outcome == 1) {
    GauntletLadderProgress *p = lookupLadder(mp.from);
    if (p) {
        if (p->ladderIdx + 1 < 5) {
            p->ladderIdx++;
        } else {
            // Cleared the leader at idx 4 — promote them and reset.
            // (existing promoteToLeader path stays here, gated on
            // p->ladderIdx == 4.)
            promoteToLeader(...);
            p->ladderIdx = 0;
        }
        p->lastSeenMs = millis();
    }
} else {
    // Loss resets the ladder so they start over next time.
    GauntletLadderProgress *p = lookupLadder(mp.from);
    if (p) p->ladderIdx = 0;
}
```

Important: **only the leader fight (idx == 4) triggers
`promoteToLeader`.** Beating a regular trainer (idx 0..3) just advances
the index — no leader change, no profile update.

### Loss / abandonment handling

If the challenger goes silent (no `BBS_FIGHT_RESULT` and no follow-up
`BBS_FIGHT_REQUEST` for, say, 10 minutes), evict their ladder progress.
On their next request they restart at idx 0. Use `lastSeenMs` for this.

## Protocol-level invariants the challenger expects

- The gym MUST send a non-empty `Gen1Party` for every request. If the
  ladder slot is misconfigured (e.g. members[2] never set), fall back
  to whatever the leader serves today — better than a hung challenger.
- The gym MUST NOT change the wire format of `TEXT_BATTLE_PARTY` chunks.
  The challenger's reassembly is keyed on `(partIdx, partTotal,
  payload + 2, dataLen)` exactly as you ship today.
- Per-trainer scaling (the level the gym admin set via
  `gym <gym> <member> <level>`) should apply — challenger trusts the
  party as-shipped.

## Trainer roster sizes — gameplay note

Reported user feedback (2026-05-07): "I only faced one pokemon per trainer.
I should face the whole party then the next trainer."

The challenger correctly fights every pokemon in the party the gym ships
— battles run until P1_WIN (all opponent KO'd). The 1-mon-per-trainer
feel comes from `GauntletGyms.h` per-trainer roster sizes (e.g.
`gym1_t0` has 1 Geodude, `gym1_t1` has 2 mons). That's canonical Red/Blue
Pewter Gym data — grunts have 1-2, leader has 5-6.

If the experience feels too thin, beef up the grunt rosters in
`GauntletGyms.h` (e.g. give every trainer 2-3 pokemon). Alternatively,
the canonical data could be kept and the ladder length could be cut
from 5 → 3 (one warmup grunt + sub-leader + leader). Either is a
gym-data change, not a protocol change. The challenger doesn't care
how many mons per trainer.

## Optional polish

- `BBS_FIGHT_REPLY` (or a side channel in the chunk header) could carry
  the trainer's display name so the challenger could show
  `MM Gym - Brock 5/5` instead of the generic `MM Gym - trainer 5/5`.
  Wire-format tweak; not required for the ladder to work.
- The challenger always asks for "the next trainer." There's no way to
  say "skip trainer 2." If you want that, add a payload byte to
  `BBS_FIGHT_REQUEST` carrying the desired trainer index — challenger
  doesn't send it today, but you could add an opt-in field.

## Files the challenger side already touches

- `MonsterMeshModule.h` — added `bbsLadderTrainerIdx_`,
  `bbsLadderCount_`, `bbsLadderRequestPending_`. Don't move these to
  GauntletModule's namespace; they're per-challenger state.
- `MonsterMeshTextBattle.{h,cpp}` — added `healPlayer()` for the
  inter-trainer heal.
- `MonsterMeshModule.cpp` — runOnce branches that re-fire
  `BBS_FIGHT_REQUEST` and call `nextOpponent` mid-ladder.

These are challenger-side only; you should not need to read or modify
them. Just emit the right party for the right request and the loop
takes care of itself.
