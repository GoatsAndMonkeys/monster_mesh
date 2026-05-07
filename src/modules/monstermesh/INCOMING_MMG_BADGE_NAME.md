# INCOMING — MMG gym should ship its badge name

> **For the gym/Gauntlet agent iterating on `gauntlet/GauntletModule.cpp`:**
> The challenger wants to display the gym's "badge name" (e.g. "Boulder",
> "Cascade") in the MM Gym discovery list and on the battle header. Right
> now the bulk-ladder protocol carries trainer names + parties but no
> badge string, so the T-Deck has to fall back to "MM Gym" everywhere.

## What the challenger wants

When the user runs `mmg` (gym discovery), each row in the discovered-gym
list should show the badge name alongside the gym + leader name, e.g.

```
1. Pewter [Boulder] ldr:Brock rank:5
```

The `BBS_REPLY` (0x71) packet already encodes the badge — that part works
today and is parsed by `MonsterMeshTerminal::onBbsReply`. The list looks
right after a probe.

The gap is in `BBS_LADDER_NAMES` (0x75): it ships per-trainer names but
no top-level badge for the gym itself. So the in-battle header reads:

```
MM Gym - Camper Liam 1/5
```

instead of:

```
Boulder - Camper Liam 1/5
```

## Ask

Add a single string to `BBS_LADDER_NAMES.payload[]` BEFORE the
`trainerCount` byte, length-prefixed:

```
u8  badgeLen          // 0..16
char badge[badgeLen]
u8  trainerCount      // = 5     (existing)
for each trainer:
    u8  nameLen
    char name[nameLen]
```

Total worst case: 17 + 1 + 5×17 = 103 bytes — still fits inside one
PRIVATE_APP packet.

If `badgeLen == 0` the challenger keeps using "MM Gym" as the prefix
(graceful default). Old gyms that don't ship the badge still work; the
challenger sniffs by checking whether the first byte's length is plausible
(0..16) AND the next 5 trainer-name fields parse cleanly. If anything
looks off, it falls back to legacy parsing without a badge.

## Wire-format guarantee

To make detection unambiguous and avoid the heuristic above, you can
instead bump `BBS_LADDER_NAMES`'s `seq` byte from 0 → 1 to signal the
new format:

- `seq=0` → legacy: `[trainerCount][nameLen, name]…`
- `seq=1` → new:    `[badgeLen, badge][trainerCount][nameLen, name]…`

Challenger picks the parser based on `seq`. No heuristics needed.

## Testing once both sides land

1. T-Deck `mmg` probe → list shows `[Boulder]` next to the gym row.
2. T-Deck `mmg fight 1` → battle header says `Boulder - Camper Liam 1/5`.
3. Gym admin runs `gym 1 badge Boulder` (or however badge is configured)
   → next ladder dump reflects the new badge.
4. Old gym firmware → challenger parses the legacy format (seq=0)
   correctly, header falls back to `MM Gym - Camper Liam 1/5`.

## Files the challenger will touch on its side

- `MonsterMeshModule.h` — add `bbsLadderBadge_[17]`.
- `MonsterMeshModule.cpp` — extend the `BBS_LADDER_NAMES` parser to
  read the new prefix when `seq=1`; use the badge in the battle
  `setHeader` and (eventually) the gym list rendering.

The `BBS_REPLY`-based `mmg` list already has badge data; that path
doesn't need a wire change. Only the bulk-ladder dump packet does.
