# MonsterMesh — PvP ELO, Double-Elim Tournaments, Live Gym Takeovers

Plan for a follow-up session. Three features that share one backbone: a
trusted per-node identity + a way to agree on battle outcomes across the mesh.

## Opening prompt for the takeover window

> Read `/Users/goatsandmonkeys/Documents/pokemesh/PLAN_PVP_ELO_TOURNAMENTS_GYMS.md`.
> Also skim `HANDOFF_LORD_v1.md` for repo/branch/flash conventions.
> Then give me:
> 1. A one-paragraph read of the plan.
> 2. Which of the three features (ELO, Double-Elim Tournaments, Gym Takeovers) you'd land first, and why.
> 3. Any assumption in the plan that doesn't survive contact with the code.
>
> Do not commit, push, or flash without confirmation. All work on branch
> `0.1.02-pre-alpha`, push to remote `monstermesh`.

## Repo anchors

- Module dir: `meshtastic-firmware/src/modules/monstermesh/`
- PvP primitives already present: `BattlePacket.h` (lobby beacon `0x40`,
  challenge/accept/result types), `MonsterMeshLobby.{h,cpp}`, live PvP command
  `mmt <name>` routed in `MonsterMeshTerminal.cpp::handleCommand`.
- Save: `LordSave` in `LordSave.h` has `reserved[128]` — use it for v1 before
  bumping to a dedicated `/monstermesh/pvp.dat`.
- Identity: `nodeDB->getNodeNum()` (uint32 node ID) is the canonical player
  handle; short names come from `NodeInfo`.

---

## Feature 1 — PvP ELO (foundation; build first)

Everything else depends on trusted match results. Nail this first.

### Data model

New file `/monstermesh/pvp.dat`:

```c
struct PvpRecord {           // 24 bytes
    uint16_t rating;         // single ELO, starts at 1000
    uint16_t wins;           // casual + tournament combined
    uint16_t losses;
    uint16_t draws;
    uint32_t lastMatchEpoch;
    uint16_t peakRating;
    uint16_t tourneysEntered; // stat only (not ELO)
    uint16_t tourneysWon;     // stat only (not ELO)
    uint16_t _pad;
};

struct PvpOpponentCounter {  // 8 bytes — anti-farming
    uint32_t nodeNum;
    uint8_t  matchesToday;
    uint8_t  _pad[3];
};

struct PvpSave {             // ~256 bytes, pad to 512
    uint32_t magic;          // 'PVP1'
    uint8_t  version;        // 1
    uint8_t  _pad[3];
    PvpRecord self;
    uint32_t nextResetEpoch; // daily rollover same 9am-local convention
    PvpOpponentCounter recent[8];
    uint8_t  reserved[128];
};
```

### ELO math

- K-factor 32 for first 30 games (provisional), 16 after.
- `expected = 1 / (1 + 10^((oppRating - selfRating) / 400))`
- `newRating = rating + K * (score - expected)` where score ∈ {1, 0.5, 0}.

### Anti-farming

- Track matches-per-opponent-per-day in `recent[8]` ring.
- 3rd+ win vs same opponent same day: half ELO, logged in news but not full
  value. 5th+: no ELO change. Losses always count (you can't cheese by
  losing to a friend).
- Forfeit (`quit` mid-battle): counts as a loss with K doubled one-way
  (the forfeiter eats the full hit; the opponent gets normal win value).
- **Tournament matches are exempt** from the daily-cap anti-farming rule
  (you can only meet someone in a bracket via the pairing algorithm, so
  there's no farming surface). They still count toward `wins`/`losses`
  and move regular `rating`.

### Match-type flag

`BATTLE_RESULT` gains a `matchType` field so the reconciler knows which
anti-farming rules to apply. Every type moves the **same single `rating`**
with the **same K-factor** — ELO is uniform. The flag only gates the
daily farm-cap and informs the news event.

```c
enum MatchType : uint8_t {
    MATCH_CASUAL        = 0,  // mmt/mml ad-hoc PvP
    MATCH_TOURNEY_WB    = 1,  // winners bracket
    MATCH_TOURNEY_LB    = 2,  // losers bracket
    MATCH_TOURNEY_GF    = 3,  // grand final
    MATCH_GYM_DEFENSE   = 4,  // gym takeover challenge (Feature 3)
};
```

### One rating, applied fairly

The design goal: a single `rating` number that tracks PvP skill
regardless of whether a match was casual or happened inside a tournament.
"Fair" here means three things:

1. **Same formula, same K.** Every battle-engine result moves `rating` by
   `K * (score - expected)` with `score ∈ {1, 0.5, 0}` and
   `expected = 1 / (1 + 10^((oppRating - selfRating) / 400))`. K is
   provisional-vs-seasoned only (K=32 for first 30 games, K=16 after).
   Tournament matches use the same K as casual matches. No secret
   multiplier.
2. **No flat "trophy" bonuses.** Winning a tournament gives you no ELO
   you didn't earn by beating the opponents in front of you. A good
   tournament run naturally pumps `rating` — that IS the reward. Flat
   bonuses break the fairness property.
3. **Anti-farming is scoped correctly.** Casual matches can be
   spam-played against the same friend — the daily-cap rule (3rd win
   same day = half ELO, 5th+ = zero) blocks that. Tournament matches
   are exempt because the bracket pairing decides opponents; you can't
   choose to farm someone in a tournament. Losses always count in both
   modes.

Stat-only counters (no ELO impact): `tourneysEntered` increments on
start, `tourneysWon` increments on `TOURNEY_END` winner. These feed the
leaderboard alongside `rating` but never add to it.

### Gym-defense exception

Gym-defense matches (Feature 3) are the one place I'd deviate from the
uniform-K rule, and only mildly: leader K=16, challenger K=32. The
asymmetry discourages holding a gym just to ELO-farm challengers, and
rewards successful takeovers. This is the only asymmetric K in the
system. If it feels like too much of a carve-out in practice, unify it
to K=16/K=16 and revisit.

### Leaderboards

Terminal commands:
- `rating` — show your `rating`, W/L, peak, tournaments entered/won.
- `rating recent` — last 8 opponents from `recent[8]` with rating deltas.
- `rating top` — locally cached top 8 by `rating` from heard heartbeats.

Heartbeats (every ~6h) broadcast `{ nodeNum, rating, peakRating,
tourneysWon, wins, losses }` so each node can compute a global view
from what it's heard. No central server.

### Result protocol — the hard part

Both nodes must agree on who won. Current `mmt` already exchanges moves
over mesh; extend it:

1. When `Gen1BattleEngine::result()` returns non-PENDING on either side,
   that node sends `BATTLE_RESULT { matchId, winnerNodeNum, loserNodeNum,
   turnCount, finalRngState }` on port `MMESH_APP`.
2. Both nodes independently compute their own result and send it.
3. Reconciler (in `MonsterMeshLobby`): if both reports agree → apply ELO
   on both sides. If they disagree or one is missing for >30s → log as
   "disputed", no ELO change, news entry tagged `PVP_DISPUTED`.
4. MatchId = sha32(min(nodeA,nodeB) || max(nodeA,nodeB) || startEpoch) —
   both sides can compute it without extra traffic.

Determinism aside: the battle engine is already deterministic (xoshiro), so
the `finalRngState` check gives a free integrity hash of the whole battle.
Mismatch = someone tampered. Log and skip ELO.

### Files

- New: `PvpSave.{h,cpp}` (FSCom mirror of `LordSave`).
- New: `PvpElo.{h,cpp}` (pure-function math + reconciler).
- Modify: `MonsterMeshLobby.cpp` — extend result handshake.
- Modify: `MonsterMeshTerminal.cpp` — `rating`, `pvp stats`, `pvp recent`
  commands; show rating delta at end-of-battle for `mmt`/`mml`.

### Effort: ~2 evenings. Test path: two T-Deck devices, verify dispute/agree
paths, check ELO math vs a spreadsheet.

---

## Feature 2 — Double-Elimination Tournaments (with late registration)

Two brackets: **Winners (WB)** and **Losers (LB)**. First loss drops you to
LB. Second loss eliminates. LB winner meets WB winner in the Grand Final.
Late registration is supported — drop-ins join into the losers bracket so
the winners bracket stays clean.

### Architecture

One node is the **tournament host** (`tourney host [maxPlayers]`). Other
nodes `tourney join <hostName>`. Host owns bracket state and broadcasts it;
participants are state machines that react to broadcasts.

If host drops: any participant runs `tourney recover` and takes over from
their local cached state. Lowest `nodeNum` wins host election.

### Packet set (extend `BattlePacket.h`)

```c
TOURNEY_CREATE     = 0x50   // host → broadcast: { tourneyId, maxPlayers, format=DOUBLE_ELIM,
                            //                     transportProfile, regOpen, lateRegClosesRound }
TOURNEY_JOIN       = 0x51   // player → host:    { tourneyId, nodeNum, shortName, rating }
TOURNEY_JOIN_ACK   = 0x52   // host → player:    { tourneyId, bracketEntry, slotIdx, accepted }
TOURNEY_START      = 0x53   // host → broadcast: { tourneyId, playerCount, wbRoundsTotal, crc }
TOURNEY_PAIRING    = 0x54   // host → broadcast: { tourneyId, round, bracket, pairs[] }
TOURNEY_RESULT     = 0x55   // player → host:    { tourneyId, round, bracket, winnerNodeNum, matchId }
TOURNEY_BRACKET    = 0x56   // host → broadcast: { tourneyId, round, wbStandings[], lbStandings[] }
TOURNEY_END        = 0x57   // host → broadcast: { tourneyId, winnerNodeNum, runnerUpNodeNum,
                            //                     finalStandings[] }
```

IDs are `uint16`. TourneyId = `(hostNodeNum ^ startEpoch) & 0xFFFF`.
`bracket` field: `0 = WB, 1 = LB, 2 = Grand Final`.

### Bracket shape

For N players (cap 16), a double-elim tournament runs:
- **WB**: `ceil(log2(N))` rounds (e.g. 4 rounds for 16 players).
- **LB**: `2 * ceil(log2(N)) - 1` rounds (e.g. 7 rounds for 16).
- **Grand Final**: WB champion vs LB champion. One match (simple), or
  best-of-two if LB winner takes the first ("true double-elim"). Default
  to **simple** — one match decides it. A best-of-two grand final can be
  a v2 toggle.

When N isn't a power of two: top-seeded players get byes in WB round 1.
Byes never happen in LB (late joiners absorb odd numbers there — see below).

### Pairing algorithm

- **Round 1 WB**: seed by ELO (pulled from each player's `PvpSave.self.rating`
  via `TOURNEY_JOIN`). 1v16, 2v15, etc.
- **Subsequent WB rounds**: winners from the previous WB round pair by
  seed-preserving bracket (standard tournament progression).
- **LB rounds**: alternate between "new drops" rounds (WB losers meet LB
  survivors) and "LB-only" rounds (LB survivors play each other). Standard
  double-elim ordering — see `LB_PAIRING_TABLE` below.
- **Grand Final**: WB champ vs LB champ.
- **Tiebreaker for seeding ties**: rating → join order → `nodeNum`.

LB pairing follows the canonical double-elim schedule — implement the
standard "WB losers drop into LB at round R_drop(R_wb)" table rather than
computing it on the fly.

### Late registration

Drop-ins are **allowed until the end of WB round 1** (or a host-configurable
`lateRegClosesRound`, default 1). After that, the bracket is locked.

Late joiners enter the **losers bracket** at the earliest LB round that
hasn't started yet:
- They get a notional "WB bye + WB round-1 loss" on their record — so they
  have no wins but sit in LB with everyone else who lost round 1.
- If more than one late joiner arrives for the same LB slot, they play each
  other (good) rather than an existing LB participant (keeps the bracket
  from being "diluted" by drop-ins).
- Their seed for LB pairing = their ELO at join time. Fresh players
  (provisional rating) seed at the bottom.
- Late joiners are **capped at `maxPlayers`** total (including already-
  registered players). If the bracket is full, they get `TOURNEY_JOIN_ACK`
  with `accepted=false`.

Why LB and not WB: adding a player to WB after round 1 requires re-pairing,
which invalidates broadcasts participants have already acted on. LB absorbs
new entrants cleanly because LB is where you go after one loss anyway —
a late joiner is conceptually "a player who missed round 1 and is treated
as having lost it."

### Round flow

1. Host broadcasts `TOURNEY_PAIRING` for round R in the current bracket
   (WB or LB). Each payload row: `{ matchId, playerA, playerB, bracket,
   deadline }`.
2. Each paired player's terminal shows "WB Round R: vs <shortName>" or
   "LB Round R: vs <shortName>". They initiate a normal `mmt` battle; the
   result reconciler from Feature 1 is what the host listens to.
3. On `BATTLE_RESULT` agreement, host records winner → emits
   `TOURNEY_BRACKET` update.
4. Loser path: WB loser → `TOURNEY_PAIRING` for their LB slot next round.
   LB loser → eliminated, news event `TOURNEY_ELIMINATED`.
5. Round deadline (default 15 min): unfinished match → both get a loss
   (both drop one bracket), or if one reported ready and the other didn't,
   the reporting player advances.
6. After Grand Final, `TOURNEY_END` with winner, runner-up, and full
   bracket history. `TOURNEY_WIN` news event for the winner.
   `tourneysEntered` ticks for every participant; `tourneysWon` ticks
   for the winner. **No flat ELO bonus** — the winner's `rating` already
   reflects the run they just did. See Feature 1 §"One rating, applied
   fairly" for why.

Every tournament match moves the single `rating` using the same formula
and K-factor as a casual PvP match. The only difference is that
tournament matches are exempt from the daily farm-cap (you can't choose
to farm someone in a bracket). `BATTLE_RESULT` carries a `matchType`
field so the reconciler on each side applies the right anti-farming
rule. See Feature 1 §"Match-type flag" and §"One rating, applied fairly".

### Files

- New: `TournamentHost.{h,cpp}` — state machine, double-elim pairing,
  late-reg queue.
- New: `TournamentClient.{h,cpp}` — non-host participants.
- New: `TournamentBracket.{h,cpp}` — bracket math, pairing tables,
  seed-preserving progression. Pure functions, easy to unit test.
- Modify: `BattlePacket.h` — packet types above.
- Modify: `MonsterMeshTerminal.cpp` — commands: `tourney host [max]`,
  `tourney join <host>`, `tourney status`, `tourney bracket`,
  `tourney drop`.
- Modify: `MonsterMeshLobby.cpp` — route the new packet types.

### Transport profiles

Bandwidth and round pacing depend heavily on transport. The host declares
which profile a tournament is running under on `TOURNEY_CREATE`; clients
use it to size timeouts and warn the user about feasibility.

| Profile           | Transport                | On-air rate | Scope        | Default `maxPlayers` | Round timeout |
|-------------------|--------------------------|-------------|--------------|----------------------|---------------|
| `LOCAL_LONGFAST`  | LoRa LongFast            | ~1.1 kbps   | RF range     | 16                   | 15 min |
| `LOCAL_SHORTTURBO`| LoRa ShortTurbo          | ~10.9 kbps  | RF range     | 32                   | 10 min |
| `MQTT_PRIVATE`    | MQTT via private channel | internet    | global       | 64                   | 30 min |
| `MQTT_PUBLIC`     | MQTT via default broker  | internet    | global (noisy)| 32                  | 30 min |

Notes:
- **MQTT** has no meaningful airtime budget, but round timeouts get longer
  because participants are spread across timezones and may not be actively
  watching.
- **ShortTurbo + private channel** is the recommended "in-person event"
  mode — 10× LongFast bandwidth, plus a fresh PSK keeps the tournament
  off the default channel where casual Meshtastic users would see battle
  packet spam.
- Default profile is `LOCAL_LONGFAST` (the safe fallback). Host overrides
  with `tourney host <max> <profile>`.

### Hard questions to decide before coding

- **Scale.** `maxPlayers` is a host-configurable field, not a hardcoded
  cap. Defaults come from the transport profile table above. Hard ceiling
  32 for LoRa profiles (double-elim with 32 = 62 matches = a real session).
  Hard ceiling 64 for MQTT profiles. Bracket code doesn't care — it's a
  policy knob.
- **Async vs sync rounds.** Start sync (all matches in round R finish
  before round R+1 fires). Async double-elim is a v2 feature — the
  pairing logic gets much harder when rounds overlap.
- **Late-reg cutoff.** Default: end of WB round 1. Making it
  configurable on `TOURNEY_CREATE` is cheap; actually testing cutoffs >1
  is where the edge cases live.
- **Grand-final format.** Ship simple (one match decides) in v1. Add
  true-double-elim (LB winner must beat WB winner twice) as a later
  toggle; don't try to do both at once.
- **Mesh partitions.** Host disappears → manual recovery via
  `tourney recover`. Document; don't auto-elect unless two sessions prove
  it's needed.
- **Player drops mid-tournament.** Treat as forfeit of all remaining
  matches — each forfeit is a loss in their current bracket, which
  propagates them to LB and eventually eliminates them. No special path.

### Effort: ~5 evenings (double-elim pairing + late-reg makes it ~1 evening
more than Swiss). Needs 4+ devices to cover the late-reg + bracket paths
meaningfully.

---

## Feature 3 — Live Gym Takeovers (builds on ELO + LORD)

Today: `gym N` fights a hardcoded `LORD_GYMS[N]` roster. After takeovers:
`gym N` fights *the current human leader's party* if a leader exists and is
reachable; falls back to hardcoded roster otherwise.

### Data model — piggyback on LordSave

Add to the existing `LordSave.reserved[128]`:

```c
struct GymLeaderEntry {                // 60 bytes
    uint32_t nodeNum;                  // 0 = NPC (use hardcoded roster)
    char     shortName[12];            // from NodeInfo at takeover time
    uint32_t claimedEpoch;
    uint32_t lastDefendedEpoch;
    uint16_t defenseCount;
    uint16_t rating;                   // ELO snapshot at claim time
    Gen1Pokemon party[3];              // the 3 mons they chose — NOT full 44 bytes
                                       //   — just species/level/moves/dvs (≈10 bytes × 3)
    uint8_t  _pad[N];
};

GymLeaderEntry gymLeaders[8];          // 8 × 60 = 480 bytes
```

That doesn't fit in 128 bytes of reserved space — so bump `LordSave` to a
v2 schema and grow `reserved` to 768 bytes, or split `gym_leaders.dat`.
**Recommendation: split file.** Keeps `lord.dat` stable; new file is
broadcast-cacheable (see sync below).

### Claim flow

1. Player clears `gym N` leader fight (existing code path).
2. Terminal prompts: "Claim Pewter Gym? Pick 3 mons from your party."
3. On confirm: write `gym_leaders.dat[N] = {self nodeNum, party snapshot}`.
4. Broadcast `GYM_CLAIM { gymIdx, nodeNum, shortName, claimedEpoch,
   rating, party[3] }` on the LORD news channel.
5. Other nodes receive and cache locally.

### Challenge flow

1. Another node runs `gym N`. Terminal looks up cached leader:
   - Leader is **self** → "You hold this gym. Use `gym defend preview` to
     view your roster."
   - Leader is **known + heard recently (<7 days)** → battle their 3-mon
     party.
   - Leader is **stale / unknown** → fall back to hardcoded NPC roster.
2. Challenger wins → broadcast `GYM_CHALLENGE_RESULT { gymIdx,
   newLeaderNodeNum, oldLeaderNodeNum, matchId, finalRngState }`.
3. Both the old leader and the winner update their local cache; other
   nodes converge on the next broadcast.
4. ELO: leader win = +K_leader (smaller than normal, say K=12 — held gym
   shouldn't farm ELO). Challenger win = normal K. Losing a gym costs
   the old leader prestige points (new stat) but not raw ELO.

### Offline handling

- "Heard recently" = we've seen a NodeInfo or LORD news packet from that
  node in the last 7 days (use Meshtastic's existing `NodeInfo.last_heard`).
- Stale leader → NPC roster used in battle, but the leader's name still
  shows on the `gym list` screen with a `[stale]` tag.
- When a stale leader comes back online, they can `gym reclaim N` if no
  one has taken it — no rebattle needed.

### Prestige — new stat, cheap to add

`PvpSave.self.prestige` (uint16). +10 per gym claim, +2 per successful
defense, -5 per lost gym. Purely cosmetic leaderboard. Goes alongside ELO,
not instead of it — prestige rewards *holding*, ELO rewards *winning*.

### Sync — keep it simple

- Leaders re-broadcast `GYM_CLAIM` every 6h (heartbeat).
- Challengers broadcast `GYM_CHALLENGE_RESULT` on completion.
- Every node's local cache = most recent heartbeat or result per gym.
- Conflict (two nodes think they're leader): higher `claimedEpoch` wins.
  If tied (shouldn't happen, but clocks): higher `rating` at claim time.

### Files

- New: `GymLeaders.{h,cpp}` — cache, broadcast, conflict resolution.
- Modify: `LordGyms.cpp` — `lordBuildGymParty` gains a "live leader"
  branch that reads from the cache before the hardcoded roster.
- Modify: `MonsterMeshTerminal.cpp` — claim prompt after leader victory,
  `gym defend preview`, `gym leaders` (list of all 8 with current holders).
- Modify: `BattlePacket.h` — `GYM_CLAIM`, `GYM_CHALLENGE_RESULT`.

### Effort: ~3 evenings after ELO lands.

---

## Build order (recommended)

1. **ELO + result reconciler** (Feature 1) — everything else leans on it.
   Ship it, play a few PvP matches with a friend, tune K and anti-farming.
2. **Gym Takeovers** (Feature 3) — smallest win after ELO, immediately
   visible social layer without needing multiple people online at once.
3. **Double-Elim Tournaments** (Feature 2) — last; needs 4+ devices to
   test properly and has the most moving parts.

## Hard don'ts

- **Do not** reuse the matchmaking module from mesh_bbs project — this
  firmware's wire format is `BattlePacket.h` (see handoff memory
  `feedback_monstermesh_wire_format.md`). Extend, don't fork.
- **Do not** centralize the leaderboard on one node. Each node stores its
  own `PvpSave`; ranking views are computed from broadcast heartbeats,
  not queried from a server.
- **Do not** skip the `finalRngState` integrity check on PvP results —
  it's one `uint64` on the wire and prevents a whole class of cheating.
- **Do not** bloat `LordSave` past 512 bytes. Add new files
  (`pvp.dat`, `gym_leaders.dat`) instead.

## Verification checklist

- ELO: two devices, 10 casual matches, spreadsheet-compare `rating`.
  Farm-detection kicks in on 3rd same-opponent win.
- Tournament ELO: 4-device double-elim. Confirm per-match `rating`
  moves use the same K as casual. Confirm NO flat bonus applied on
  `TOURNEY_END` (only `tourneysWon++`). Confirm farm-cap does NOT
  trigger on tournament matches when the same two players meet twice
  (WB→LB→GF).
- Tournament: 4-device double-elim. Verify WB→LB drop, LB elimination
  on second loss, late-join lands in LB not WB, bracket-full rejects new
  joiners, Grand Final advances the correct two players, host
  power-cycle → `tourney recover` resumes from last cached bracket.
- Transport smoke tests:
  - `LOCAL_LONGFAST` with 4 devices — baseline.
  - `LOCAL_SHORTTURBO` on a private channel with 8+ devices at an
    in-person event — verify all participants swap to the same PSK before
    `TOURNEY_CREATE` fires. Channel config is a pre-req, not something
    the tournament code can set.
  - `MQTT_PRIVATE` with 2 devices + MQTT gateway — verify round timeouts
    are sized for internet latency, not RF latency.
- Gym takeovers: clear Pewter on device A, challenge from device B, verify
  B fights A's real party (not NPC). Power A off 8 days, challenge → NPC
  fallback.
