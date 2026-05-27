# Architecture

How MonsterMesh fits inside Meshtastic firmware and how the game runs alongside the radio stack on a single ESP32-S3.

## The big picture

MonsterMesh is a single Meshtastic **module** (`src/modules/monstermesh/MonsterMeshModule`). It is only compiled for the T-Deck (`-D T_DECK`); on every other board the whole module `#if`-compiles away, so this fork still builds and runs as stock Meshtastic everywhere else.

The module is two things at once:

- a **`SinglePortModule`** on the Meshtastic private application port (`PRIVATE_APP`, port 256) — this is how it receives and sends MonsterMesh packets over the mesh; and
- an **`OSThread`** — its `runOnce()` is called periodically by the Meshtastic scheduler and is where deferred work happens (draining the TX queue, kicking off battles, daycare ticks, SAV reads/writes).

```
class MonsterMeshModule : public SinglePortModule, public concurrency::OSThread
```

All MonsterMesh mesh traffic uses **channel 1** (`MONSTERMESH_CHANNEL = 1`).

## Threads

The ESP32-S3 is dual-core. MonsterMesh deliberately spreads work across cores and FreeRTOS tasks so the Game Boy emulator can run at full frame rate without starving the LoRa stack:

| Thread / task | Runs on | Job |
|---|---|---|
| Meshtastic main loop + `runOnce()` | Core 0 (with the radio) | Mesh RX/TX, deferred game logic, SD I/O, SAV read/write |
| **Emulator task** | Core 1 | Runs `peanut_gb` frame-by-frame |
| **Render task** | dedicated task | Blits the emulator framebuffer (RGB565 in PSRAM) to the TFT without blocking the emulator |
| LVGL / device-ui | device-ui thread | The Meshtastic UI, the ROM browser, and the MonsterMesh terminal widget |

### The golden rule: keep work on the right thread

The single most important constraint in this codebase: **LVGL widget operations must happen on the LVGL thread, and LoRa send/SD I/O must happen on the OSThread (`runOnce`) — never the other way around.**

Because of that, the module uses a lot of `volatile bool pending…` flags. The pattern is always:

1. Something happens on thread A (e.g. a packet arrives on the mesh-receive thread, or the user submits a terminal command on the LVGL thread).
2. Thread A sets a `pending…` flag and stashes any data it needs.
3. The correct thread (`runOnce` for radio/SD, the LVGL indev tick for widgets) notices the flag, does the work, and clears it.

Examples you'll see in `MonsterMeshModule`:
- Party loaded from a `.sav` is *staged* by `runOnce` (SD read) and *consumed* by the LVGL tick (`tryConsumeStagedParty`) which pushes it into the terminal widget.
- Battle-end cleanup (`lv_obj_invalidate`, refocus) is deferred onto the LVGL thread.
- All outbound DMs/challenges are queued and sent from `runOnce`, never directly from `handleReceived` — sending under the router's receive context wedges the TX queue.

## Mode switching

The same screen shows one of three things at a time:

1. **Meshtastic UI** (device-ui / LVGL) — chat, nodes, map, settings, and **Tools → MonsterMesh Terminal**.
2. **ROM browser** — an SD-card file picker.
3. **Emulator** — the running Game Boy.

Transitions:
- **Ctrl+E** / **SYM+E** toggles emulator ↔ Meshtastic UI.
- **ALT** (from the Meshtastic UI) opens the ROM browser.
- A map/tools button hooks `monstermesh_set_toggle_cb` to open the terminal.

### Radio parking on entry/exit

When the emulator or browser takes over, the module **parks the radio** (`enterEmulatorMode`): it puts the SX1262 to sleep and turns WiFi off. On exit (`exitEmulatorMode`) it brings them back and re-arms RX. This keeps the LoRa SPI traffic from contending with the heavy TFT/SD SPI traffic the emulator generates. (BLE is independent and stays on.)

`startReceive()` must be re-issued from `runOnce` on the LoRa thread, not from the LVGL thread — doing it on the wrong thread hangs in `setStandby`/`checkNotification`. So mode-exit sets a `radioNeedsRx_` flag that `runOnce` drains.

## SD / SPI hygiene

On the T-Deck the TFT, the SD card, and the LoRa radio all share one SPI bus. MonsterMesh:
- parks the LoRa and TFT chip-selects HIGH at boot and around SD operations, inside the SPI lock; and
- routes all SD work through `runOnce` so SD reads never collide with a frame blit.

Daycare automatically **checks out** (stops touching the SD) while the emulator/browser own the bus, and **checks back in** (re-reads the party from the possibly-updated `.sav`) on exit. Both transitions are flagged on the LVGL thread and executed on the LoRa thread.

## Persistence

Game state lives on the device's **internal LittleFS** (via Meshtastic's `FSCom` filesystem handle), separate from the SD card that holds ROMs/saves:

| Path | Contents |
|---|---|
| `/monstermesh/daycare.dat` | Daycare state: per-mon hours/XP, friendship & rivalry scores, achievements |
| `/monstermesh/lord.dat` | RPG state: badges, explore runs today, news ring, best-run stats |

The player's **Game Boy `.sav`** stays on the SD card; daycare XP is written back into it (see [DAYCARE.md](DAYCARE.md)).

## Receiving packets

`wantPacket()` claims packets on the MonsterMesh port; `handleReceived()` parses them. Incoming text DMs are also inspected for the human-readable challenge/handshake tokens (e.g. the `mmb` PvP challenge and its `y`/`n` reply) so the multiplayer flows work even when the other side is driving them from a phone. Per the threading rule, `handleReceived` only *records* what it saw and lets `runOnce` act on it.

## Where to start reading the code

- `MonsterMeshModule.{h,cpp}` — the hub; start at `runOnce()` and `handleReceived()`.
- `MonsterMeshTerminal.cpp` — `executeLine()` routes every terminal command.
- `Gen1BattleEngine.{h,cpp}` — the battle engine (see [BATTLE_ENGINE.md](BATTLE_ENGINE.md)).
- `PokemonDaycare.{h,cpp}` + `DaycareEventGen.cpp` — the daycare (see [DAYCARE.md](DAYCARE.md)).
