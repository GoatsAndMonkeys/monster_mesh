# Gauntlet — Testing & Iteration Guide

A self-contained playbook for activating, smoke-testing, and incrementally
expanding the multiplayer Pokémon Gym gauntlet on the Heltec V3 (Pocket
Pikachu) node. Each phase is independent — if you stop after Phase 2 you
have a working basic gym; later phases can be tackled across multiple
sessions.

> **Workbench checklist:** Heltec V3 connected via USB, second Meshtastic
> node within range (any device that can DM), `pio` in your `PATH`, optional
> mosquitto client on your laptop for Phase 5.

---

## Phase 0 — Activate the module (one edit, ~5 min)

### What to change

In `src/modules/Modules.cpp`, **two additive blocks** matching the existing
MonsterMesh pattern:

```cpp
// 1. Near the include block, after the existing MonsterMesh include:
#if !MESHTASTIC_EXCLUDE_GAUNTLET
#include "modules/monstermesh/gauntlet/GauntletModule.h"
#endif

// 2. Inside setupModules(), after the existing MonsterMesh setup:
#if !MESHTASTIC_EXCLUDE_GAUNTLET
    gauntletModule = new GauntletModule();
#endif
```

Compile-time disable: `-DMESHTASTIC_EXCLUDE_GAUNTLET=1` removes everything.

### Build + flash

```bash
cd ~/Documents/pokemesh/meshtastic-firmware
~/meshtastic-venv/bin/pio run -e heltec-v3
~/meshtastic-venv/bin/pio run -e heltec-v3 -t upload
```

Expected: `SUCCESS`, ~2.05 MB flash (61% of 3.34 MB), ~125 KB RAM.

### Boot verification

Open serial monitor (`pio device monitor -e heltec-v3`). Within a few
seconds of boot, look for this line:

```
Gauntlet: Pallet Gym [Boulder Badge] loaded — leader=(none) roster=0 prev=0
```

If you see it, the module is alive. If not — see **Failure Modes** at the
bottom.

---

## Phase 1 — Smoke test (4 DMs, ~5 min)

From a second Meshtastic node, DM the heltec-v3. Replies should arrive
within ~5–15 seconds depending on mesh hops.

| You DM                     | Expected reply (verbatim modulo your gym name) |
|----------------------------|-------------------------------------------------|
| `!gym`                     | `Pallet Gym [Boulder Badge]`<br>`Leader: (open)`<br>`Roster: 0  Prev: 0  Battles: 0`<br>`Send '!gym challenge' to fight.` |
| `!gym help`                | Multiline command list |
| `!gym leader`              | `No gym leader yet — challenge and claim the title!` (older code) or `No leader yet — claim the title!` |
| `!gym profile`             | `No profile yet — challenge the gym!` |
| `!gym garbage`             | `Unknown command. Try '!gym help'` |

**Pass criteria:** every command gets a reply within 30 s.

---

## Phase 2 — Run a real gauntlet (~15 min, single device works)

```
You: !gym challenge
Gym: Send your party (up to 6, comma-separated):
     Pikachu,Charizard:60,Blastoise
     Default level: 50. Names or dex# accepted.

You: Pikachu,Charizard,Blastoise
Gym: Party (3): PIKACHU,CHARIZARD,BLASTOISE
     Gauntlet starting...

Gym: [E4 1/4 Lorelei] vs Lorelei: WIN (NT, X-Y)
Gym: [E4 2/4 Bruno]   vs Bruno:   WIN …
Gym: [E4 3/4 Agatha]  vs Agatha:  WIN …
Gym: [E4 4/4 Lance]   vs Lance:   WIN …
Gym: Elite Four cleared! Now the gauntlet ladder...
Gym: CHAMPION! <yourname> is the new Pallet Gym leader.
     Boulder Badge Badge earned. Party: PIKACHU,CHARIZARD,BLASTOISE
```

**Notes:**
- Outcomes are deterministic from `(party_a, party_b, rngSeed)`. The
  `T`/`X-Y` numbers (turns, survivors) will vary by seed but a given
  challenger party always faces the same E4 teams.
- If you lose to an E4 member, the gauntlet ends there. Try a stronger
  party (e.g. `Mewtwo,Dragonite,Snorlax:80`).
- If there's already a leader, you fight through the ranked roster
  bottom-up before facing the leader. Lose at slot N → take slot N.

**Pass criteria:** at least one full champion run completes.

### Verify side effects

| Command          | Expected after a champion run                    |
|------------------|--------------------------------------------------|
| `!gym leader`    | Your name + party CSV                            |
| `!gym profile`   | `Chall: 1  W: 1  L: 0`, `Best rank: LEADER`, `Titles: 1` |
| `!gym roster`    | `LEADER <you>: <party>` (roster empty after a sweep) |

---

## Phase 3 — Persistence test (~5 min)

1. Note your champion record from Phase 2.
2. Power-cycle the heltec-v3 (or `pio run -t reset`).
3. Wait for the boot log line.
4. DM `!gym leader` from your test node.

**Pass criteria:** your champion name and party are still there. The state
file lives at `/monstermesh/gauntlet.dat` on the device's LittleFS.

---

## Phase 4 — Multi-player ladder (~10 min, needs 2nd device)

From a second device, send a weaker party:

```
Device B: !gym challenge
Device B: Caterpie,Magikarp,Weedle
```

Expected: Device B loses somewhere on the E4 OR makes it to the leader and
loses there. If they reach the leader fight and lose, they take rank #1
of the ranked roster.

From any device, DM `!gym roster` — should show your defended title +
Device B as Rank #1.

If you challenge again from Device A and lose to your old champion party
(the leader), you'd take rank #2 and Device B shifts to rank #2…actually,
since you ARE the leader, your DMs that lose at the ladder/leader stage
just bump your own roster placement. Use a third device for clean
multi-trainer ladder testing.

**Pass criteria:** roster size > 0 after a non-champion run; `!gym
records` populates after the leader gets dethroned.

---

## Phase 5 — MQTT cross-gym dashboard (optional, ~15 min)

Requires Meshtastic MQTT to be **enabled** in your node's config. The
gauntlet does not start its own broker connection — it publishes through
Meshtastic's existing `mqtt` global.

### Subscribe from your laptop

```bash
mosquitto_sub -h <your-broker> -t 'msh-gym/+/state' -v
```

After a state change (challenge / leader change / roster shift), you
should see one retained message:

```json
{
  "gym": "Pallet Gym",
  "badge": "Boulder Badge",
  "node": 305419896,
  "leader": "ABCD",
  "party": "PIKACHU,CHARIZARD,BLASTOISE",
  "since": 1714935600,
  "roster": 0,
  "prev": 0,
  "challenges": 1,
  "battles": 7,
  "ts": 1714935600
}
```

Non-retained `event` messages on `msh-gym/+/event` carry per-event deltas
(`leader_change`, `roster`).

### Pass criteria

A subscribed client receives a `state` JSON within seconds of any
`!gym challenge` outcome.

### Troubleshooting

If nothing arrives:
- Confirm `mqtt->isConnectedDirectly()` is true at the Meshtastic level
  (the gauntlet's publish calls early-return otherwise).
- Verify your broker host / TLS / username match your Meshtastic config.
- The gauntlet doesn't subscribe to anything — it's publish-only by
  design. Aggregation happens at the broker.

---

## Phase 6 — BBS log file inspection (optional, ~5 min)

After running a few challenges, dump the device's filesystem:

```bash
# via pio + serial: read /monstermesh/gauntlet_log/records.jsonl
# or hook up via LittleFS over USB if your build supports it
```

Each line is a self-contained JSON object:

```jsonl
{"type":"leader","ts":1714935600,"gym":"Pallet Gym","badge":"Boulder Badge","node":305419896,"name":"ABCD","party":"PIKACHU,CHARIZARD"}
{"type":"roster","ts":1714935700,"gym":"Pallet Gym","node":2271560481,"name":"WXYZ","rank":1}
{"type":"msg","ts":1714935800,"node":305419896,"name":"ABCD","text":"GG everyone!"}
```

Files cap at 32 KB and rotate (oldest data dropped). For durable history,
rely on MQTT (Phase 5).

`!gym msg <text>` posts to the message log without affecting state.

---

## Phase 7 — OLED status frame (optional, ~10 min, do NOT install on the Pikachu node)

> **Warning:** Pocket Pikachu owns the display permanently per your
> `CLAUDE.md`. Don't register the gauntlet frame on the same physical
> board — the two will fight over the screen.

For a separate kiosk node:

```cpp
// In src/graphics/Screen.cpp, alongside the other frame entries:
#include "modules/monstermesh/gauntlet/GauntletScreen.h"
// ...
{ GauntletScreen::drawFrame, FOCUS_DEFAULT },
```

Layout (128×64): gym name at top, divider, then `Ldr: NAME`, `Rank: N`,
`Battles: NN`, `MQTT: online/offline`, badge name in bottom-right.

---

## Phase 8 — Custom Elite Four / movesets (optional, ~20 min)

The hardcoded E4 (Lorelei / Bruno / Agatha / Lance, all canonical Gen 1)
and per-type default movesets live in `GauntletBattle.cpp`. To override
without editing C++:

```bash
cd ~/Documents/pokemesh/meshtastic-firmware
cp data/gauntlet.example.json data/gauntlet.json
# edit data/gauntlet.json — keep the schema, change parties/movesets
python3 scripts/gen_gauntlet.py data/gauntlet.json \
    -o src/modules/monstermesh/gauntlet/GauntletDataPacked.h
```

Then in `GauntletBattle.cpp`, near the top:

```cpp
#include "GauntletDataPacked.h"
// then in the gauntletBuildE4Party / pickDefaultMoves bodies, swap the
// hardcoded E4_TABLE / DEFAULT_MOVES references to the GAUNTLET_E4_PACKED
// / GAUNTLET_DEFAULT_MOVES_PACKED arrays. Both packed versions match the
// shapes of the originals.
```

Rebuild + flash. Verify with `!gym challenge` against a known party — the
fight outcome should change once the E4 teams differ.

> Move IDs come from `showdown_gen1_moves.h` (1–165). Dex numbers are
> 1–151 (Gen 1 only). Validation happens in `gen_gauntlet.py`.

---

## Failure modes — quick crib

| Symptom                                    | Most likely cause                                                                 | Fix                                                                                          |
|--------------------------------------------|-----------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------|
| `!gym` reply never arrives                 | Module not constructed                                                            | Confirm `gauntletModule = new GauntletModule()` is in `setupModules()`. Check serial boot log. |
| Build link error: `undefined reference to GauntletModule` | `+<modules/monstermesh/gauntlet/>` was stripped from `heltec_v3/platformio.ini`   | Re-add it under `build_src_filter`. The global `arduino_base.ini` excludes the dir.          |
| Replies arrive but state doesn't persist   | FSCom not writing `/monstermesh/gauntlet.dat`                                     | Check serial for FS errors. Try `pio run -t erase` then re-flash to reset filesystem.        |
| Battle outcome looks wrong / always 0-X    | Default movesets too weak                                                         | Use Phase 8 to plug in custom moves, or specify levels (`Pikachu:80`).                       |
| MQTT never publishes                       | Meshtastic MQTT module not connected, or your broker config mismatched            | `!gym` boot still works — confirm Meshtastic side first via standard MQTT diagnostics.       |
| Flash bloat after activation               | All optional features (MQTT, profile, BBS, screen) compile in by default          | `-DMESHTASTIC_EXCLUDE_GAUNTLET=1` to remove all of it. ~30 KB delta.                          |

---

## Rollback recipe

If anything goes wrong and you want to restore the firmware to its
pre-activation state:

1. Remove the two blocks added to `src/modules/Modules.cpp` in Phase 0.
2. (Optional) Remove `+<modules/monstermesh/gauntlet/>` from
   `variants/esp32s3/heltec_v3/platformio.ini` to also stop compiling
   the gauntlet code.
3. (Optional) Remove the T_DECK-only-file exclusions in the same file
   only if you specifically want them back — but they fix an existing
   build bug for heltec-v3, so leaving them is recommended.
4. `pio run -e heltec-v3` to rebuild without the module.

The `arch/arduino_base.ini` global exclusion remains untouched throughout,
so other env's builds were never affected.

---

## DM command reference (for the players)

| Command                     | Effect                                            |
|-----------------------------|---------------------------------------------------|
| `!gym`                      | Show gym info, current leader, roster size        |
| `!gym challenge`            | Begin challenge — module prompts for party CSV    |
| `<Pokemon,Pokemon:60,...>`  | Submit party (after `!gym challenge`)             |
| `!gym roster`               | Top 5 ranked trainers + leader                    |
| `!gym leader`               | Current leader's full party                       |
| `!gym records`              | Hall of past leaders                              |
| `!gym profile`              | Sender's win/loss + best rank                     |
| `!gym msg <text>`           | Post to gym message board (LittleFS log)          |
| `!gym help`                 | Command list                                      |

---

## On-device file layout (after running for a while)

```
/monstermesh/
├── gauntlet.dat                      Single GauntletState record (atomic rewrite)
├── gauntlet_log/
│   ├── records.jsonl                 leader_change / roster events (≤32 KB)
│   └── messages.jsonl                player message board (≤32 KB)
└── gauntlet_profiles/
    ├── 12abcdef.bin                  Per-node GauntletProfile records
    └── …
```

All capped to keep flash usage bounded. MQTT carries the durable feed for
off-device aggregation.

---

## Suggested test sequence on the workbench

For a 90-minute session at home:

```
0:00 — Add Modules.cpp blocks, build, flash, watch boot log         (Phase 0)
0:10 — Smoke-test 4 DMs                                              (Phase 1)
0:20 — Full champion run                                             (Phase 2)
0:35 — Reboot + persistence check                                    (Phase 3)
0:45 — Two-device ladder test                                        (Phase 4)
1:00 — (optional) MQTT subscriber + observe state changes            (Phase 5)
1:15 — (optional) Pull JSONL logs over serial                        (Phase 6)
1:25 — Note any failures, refer to crib above                        (debug)
```

Phases 7 and 8 are best done in a separate session — they involve Screen
wiring or rebuild cycles after JSON edits.
