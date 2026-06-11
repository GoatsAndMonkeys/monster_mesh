# LORD v1 — Handoff Notes

Date: 2026-04-19. Session that built the door-game layer on top of MonsterMesh.

## Where things are right now

- **Repo**: `/Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware`
- **Branch**: `0.1.02-pre-alpha` (was created as `Legend-of-Charizard`, user renamed it mid-session)
- **Remote**: pushed to `monstermesh/0.1.02-pre-alpha` at
  https://github.com/GoatsAndMonkeys/monster_mesh/tree/0.1.02-pre-alpha
- **Head commit**: `d9aa1a24a feat(mm): Legend of Charizard (LORD) door-game v1`
- **Build**: `pio run -e t-deck-tft` passes. Flash 59.4%, RAM 45.0%, ~83s.
- **Already flashed** to a T-Deck at `10.1.2.193` via bmorcelli-Launcher OTA
  (POST `/OTA` with `command=0&size=N`, then multipart `/OTAFILE file1=...`).
  Credentials the user gave: `admin` / `launcher`. Device was rebooted after upload.

## What LORD v1 ships

All inside `src/modules/monstermesh/`:

- **LordSave.{h,cpp}** — persistence at `/monstermesh/lord.dat` using FSCom.
  Schema: magic, version, badges bitmap, exploreRunsToday, nextResetEpoch,
  totals, best-run stats, per-gym progress[8], 8-slot news ring, 128B reserved.
- **LordLogic.{h,cpp}** — daily reset at 9am-local (matches the Wordle/FRPG
  convention), `lordOnRunEnd`, `lordOnGymCleared`, badge/unlock helpers.
- **LordGyms.{h,cpp}** — hand-baked rosters for all 8 Kanto gyms
  (4 grunts + leader each). Species/level/moves use Showdown IDs verbatim.
  Future: drive this from `data/lord_gyms.json` via a Python packer (deferred).
- **MonsterMeshTerminal.{h,cpp}** — new states `IN_GYM_SELECT`, `IN_GYM_BATTLE`;
  commands `gym`, `gym N`, `explore`, `stats`, `badges`, `news`, `leaderboard`;
  `explore` gates one-run-per-day and forces wild-only encounters
  (`runWildOnly_`) — bypasses the mesh-peer pull in `buildWildOpponent`.
  `run` keeps its legacy mesh-peer behavior.

Reuses the existing `Gen1BattleEngine` and `initBattlePokeFromBase` verbatim —
no engine changes.

## What's intentionally NOT in v1 (per user scope)

- **PvP / challenge-another-node** — deferred. Mesh lobby packets already exist
  (`BattlePacket.h` defines `LOBBY_BEACON=0x40` etc.) but LORD doesn't wire to them.
- **Trainer Level** — player-level XP separate from mon XP. Deferred.
- **Mesh-sync'd leaderboard / news broadcast** — local only.
- **JSON → header packer** — gyms are hand-baked C arrays. Design has a hook
  for `scripts/gen_lord_gyms.py` + `data/lord_gyms.json` when it's worth it.
- **LordLogic init hook in MonsterMeshModule.cpp** — skipped because terminal
  does `lordEnsureLoaded()` lazily on first LORD command. If start-time load
  becomes desired, put it next to the daycare load in `MonsterMeshModule`.

## Known gotchas for the next session

1. **Branch was renamed.** The user renamed `Legend-of-Charizard` →
   `0.1.02-pre-alpha` after my first commit (see reflog). Any notes/plans
   still referring to `Legend-of-Charizard` are stale — the work lives on
   `0.1.02-pre-alpha`.

2. **Approved plan file**: `~/.claude/plans/swirling-squishing-sparrow.md`.
   Written before the reality-check corrected the repo path and confirmed
   `Gen1BattleEngine` + the `IN_RUN*` crawler already exist. Treat the plan
   as reference; it's accurate about file layout and gotchas.

3. **Explore-run stats tally**: wired inline in `resolvePlayerAction`
   (MonsterMeshTerminal.cpp, case `IN_RUN_BATTLE` + P1_WIN). It collects
   `waves / highestOppLevel / xpEarned`, and on wipe calls `lordOnRunEnd`
   + `lordSave`. XP is a rough proxy (`lvl * 4` per wave).

4. **`run` vs `explore`**: both call `startRun()`. The only difference is
   `runWildOnly_` is set to `true` by `explore` and `false` by `run`.
   `explore` also increments `exploreRunsToday` and saves; `run` does not.

5. **Timezone**: hard-coded to `tzOffsetHours_ = -5` in the terminal.
   TODO: wire to Meshtastic TZ config. Daily reset is no-op when
   `getTime() == 0` (RTC unsynced), matching FRPG's defensiveness.

6. **clangd spam**: the index complains about `-mlongcalls`,
   `-fstrict-volatile-bitfields`, and `../hal.h` not found — these are
   ESP32 toolchain flags clangd doesn't understand. Ignore; they don't
   affect PIO builds.

## Still uncommitted in `meshtastic-firmware` (pre-existing WIP — NOT mine)

```
 M patches/device-ui/screens.c
 M protobufs                                  # submodule pointer change
?? src/modules/poke/                          # another module from user's other sessions
?? scripts/gen_daycare_data.py
?? scripts/gen_showdown_data.py               # referenced in project memory
?? scripts/.showdown_cache/                   # gitignore candidate (per memory)
?? scripts/pokeapi_cache/                     # gitignore candidate
?? patches/dmg_green_tftview.patch            # DMG theme work
?? patches/dmg_green_theme.patch
?? .github/workflows/*                        # probably upstream merge
?? variants/**/*.ini                          # probably upstream merge
?? test/test_admin_radio/, test/test_traffic_management/
```

I deliberately did not touch any of these. Do not commit blindly — ask
the user which groupings they want committed. The user has mentioned
daycare tooling and DMG theme work in other sessions; `src/modules/poke/`
could be a separate module in-progress.

## Outer pokemesh repo state

- Branch: `mesh-pre-alpha-0.0.10`
- Has `M meshtastic-firmware` in its diff because the inner repo's HEAD
  moved. The outer is **not a proper submodule** (no `.gitmodules` mapping)
  — it's a nested git repo. Updating the outer's pointer is optional; the
  user typically just works in the inner.
- Separate uncommitted work in the outer (firmware-builds/, tdeck-maps/,
  scripts/fallout_scrape/ etc.) — also pre-existing, not LORD-related.

## Next logical steps

1. **Flash-test on T-Deck hardware.** Load a Pokémon Red save, try:
   - `gym` (list), `gym 1` (Pewter gauntlet — 5 fights)
   - `explore` (roguelike with daily cap)
   - `stats`, `badges`, `news`
   - Reboot, confirm persistence of `/monstermesh/lord.dat`
   - `explore` twice same day → second should refuse
2. **Pewter-gym difficulty check.** Hand-baked rosters might be too easy
   or too hard vs a real Red/Blue Pewter team. Tune levels in
   `LordGyms.cpp` → `g1_t*` arrays.
3. **JSON-driven rosters** if the user wants to iterate quickly:
   - `data/lord_gyms.json` + `scripts/gen_lord_gyms.py` + Makefile hook.
4. **Wire timezone** from Meshtastic config (`config.device.tzdef` or
   similar) so the 9am-local reset matches the user's wall clock.
5. **LordMenu hint on IDLE** (mentioned in plan, not done): one-liner
   "LORD: gym | explore | stats | badges | news" when entering IDLE
   with a loaded save.

## Files to read first if you're a new agent

- `src/modules/monstermesh/Gen1BattleEngine.h` (especially `:91-114` for
  `initBattlePokeFromBase`)
- `src/modules/monstermesh/MonsterMeshTerminal.cpp` — `handleCommand()` at
  `:134` routes commands; LORD helpers are at the end of the file
- `src/modules/monstermesh/LordGyms.cpp` — roster schema + `lordBuildGymParty`
- `~/.claude/plans/swirling-squishing-sparrow.md` — full v1 plan
