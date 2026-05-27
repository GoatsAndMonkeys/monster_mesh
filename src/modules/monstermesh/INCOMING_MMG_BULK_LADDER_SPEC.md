# INCOMING — MMG bulk-ladder protocol (replaces 5× per-trainer requests)

> **For the challenger agent (`MonsterMeshModule.cpp` on `t-deck-tft`):**
> The gym side now answers a single `BBS_LADDER_REQUEST` with two upfront
> packets containing all 5 trainer names + parties. The T-Deck runs every
> fight locally with zero mid-ladder LoRa traffic.

## What the gym side ships (already implemented in `GauntletModule.cpp`)

When the challenger sends `BBS_LADDER_REQUEST` (0x74, no payload) over
PRIVATE_APP on the MonsterMesh channel, the gym replies with two packets:

### Packet 1 — `BBS_LADDER_NAMES` (0x75)

`payload[]`:
```
u8 trainerCount    // = 5
for each trainer:
    u8 nameLen     // 0..16
    char name[nameLen]
// Optional tail (always present in current firmware, but parse defensively):
u8 gymNameLen      // 0..16
char gymName[gymNameLen]
u8 badgeLen        // 0..16
char badge[badgeLen]
```

Total: 1 + 5 × (1 + ≤16) + 2 + 2 × ≤16 = up to ~120 bytes. Fits in one
packet. Old gym firmware that predates the tail will simply omit the
two trailing length-prefixed strings — read defensively (stop when you
hit the end of `payload.size`).

The challenger should label its battle UI using the badge name when
present:
- `<gymName> [<badge>] — <trainerName> N/5`  e.g. `Pewter Gym [Boulder Badge] — Brock 5/5`

Falls back to `MM Gym — <trainerName> N/5` when `gymName`/`badge` are
empty or missing.

### Packet 2 — `BBS_LADDER_PARTIES` (0x76)

`payload[]`:
```
u8 trainerCount    // = 5
for each trainer:
    u8 monCount        // 0..6
    for each mon:
        u8 dex         // 1..151
        u8 level       // 1..100
        u8 move0       // showdown gen1 move id (PP defaults to canonical max)
        u8 move1
        u8 move2
        u8 move3
```

Worst case: 1 + 5 × (1 + 6 × 6) = **186 bytes** — fits inside the
196-byte `BATTLELINK_MAX_PAYLOAD`. Trainers with fewer mons are shorter.

### Stat derivation

Stats are NOT shipped. The T-Deck recomputes HP/atk/def/spd/spc + types from
`showdown_gen1_basestats[dex]` + level using the same formula already in
use (`Gen1MinimalStats::gen1MinimalStats(dex, level)` mirrors the engine's
wild-encounter shape: IV=8, StatExp=0). PP defaults to canonical max
from the move table.

### Both packets share `sessionId`

The gym copies `sessionId` from the incoming `BBS_LADDER_REQUEST`. Both
reply packets carry the same sessionId; `seq=0` for NAMES, `seq=1` for
PARTIES. Use this for cache-aware reassembly if the user re-fires the
request (treat duplicates as authoritative).

## What the challenger needs to do

1. Replace the per-trainer `BBS_FIGHT_REQUEST` ladder loop in
   `MonsterMeshModule.cpp` with a single `BBS_LADDER_REQUEST` send.
2. On `BBS_LADDER_NAMES` arrival: store the 5 names in `bbsLadderNames_[5]`.
3. On `BBS_LADDER_PARTIES` arrival: parse the 5 mon-lists into a
   `Gen1Party[5]` (or equivalent), reconstructing each `Gen1Pokemon`:
   - `species = gen1DexToInternal(dex)`
   - `boxLevel = level = level`
   - HP/atk/def/spd/spc/types from `gen1MinimalStats(dex, level)`
   - `dvs[0] = dvs[1] = 0x88` (matches IV=8 used in stat calc)
   - `moves[0..3] = moves[0..3]`
   - `pp[i] = canonical max PP` for `moves[i]` (or 25 as a safe default)
   - `nicknames[i]` = species name from `gen1SpeciesName(internal)`
4. Run all 5 fights locally, healing the player party between fights.
   Battle header reads `MM Gym - <bbsLadderNames_[i]> i+1/5`.
5. Send `BBS_FIGHT_RESULT` ONLY:
   - On final win (beat trainer 4) — outcome=1.
   - On any loss — outcome=0.
   - Skip `BBS_FIGHT_RESULT` for grunt-trainer wins (1..4); the gym no
     longer needs them with the bulk dump model.

## Gym-side implications you can rely on

- Per-trainer `memberLevels[i]` scaling is already applied in the dumped
  parties — the dex+level wire fields reflect the admin's final
  configuration.
- The gym keeps its old `BBS_FIGHT_REQUEST` per-trainer flow alive for
  backward compat. New challengers that speak `BBS_LADDER_REQUEST` get
  the bulk dump; old ones still work the legacy way.
- The gym's `ladders_[]` per-challenger ring (from
  `INCOMING_MMG_LADDER_SPEC.md`) is unused on the bulk path — the
  challenger doesn't need it because progress is local.

## Testing

1. T-Deck DM `mmg fight 1` to a bulk-capable gym → expect 2 reply
   packets (NAMES, then PARTIES) within ~1s on PRIVATE_APP.
2. Battle UI immediately starts trainer 1/5 with the correct trainer
   name in the header.
3. Win all 5 → `MM Gym cleared!` + single `BBS_FIGHT_RESULT(outcome=1)`
   sent to the gym.
4. Lose to trainer 3 → `BBS_FIGHT_RESULT(outcome=0)`, ladder ends.
5. Reboot the gym mid-run — challenger should still finish locally
   without errors.

## Open questions / optional polish

- **Empty trainer slot** (e.g. autoSlots[2] is somehow blank): the gym
  ships `monCount=0` for that slot. Challenger should treat as auto-win
  and advance immediately.
- **Cache key**: gym admin reconfigures? The bulk dump always reflects
  current state. If you want to detect "this dump is stale", we can add
  a 2-byte gym-config hash to the NAMES packet header — say the word.
- **Skip-to-leader**: not supported. Add a request flag if you want
  speed-runs.
