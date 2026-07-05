# Credits & open-source acknowledgements

MonsterMesh is built almost entirely out of other people's excellent open-source work. This page lists every third-party project we depend on, what it's used for, and its license. Please keep the per-file SPDX/license headers intact when editing the corresponding files.

## Core platform

### Meshtastic firmware
- **What:** The entire base — the LoRa mesh stack, the device UI, settings, GPS/telemetry, the build system. MonsterMesh is a *fork* of Meshtastic firmware with one module added.
- **Where:** the whole repo outside `src/modules/monstermesh/` and `patches/`.
- **Project:** <https://meshtastic.org> · <https://github.com/meshtastic/firmware>
- **License:** GPL-3.0
- Meshtastic® is a registered trademark of Meshtastic LLC. This is an unofficial fork and is not endorsed by Meshtastic LLC.

## Game Boy emulation

### Peanut-GB
- **What:** The Game Boy CPU/PPU emulator core (`src/modules/monstermesh/peanut_gb.h`).
- **Author:** Mahyar Koshkouei (deltabeard). © 2018–2023.
- **Project:** <https://github.com/deltabeard/Peanut-GB>
- **License:** MIT

### SameBoy (incorporated into Peanut-GB)
- **What:** PPU/timing code that Peanut-GB incorporates.
- **Author:** Lior Halphon. © 2015–2019.
- **Project:** <https://github.com/LIJI32/SameBoy>
- **License:** MIT

### minigb_apu
- **What:** Game Boy audio (APU) emulation feeding the I2S output (`src/modules/monstermesh/minigb_apu.c`, `minigb_apu.h`).
- **Authors:** Mahyar Koshkouei © 2019; based on **MiniGBS** by Alex Baines © 2017.
- **Projects:** part of <https://github.com/deltabeard/Peanut-GB> · MiniGBS <https://github.com/baines/MiniGBS>
- **License:** MIT

## Game data

### Pokémon Showdown
- **What:** Generation-1 battle data — base stats and moves — extracted into `showdown_gen1_basestats.h` and `showdown_gen1_moves.h`. These headers are generated from the Showdown data set and are gitignored, not committed.
- **Project:** <https://github.com/smogon/pokemon-showdown>
- **License:** MIT
- The generated headers carry an SPDX-MIT block and Showdown attribution — do not strip it.

### PokéAPI
- **What:** Species data (types, habitat, stats, movesets, evolution) used to build the daycare's species profiles in `DaycareData.h` (generated, not hand-edited).
- **Project:** <https://pokeapi.co>

## UI & graphics

### LVGL
- **What:** The embedded UI toolkit underlying the device UI and the MonsterMesh terminal (pulled in via Meshtastic device-ui).
- **Project:** <https://lvgl.io> · <https://github.com/lvgl/lvgl>
- **License:** MIT

### LovyanGFX
- **What:** The T-Deck TFT display driver.
- **Project:** <https://github.com/lovyan03/LovyanGFX>
- **License:** FreeBSD (BSD-2-Clause-style)

### meshtastic-device-ui
- **What:** The LVGL-based Meshtastic UI we theme and patch (`patches/device-ui/`).
- **Project:** <https://github.com/meshtastic/device-ui>
- **License:** GPL-3.0

### Cozette font
- **What:** The bitmap font used by the green (DMG/GBC/Pocket) themes (`patches/device-ui/generated/ui_320x240/lv_font_cozette_*.c`).
- **Author:** Slavfox.
- **Project:** <https://github.com/slavfox/Cozette>
- **License:** MIT

## Transitive dependencies

Because this is a Meshtastic fork, the build also pulls in Meshtastic's own dependency stack — including **RadioLib** (LoRa radio driver), **Nanopb** (protobuf), and various **Adafruit** sensor/display libraries. Each is governed by its own license; see the respective libraries.

---

## Trademarks & fan-project notice

MonsterMesh is an **unofficial, non-commercial fan project**. It is **not affiliated with, endorsed by, or sponsored by** Nintendo, Game Freak, Creatures Inc., or The Pokémon Company, nor by Meshtastic LLC.

- "Pokémon", "Game Boy", "Game Boy Color", and related names, characters, sprites, music, and game data are trademarks and copyrights of their respective owners.
- **No game ROMs, save files, sprites, music, or other copyrighted game assets are included or distributed with this project.** The emulator runs only ROMs and saves that the user supplies from their own legally-obtained cartridges.
- The Gen-1 battle/species data is sourced from the open-source Pokémon Showdown and PokéAPI projects and is used for gameplay simulation and interoperability only.
- If you are a rights-holder with a concern, please open an issue on the repository.
