# MonsterMesh emulator mid-play freeze — handoff

**Branch:** `emulator-stability`
**Current build:** `emulator-stability-b86` (WiFi-off test, pending flash/verify)
**Devices:** LilyGO T-Deck × 2 — Red (`!f1a05e70`), Blue (`!a1ad6880`)

Repro'd on both devices. This doc captures everything we've tried, what worked, what didn't, and where to look next. Written as a handoff so Codex (or a fresh session) can pick up without re-reading the chat.

> **Update 2026-04-21 (extended investigation, b76–b86):** added priority-inheriting Lock + bounded tryLock + owner telemetry (reverted — made things worse), per-task heartbeat counters, LogRotate flash-write suppression, NodeDB::saveToDisk suppression, ROM-loader phase suppression, and (pending verify) WiFi-off. Repeatable pattern during every freeze is `[hb] emu+0 blit+0 run+20` — both MonsterMesh foreground tasks (emu on Core 1, render on Core 0) stop advancing simultaneously while Meshtastic's runOnce keeps firing. That pattern rules out any single-task bug and points at a system-level cross-core pause (flash I/O, WiFi stack, or IPC). Flash-write suppression didn't clear it. See §9 at the bottom.

---

## Symptom

1. Device boots cleanly on `emulator-stability-b75`. Meshtastic UI comes up, ROM browser opens once message-history replay is done.
2. User launches any Gen 1 Pokémon ROM. Emulator starts rendering + playing audio.
3. Somewhere between 30 s and a few minutes in, the **display stops updating** mid-play. The game appears frozen from the user's perspective.
4. On at least one freeze, **audio kept playing** — peanut-gb's APU continued generating samples while the screen was dead. On other freezes audio may also stop (not confirmed which).
5. Reset button recovers some of the time; a full power cycle always does.

The freeze happens on a branch that has the daycare / lobby / LORD / Gen1BattleEngine / MonsterMeshTextBattle / SAV-cache subsystems **all disabled**. So this isn't network chatter or SD write-through or daycare beacons. The minimum we're running is: peanut-gb emulator task (Core 1, prio 5), framebuffer blit/render task (Core 0, prio 2), LVGL + Meshtastic's normal tasks, and the BattleShim/PokemonLinkProxy code (kept because MML uses it — dormant unless a cable-club session is active).

---

## What we've done — ordered

All commits land on the `emulator-stability` branch (which itself is based on pre-session main before the SD-cache redesign). Build numbers below are git rev-count stamps.

### b68 — clean-stable baseline cherry-picked onto pre-session base

Only code kept from the broader session: cozette font sweep, home-container font fix, party-column alignment, chunked 32 KB SD ROM read (with `spiLock->unlock(); vTaskDelay(1); spiLock->lock();` between chunks to let the radio task breathe), nothing else. Everything else that was added during the session (SAV cache, daycare refactor, SD init hardening, MMT-DM flow, admin terminal, etc.) is gone.

### b69 — daycare subsystem fully disabled

`daycare_.init()` and its three callbacks (`setSendDm`/`setBroadcast`/`setSendBeacon`) no longer run in `setup()`. `daycareAutoCheckIn()` is never invoked from runOnce. No more boot-time SAV read from SD, no more beacon broadcasts, no more periodic SD write-through. The `daycare_` member still exists so existing callers compile against the default-constructed instance; at runtime it no-ops.

### b70 — terminal stripped to debug-only commands

Removed the gym / rogue / explore / fight / mmt / pick / move-key / Y-N battle-challenge / status / quit handlers from `MonsterMeshTerminal::handleCommand`. Terminal now routes only:
- `help` — lists what's below
- `party` — show 6 Pokémon from the SAV
- `sysinfo` — heap / PSRAM / uptime
- `whoami` — own node ID
- `nodes` — list up to 16 mesh nodes
- `mml <name>` — cable-club invite (peer lookup now goes through `nodeDB` directly, not the disabled daycare neighbor list)

### b71 — route all direct `Serial.print*` through `LOG_DEBUG`

61 direct `Serial.printf` / `Serial.println` call sites across `MonsterMeshAudio.cpp`, `MonsterMeshBattleShim.cpp`, `MonsterMeshEmulator.cpp`, `MonsterMeshLobby.cpp`, `PokemonLinkProxy.cpp` rewritten to use `LOG_DEBUG`. Raw `Serial.print` dumps outside Meshtastic's `RedirectablePrint` wrapper were corrupting the protobuf framing on USB-CDC, which is why `meshtastic --port ... --info` had been timing out session-wide.

Added `#include "DebugConfiguration.h"` to the five files for the `LOG_*` macros.

### b72 — signal-based ROM browser gate via `notifyMessagesRestored()`

Replaced the old 20 s / 10 s timer on `pendingBrowserOpen_` with a tight signal. Meshtastic's `ViewController::restoreTextMessages()` calls `TFTView::notifyMessagesRestored()` once `LogRotate::readNext()` is exhausted. We patch that override in `patches/device-ui/TFTView_320x240.cpp` to flip a new global `g_mmMessagesRestored = true`. `runOnce` now opens the browser on that flag, with an 8 s `millis()` fallback in case the hook never fires (empty log dir / replay crash).

### b73 — task stack bumps + early USB-CDC probe

- Emu task stack `16384` → `49152`
- Render task stack `8192` → `16384`
- Added `Serial.write("\n[MMPROBE] monstermesh module ctor\n", 36)` at the top of the `MonsterMeshModule` constructor as a raw pre-LOG probe. (Marker never appeared in host captures — the host CDC hadn't finished enumerating that early, so this was a wash; left in as a no-op.)

### b74 — subscribe emu + render tasks to ESP-IDF task watchdog

Goal: turn the silent hangs into panic + backtrace + reboot so the next-boot serial log shows which task is stuck where. Added `esp_task_wdt_add(emuTaskHandle_)` and `esp_task_wdt_add(renderTaskHandle_)` immediately after `xTaskCreatePinnedToCore`. Added `esp_task_wdt_reset()` at the top of each iteration in `emuTaskLoop()` and `renderTaskLoop()`.

**Did not work.** During a subsequent freeze, captured 25 seconds of serial post-freeze — zero bytes. No TWDT panic, no backtrace. Suspicion: `esp_task_wdt_add` returned an error silently because `esp_task_wdt_init` had never been explicitly called.

### b75 — explicit `esp_task_wdt_init(10, true)` (ESP-IDF 4.x classic API) before subscribes

Added the init call with logged return code so we can distinguish fresh init (err 0) from "already initialized" (err `0x103` / `ESP_ERR_INVALID_STATE`).

The newer ESP-IDF 5.x struct-based `esp_task_wdt_config_t` + `esp_task_wdt_reconfigure` form **does not compile** on this project's Arduino-ESP32 toolchain — got `esp_task_wdt_config_t was not declared in this scope`, so this project is on the older API. Using classic form.

---

## What doesn't work — observations from the last freeze on b75

Captured ~45 seconds of USB-CDC serial during and after a user-confirmed freeze. Result:

- **Serial keeps flowing.** We see continuous `[Router]`, `[Packet History]`, `[DeviceUI]`, `[NodeInfo]`, `[RadioIf]` chatter — ~3–4 KB per 20 seconds. Meshtastic's main loop + radio task + device-ui task are very much alive.
- **No TWDT panic fires.** Over 45 seconds of freeze, zero `TWDT`, `Guru Meditation`, `Backtrace`, `panic`, `abort`, `rst:` markers.
- **USB still works for log output.** But `meshtastic --port <cdc> --info` still times out on the stream handshake — separate issue, unrelated to the freeze. `SerialConsole` task is probably the one receiving our `want_config_id` request but never replying; possibly starved by render-task CPU, or some protobuf-over-log framing thing.

**Implication:** the emu task is probably *not* actually stuck. `emu_.runFrame()` is short per frame; `esp_task_wdt_reset()` at the top of `emuTaskLoop()` would keep getting called. Audio staying on in the "music kept playing" freeze fits this — audio is generated inside `runFrame()`, so if that runs, audio plays.

What's stuck is the **display path**. Two candidates:

1. **Render task stuck inside `blitFrame()`.** `blitFrame()` takes `spiLock` and issues LGFX writes. If someone else (SD op, radio TX, LVGL flush callback) leaked the lock or is holding it across a long operation, render task blocks forever. **But** the render task was also subscribed to TWDT on b74/b75, and TWDT did not fire. Either `esp_task_wdt_add` on the render task errored, OR the render task is still servicing the feed somehow (semaphore waits with timeout are the usual culprit — render would wake, feed, then re-wait).
2. **LVGL / scanline callback write race.** `scanlineCallback` writes to `frameBuf_` from the emu task; render task reads `frameBuf_` and sets `frameDirty_`. If a theme change or LVGL refresh interacts with `frameBuf_` or trips an assertion inside LVGL, you can get a visual freeze without any task dying.

Also worth considering:

3. **Audio I²S DMA buffer starvation → peanut-gb stuck in audio write.** If I²S DMA underruns and `i2s_write` blocks, the emu task would be stuck inside `audio_->processFrame()` with no frame advance + no audio. Doesn't fit the "music kept playing" observation.
4. **MBC write corruption in peanut-gb.** Certain ROMs with unusual MBC1/MBC3 write patterns can poke outside the cartRam bounds and corrupt adjacent PSRAM. Doesn't fit TWDT not firing either, unless the corruption hits a structure that causes a deadlock rather than a fault.

---

## Hottest next moves

In descending priority:

1. **Confirm the TWDT init log on boot.** On next reset, grep the boot serial for `[MonsterMesh] TWDT init err=` — value tells us whether our init took (`0`) or Arduino beat us to it (`0x103`). Also grep for `TWDT add emu err=` and `TWDT add render err=`. If either add returned non-zero, TWDT isn't actually armed on those tasks and that explains the silence on hang.
2. **If TWDT is confirmed armed but still doesn't fire on hang**, the render task is somehow still servicing its feed even while blitFrame hangs. Either lift `esp_task_wdt_reset()` out of the top of the loop and put it **before** each potentially-blocking call (spiLock acquire, LGFX writes) so the hang window gets bracketed, or add an explicit timeout to the render-side spiLock take.
3. **Add a heartbeat `LOG_INFO` inside `blitFrame()` entry + exit** with a sequence counter. During a freeze, if we see only "enter #N" without "exit #N" appearing, the render task is stuck inside LGFX. If we see matching enter/exit continuing, the blit itself is fine and the freeze is upstream of it.
4. **Instrument `spiLock`.** `src/concurrency/Lock.cpp` has simple FreeRTOS semaphore lock/unlock — add a global `volatile TaskHandle_t g_spiLockOwner` + `uint32_t g_spiLockTakenMs` that's set/cleared around the lock, and dump them from a 1 Hz LOG_INFO in `runOnce`. If someone is holding the lock for >1 s during a freeze we catch them.
5. **Try disabling the BattleShim `tick()` call in `emuTaskLoop`.** `shim_.tick()` runs every frame while the emulator is active. Even though BattleShim is dormant without MML, the tick could still be touching mutexes or transport queues that interact badly. Commenting out one call is a 30-second test.
6. **Rule out the framebuffer double-write race.** `scanlineCallback` on the emu task and `blitFrame()` on the render task both touch `frameBuf_` without a lock. Use `frameDirty_` as the only sync. It's racy but historically "works." If the race finally bites, you get a half-updated frame → maybe LVGL chokes on it.

---

## How to reproduce

1. Device in ESP32 download mode (USB enumerates as `/dev/cu.usbmodem2101`).
2. `cd ~/Documents/pokemesh/meshtastic-firmware`
3. `source ~/Documents/mesh_bbs/.venv/bin/activate`
4. `pio run -e t-deck-tft -t upload --upload-port /dev/cu.usbmodem2101`
5. Press reset on the device. USB re-enumerates as `/dev/cu.usbmodem206EF1A05E701` (Red) or `usbmodemE072A1AD68801` (Blue).
6. Wait for the Meshtastic boot splash + message-history replay (~5–10 s).
7. Open ROM browser, pick a Pokémon Gen 1 ROM.
8. Play. Freeze happens in 30 s to a few minutes.

To watch serial during gameplay:

```bash
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodem206EF1A05E701', 115200, timeout=0.5)
time.sleep(0.3); s.reset_input_buffer()
start = time.time()
with open('/tmp/watch.log','wb') as f:
    while time.time() - start < 180:
        c = s.read(4096)
        if c: f.write(c); f.flush()
"
```

---

## Code anchor map

- `src/modules/monstermesh/MonsterMeshModule.cpp`
  - `MonsterMeshModule::MonsterMeshModule()` ~106 — constructor (`MMPROBE` Serial.write is here but host misses it)
  - `MonsterMeshModule::setup()` ~123 — trivial, just logs + sets status
  - `MonsterMeshModule::runOnce()` — module heartbeat on Meshtastic's OSThread
  - Task-creation block ~1977 — `esp_task_wdt_init`, `xTaskCreatePinnedToCore` for emu + render, `esp_task_wdt_add` on both
  - `emuTaskLoop()` — per-frame loop, top calls `esp_task_wdt_reset()`, then `emu_.runFrame()`, `shim_.tick()`, WRAM reads for auto-save detection, `vTaskDelayUntil`
  - `renderTaskLoop()` — `while (true) { esp_task_wdt_reset(); if (emulatorActive_ && frameDirty_) blitFrame(); vTaskDelay(33); }`
  - `blitFrame()` — LGFX blit from `frameBuf_`, takes `spiLock`
  - `scanlineCallback()` — peanut-gb scanline callback, writes into `frameBuf_`, runs on emu task
- `src/modules/monstermesh/MonsterMeshEmulator.cpp`
  - `runFrame()` — peanut-gb `gb_run_frame()` + audio dispatch
  - `loadROM()` — chunked SD read with `spiLock->unlock(); vTaskDelay(1); spiLock->lock();` between 32 KB chunks (this was the fix for the radio-watchdog reboot during launch; unrelated to the mid-play freeze but lives in the same file)
- `patches/device-ui/TFTView_320x240.cpp`
  - `notifyMessagesRestored()` — sets `g_mmMessagesRestored = true` used by the browser-open gate in `runOnce`
- `src/concurrency/Lock.cpp` — the non-recursive binary semaphore behind `spiLock`. No owner/time instrumentation yet.

---

## Related docs

- `docs/2026-04-20-session-sd-rom-ui.md` — full prior session log covering the SD-cache redesign that was rolled back, the font sweep that was kept, and the ROM-load + MMT-flow experiments that informed the emulator-stability branch.

---

## 9. Extended investigation (b76 — b86)

Continuing from the handoff snapshot above. Everything in this section is on top of `emulator-stability-b75`.

### b76–b77: priority-inheriting Lock + tryLock + spiLock owner telemetry — REVERTED

`concurrency::Lock` swapped from `xSemaphoreCreateBinary` to `xSemaphoreCreateMutex` (every `Lock` in the project, not just `spiLock`), added `tryLock(timeoutMs)`, and added `owner()` / `heldMs()` accessors. `blitFrame()` was rewritten to use `tryLock(150)` with re-dirty-on-fail. A 1 Hz heartbeat in `runOnce` warned when `spiLock` was held > 500 ms with the owner task name.

Result: **worse.** Freezes went from "Meshtastic tasks keep running while the display dies" to full-device silence — serial went completely quiet during the freeze, implying a cross-task deadlock the binary semaphore didn't have. Reverted in b78.

Takeaway: the mutex itself is fine in isolation, but some code path outside `Lock` relied on the binary-semaphore reentrancy semantics (or relied on the lack of priority inheritance). Without a full audit of every `Lock` user in Meshtastic, the mutex swap is not safe.

### b79: disable audio — EXONERATED

Commented out the `MonsterMeshAudio` init in `MonsterMeshEmulator::begin()` to test whether `i2s_write` back-pressure was stalling the emu task. Freeze repro'd on the audio-off build, so audio was ruled out and reenabled in b82.

### b80: per-chunk `LOG_DEBUG` traces inside `blitFrame()` — REPLACED

Added enter/per-chunk/exit traces. The logging itself was too chatty and rendered the emulator visibly choppy (each chunk acquired the `DEBUG_PORT` mutex and did a USB-CDC write). Critical finding though: **blitFrame kept advancing** (`#28 chunk 2 got lock` → `#35 take lock` → `#41` …) even through what the user reported as freezes. So the render task was NOT stuck — the image on screen was just stale because the framebuffer wasn't being updated. Replaced by the quieter counter approach in b81.

### b81: per-task heartbeat counters — **KEY DIAGNOSTIC SIGNAL**

Replaced the per-chunk logs with three `volatile uint32_t` counters, each bumped at the end of its task's main unit of work:

- `g_mmEmuCount`  — bumped after `emu_.runFrame()` in `emuTaskLoop`
- `g_mmBlitCount` — bumped at the bottom of `blitFrame()`
- `g_mmRunCount`  — bumped at the top of `runOnce()`

`runOnce` runs a 1 Hz check: if the emulator is active but `emuCount` or `blitCount` hasn't advanced since the last sample, emit a `LOG_WARN [hb] emu+N blit+N run+N STUCK=...`; otherwise `LOG_INFO`. Gives a per-second health signal that survives freezes because it's on the Meshtastic task, not our own.

**Freeze signature captured on b81:**

```
[hb] emu+0 blit+0 run+20
[hb] emu+0 blit+0 run+19
[hb] emu+0 blit+0 run+20
[hb] emu+0 blit+0 run+20
[hb] emu+226 blit+47 run+11     ← resumes, catches up 226 frames
```

Emu (Core 1) and blit (Core 0) **both stop** for ~4 s and then both catch up together. The fact that two tasks pinned to different cores pause simultaneously is the whole story — this is not an emu bug or a render bug, it's something pausing both CPUs at once.

ESP32 candidates that pause both cores:
1. **Flash program/erase** — the whole ESP32 cache is disabled during writes; any task fetching from flash-resident code blocks until the op finishes.
2. **WiFi/BT stack** driver operations that take global critical sections.
3. **PSRAM cache disable** (only on some SoC revisions).
4. **Power management** routines (deep-sleep entry/exit).

### b82: reenable audio — as intended after b79

Trivial revert of the audio disable.

### b83: suppress `LogRotate::write()` while MonsterMesh is foreground — DIDN'T FIX

Patched `patches/device-ui/LogRotate.cpp` (new patch file, copied into the lib tree) to early-return when the new flag `extern "C" volatile bool g_mmSuppressFlashWrites` is true. `MonsterMeshModule::runOnce` keeps the flag in sync with `emulatorActive_`. LogRotate is the highest-frequency flash write source in Meshtastic — packet history is persisted on every inbound packet.

Result: same `[hb] emu+0 blit+0 run+20` freeze pattern. LogRotate writes **are not** the only cross-core pause source. (They may still be one of several — the change stays in.)

### b84: also suppress `NodeDB::saveToDisk()` — DIDN'T FIX

`NodeDB::saveToDisk` commits node state + config to NVS/LittleFS on every node-info update. Added the same `g_mmSuppressFlashWrites` early-return in `src/mesh/NodeDB.cpp::saveToDisk`.

Result: same freeze pattern.

### b85: widen the flag to also cover the ROM browser phase — DIDN'T FIX

On b84 the freeze sometimes happened on the ROM loader screen before the emulator actually started, when `emulatorActive_` was still false and the flag wasn't set. Changed the gate to `emulatorActive_ || browserActive_` so the entire MonsterMesh foreground session suppresses flash writes.

Result: same freeze pattern. ROM loader still freezes too. Flash-write suppression is no longer a credible sole explanation for the freezes.

### b86 (pending verify): disable WiFi — FINAL CHEAP TEST

One-latch check in `runOnce`: the first time `emulatorActive_ || browserActive_` becomes true, call `WiFi.disconnect(true) + WiFi.mode(WIFI_OFF)` and never turn it back on until reboot. BLE still works for phone admin.

If b86 fixes the freezes → WiFi stack driver was causing the pauses (likely periodic scans or connection maintenance).
If b86 doesn't fix the freezes → flash and WiFi are both ruled out; the cross-core pause is coming from somewhere deeper (ESP-IDF power management, Arduino runtime critical section, or something we haven't considered).

### What we **know** after all this

- The freeze is **not** MonsterMesh's emu task bug (counter stops, no stack smash, no panic).
- The freeze is **not** MonsterMesh's render task bug (same).
- The freeze is **not** audio (b79 ruled out).
- The freeze is **not** MonsterMesh's direct Serial.print spew (b71 sweep didn't fix, and the pattern isn't corruption-shaped).
- The freeze is **not** daycare / SD cache / lobby / LORD / battle engine (all stripped on emulator-stability).
- The freeze is **not** LogRotate flash writes alone (b83).
- The freeze is **not** NodeDB flash writes alone (b84).
- The freeze is **not** any of the above combined, including the browser phase (b85).

### What to try next (Codex handoff)

1. **Confirm b86 result.** If WiFi-off stops freezes, that's the answer: wrap the `WiFi.mode(WIFI_OFF)` call in a user-configurable toggle (default off while gaming, restorable on eject) and ship.
2. **Add TWDT panic reliably.** The `esp_task_wdt_add` calls on b74/b75 logged `err=0` in the init but no TWDT panic ever fired during a freeze. If the task is truly stuck for >10 s, the watchdog should panic. Not panicking means either the task is still feeding the watchdog from a "pause" that isn't a stuck loop (consistent with the flash/WiFi cross-core pause hypothesis — the task is paused by the OS, not spinning), or the subscription didn't take. Worth a dedicated test that bumps the TWDT timeout to 3 s and confirms it fires.
3. **Disable PMU light sleep.** Meshtastic runs a power-management FSM; investigate whether light-sleep could pause both cores on certain triggers. Look at `PowerFSM.cpp` for any `esp_light_sleep_start()` calls.
4. **Dedicated Core 1 pinning check.** The Arduino default is Core 1 for the main loop. Meshtastic has modules running on Meshtastic's OSThread scheduler which may or may not be Core 1. If the emu task (Core 1, priority 5) is starved by a higher-priority WiFi/BT task that bounces between cores, we might see this pattern. `vTaskGetInfo()` inside the heartbeat to report current task list + their run time would confirm.
5. **Coredump to flash.** Enable ESP-IDF core dump capture (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`). Next panic dumps a full core image including all task stacks. Much more information than serial panic lines. Extracted via esptool.
6. **Disable BLE temporarily.** BLE advertising periodic tasks also span both cores. If WiFi-off still freezes, try BLE-off as the next cheap test.

### Commits on `emulator-stability` since b75

```
b86  (pending)     diag(mm): disable WiFi while MonsterMesh is foreground
b85                diag(mm): suppress flash writes during ROM browser too, not just emulator
b84                diag(mm): also gate NodeDB::saveToDisk on g_mmSuppressFlashWrites
b83                diag(mm): suppress LogRotate flash writes while emulator is active
b82                diag(mm): reenable audio — I²S ruled out as freeze cause
b81                diag(mm): replace blitFrame logs with three-counter 1Hz heartbeat
b80                diag(mm): enter/chunk/exit LOG_DEBUG traces inside blitFrame
b79                diag(mm): disable audio to test if I²S back-pressure drives freezes
b78                Revert "fix(concurrency): priority-inheriting Lock + tryLock + owner telemetry"
b77                fix(concurrency): priority-inheriting Lock + tryLock + owner telemetry  [REVERTED]
b76  (stamp reuse) diag(mm): subscribe emu + render tasks to ESP-IDF task watchdog  [docs — b74 actual]
```

### Key code anchors (as of b86)

- `src/modules/monstermesh/MonsterMeshModule.cpp`
  - Globals ~line 39 — `g_mmBlitCount` / `g_mmEmuCount` / `g_mmRunCount` / `g_mmSuppressFlashWrites`
  - `runOnce` — updates `g_mmSuppressFlashWrites`, kills WiFi latch, heartbeat/STUCK log
  - `emuTaskLoop` — `esp_task_wdt_reset()` + `g_mmEmuCount++` per frame
  - `renderTaskLoop` — `esp_task_wdt_reset()` per iteration, calls `blitFrame()`
  - `blitFrame` — 4-chunk push of framebuffer to TFT under `spiLock` with `vTaskDelay(1)` between chunks, bumps `g_mmBlitCount` at exit
  - Task creation — `esp_task_wdt_init(10, true)` + `esp_task_wdt_add()` for both tasks
- `patches/device-ui/LogRotate.cpp` — `write()` early-returns when `g_mmSuppressFlashWrites` is true. **Must be copied into** `.pio/libdeps/t-deck-tft/meshtastic-device-ui/source/util/LogRotate.cpp` after edits.
- `src/mesh/NodeDB.cpp` — `saveToDisk` early-returns when `g_mmSuppressFlashWrites` is true.
