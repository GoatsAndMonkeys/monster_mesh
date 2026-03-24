# MonsterMesh Handoff Notes (2026-03-24)

## Current State

The module compiles and the emulator runs on T-Deck, but **keyboard input is not yet working**. The firmware needs to be flashed and tested on the **base `t-deck` env** (not `t-deck-tft`).

A built firmware.factory.bin exists at:
```
meshtastic-firmware/.pio/build/t-deck/firmware.factory.bin
```

## Critical Finding: t-deck-tft Keyboard is a Dead End

The `t-deck-tft` Meshtastic build uses device-ui + LVGL for display and input. **The keyboard does not work AT ALL on t-deck-tft** — not for MonsterMesh, and not even for Meshtastic's own UI (can't type in chat).

### What was tried on t-deck-tft (all failed):
1. **InputBroker subscription** — InputBroker is `nullptr` on TFT/COLOR builds. Meshtastic's `Modules.cpp` line 120 explicitly skips InputBroker creation when `displaymode == COLOR`.
2. **Direct I2C polling (Wire.requestFrom 0x55)** — device-ui's `TDeckKeyboardInputDriver` reads 0x55 first in its LVGL tick, consuming the keypress. Our poll always gets 0.
3. **Patching device-ui's TDeckKeyboardInputDriver** — Added a `tdeck_setKeyCallback()` function pointer callback. LTO (Link Time Optimization) stripped the symbol. Fixed with `extern "C" __attribute__((used))` — symbol survived in binary, but keyboard still didn't respond. The keyboard itself appears non-functional on t-deck-tft v2.7.15.
4. **Previous session attempts** (from project memory) — LVGL read_cb replacement, lv_indev_add_event_cb, lv_indev_delete, lv_indev_enable, vTaskSuspend. All failed.

### Conclusion: Use base `t-deck` build

| Feature | `t-deck` (base) | `t-deck-tft` |
|---------|-----------------|--------------|
| Keyboard | Works (InputBroker) | Broken |
| SD card | Crashes (SPI conflict) | Works |
| Display | LovyanGFX OLED-style | LVGL TFT |

**The path forward is base `t-deck` with the SD crash fixed.**

## SD.begin() Crash Fix (Applied but Untested)

The T-Deck shares one SPI bus for TFT, SD, and LoRa. Calling `SD.begin()` while LovyanGFX owns the bus crashes the ESP32-S3 (StoreProhibited exception).

**Fix:** Wrap SD.begin() in Meshtastic's SPI mutex:
```cpp
#include "SPILock.h"
#include "concurrency/LockGuard.h"

{
    concurrency::LockGuard g(spiLock);
    sdOk = SD.begin(SPI_CS);  // SPI_CS = GPIO 39
}
```

This is already applied in the module code but has NOT been flashed and tested yet. The last successful build was for `t-deck` env.

## Key Technical Details

### T-Deck Hardware
- ESP32-S3 + 8MB PSRAM + ST7789V 320x240 TFT + SX1262 LoRa
- Keyboard: ESP32-C3 at I2C address 0x55 (SDA=18, SCL=8)
- Keyboard power: GPIO10 must be HIGH (handled by Meshtastic main.cpp)
- SD card: CS=GPIO39, shares SPI bus with TFT (CS=12) and LoRa (CS=9)
- Buzzer: GPIO14

### ALT Key
The T-Deck "ALT" key sends `0x0c` over I2C. Meshtastic's `kbI2cBase.cpp` uses it as a modifier toggle (`is_sym`). Our `handleKeyPress()` handles this:
- `0x0c` -> toggle `kbSym_` flag
- `kbSym_ + 'e'` -> toggle emulator on/off
- `0x05` (Ctrl+E) also works as toggle (if keyboard sends it)

### getLovyanGfx() Linker Error
TFTDisplay.cpp has display driver classes in mutually exclusive `#elif` blocks. The `getLovyanGfx()` accessor was only in the ST7735S block. Added it to the ST7789 block too (line ~618). Patch is in `patches/TFTDisplay.patch`.

### LTO (Link Time Optimization) Gotcha
The t-deck build uses LTO. Functions defined in library code (like device-ui) that are only called from application code may be stripped. Use `extern "C" __attribute__((used))` to prevent this. Check with:
```bash
xtensa-esp32s3-elf-nm firmware.elf | grep functionName
```

### Meshtastic Module Registration
- Module is `#include`'d and `new`'d in `src/modules/Modules.cpp` inside `#if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH` guards
- `setup()` is NEVER called by Meshtastic -- use lazy init in `runOnce()` with `millis() < 8000` guard
- Uses PRIVATE_APP portnum on channel 1 ("MonsterMesh Center")

### Build
```bash
source ~/Documents/mesh_bbs/.venv/bin/activate
cd ~/Documents/pokemesh/meshtastic-firmware
pio run -e t-deck     # <-- use this, NOT t-deck-tft
```

### Flash
```bash
source ~/Documents/mesh_bbs/.venv312/bin/activate
esptool --chip esp32s3 --port /dev/cu.usbmodem2101 \
  --before default-reset --after hard-reset \
  write-flash 0x0 .pio/build/t-deck/firmware.factory.bin
```
Port may vary: check `ls /dev/cu.usb*`. If connection fails, power cycle T-Deck (hold trackball + replug for bootloader mode).

## BOOT FREEZE on Base t-deck Build (2026-03-24 session 2)

**The base `t-deck` build freezes at the Meshtastic boot logo when MonsterMesh module is enabled.** Disabling the module (`// monsterMeshModule = new MonsterMeshModule()` in Modules.cpp) allows normal boot.

### What was tested:
1. **Module disabled** -- boots fine, keyboard works in Meshtastic UI
2. **Module enabled, `ensureMonsterMeshChannel()` disabled** -- still freezes
3. **Module enabled, full code** -- freezes at boot logo

### The freeze happens before runOnce() even fires (8-second delay guard)
This means the crash is in:
- The `new MonsterMeshModule()` constructor chain, OR
- A static initializer pulled in by one of the MonsterMesh source files, OR
- The `#include <SD.h>` or peanut_gb.h headers triggering problematic global constructors

### Likely culprits (not yet tested individually):
1. **`#include <SD.h>`** -- on ESP32-S3, the SD library may have global constructors that conflict with the SPI bus (already owned by LovyanGFX at boot time)
2. **`peanut_gb.h`** (4000-line header-only Game Boy emulator) -- included in MonsterMeshEmulator.cpp, may have problematic static variables
3. **OSThread constructor** -- `concurrency::OSThread("MonsterMesh")` registers the thread immediately; if the scheduler calls `runOnce()` before Meshtastic is ready, even the 8-second guard may not help if the thread registration itself conflicts
4. **Member object construction** -- `shim_(transport_)` and `lobby_(transport_, emu_)` construct FreeRTOS queues and other objects that may conflict during early boot

### Next debugging steps:
1. Strip module to absolute minimum (just constructor + empty runOnce returning 1000) and see if it boots
2. If it boots, add back components one by one: first includes, then member objects, then SD, then emulator
3. If just the includes cause the freeze, the problem is in `<SD.h>` or `peanut_gb.h` global init
4. Consider using `-D MESHTASTIC_EXCLUDE_MONSTERMESH` build flag to disable module without code changes

### Memory/size is NOT the issue:
- RAM: 42.0% (137KB of 320KB)
- Flash: 32.8% (2.1MB of 6.5MB)

## Next Steps

1. **Debug the boot freeze** -- binary search which component causes it
2. **Test keyboard** -- once boots, ALT+E should toggle emulator via InputBroker
3. **If SD.begin() still crashes after boot** -- try spiLock, or init SD before TFT
4. **Scanline rendering** -- getLovyanGfx() accessor needed for base build too

## Files Modified in Meshtastic Firmware

- `src/modules/monstermesh/` -- entire module directory (copy from `meshtastic-module/`)
- `src/modules/Modules.cpp` -- include + instantiation (see `patches/Modules.patch`)
- `src/graphics/TFTDisplay.cpp` -- getLovyanGfx() accessor (see `patches/TFTDisplay.patch`)
