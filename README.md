# MonsterMesh Pre-Alpha

A Game Boy emulator and Pokemon multiplayer system running as a Meshtastic module on the LilyGo T-Deck.

**Version:** v0.1.03-pre-alpha  
**Status:** Active development — expect bugs and breaking changes.

## What Is This?

MonsterMesh turns a T-Deck (ESP32-S3 with keyboard, TFT display, LoRa radio, and SD card) into a portable Game Boy that can battle and trade Pokemon with other T-Deck users over the Meshtastic mesh network — no internet, no servers, just LoRa radio.

This repo is a fork of Meshtastic firmware with the MonsterMesh module added. All standard Meshtastic functionality (messaging, GPS, telemetry) still works alongside the emulator.

---

## Features

### Game Boy Emulator
- Runs Pokemon Red/Blue/Yellow at full speed on ESP32-S3
- T-Deck keyboard mapped to Game Boy controls
- SD card file browser (GB/GBC/SGB filtering)
- Game ROMs and save files load from SD card
- Audio output via I2S

### MonsterMesh Terminal
Access via Tools → MonsterMesh Terminal in the Meshtastic UI.

| Command | Effect |
|---------|--------|
| `party` | Show your current party from the loaded save file |
| `status` | Show daycare status and neighbor count |
| `fight <name>` | Challenge a nearby trainer to a text battle |
| `run` | Enter the roguelike dungeon crawler |
| `quit` | Return to normal Meshtastic UI |

### Gen 1 Text Battle Engine
- Full Gen 1 move resolution: type effectiveness, critical hits, STAB, stat stages
- Pokemon stats computed from Gen 1 species base stats + level
- Real moves read from your loaded Game Boy save file
- `[F]` to flee in solo/roguelike battles (Gen 1 speed-weighted formula, up to 3 attempts)
- `ESC` to forfeit in networked battles
- Showdown-sourced data: all Gen 1/2 base stats, moves, type chart

### Roguelike Dungeon Crawler
- Start with `run` in the terminal
- 3 encounters per floor, boss every 5 floors
- HP and PP persist across encounters within a run
- Neighboring trainers' parties appear as boss encounters

### Pokemon Daycare Over Mesh
- When the emulator is idle, your party Pokemon auto-check into the daycare
- Reads species, levels, nicknames, **moves**, and XP from the Game Boy save file (Gen 1 SRAM)
- Social events: Pokemon explore, spar, and bond with Pokemon on other nodes
- Type affinity system: same-type bonds faster; Eevee evolutions have mutual affinity
- Friendship and Rivalry scores tracked numerically (e.g. `[Friendship 42 +8]` in event messages)
- XP earned in events writes back to the save file using Gen 1 EXP curves + checksum fix
- 35 achievements, rare ones broadcast to the mesh
- Night detection from GPS; weather events

### Pokemon Link Cable Over LoRa
- Trade and battle Pokemon with nearby T-Deck users
- Protocol-aware proxy translates Game Boy serial link cable protocol to LoRa packets
- Activate with DM: `mm cable on`

### Theme System (7 Themes)
Change via Settings → Theme dropdown.

| Theme | Description |
|-------|-------------|
| Dark | Default dark UI |
| Light | Light UI |
| DMG | Original Game Boy LCD green |
| GBC | Game Boy Color teal palette |
| Pocket | High-contrast Pocket palette |
| Poke Blue | Gen 1 Pokemon Blue palette |
| Poke Red | Gen 1 Pokemon Red palette |

Green themes use Cozette bitmap fonts throughout the UI.

---

## Hardware

- **LilyGo T-Deck** (ESP32-S3, 16MB flash, 8MB PSRAM, 320×240 TFT, QWERTY keyboard, SX1262 LoRa)
- SD card (FAT32) for game ROMs and saves
- Two T-Decks needed for multiplayer features

---

## Key Controls

| Key | Action |
|-----|--------|
| Arrow keys | D-pad |
| Z | A button |
| X | B button |
| Enter | Start |
| Backspace | Select |
| Alt+E | Toggle emulator / Meshtastic UI |

---

## Building

```bash
source ~/meshtastic-venv/bin/activate
PLATFORMIO_BUILD_DIR=~/.pio_build_pokemesh pio run -e t-deck-tft
```

The version string (`main-mm-b60` etc.) is auto-generated from the branch name and `git rev-list --count HEAD` by `scripts/monstermesh_version.py`.

### Flash via OTA (M5Launcher)

When the device is in M5Launcher WiFi OTA mode:

```bash
FW=.pio/build/t-deck-tft/firmware.bin
SIZE=$(wc -c < "$FW")
curl -c /tmp/cookies.txt -d "username=admin&password=launcher" http://<IP>/login -o /dev/null
curl -b /tmp/cookies.txt -F "command=0" -F "size=$SIZE" http://<IP>/OTA
curl -b /tmp/cookies.txt -F "file1=@${FW};filename=firmware.bin-app.bin" http://<IP>/OTAFILE --max-time 120
```

**Device IPs (local network):**
- Blue (MMBl): `10.1.2.79`
- Red (MMRd): `10.1.2.193`

### Flash via USB

```bash
PLATFORMIO_BUILD_DIR=~/.pio_build_pokemesh pio run -e t-deck-tft -t upload --upload-port /dev/cu.usbmodem2101
```

### Build Notes

- Delete `~/.pio_build_pokemesh/t-deck-tft/` for a full clean rebuild (fixes stale library cache issues)
- Firmware binaries saved to `~/Documents/mesh_bbs/firmware-builds/MonsterMesh-v0.1.03-pre-alpha-bNN.bin`
- After editing `patches/device-ui/` files, copy to BOTH:
  - `.pio/libdeps/t-deck-tft/meshtastic-device-ui/source/graphics/TFT/`
  - `.pio/libdeps/t-deck-tft/meshtastic-device-ui/generated/ui_320x240/`

---

## Project Structure

```
src/modules/monstermesh/
  MonsterMeshModule.cpp/h      -- Main Meshtastic module (emulator + UI + LoRa wiring)
  MonsterMeshEmulator.cpp/h    -- Game Boy emulator wrapper (peanut_gb)
  MonsterMeshAudio.cpp/h       -- I2S audio output
  MonsterMeshTerminal.cpp/h    -- In-game terminal UI and command handler
  MonsterMeshTextBattle.cpp/h  -- Gen 1 text battle client (networked + roguelike)
  MonsterMeshRoguelike.cpp/h   -- Roguelike dungeon crawler
  Gen1BattleEngine.cpp/h       -- Core Gen 1 battle resolution engine
  BattlePacket.h               -- Packet format for networked battles
  MonsterMeshBattleShim.cpp/h  -- Link cable over LoRa shim
  PokemonLinkProxy.cpp/h       -- Protocol-aware serial link proxy
  PokemonDaycare.cpp/h         -- Daycare orchestrator + beacon broadcast
  DaycareEventGen.cpp/h        -- Dual-layer event generator (180 templates + compositional)
  DaycareTypes.h               -- Data structures, beacon format, persistence
  DaycareAchievements.h        -- 35 achievement definitions
  DaycareSavPatcher.h          -- Gen 1 SRAM read/write/checksum/XP/moves patch
  DaycareData.h                -- 151 species profiles (auto-generated from PokeAPI)
  showdown_gen1_basestats.h    -- Gen 1 base stats (from Pokemon Showdown)
  showdown_gen1_moves.h        -- Gen 1 move data
  showdown_gen1_typechart.h    -- Gen 1 type effectiveness table
  showdown_gen1_text.h         -- Gen 1 move/status flavor text
  showdown_gen2_moves.h        -- Gen 2 move additions
  peanut_gb.h                  -- Game Boy CPU/PPU/APU emulator core

patches/device-ui/
  Themes.cpp / Themes.h        -- 7-theme color system with cozette font support
  screens.c / screens.h        -- LVGL screens (boot, terminal button, theme dropdown)
  styles.c                     -- LVGL style definitions
  lv_font_cozette_13/20/26.c   -- Cozette bitmap fonts for green themes
  lv_i18n.c                    -- Localization strings including theme names
```

---

## Credits

- [Meshtastic](https://meshtastic.org) — the mesh networking firmware this builds on
- [Peanut-GB](https://github.com/deltabeard/Peanut-GB) — Game Boy emulator core
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) — display driver
- [Pokemon Showdown](https://github.com/smogon/pokemon-showdown) — Gen 1/2 battle data
- [PokeAPI](https://pokeapi.co) — species personality data for daycare system

## License

Based on Meshtastic firmware (GPL-3.0). MonsterMesh module code follows the same license.
