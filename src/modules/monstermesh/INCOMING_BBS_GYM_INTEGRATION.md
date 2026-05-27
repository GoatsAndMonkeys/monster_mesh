# INCOMING — `bbs` / `bbs fight #` terminal commands

> **For the MonsterMesh agent / human iterating on `MonsterMeshTerminal.cpp`:**
> Another work stream is going to add a `bbs` command family to the terminal.
> This note pre-declares exactly what it will touch so you don't accidentally
> overwrite or duplicate the work.

## Status

NOT YET IMPLEMENTED — pre-declaration only. The work is queued behind:
1. Phase A — unify Pikachu + Gauntlet in heltec-v3 firmware (in progress)
2. Phase B — Pocket Pikachu gift menu (10/100/200/300/400/500/All/Back)

This note is so you can keep working on the terminal without colliding.

## What this is

A new "online gym" door-game that complements the existing single-player
LORD gym command (`gym fight N`). Online gyms = Meshtastic nodes running the
new `GauntletModule` (in `src/modules/monstermesh/gauntlet/`). Players can
challenge them via networked Gen1BattleEngine battles, rendered in
`MonsterMeshTextBattle`'s existing battle screen — same one used for
peer-vs-peer fights.

## Files I will touch

### `MonsterMeshTerminal.h`

- Add nested struct `DiscoveredGym` (~48 bytes):
  ```cpp
  struct DiscoveredGym {
      uint32_t nodeNum;
      char     gymName[16];
      char     badgeName[16];
      char     leaderName[12];
      uint8_t  rosterSize;
      uint32_t lastSeen;     // RTC seconds
  };
  ```
- Add member: `DiscoveredGym discoveredGyms_[16];` and `uint8_t discoveredCount_;`
- Add member: `using BbsFightFn = void(*)(void *ctx, uint32_t gymNodeNum); BbsFightFn bbsFightFn_; void *bbsFightCtx_;`
- Add public setter `setBbsFightHook(BbsFightFn fn, void *ctx)`
- Add private method declarations:
  - `void onBbsListReply(uint32_t fromNodeNum, const char *gymName, const char *badge, const char *leader, uint8_t roster)`
  - `int8_t resolveBbsIndex(const char *arg)` — accepts `1..N` or hex node id
  - `void renderBbsList()`

### `MonsterMeshTerminal.cpp`

- ONE new `else if` branch in the main command dispatcher, alongside the
  existing `gym`/`daycare`/`heard`/etc. branches:
  ```cpp
  else if (strncmp(line, "bbs", 3) == 0) {
      const char *arg = line + 3;
      while (*arg == ' ') arg++;
      if (*arg == '\0')                        renderBbsList();
      else if (strncmp(arg, "fight", 5) == 0)  /* dispatch fight */ ;
      else if (strncmp(arg, "ping", 4) == 0)   /* re-broadcast probe */ ;
      else                                      println("usage: bbs | bbs fight N");
  }
  ```
- ONE new line in the `help` printer, near the existing `gym fight N` entry:
  ```cpp
  println("  bbs         - list online BBS gyms");
  println("  bbs fight N - fight gym N (online, networked battle)");
  ```
- A 5-second discovery routine (probe via `TEXT_MESSAGE_APP` broadcast of
  `!gym ping` to ALL nodes; aggregate replies into `discoveredGyms_`). The
  reply handler (`onBbsListReply`) is called by the MM module when it sees a
  reply payload starting with `GYM:` or similar marker.

### `MonsterMeshModule.cpp` (the wiring)

- `setup()` calls `terminal_.setBbsFightHook(bbsFightThunk, this)`.
- New private method `void onBbsFightCallback(uint32_t gymNodeNum)` that does:
  ```cpp
  textBattle_.startNetworkedAsInitiator(gymNodeNum, gen1Save_.activeParty());
  ```
- `handleReceived()` adds a small branch: if the incoming text DM starts with
  `GYM:` (gauntlet node beacon reply), parse it and call
  `terminal_.onBbsListReply(...)`.

### NEW file: `src/modules/monstermesh/gauntlet/GauntletModule.cpp`

- A `!gym ping` handler will be added (one new `if (strncasecmp(cmd,"ping",4)==0)`
  branch in `handleCommand()`). The reply payload format is:
  ```
  GYM:<gymName>|<badge>|<leaderName>|<rosterSize>
  ```
  This is what MonsterMeshTerminal parses to populate `DiscoveredGym`.

## What I will NOT touch in MonsterMeshTerminal

- Any daycare logic (`daycare ...` branch and helpers)
- The existing `gym` / `gym fight N` LORD branch (local roguelike — unchanged)
- The `roulete`, `heard`, news/quest subsystems
- Render/keyboard/scrolling code in any of `render*()` / `handleKey()` /
  `appendLog()` / `redraw()` etc.
- LovyanGFX panel layout

## Symbols I'll reserve (please don't claim these names)

- Class members: `discoveredGyms_`, `discoveredCount_`, `bbsFightFn_`,
  `bbsFightCtx_`, `bbsLastProbeMs_`
- Methods: `renderBbsList`, `onBbsListReply`, `resolveBbsIndex`,
  `setBbsFightHook`
- File: `INCOMING_BBS_GYM_INTEGRATION.md` (this file — delete when integration lands)

## Compile/runtime guards

The whole `bbs` block will be guarded by `#if !MESHTASTIC_EXCLUDE_GAUNTLET`
so terminal builds without the gauntlet module just lose the command
gracefully.

## Coordination protocol

- If you need to rename or refactor any of the symbols above, leave a
  comment at the top of `MonsterMeshTerminal.{h,cpp}` and I'll adapt.
- If you add a *different* new command to the dispatcher, place it
  anywhere; the `bbs` branch is order-independent.
- If you change the `BbsFightFn` typedef shape, the wiring breaks — ping
  me first.

— added by the gauntlet integration work stream
