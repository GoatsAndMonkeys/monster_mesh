# MonsterMesh emulator mid-play freeze — handoff

**Branch:** `emulator-stability`
**Current build:** `emulator-stability-b87` (everything below b75 in §9 applies; b86 WiFi-off reverted in b87)
**Devices:** LilyGO T-Deck × 2 — Red (`!f1a05e70`), Blue (`!a1ad6880`)

Repro'd on both devices. This doc captures everything we've tried, what worked, what didn't, and where to look next. Written as a handoff so Codex (or a fresh session) can pick up without re-reading the chat.

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

## 9. Extended investigation (b76 — b87)

Continuing from the b75 snapshot in the sections above.

### b76–b78: priority-inheriting Lock + tryLock + spiLock telemetry — REVERTED

Swapped `concurrency::Lock` from `xSemaphoreCreateBinary` to `xSemaphoreCreateMutex` (every `Lock` in the project, not just `spiLock`), added `tryLock(timeoutMs)`, added `owner()` / `heldMs()` accessors, a 1 Hz watcher that logs a `WARN` if `spiLock` is held > 500 ms with the owner task name, and rewrote `blitFrame()` to use `tryLock(150)` with re-dirty-on-fail.

Result: **worse.** Freezes turned into full-device silence — zero serial for 25 s during the freeze. Some code path outside `Lock` relied on the binary-semaphore reentrancy semantics or on the absence of priority inheritance. Reverted in b78.

### b79: disable audio — EXONERATED

Guarded the `MonsterMeshAudio` init in `MonsterMeshEmulator::begin()` with `#if 0` to rule out `i2s_write` back-pressure. Freeze still repro'd. Audio reenabled in b82.

### b80: per-chunk `LOG_DEBUG` traces inside `blitFrame()` — REPLACED

Enter/per-chunk/exit traces. Too chatty — each log line takes the `DEBUG_PORT` mutex and writes USB-CDC, so each `pushImage` got interrupted and the emulator rendered visibly choppy. Key observation before reverting: **`blitFrame` kept advancing** through user-reported freezes (we saw sequence #28, #35, #41, #48…#108). The render task was not stuck; the framebuffer was stale because the emu task wasn't writing to it.

### b81: per-task heartbeat counters — **KEY DIAGNOSTIC SIGNAL**

Replaced the per-chunk logs with three `volatile uint32_t` counters:

- `g_mmEmuCount`  — bumped after `emu_.runFrame()` in `emuTaskLoop`
- `g_mmBlitCount` — bumped at the bottom of `blitFrame()`
- `g_mmRunCount`  — bumped at the top of `runOnce()`

1 Hz check in `runOnce` logs `LOG_WARN [hb] emu+N blit+N run+N STUCK=...` if the emulator is active but either counter stops advancing. Quiet enough not to slow the emulator.

**Repeatable freeze signature on b81 and every build after:**

```
[hb] emu+0 blit+0 run+20
[hb] emu+0 blit+0 run+19
[hb] emu+0 blit+0 run+20
[hb] emu+0 blit+0 run+20
[hb] emu+226 blit+47 run+11     ← resumes, catches up 226 frames
```

Both the emu task (Core 1 / priority 5) and the render task (Core 0 / priority 2) **pause simultaneously** for ~4 s and then both catch up together, while the Meshtastic `runOnce` (on whatever core it's scheduled) keeps ticking at 20 Hz. Two different cores pausing together points at a system-level event, not a task bug.

ESP32 events that pause both cores:

1. **Flash program/erase** — cache is disabled while the op runs; any task fetching from flash-resident code blocks.
2. **WiFi or BT radio driver** operations that take global critical sections.
3. **PSRAM cache invalidation** on specific chip revisions.
4. **Power management** (light-sleep entry/exit).

### b82: reenable audio

Trivial revert of the b79 audio disable.

### b83: suppress `LogRotate::write()` while MonsterMesh is foreground — DIDN'T FIX

Patched `patches/device-ui/LogRotate.cpp` (new patch file, synced into the lib tree at build) to early-return when a new `extern "C" volatile bool g_mmSuppressFlashWrites` is true. `MonsterMeshModule::runOnce` kept the flag in sync with `emulatorActive_`.

LogRotate is Meshtastic's highest-frequency flash writer — packet history is persisted on every inbound packet. Result: same freeze signature.

### b84: also suppress `NodeDB::saveToDisk()` — DIDN'T FIX

Added the same `g_mmSuppressFlashWrites` early-return in `src/mesh/NodeDB.cpp::saveToDisk` (NVS commits on every node-info update). Result: same freeze signature.

### b85: widen the suppression flag to cover the ROM browser phase — DIDN'T FIX

Changed the gate from `emulatorActive_` to `emulatorActive_ || browserActive_` so the flag is set from the moment the user opens the ROM browser, not just when the emulator is actually running. User was seeing freezes on the ROM loader too.

Result: same freeze signature. Flash-write suppression is no longer a credible explanation for these freezes — either the relevant flash writes are happening elsewhere (ESP-IDF internals, Arduino runtime, module configs I didn't patch) or the cross-core pause isn't flash at all.

### b86: disable WiFi during MonsterMesh foreground — BROKE UI, REVERTED

Latch in `runOnce`: first time `emulatorActive_ || browserActive_` goes true, call `WiFi.disconnect(true) + WiFi.mode(WIFI_OFF)` and leave it off until reboot. BLE path stays up.

User reported **ALT no longer opens the ROM browser**. Serial log showed Meshtastic's `WifiConnect` module reconnecting every second to the user's AP, then our latch firing on the next browser-open attempt, then reconnect, then latch, in a loop — the "one-shot" latch was defeated because `WifiConnect` has its own reconnect logic. The constant WiFi bouncing stalled the UI path. Reverted in b87.

### b87: revert b86

Clean rollback of the WiFi change. Back to b85's behavior: flash-write suppression across LogRotate + NodeDB for both browser + emulator phases, plus everything b82–b85 added. Freeze still repros.

### What we **know** after b76 — b87

- The freeze is **not** a MonsterMesh emu task stack / data bug (counter stops, no stack smash, no panic).
- The freeze is **not** a MonsterMesh render task bug (render task isn't stuck — it just has nothing new to draw).
- The freeze is **not** audio (b79).
- The freeze is **not** our raw `Serial.print*` spew (b71 sweep to LOG_DEBUG).
- The freeze is **not** any of the stripped subsystems (daycare, lobby, LORD, battle engine, SAV cache).
- The freeze is **not** `LogRotate::write()` alone (b83).
- The freeze is **not** `NodeDB::saveToDisk()` alone (b84).
- The freeze is **not** any of the above combined (b85).
- Disabling WiFi without taming `WifiConnect`'s reconnect loop breaks the UI (b86).

### What's left to try

1. **Capture a real panic.** The heartbeat shows `run+20` during freezes, which means `runOnce` (on Meshtastic's OSThread scheduler) keeps firing — but we also sometimes see the device reboot. The reset must be triggering some panic we're missing because the USB-CDC re-enumerates mid-dump. Enable ESP-IDF **coredump to flash** (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`). Next panic writes a full coredump; extract later with esptool. This is the highest-info signal we haven't gotten yet.
2. **Verify TWDT is actually armed.** b75 logged `[MonsterMesh] TWDT init err=...` and `TWDT add emu err=...`. Confirm those are 0 or 0x103 on the current build. If `add` returned non-zero the watchdog isn't actually subscribed and that explains why no panic fires on a hang.
3. **Kill WiFi without letting `WifiConnect` re-enable it.** Either (a) gate `WifiConnect::reconnect()` on the same `g_mmSuppressFlashWrites` flag, (b) turn off the WiFi admin feature at build time for emulator-stability, or (c) call `WiFi.mode(WIFI_OFF)` on every runOnce so Meshtastic's reconnect is defeated. If freezes disappear with WiFi truly off, WiFi driver is the cross-core pause source.
4. **Try BLE off.** Same one-shot latch approach but for BLE. Worth trying only after WiFi because BLE is typically quieter.
5. **Inspect PowerFSM for any `esp_light_sleep_start()` or `vTaskDelay`-style holds that span both cores.** Meshtastic's idle/light-sleep path is another cross-core culprit candidate.
6. **`vTaskGetInfo()` inside the heartbeat** to dump the current task list with priorities + last-run-time. Would immediately show if a Meshtastic task jumped to high priority and starved our Core 1 task.

### Commits on `emulator-stability` since b75 (newest first)

```
b87  e3e4dc166  Revert WiFi disable — WifiConnect reconnect fought the latch, broke UI
b86  188530c40  [REVERTED]  Disable WiFi during MonsterMesh foreground
b85  861bcaeac  Suppress flash writes during ROM browser too
b84  4a072a5b0  Gate NodeDB::saveToDisk on g_mmSuppressFlashWrites
b83  a07c6ab91  Suppress LogRotate flash writes while emulator is active
b82  4e44d0bca  Reenable audio — I²S ruled out
b81  996845e66  Three-counter 1Hz heartbeat (replaces per-chunk blitFrame logs)
b80  3bb6e9550  Per-chunk LOG_DEBUG inside blitFrame  [REPLACED by b81]
b79  9ff3095d0  Disable audio (diagnostic)
b78  b2ab17995  Revert "priority-inheriting Lock + tryLock + owner telemetry"
b77              priority-inheriting Lock + tryLock + owner telemetry  [REVERTED in b78]
```

### Key code anchors as of b87

- `src/modules/monstermesh/MonsterMeshModule.cpp`
  - Globals — `g_mmBlitCount` / `g_mmEmuCount` / `g_mmRunCount` / `g_mmSuppressFlashWrites`
  - `runOnce` — keeps `g_mmSuppressFlashWrites` in sync with `emulatorActive_ || browserActive_`, runs the 1 Hz heartbeat, emits `STUCK=emu blit` when either counter stalls while the emulator is active.
  - `emuTaskLoop` — `esp_task_wdt_reset()` + `g_mmEmuCount++` per frame
  - `renderTaskLoop` — `esp_task_wdt_reset()` + `blitFrame()` per iteration
  - `blitFrame` — 4-chunk push under `spiLock` with `vTaskDelay(1)` between chunks, bumps `g_mmBlitCount` at exit
  - Task creation block — `esp_task_wdt_init(10, true)` + `esp_task_wdt_add()` for both tasks, logs returns
- `patches/device-ui/LogRotate.cpp` — `write()` early-returns when `g_mmSuppressFlashWrites`. Must be copied to `.pio/libdeps/t-deck-tft/meshtastic-device-ui/source/util/LogRotate.cpp` after edits.
- `src/mesh/NodeDB.cpp::saveToDisk` — early-return when `g_mmSuppressFlashWrites`.

### Repro notes

Same as the b75 repro steps at the top of this doc — no changes.

Heartbeat is the fastest way to classify any new freeze: filter serial for `[hb]` and look at whether `emu+` and `blit+` are both zero while `run+` is nonzero. That's "the" pattern. If you see a different pattern, it's a new class of freeze.
