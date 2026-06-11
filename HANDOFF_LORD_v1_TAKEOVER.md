# LORD v1 — Takeover Instructions

Paste or reference this when starting a fresh Claude session to continue the LORD work.

## Opening prompt for the new agent

> I'm continuing work on the LORD (Legend of Charizard) door-game layer in
> MonsterMesh. Please read `/Users/goatsandmonkeys/Documents/pokemesh/HANDOFF_LORD_v1.md`
> first — it documents the current state, what shipped in v1, branch/remote
> status, known gotchas, and next logical steps. After reading, report back with:
>
> 1. A one-paragraph summary of where v1 left off.
> 2. Any uncommitted local changes in `meshtastic-firmware/src/modules/monstermesh/`
>    that appear to be LORD-related but not yet pushed.
> 3. Which "Next logical step" from the handoff you'd tackle first, and why.
>
> Do not commit, push, or flash anything until I confirm.

## Ground rules for the takeover session

- **Repo**: `/Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware`
- **Branch**: `0.1.02-pre-alpha` (was `Legend-of-Charizard` — see handoff §gotcha 1)
- **Remote**: push to `monstermesh` (`monstermesh/0.1.02-pre-alpha` is current HEAD).
- **Build**: `pio run -e t-deck-tft` from the firmware dir.
- **Flash**: T-Deck at `10.1.2.193` via bmorcelli Launcher OTA
  (`POST /OTA command=0&size=N` then multipart `POST /OTAFILE file1=...`;
  creds `admin` / `launcher`). Device auto-reboots after upload.

## Things the new agent MUST NOT do without asking

1. **Do not commit the unrelated pre-existing WIP** listed in the handoff
   §"Still uncommitted in meshtastic-firmware" — those belong to other sessions
   (daycare tooling, DMG theme, poke module, etc.).
2. **Do not push to `origin` or `pre-alpha` remotes.** The LORD work lives on
   `monstermesh`. Confirm with the user before touching other remotes.
3. **Do not rewrite history** on `0.1.02-pre-alpha` — it's already pushed.
   Add new commits; never `--amend` or force-push.
4. **Do not flash without confirmation.** The user sometimes has the device
   doing other things; always confirm before the OTA POST.

## Known uncommitted LORD delta (as of handoff time)

`MonsterMeshTerminal.cpp` has local edits that are NOT in the pushed commit:

- `AREA_POOL_0` through `AREA_POOL_7` — wild-encounter pools indexed by badge
  count (replaces the flat random pool inside `buildWildOpponent` when
  `runWildOnly_` is true).
- Init/help text reworked under a "-- Legend of Charizard --" header.
- `gym list` and `gym go` subcommands added (`gym go` = auto-challenge the
  next unlocked, unbadged gym).

These should be verified with `git diff` before the new agent commits them.
They are reasonable and in-scope for LORD v1.5; just confirm with the user
before staging.

## Where the plan lives

`~/.claude/plans/swirling-squishing-sparrow.md` — the approved v1 plan.
Accurate on file layout and gotchas; stale on a couple of paths (see
handoff §gotcha 2).

## Quick sanity commands for the new agent

```bash
# Where is HEAD and what's uncommitted?
git -C /Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware status
git -C /Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware log --oneline -5

# What's on the monstermesh remote?
git -C /Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware \
    log monstermesh/0.1.02-pre-alpha --oneline -5

# Diff since the pushed tip (will show the AREA_POOL + gym list/go work)
git -C /Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware \
    diff monstermesh/0.1.02-pre-alpha -- src/modules/monstermesh/
```

## If the user says "keep going"

Start with §"Next logical steps" item 1 in the handoff (flash-test on
hardware + verify persistence), unless the `git diff` above reveals the
AREA_POOL / `gym list`/`gym go` work is still uncommitted — in that case,
offer to stage+commit those first so the tree is clean before new work.
