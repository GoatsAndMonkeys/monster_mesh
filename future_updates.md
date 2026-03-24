# Future Updates

## Meshtastic Rebase (from v2.7.15 to current develop)

The Meshtastic module is currently pinned to `v2.7.15` (tag `v2.7.15.567b8ea`). Both patches in `patches/` apply cleanly against that tag but fail on current `master` and `develop`. Once the module itself is working correctly on v2.7.15 (init path, double-init, TFT hooks — audit findings 1, 8, 3), the next step is rebasing onto modern Meshtastic.

### What will need to change

**1. `patches/Modules.patch` — module registration**

This patch inserts a `#include` and a `new MonsterMeshModule()` call into `src/modules/Modules.cpp`. The patch context (surrounding lines, line numbers) has drifted on current upstream. To rebase:

- Check out the target Meshtastic tag/branch
- Open `src/modules/Modules.cpp`
- Find the include block (near the other `#include "modules/..."` lines) and add:
  ```cpp
  #if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH
  #include "modules/monstermesh/MonsterMeshModule.h"
  #endif
  ```
- Find `setupModules()` and add the instantiation near the other module constructors:
  ```cpp
  #if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH
      monsterMeshModule = new MonsterMeshModule();
  #endif
  ```
- Regenerate the patch: `git diff > patches/Modules.patch`

The logic is identical — only the line numbers and surrounding context will differ.

**2. `patches/kbI2cBase.patch` — Ctrl+E keyboard hook**

This patch adds a `case 0x65:` block to `src/input/kbI2cBase.cpp` inside the key-handling switch statement. Same situation: the patch context has drifted. To rebase:

- Open `src/input/kbI2cBase.cpp` on the target branch
- Find the key-handling switch (look for existing `case 0x67:` for the 'g' key)
- Add the `case 0x65:` block immediately before it (same content as current patch)
- Regenerate: `git diff > patches/kbI2cBase.patch`

**3. Build target: `t-deck` to `t-deck-tft`**

Current upstream has moved the T-Deck TFT display build to the `t-deck-tft` environment. Update:

- `README.md` line 13: change `t-deck` to `t-deck-tft`
- `README.md` line 17: change `pio run -e t-deck` to `pio run -e t-deck-tft`

**4. `getLovyanGfx()` symbol for TFT rendering**

The scanline renderer in `MonsterMeshModule.cpp` references `extern lgfx::LGFX_Device *getLovyanGfx();`. This symbol was not found in v2.7.15 or current upstream. When rebasing, this needs to be resolved — either by finding the equivalent upstream display accessor on the target branch, or by adding a small shim that exposes the LovyanGFX instance. This is tied to audit finding 3 (TFT path incomplete) and should be addressed as part of that work.

**5. Verify `T_DECK` preprocessor define**

The patches and module code gate on `#if defined(T_DECK)`. Confirm this define still exists in the target branch's `t-deck-tft` variant configuration. If it was renamed (e.g., to `T_DECK_TFT`), update all guards.

### Recommended approach

1. Fix findings 1, 8, and 3 first (module init, double-init, TFT hooks) against v2.7.15
2. Confirm the module works end-to-end on v2.7.15
3. Pick a specific Meshtastic release tag (not a moving branch) as the rebase target
4. Rebase both patches and re-test
5. Update README with the new tag and build target

## Standalone T-Deck Variant (Finding 7) — Resolved

The custom board variant at `standalone-firmware/variants/tdeck/pins_arduino.h` has been deleted. No future work is needed.

**Why it's safe to delete permanently**: Every pin assignment in the standalone firmware uses explicit constants from `standalone-firmware/pins.h`. Specifically:

- `Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL)` — explicit pins, not Arduino defaults
- `SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SD_CS)` — explicit pins
- All other peripherals (TFT, radio, buzzer, trackball, SD) use named pin constants

No code path relies on the Arduino variant's default `SDA`, `SCL`, `MOSI`, `MISO`, or `SCK` macros. The `platformio.ini` never referenced the custom variant anyway (`board_build.variant` and `board_build.variants_dir` were never set). The variant file had wrong pin values copied from an `esp32s3box` template and would have caused problems if it had been wired in.

If a future library is added that calls bare `Wire.begin()` or `SPI.begin()` without explicit pins, the fix is to pass explicit pins at the call site (matching `pins.h`), not to resurrect a custom variant.
