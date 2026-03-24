# Audit Log 04 — Reconciled Findings

Date: 2026-03-24
Branch: `dev_tc`
Basis: Reconciliation of `audit_log_03.md` (Codex) and `audit_log_03_gemini.md` (Gemini), verified against current source

## Reported Symptoms

- The keyboard still is not working
- The emulator is not running

## Summary

Five distinct bugs were identified across the two prior audits. After source verification, one is confirmed as the primary blocker for the Meshtastic module, two are secondary blockers in the Meshtastic module, one is a latent safety bug affecting both codebases, and one is a usability bug in the standalone firmware only. One Gemini finding (cross-core I2C conflict) does not hold up under modern ESP-IDF.

---

## Finding 1 — PRIMARY BLOCKER: Direct I2C poll races with and starves InputBroker's modifier translation

**Source:** Codex audit finding 1 (confirmed, refined)

**Status: CONFIRMED — this is the most direct explanation for both reported symptoms**

**Files:**

- `meshtastic-module/MonsterMeshModule.cpp:240-253` (`pollKeyboard()`)
- `meshtastic-module/MonsterMeshModule.cpp:66-100` (`handleInputEvent()`)
- `meshtastic-module/MonsterMeshModule.cpp:405-418` (`handleKeyPress()`)
- `meshtastic-module/MonsterMeshModule.h:84` (`kbSym_` — declared, never used)
- `patches/kbI2cBase.patch`

**What happens:**

The Meshtastic module has two keyboard input paths running concurrently:

1. **InputBroker path** — `kbI2cBase::runOnce()` polls I2C address `0x55`, translates modifier sequences (SYM + `e` → `0x05` via `kbI2cBase.patch`), and delivers translated events to `handleInputEvent()` through `inputObserver_`.
2. **Direct I2C path** — `pollKeyboard()` in `emuTaskLoop()` also polls I2C address `0x55` at ~60 Hz and passes raw bytes directly to `handleKeyPress()`.

Both paths read from the same I2C address. The T-Deck keyboard controller's buffer is consumed by each read — once one reader takes the byte, the other gets `0`. Since `pollKeyboard()` runs at 60 Hz on the dedicated emulator task (Core 1), it frequently steals bytes before `kbI2cBase::runOnce()` on Meshtastic's main loop can read them.

**Why the toggle key fails:**

The emulator toggle requires byte `0x05` (Ctrl+E). This byte is synthesized by `kbI2cBase` from a two-byte SYM + `e` sequence (SYM = `0x0C`, `e` = `0x65`). When `pollKeyboard()` steals either the SYM byte or the `e` byte from the I2C bus, `kbI2cBase` never sees the complete sequence and never generates `0x05`. The raw bytes `0x0C` and `0x65` reach `handleKeyPress()` individually — `0x0C` falls through to `default: return;`, and `0x65` maps to no Game Boy button (also `default: return;`).

Additionally, `kbSym_` is declared in the header (line 84) but never referenced in `pollKeyboard()`, confirming there is no modifier-state machine in the direct path.

**Why all game keys appear dead:**

`handleKeyPress()` at line 418: `if (!emulatorActive_) return;` — since the toggle never fires, `emulatorActive_` stays `false`, and every game key (W/A/S/D, K, L, Enter, Space) is discarded.

**Impact:** Both reported symptoms — "keyboard not working" and "emulator not running" — are fully explained by this single bug.

**Fix approach:** Remove `pollKeyboard()` from the Meshtastic module entirely and rely on `handleInputEvent()` via InputBroker, which already correctly handles both the toggle key and game keys. Alternatively, if direct I2C polling is needed for TFT builds where InputBroker may be unavailable, implement a proper modifier state machine in `pollKeyboard()` (using `kbSym_`) AND disable `kbI2cBase` polling to eliminate the race.

---

## Finding 2 — Null `gb_error` callback passed to `gb_init` (latent crash)

**Source:** Gemini audit finding 1 (confirmed, severity adjusted)

**Status: CONFIRMED — latent bug, not the active blocker**

**Files:**

- `standalone-firmware/EmulatorApp.cpp:28-34` — `gb_init(..., nullptr, this)`
- `meshtastic-module/MonsterMeshEmulator.cpp:35-41` — `gb_init(..., nullptr, this)`
- `meshtastic-module/peanut_gb.h:886` — `(gb->gb_error)(gb, GB_INVALID_READ, addr);` (no null guard)
- `meshtastic-module/peanut_gb.h:3271` — `(gb->gb_error)(gb, GB_INVALID_OPCODE, ...);` (no null guard)
- `meshtastic-module/peanut_gb.h:3883-3885` — documentation: "Must not be NULL"

**What happens:**

Both `EmulatorApp::begin()` and `MonsterMeshEmulator::begin()` pass `nullptr` for the `gb_error` callback parameter of `gb_init()`. The peanut_gb documentation explicitly states this parameter "Must not be NULL. Returning from this function is undefined and will result in SIGABRT."

Inside `peanut_gb.h`, the error callback is invoked without a null check on two code paths:
- `GB_INVALID_READ` (line 886) — triggered by out-of-range memory reads
- `GB_INVALID_OPCODE` (line 3271) — triggered by unknown CPU opcodes

On ESP32, calling through a null function pointer causes a `LoadProhibited` exception and an immediate reboot.

**Severity assessment:**

Gemini's audit described this as occurring "during normal emulator operation or initialization." This overstates the likelihood. With a known-good Pokemon Red/Blue ROM, hitting an invalid opcode or out-of-range read during normal gameplay is unlikely. The error paths are defensive guards for malformed ROMs, PSRAM corruption, or edge cases. However:
- A corrupted ROM file on SD would trigger this during init
- A PSRAM bit-flip during play could trigger this unpredictably
- The crash would manifest as a silent ESP32 reboot with no diagnostic output

**Fix:** Define a static error callback in both `EmulatorApp` and `MonsterMeshEmulator` that logs the error and halts gracefully, then pass it to `gb_init()` instead of `nullptr`. Example:

```cpp
static void gbErrorCb(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr) {
    Serial.printf("[EMU] peanut_gb error %d at 0x%04X\n", (int)err, addr);
    while (true) vTaskDelay(portMAX_DELAY); // halt — do not return
}
```

---

## Finding 3 — TFT keyboard fallback depends on successful emulator startup

**Source:** Codex audit finding 2 (confirmed)

**Status: CONFIRMED — secondary blocker**

**Files:**

- `meshtastic-module/MonsterMeshModule.cpp:238` (`installKeyboardHook()` — empty body)
- `meshtastic-module/MonsterMeshModule.cpp:263-270` (`emuTaskLoop()` — only runs if task was created)
- `meshtastic-module/MonsterMeshModule.cpp:177-181` (SD mount failure path)
- `meshtastic-module/MonsterMeshModule.cpp:187-202` (emulator task creation, only on ROM success)

**What happens:**

- `installKeyboardHook()` has an empty body (line 238)
- `pollKeyboard()` is only called from `emuTaskLoop()` (line 270)
- The emulator task is only created if both `SD.begin()` and `emu_.begin("/pokemon.gb")` succeed (lines 177-197)
- If SD mount fails, `runOnce()` returns early at line 181 with the InputBroker observer registered but `pollKeyboard()` never called
- If ROM load fails, the task is never created (lines 199-202)

**Impact:** Any startup failure eliminates the direct I2C keyboard path entirely. On TFT builds where InputBroker may be disabled or routed through LVGL, this leaves no keyboard input at all.

**Note:** Even after finding 1 is fixed (so InputBroker works), this finding still matters for TFT-specific builds where InputBroker might not be available.

---

## Finding 4 — Emulator initialization is single-shot with no retry

**Source:** Codex audit finding 3 (confirmed)

**Status: CONFIRMED — secondary reliability issue**

**Files:**

- `meshtastic-module/MonsterMeshModule.cpp:154-158` (`setupDone_ = true` before success is known)
- `meshtastic-module/MonsterMeshModule.cpp:177-181` (SD failure path — returns, never retries)
- `meshtastic-module/MonsterMeshModule.cpp:199-202` (ROM failure path — continues without task)

**What happens:**

`setupDone_` is set to `true` at line 158, before SD mount or ROM load is attempted. If either fails, the init block is never re-entered because the `if (!setupDone_)` guard (line 154) permanently blocks re-entry.

**Impact:** A transient SD failure (card not yet ready, SPI bus contention) or a brief file-not-found condition permanently disables the emulator for the rest of that boot. The only recovery is a full device reboot.

**Fix:** Either defer setting `setupDone_ = true` until after successful initialization, or add a retry mechanism with a backoff (e.g., retry SD mount up to 3 times with 1-second delays).

---

## Finding 5 — Key-hold duration too short in standalone firmware

**Source:** Gemini audit finding 2B (confirmed, scope corrected)

**Status: CONFIRMED — standalone firmware only, usability issue**

**Files:**

- `standalone-firmware/InputMap.h:62-74` (`pollKeyboard()`)

**What happens:**

In `InputMap::pollKeyboard()`, when the keyboard returns `0` (no new key event), the code immediately clears the key state:
```cpp
if (key == 0) {
    noInterrupts();
    state   &= ~kbMask_;
    kbMask_  = 0;
    interrupts();
}
```

At ~60 Hz polling, a key press lasts only ~16ms (one frame) before being cleared on the next poll that returns 0. The T-Deck keyboard does not send continuous key-down events while a key is held — it sends one character on press and `0` thereafter.

**Impact:** Game inputs last only one frame. Single-press interactions (menu selection with A/B, starting with Start) work but feel unresponsive. Holding directional keys for continuous movement requires rapid repeated presses.

**Note — Meshtastic module is NOT affected:** The Meshtastic module uses a timer-based release at `MonsterMeshModule.cpp:347-353`:
```cpp
if (kbMask_ && lastKeyMs_ && (millis() - lastKeyMs_ > KEY_RELEASE_MS)) {
    joypadState_ &= ~kbMask_;
    kbMask_ = 0;
}
```
With `KEY_RELEASE_MS = 100`, keys are held for 100ms — significantly better behavior. The standalone firmware should adopt a similar approach.

---

## Rejected Finding — Cross-core I2C conflict in standalone firmware

**Source:** Gemini audit finding 2A

**Status: NOT CONFIRMED as a real bug**

**Gemini's claim:** `Wire.begin()` in `setup()` (Core 1) binds the I2C ISR to Core 1, and `kbPollTask` on Core 0 calling `Wire.requestFrom()` causes cross-core synchronization failures.

**Why this doesn't hold:**

1. On ESP-IDF v4.4+ (which modern PlatformIO ESP32-Arduino uses), the I2C driver is fully thread-safe. `Wire.requestFrom()` calls into `i2c_master_cmd_begin()`, which acquires a mutex. Cross-core access is handled correctly by the FreeRTOS port.
2. In the standalone firmware, only `kbPollTask` (Core 0) accesses `Wire` after setup. The emulator task (Core 1) does not touch I2C. There is no concurrent access to contend.
3. The I2C "ISR bound to a core" model Gemini describes applies to older ESP-IDF versions or bare-metal I2C usage, not the Arduino `Wire` library on current toolchains.

While moving `Wire.begin()` to `kbPollTask` would be marginally cleaner, the current code is functionally correct and is not causing the keyboard to fail.

---

## What Is Not The Main Blocker

The `getLovyanGfx()` linker issue from earlier audits is resolved:
- `patches/TFTDisplay.patch` adds the `getLovyanGfx()` accessor function
- `scanlineCallback()` in `MonsterMeshModule.cpp:364-380` uses it correctly

The display pipeline is not the current blocker.

---

## Priority Order for Fixes

| Priority | Finding | Scope | Severity |
|----------|---------|-------|----------|
| **P0** | #1 — I2C poll races with InputBroker | Meshtastic module | Blocks both keyboard and emulator |
| **P1** | #2 — Null `gb_error` callback | Both codebases | Latent crash on any emulator error |
| **P1** | #4 — Single-shot init with no retry | Meshtastic module | Permanent failure on transient SD issues |
| **P2** | #3 — No keyboard without emulator task | Meshtastic module | No input if SD/ROM fails |
| **P2** | #5 — 16ms key hold in standalone | Standalone firmware | Poor input feel |

---

## Verification Notes

All findings verified directly from source on the `dev_tc` branch:

- `MonsterMeshModule.cpp`, `MonsterMeshModule.h` — keyboard paths, init flow, key release timer
- `MonsterMeshEmulator.cpp` — `gb_init()` call with `nullptr` error callback
- `EmulatorApp.cpp`, `EmulatorApp.h` — standalone `gb_init()` call
- `standalone-firmware/main.cpp` — task pinning, `Wire.begin()` location
- `standalone-firmware/InputMap.h`, `InputMap.cpp` — key state logic, ISRs
- `meshtastic-module/peanut_gb.h` — `gb_init` signature, `gb_error` call sites (lines 886, 3271), documentation (lines 3883-3885)
- `patches/kbI2cBase.patch` — SYM+E → 0x05 translation
- `patches/TFTDisplay.patch` — `getLovyanGfx()` accessor

No build toolchain (`pio` / `platformio`) was available; all findings are from source-level analysis, not runtime testing.
