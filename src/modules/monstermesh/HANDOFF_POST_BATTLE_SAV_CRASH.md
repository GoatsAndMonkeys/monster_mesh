# Handoff: post-battle ROM-load crash in MonsterMeshEmulator::loadSaveFile

**Branch:** `mm/stable-restart`
**Latest build:** b406 (`MonsterMesh-b406-savload-minimal.bin`)
**Hardware:** T-Deck (ESP32-S3, USB-CDC for serial)
**Symptom:** After playing a PvP text battle, ALT-ing to the emulator and selecting a ROM crashes during `loadSaveFile`. Pre-battle ROM loads from a fresh boot work fine.

The crash signature is always the same: device resets with **no Guru Meditation header** (USB CDC re-enumerates instantly on reset, so the panic dump is lost). All we see is a truncated mid-printf line on the host side, followed by `serial.serialutil.SerialException: read failed: [Errno 6] Device not configured`.

## Files

| File | Role |
|---|---|
| `src/modules/monstermesh/MonsterMeshEmulator.cpp` | `loadROM` + `loadSaveFile` (the crash site) |
| `src/modules/monstermesh/MonsterMeshModule.cpp` | `launchROM` at line ~4874; `writePartyToSavOnSd` at line ~4094; runOnce drain at line ~2293 |
| `src/modules/monstermesh/MonsterMeshTextBattle.cpp` | Battle UI; sets `pendingSavWriteBack_` at end of battle |

## What's been ruled out across b397 → b406

Each iteration moved the truncation point to a different line in `loadSaveFile`. The crash never disappeared, but it never reproduced in the same place twice either.

| Build | Change | Truncation observed |
|---|---|---|
| b397 | Added `spiLock` inside `loadSaveFile` | `[EMU] save l` (mid success printf) |
| b398 | Dropped double SD reinit (loadROM already did it) | Same |
| b399 | Added per-step printfs (`waiting/got spiLock`, `open() handle=`, `size=`, `read returned`) | `[EMU] save loaded: /pokemon` (further) |
| b403 | Added `sav: closed, free=X largest=Y` marker after `f.close()` | `[EMU] sav: clo` (mid heap-stat printf) |
| b404 | Replaced heap-stat printf with simple `pre-close` / `post-close` markers, each with `Serial.flush()` | `[EMU] sav: po` (mid `post-close` print) |
| b405 | Scoped `spiLock` to a `{}` block ending right after `f.close()`, then `vTaskDelay(2)` and post-lock printf | `[E` (mid the very next printf after the lock release) |
| b406 (current) | **Stripped everything**: no per-step prints, no explicit close (destructor handles it), one printf above lock + one printf after lock + 10ms yield | TBD — user testing now |

### Observations from the b397-b405 logs

- `f.read()` consistently **returns 32768** (the full SAV) before the crash. The read works.
- The crash is **always during or immediately after a Serial.printf or f.close()**.
- It happens **only after a battle has completed and `sav writeback: wrote party + checksum to '/pokemon.sav'` has logged**. Pre-battle the same code path is fine.
- The MM heartbeat right at `enterEmulatorMode` consistently shows **free heap ~10KB / largest ~7KB** — extremely low — but the heartbeat after WiFi teardown shows healthy ~75KB free / ~35KB largest, and the ROM launch happens with healthy heap.
- User confirmed (b405 attempt): the crash happens "long after the save," so it isn't a write-still-flushing race. Writeback at 03:05:20.238, ALT at 03:05:20.239, 17 seconds in browser, then ROM load → crash. The card is definitely settled.

## Reproduction

1. Boot device on b405 or later.
2. Start a networked text battle with a peer over MQTT (`mmb <peer_short>`).
3. Fight at least one turn to completion.
4. Wait for `[MonsterMesh] sav writeback: wrote party + checksum` log.
5. Press ALT to enter ROM browser.
6. Select `/pokemon.gb` and confirm.
7. Crash happens during `loadSaveFile` — exact truncation point depends on the build.

Pre-battle (no fight) → same ROM load works.

## Hypotheses to investigate

> **User's lived experience (important context):** "I never get audio issues now, only crashing." The audio-init churn from b387 → b396 may have over-corrected — every cause of post-battle silent emu was solved, but we may have introduced a side effect that destabilizes a later code path. The timeline supports this:
>
> - b387: ROM-load-after-battle crashed with `i2s_stop` + `vTaskDelay` added → device reset right after "save loaded" line.
> - b388: removed `i2s_stop` + `vTaskDelay`, kept just `driver_uninstall` → audio worked, crash gone.
> - b395: added `audioThread->stop()` if playing.
> - b396: added `ESP_ERR_INVALID_STATE` retry around `i2s_driver_install`.
> - **b397 onward:** started crashing during `loadSaveFile`, post-battle only.
>
> The audio init runs in `MonsterMeshEmulator::begin()` *after* `loadSaveFile` (see `MonsterMeshEmulator.cpp:87-94`), so it shouldn't be in the call chain that crashes — **but** `begin()`'s preamble at lines 47-51 does this BEFORE `loadROM`/`loadSaveFile`:
>
> ```cpp
> running_ = false;
> if (audio_) { audio_->stop(); delete audio_; audio_ = nullptr; }
> if (gb_) { free(gb_); gb_ = nullptr; }
> ```
>
> On the **second** ROM launch in a session (or after a battle that ran `audio_`?) this teardown executes `i2s_driver_uninstall(I2S_NUM_0)`, then sets `instance_ = nullptr`, then ` delete audio_`. If anything in the system still holds an I2S DMA reference, or if the LoRa thread's notification AudioThread (`audioThread`) re-grabs the GPIO matrix between our uninstall and the next emu init, the SD SPI ops later in `loadSaveFile` would be hitting hardware in an unexpected state — and SD shares the SPI bus with the LoRa chip on T-Deck.
>
> Specifically inspect `MonsterMeshAudio::begin()` in `src/modules/monstermesh/MonsterMeshAudio.cpp`. Note the comment block at lines 39-43:
> ```
> // NOTE: do NOT add i2s_stop / vTaskDelay around these — that combo
> // crashed the device on ROM-load-after-battle on b387 (see logs:
> // device reset right after "save loaded" line, before any further
> // emulator log). driver_uninstall is sufficient.
> ```
> The current crash signature (post-battle ROM-load, mid-printf around save-load completion) is the SAME signature as b387's, even though we removed the offending `i2s_stop`. This is the strongest single clue: **whatever conditions allowed the b387 crash may still exist; we just moved the trigger.**

In rough order of plausibility:

1. **Stale audio-thread / I2S driver state from a prior battle or audio playback corrupts SD SPI later.** See above. The `audioThread->stop()` and `i2s_driver_uninstall(I2S_NUM_1)` in `MonsterMeshAudio::begin()` are touching Meshtastic's notification AudioThread (which the user is using over BLE/phone) — this could leave a half-torn-down DMA channel that interferes with SD SPI in `loadSaveFile`. Try: comment out the `audioThread->stop()` and `i2s_driver_uninstall(I2S_NUM_1)` calls and rebuild. If post-battle ROM-load now succeeds, this is the bug. (Audio may regress; we re-design the audio init from scratch then.)
2. **USB-CDC TX buffer flood under spiLock → LoRa task watchdog.** Many `Serial.printf` calls fire while spiLock is held; if the host stops draining USB-CDC even for ~100ms, our writes block while holding the lock; LoRa task on Core 0 is blocked waiting for spiLock and trips TWDT. b406 strips all in-lock prints to test this. **If b406 survives, hypothesis 1 was wrong and this was the bug.**
2. **SD library / FAT state corruption from the prior `writePartyToSavOnSd`.** `SD.end()` / `SD.begin()` reinit in `writePartyToSavOnSd` and again in `loadROM` may leave the FAT cache or directory entries in an inconsistent state that only manifests on the *second* file operation. Try: run a battle, write SAV, then re-read the SAV from within the same module (no emulator) to see if the re-read crashes too.
3. **Heap fragmentation from textBattle session.** The text battle module has a fixed-size log buffer + other state. After it tears down via `pendingBattleEndCleanup_`, something may not be freed; on subsequent `loadSaveFile`, a small malloc inside SD/USB-CDC fails with corrupt-on-free behavior. Look at `MonsterMeshTextBattle::exit()` and `pendingBattleEndCleanup_` path in `MonsterMeshModule.cpp` around line 3311.
4. **Stack overflow on LVGL/main thread.** `launchROM` → `emu_.begin()` → `loadROM` → (after ROM, big stack frame) → `loadSaveFile` (256-byte savPath + LockGuard + File object). The chained call depth + locals could be tight on Arduino's 8KB main-loop stack. Try moving `savPath` to a class member or shrinking to 64.
5. **LoRa chip IRQ during f.close().** "Radios parked (soft — IRQ left enabled)" means the LoRa chip can still fire IRQs during emulator mode. The IRQ handler does SPI reads on the LoRa chip. We hold spiLock at the application level, but **IRQs don't honor application mutexes**. If a LoRa DIO1 IRQ fires during SD f.close()'s SPI access, the SPI controller state could be corrupted. The "soft park" was chosen for resume-hang reasons (see `feedback_mm_alt_exit_radio_park.md` in user memory); a different mitigation would be needed if this is the bug.

## What I'd try next if b406 still crashes

- **Replace SD.open/read/close with raw FatFS / vfs POSIX `fopen("/sd/pokemon.sav")`.** Removes the SD class wrapper entirely; if POSIX read works, the bug is in the Arduino SD class.
- **Read the SAV on the emu task (Core 1) after `xTaskCreatePinnedToCore`** instead of on the LVGL/main thread inline. The emu task gets a fresh stack and runs concurrently with the LoRa thread on a different core.
- **Defer the SAV load by 100ms after spawning the emu task.** Currently `launchROM` calls `emu_.begin()` synchronously, which does ROM read + SAV read on the LVGL thread before any task is spawned. Letting the emu task load the SAV at a known-quiet point avoids the LoRa interaction entirely.
- **Disable LoRa chip IRQ during `enterEmulatorMode` (hard park).** Per `feedback_mm_alt_exit_radio_park.md`, this caused resume-hang issues — but maybe the issue is fixable now that we have spiLock everywhere.
- **Backport `loadSaveFile` from a known-good baseline.** Project memory `project_mm_checkpoint_b23.md` references commit `26b179676` on `mm/stable-restart` (b23) as user-validated stable. Check if that commit's `loadSaveFile` differs structurally.

## Useful greps / commands

```bash
# Current loadSaveFile
sed -n '298,340p' src/modules/monstermesh/MonsterMeshEmulator.cpp

# Battle-end writeback flow (sets pendingSavWriteBack_)
grep -n "pendingSavWriteBack_" src/modules/monstermesh/MonsterMeshModule.cpp

# runOnce drain
sed -n '2290,2315p' src/modules/monstermesh/MonsterMeshModule.cpp

# writePartyToSavOnSd implementation
sed -n '4094,4190p' src/modules/monstermesh/MonsterMeshModule.cpp

# Where ALT triggers enterEmulatorMode
grep -n "enterEmulatorMode" src/modules/monstermesh/MonsterMeshModule.cpp
```

## Build / flash / soak

```bash
source /Users/goatsandmonkeys/Documents/mesh_bbs/.venv/bin/activate
SCONSFLAGS=-j1 pio run -e t-deck-tft
# Build counter auto-increments in firmware-builds/.build_counter
# DO NOT use git rev-list count — branch was re-rooted.

# Flash app-only (preserves WiFi/PSK/keys). NEVER write factory.bin at 0x0.
esptool.py --port /dev/cu.usbmodemNNNN --baud 921600 \
  write_flash 0x10000 firmware-builds/MonsterMesh-bNNN-tag.bin

# Capture serial
~/Desktop/mmlog.sh   # tee's /dev/cu.usbmodem* to /tmp/serial_b8.log
```

## Constraints / things not to break

- Pre-battle ROM load works — don't regress that flow.
- The MMT PvP echo-loop fix in b402 (`fromId == remoteId_` check in `MonsterMeshTextBattle::handlePacket`) is the FIRST root-cause fix for months-long desyncs. Preserve it.
- Daycare writeback gate on `runOnce` requires `!emulatorActive_ && !browserActive_ && !cartLoaded`. If user ALTs to emulator before the gate fires, XP write is silently skipped. Worth fixing separately but not the cause of THIS crash.
- The user runs networked PvP **over MQTT only** (LoRa-off) when collaborating remotely; the radio park / TX queue is largely a no-op for them, but the LoRa thread is still running.

— Mark / Claude Opus 4.7, 2026-05-22
