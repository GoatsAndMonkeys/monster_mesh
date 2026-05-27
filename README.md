# MonsterMesh

**A Game Boy emulator and monster-battling RPG that runs as a Meshtastic module on the LilyGO T-Deck — raise your team, earn badges, and battle other trainers over LoRa radio, no internet required.**

**Version:** pre-alpha · **Status:** active development — expect bugs and breaking changes.

---

## What is this?

MonsterMesh turns a [LilyGO T-Deck](https://www.lilygo.cc/products/t-deck) (ESP32-S3 with a QWERTY keyboard, color TFT, SX1262 LoRa radio, and SD card) into a portable Game Boy that also networks with other T-Decks over the [Meshtastic](https://meshtastic.org) mesh — no servers, no internet, just long-range LoRa radio.

It is a **fork of the Meshtastic firmware** with one extra module (`src/modules/monstermesh/`). Everything Meshtastic normally does — messaging, GPS, telemetry, the node map — keeps working. Press a key and the same device becomes a Game Boy; the emulator and a full monster-RPG/social layer run alongside the mesh stack.

On top of the raw emulator, MonsterMesh adds a native **Generation-1 battle engine**, a mesh-wide **daycare** where your party socializes with monsters on nearby nodes while you're not playing, the **"Legend of Charizard"** gym-and-badges RPG (eight gyms plus an Elite Four), **player-hosted gyms** other trainers can discover and challenge over the mesh, and **live trainer-vs-trainer battles** over the radio.

> **Fan project / no ROMs included.** MonsterMesh ships **no copyrighted ROMs or save files**. You supply your own legally-obtained Game Boy `.gb` ROM and `.sav` save on the SD card. See [Legal & trademarks](#legal--trademarks).

---

## Table of contents

- [Features](#features)
- [Hardware](#hardware)
- [Controls](#controls)
- [Terminal command reference](#terminal-command-reference)
- [Getting started](#getting-started)
- [Building from source](#building-from-source)
- [Documentation](#documentation)
- [Project structure](#project-structure)
- [Credits & open-source acknowledgements](#credits--open-source-acknowledgements)
- [Legal & trademarks](#legal--trademarks)
- [License](#license)

---

## Features

### 🎮 Game Boy emulator
- Runs Generation-1 monster games (and other GB/GBC titles) at full speed on the ESP32-S3, powered by [Peanut-GB](https://github.com/deltabeard/Peanut-GB).
- T-Deck QWERTY keyboard mapped to Game Boy controls.
- SD-card **file browser** to pick a ROM; ROM + battery save (`.sav`) load straight from the card.
- Sound via on-board I2S audio ([minigb_apu](https://github.com/deltabeard/Peanut-GB)).
- Emulator keeps running in the background when you switch back to the Meshtastic UI.

### 🧬 Daycare over the mesh
When you're not actively playing, your six party monsters "check in" to a mesh-wide daycare.
- Reads species, levels, nicknames, moves, and XP directly from your Game Boy save (Gen-1 SRAM).
- Broadcasts a compact party beacon over LoRa; nearby nodes become daycare neighbors.
- A **dual-layer event generator** (hand-written templates + compositional sentence assembly) narrates what your monsters get up to with monsters on other nodes, DM'd to the other trainer's node.
- **Friendship and rivalry** scores grow between specific pairs across the mesh.
- XP earned in the daycare is written **back into your save file** (Gen-1 EXP curves + SRAM checksum fix), so progress shows up next time you boot the game.
- **34 achievements**, the rare ones broadcast to the whole mesh.
- Optional WiFi-gated **weather** events and GPS-based **day/night** events.

See [docs/DAYCARE.md](docs/DAYCARE.md).

### ⚔️ Native Gen-1 battle engine
- A from-scratch C++ Generation-1 battle engine (`Gen1BattleEngine`) — type effectiveness, STAB, critical hits, stat stages, and Gen-1's speed-tie and accuracy quirks.
- Stats are computed from base stats + level; real movesets are read from your loaded save.
- **Deterministic and pure-input** so it can run the *same* battle on two nodes over LoRa by exchanging only the moves, plus periodic state hashes for desync detection.
- Battle data (base stats, moves) is sourced from [Pokémon Showdown](https://github.com/smogon/pokemon-showdown).

See [docs/BATTLE_ENGINE.md](docs/BATTLE_ENGINE.md).

### 🏆 Legend of Charizard — the gym RPG
A door-game-style RPG layer built on the battle engine, with state that persists across reboots and resets daily:
- **Gyms 1–9** — work through the eight Kanto-style gym gauntlets to earn badges, then take on **gym 9: the Indigo Plateau** (the Elite Four + Champion).
- **Explore** — a daily wild-encounter run down a route.
- **New Game+** — once you clear the league, the gyms and Elite Four scale up for another loop.
- Persistent **badges**, a **news** ring, and per-run stats.

See [docs/GAMEPLAY.md](docs/GAMEPLAY.md).

### 📡 Multiplayer over LoRa
- **MonsterMesh Gyms (`mmg`)** — host your own party as a gym other trainers can find; `mmg` discovers gyms on the mesh and `mmg fight N` challenges a discovered gym's **five-trainer ladder**.
- **MonsterMesh Battle (`mmb`)** — a **live** trainer-vs-trainer battle: a DM challenge/accept handshake, then a deterministic networked battle where both sides exchange only moves over the radio.
- **Local `fight`** — an instant CPU battle against a nearby trainer's published beacon party (a "mirror match"), no coordination needed.

See [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).

### 🎨 Seven Game Boy themes
A device-wide theme system: **Dark, Light, DMG** (classic LCD green), **GBC** (Color teal), **Pocket** (high-contrast green), **Poke Blue**, and **Poke Red**. Green themes switch the whole UI to crisp [Cozette](https://github.com/slavfox/Cozette) bitmap fonts. Change it from the Meshtastic Settings → Theme dropdown.

See [docs/THEMES.md](docs/THEMES.md).

---

## Hardware

- **LilyGO T-Deck** — ESP32-S3, 16 MB flash, 8 MB PSRAM, 320×240 TFT, QWERTY keyboard, SX1262 LoRa, micro-SD slot, I2S speaker.
- A **micro-SD card** (FAT32) holding your own `.gb` ROM and `.sav` save files.
- **Two T-Decks** (or more) for the multiplayer features.

MonsterMesh only builds for the T-Deck target (`-D T_DECK`); the module compiles out on every other Meshtastic board.

---

## Controls

### Switching modes

| Key | Action |
|-----|--------|
| **Ctrl+E** (or **SYM+E**) | Toggle between the emulator and the Meshtastic UI |
| **ALT** (in Meshtastic UI) | Open the SD-card ROM browser |
| **SYM+ALT** | Eject the cartridge (pause back to the browser) |
| **Mic button** | Toggle sound on/off |
| Tools → **MonsterMesh Terminal** | Open the in-game terminal (RPG / battles / daycare) |

### Emulator (Game Boy) controls

| T-Deck key | Game Boy button |
|-----------|------------------|
| **W / A / S / D** | D-pad (Up / Left / Down / Right) |
| **K** | A |
| **L** | B |
| **Enter** | Start |
| **Space** | Select |

---

## Terminal command reference

Open the terminal from **Tools → MonsterMesh Terminal** in the Meshtastic UI, then type commands. `help` lists game commands; `help sys` lists system commands.

### Game commands

| Command | Effect |
|---------|--------|
| `party` | Show your party loaded from the save file |
| `daycare` | Daycare status + neighbor list |
| `gym` | List the local Kanto gym ladder and your badges |
| `gym fight <N>` | Challenge gym N (1–9); **gym 9 is the Indigo Plateau** (Elite Four + Champion) |
| `explore` | Wild-encounter run on a nearby route |
| `fight` | Local CPU battle vs a nearby trainer's party |
| `mmg` | Discover **MonsterMesh Gyms** hosted by other nodes |
| `mmg fight <N>` | Challenge a discovered MM gym's five-trainer ladder |
| `mmb` | List peers currently online for battle |
| `mmb <peer>` | **MonsterMesh Battle** — live PvP against a peer |
| `news` | Legend of Charizard news ring |
| `achievements` | List achievements you've earned |

### System commands (`help sys`)

| Command | Effect |
|---------|--------|
| `version` | Firmware build string |
| `echo <text>` | Print text |
| `clear` | Wipe the screen |
| `beacon` | Broadcast presence (daycare + battle discovery) now |

---

## Getting started

> Pre-built firmware images live in the project's `firmware-builds/` archive (named `MonsterMesh-…-app.bin` for app-only flashes and `…-factory.bin` for full images). If you're flashing yourself, **see [docs/BUILD.md](docs/BUILD.md)** — there are important cautions (never factory-flash a configured node; it wipes WiFi/keys).

1. Format a micro-SD card as **FAT32** and copy your own Game Boy ROM (`.gb`) and its save (`.sav`) onto it. Insert it into the T-Deck.
2. Flash a MonsterMesh build (see [docs/BUILD.md](docs/BUILD.md)).
3. Boot the T-Deck. It comes up in the normal Meshtastic UI.
4. Press **ALT** to open the ROM browser, pick your ROM — the emulator launches.
5. Press **Ctrl+E** any time to flip between the game and the Meshtastic UI.
6. Open **Tools → MonsterMesh Terminal** and type `help` to start the RPG/daycare/battle layer.

---

## Building from source

MonsterMesh builds with PlatformIO against the T-Deck environment:

```bash
# from the repo root, in a PlatformIO-capable Python venv
pio run -e t-deck-tft
```

Full setup, clean-build, USB and over-the-air (M5/bmorcelli-Launcher) flashing, and the device-UI patch workflow are documented in **[docs/BUILD.md](docs/BUILD.md)**.

---

## Documentation

| Doc | Contents |
|-----|----------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the module is wired into Meshtastic: threads, mode switching, the LoRa channel, SD/SPI hygiene |
| [docs/GAMEPLAY.md](docs/GAMEPLAY.md) | Player-facing guide: gyms, Elite Four, explore, NG+, the terminal |
| [docs/BATTLE_ENGINE.md](docs/BATTLE_ENGINE.md) | The Gen-1 engine: determinism, desync detection, data pipeline |
| [docs/DAYCARE.md](docs/DAYCARE.md) | Daycare over the mesh: beacons, events, friendship/rivalry, SAV write-back, achievements |
| [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md) | `mmg` player-hosted gyms, `mmb` live PvP, local `fight`, the wire protocol |
| [docs/THEMES.md](docs/THEMES.md) | The 7-theme system and Cozette fonts |
| [docs/BUILD.md](docs/BUILD.md) | Build, flashing (USB + OTA), device-UI patches, safety cautions |
| [docs/CREDITS.md](docs/CREDITS.md) | Full open-source attributions and licenses |

---

## Project structure

```
src/modules/monstermesh/
  MonsterMeshModule.{cpp,h}     Main Meshtastic module: emulator host, UI/mode switching, LoRa wiring
  MonsterMeshEmulator.{cpp,h}   Game Boy emulator wrapper around peanut_gb
  MonsterMeshFileBrowser.h      SD-card ROM browser
  MonsterMeshAudio.{cpp,h}      I2S audio output
  MonsterMeshTerminal.{cpp,h}   In-game LVGL terminal + command handler (RPG/battle/daycare)
  MonsterMeshTextBattle.{cpp,h} Battle "client": local CPU, gym, explore, and networked battles
  Gen1BattleEngine.cpp          Deterministic Gen-1 battle resolution engine
  Gen1Species.h / PokemonData.h Species name table + party data structures
  MeshtasticTransport.h         Sends MonsterMesh packets over the Meshtastic mesh
  PokemonDaycare.{cpp,h}        Daycare orchestrator + beacon broadcast
  DaycareEventGen.{cpp,h}       Dual-layer event generator
  DaycareTypes.h                Daycare data structures, beacon format, persistence
  DaycareAchievements.h         34 achievement definitions
  DaycareSavPatcher.h           Gen-1 SRAM read/write, checksum, XP & move patching
  DaycareData.h                 Species profiles (generated from PokeAPI)
  LordGyms.{cpp,h}              The eight Kanto gym rosters
  LordE4.{cpp,h}                Indigo Plateau roster (Elite Four + Champion)
  LordRoutes.{cpp,h}            Wild-encounter route pools for `explore`
  LordLogic.{cpp,h}             RPG logic: daily reset, run stats, badge/unlock/NG+ rules
  LordSave.h                    RPG persistence (badges, news, stats)
  peanut_gb.h                   Game Boy CPU/PPU/APU emulator core (Peanut-GB)
  minigb_apu.{c,h}              Game Boy audio processing unit (minigb_apu)
  showdown_gen1_basestats.h     Gen-1 base stats — generated battle data (gitignored)
  showdown_gen1_moves.h         Gen-1 move data — generated battle data (gitignored)

patches/device-ui/              Patched into the meshtastic-device-ui library at build time
  .../TFT/Themes.{cpp,h}        7-theme color system with cozette-font support
  generated/ui_320x240/screens.c          themed LVGL screens
  generated/ui_320x240/lv_font_cozette_*  Cozette bitmap fonts for the green themes
```

> `DaycareData.h` and the `showdown_*.h` battle-data headers are **generated** from PokéAPI / Pokémon Showdown rather than hand-edited, and the battle-data headers are gitignored — they're produced as part of setting up a build.

---

## Credits & open-source acknowledgements

MonsterMesh stands entirely on the work of others. Full license texts and details are in **[docs/CREDITS.md](docs/CREDITS.md)**.

| Project | Used for | License |
|---------|----------|---------|
| **[Meshtastic firmware](https://meshtastic.org)** | The mesh-networking firmware this is forked from — radio stack, UI, everything non-game | GPL-3.0 |
| **[Peanut-GB](https://github.com/deltabeard/Peanut-GB)** by Mahyar Koshkouei | Game Boy CPU/PPU emulator core (`peanut_gb.h`) | MIT |
| **[SameBoy](https://github.com/LIJI32/SameBoy)** by Lior Halphon | PPU/timing code incorporated into Peanut-GB | MIT |
| **minigb_apu** by Mahyar Koshkouei, based on **[MiniGBS](https://github.com/baines/MiniGBS)** by Alex Baines | Game Boy audio emulation (`minigb_apu.c/.h`) | MIT |
| **[Pokémon Showdown](https://github.com/smogon/pokemon-showdown)** | Gen-1 battle data: base stats and moves (`showdown_*.h`, generated) | MIT |
| **[PokéAPI](https://pokeapi.co)** | Species data used to build the daycare profiles (`DaycareData.h`, generated) | data sourced via PokéAPI |
| **[LVGL](https://lvgl.io)** | UI toolkit (via Meshtastic device-ui) | MIT |
| **[LovyanGFX](https://github.com/lovyan03/LovyanGFX)** | T-Deck TFT display driver | FreeBSD |
| **[meshtastic-device-ui](https://github.com/meshtastic/device-ui)** | The LVGL UI we theme/patch | GPL-3.0 |
| **[Cozette](https://github.com/slavfox/Cozette)** by Slavfox | Bitmap font used by the green themes | MIT |

Plus the broader Meshtastic dependency stack (RadioLib, Nanopb, Adafruit drivers, and more) — see each library's own license.

---

## Legal & trademarks

MonsterMesh is an **unofficial, non-commercial fan project**. It is **not affiliated with, endorsed by, or sponsored by** Nintendo, Game Freak, Creatures Inc., or The Pokémon Company. "Pokémon", "Game Boy", and related names, characters, and game data are trademarks and copyrights of their respective owners.

- **No game ROMs, save files, sprites, or other copyrighted game assets are included or distributed** with this project. The emulator runs ROMs you provide yourself, from your own legally-obtained cartridges.
- The Gen-1 battle/species data comes from the open-source [Pokémon Showdown](https://github.com/smogon/pokemon-showdown) and [PokéAPI](https://pokeapi.co) projects and is used for interoperability and gameplay simulation only.
- If you are a rights-holder with a concern about this project, please open an issue.

---

## License

MonsterMesh is a fork of [Meshtastic firmware](https://github.com/meshtastic/firmware) and is distributed under the **GNU General Public License v3.0** (`LICENSE`), the same license as the upstream firmware. The MonsterMesh module source under `src/modules/monstermesh/` follows the same license, except where individual files carry their own SPDX/MIT headers (the Peanut-GB, minigb_apu, Showdown, and font files retain their original upstream licenses — see [docs/CREDITS.md](docs/CREDITS.md)).
