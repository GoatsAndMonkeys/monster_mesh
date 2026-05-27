# Gauntlet — Integration Guide

How to wire the multiplayer Pokémon Gym gauntlet into the rest of the
firmware. Every piece is opt-in: the gauntlet directory compiles cleanly
with no other firmware changes required.

## File map

```
src/modules/monstermesh/gauntlet/
├── GauntletData.h        Trainer/state structs, GAUNTLET_* constants
├── GauntletStorage.{h,cpp}  FSCom save/load → /monstermesh/gauntlet.dat
├── GauntletBattle.{h,cpp}   Auto-resolve via Gen1BattleEngine; party CSV parse
├── GauntletProfile.{h,cpp}  Per-player records → /monstermesh/gauntlet_profiles/
├── GauntletMQTT.{h,cpp}     Publishes via Meshtastic's existing `mqtt` global
├── GauntletBBS.{h,cpp}      JSONL log + door-game stub for future TinyBBS
├── GauntletScreen.{h,cpp}   OLED carousel frame (opt-in, not auto-registered)
└── GauntletModule.{h,cpp}   Meshtastic SinglePortModule + DM state machine
```

## Required wiring

### 1. Instantiate the module

`src/modules/Modules.cpp`, in `setupModules()`:

```cpp
#include "modules/monstermesh/gauntlet/GauntletModule.h"
// ...
#if !MESHTASTIC_EXCLUDE_GAUNTLET
    gauntletModule = new GauntletModule();
#endif
```

That's it for the basics. The module:
- Listens on `TEXT_MESSAGE_APP` for DMs only (its `wantPacket` filters
  broadcasts and ignores non-DM traffic).
- Loads/saves state on every leader change, roster shift, or new
  challenger.
- Logs records to `/monstermesh/gauntlet_log/*.jsonl`.
- Publishes MQTT state on every change (no-op if MQTT not connected).

### 2. (optional) Customise the gym name + badge

The default constructor sets `Pallet Gym` / `Boulder Badge`. To change,
edit the constructor in `GauntletModule.cpp`:

```cpp
const char *defaultName  = "Cinnabar Gym";
const char *defaultBadge = "Volcano Badge";
```

…or override after construction:

```cpp
gauntletModule = new GauntletModule();
strncpy(gauntletModule->state_.gymName, "Cinnabar Gym",  GAUNTLET_NAME_MAX - 1);
```

(state_ is private — add a setter if you want runtime config.)

### 3. (optional) Disable for a specific build

```ini
build_flags = -DMESHTASTIC_EXCLUDE_GAUNTLET=1
```

The module is then compiled out entirely.

## Optional integrations

### OLED carousel frame

`GauntletScreen::drawFrame` is NOT auto-registered. Pocket Pikachu owns
the display on the primary build target. To register it on a different
node, in `Screen.cpp`'s frame array:

```cpp
#include "modules/monstermesh/gauntlet/GauntletScreen.h"
// ...
{ GauntletScreen::drawFrame, FOCUS_DEFAULT },
```

### TinyBBS door-game (future)

When TinyBBS lands in this firmware tree, route its games-menu 'Y'
selection to `gauntletBBSStartChallenge` and subsequent input to
`gauntletBBSHandleStep`. The current implementation just defers to DM-
based challenges; the BBS state machine can be filled in alongside the
TinyBBS port itself.

`GauntletBBS.h` defines `GAUNTLET_BBS_BOARD_RECORDS=4` and `_MESSAGES=5`
to match the BOARD_GYM/BOARD_GYM_MSG constants TinyBBS would add.

### MQTT cross-gym dashboard

Topics published (retained=`state`, non-retained=`event`):

```
msh-gym/<nodeHex>/state   — full gym snapshot (JSON)
msh-gym/<nodeHex>/event   — leader_change / roster (JSON envelope)
```

Subscribe with `msh-gym/+/state` from any external client to get a live
map of every gym node on the mesh. The default Meshtastic broker config
applies; no separate MQTT credentials needed.

Sample dashboard query:

```bash
mosquitto_sub -h <broker> -t 'msh-gym/+/state'
```

### Custom Elite Four / default movesets (gen_gauntlet.py)

`scripts/gen_gauntlet.py` packs a JSON spec into a header that overrides
the hardcoded `E4_TABLE` and `DEFAULT_MOVES` arrays in
`GauntletBattle.cpp`. Sample spec at
`data/gauntlet.json` (commit + regenerate if you want different teams).

```bash
python3 scripts/gen_gauntlet.py data/gauntlet.json \
    -o src/modules/monstermesh/gauntlet/GauntletDataPacked.h
```

To activate, add `#include "GauntletDataPacked.h"` near the top of
`GauntletBattle.cpp` and redirect E4_TABLE / DEFAULT_MOVES references
to the `GAUNTLET_*_PACKED` versions. (Not done by default to keep the
main code self-contained.)

## DM commands (player-facing)

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

## File-system layout

After running for a while, the on-device tree looks like:

```
/monstermesh/
├── gauntlet.dat                      Single GymState record (atomic rewrite)
├── gauntlet_log/
│   ├── records.jsonl                 leader_change / roster events
│   └── messages.jsonl                player message board
└── gauntlet_profiles/
    ├── 12abcdef.bin                  per-node GauntletProfile records
    └── …
```

All files are size-capped to keep flash bounded.

## Build notes

- Already builds clean for `heltec-v3` with the source filter exclusions in
  `variants/esp32s3/heltec_v3/platformio.ini` (which skips T-Deck-only UI
  cpp files in the parent monstermesh/ directory).
- Reuses existing `Gen1BattleEngine`, `showdown_gen1_*` data, `Gen1Party`,
  `dexToInternal[]`, and `gen1SpeciesName()` — no Pokémon-data
  duplication.
- Total flash impact (heltec-v3): ~30 KB for module + battle + storage +
  profile + MQTT + BBS layers combined.

## Testing without flashing

The DM flow is pure-text and entirely deterministic. To smoke-test:

1. Send `!gym` to the node — should reply with default `Pallet Gym
   [Boulder Badge]` info.
2. Send `!gym challenge` — node prompts for party.
3. Send `Pikachu,Charizard,Blastoise` — node runs the full Elite-Four +
   ranked-roster + leader gauntlet, sends one summary line per fight.
4. After running, `!gym profile` — should show 1 challenge logged.

Per-player profiles, MQTT publishes, and BBS log writes all happen
automatically as part of step 3.
