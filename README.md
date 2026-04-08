# MonsterMesh Pre-Alpha

A Game Boy emulator and Pokemon multiplayer system running as a Meshtastic module on the LilyGo T-Deck.

**Status: Pre-Alpha** -- active development, expect bugs and breaking changes.

## What Is This?

MonsterMesh turns a T-Deck (ESP32-S3 with keyboard, TFT display, LoRa radio, and SD card) into a portable Game Boy that can battle and trade Pokemon with other T-Deck users over the Meshtastic mesh network -- no internet, no servers, just LoRa radio.

This repo is a fork of Meshtastic firmware with the MonsterMesh module added. All standard Meshtastic functionality (messaging, GPS, telemetry) still works alongside the emulator.

## Features

### Game Boy Emulator
- Runs Pokemon Red/Blue/Yellow at full speed on ESP32-S3
- T-Deck keyboard mapped to Game Boy controls
- Game ROMs and save files load from SD card
- Audio output via I2S

### Pokemon Link Cable Over LoRa
- Trade and battle Pokemon with nearby T-Deck users over mesh
- Protocol-aware proxy translates Game Boy serial link cable protocol to LoRa packets
- DM-based activation: send "MM cable on" to connect

### Pokemon Daycare Over Mesh
- When the emulator is idle, your party Pokemon check into a mesh-wide daycare
- Reads species, levels, nicknames, and XP directly from the Game Boy save file
- Hourly events: your Pokemon explore, spar, make friends with Pokemon on other nodes
- Type affinity system: same-type Pokemon bond faster, Eevee evolutions have mutual affinity
- Friendship and rivalry build across 5 tiers through social interactions
- Real XP: daycare experience writes back to the Game Boy save file on checkout using Gen 1 EXP curves, stat recalculation, and checksum fix
- Sunrise/sunset night detection from GPS for dream events
- 35 achievements, rare ones broadcast to the mesh
- Weather integration affects event types and XP multipliers

### Matchmaking (Planned)
- Queue for casual or rated battles
- ELO ranking system
- Automatic pairing by skill level

## Hardware

- **LilyGo T-Deck** (ESP32-S3, 16MB flash, 8MB PSRAM, 320x240 TFT, QWERTY keyboard, SX1262 LoRa)
- SD card for game ROMs and saves

## Building

```bash
# Activate PlatformIO environment
source /path/to/venv/bin/activate

# Build for T-Deck
pio run -e t-deck-tft

# Flash
pio run -e t-deck-tft -t upload
```

## Project Structure

```
src/modules/monstermesh/
  MonsterMeshModule.cpp/h    -- Main Meshtastic module (emulator + UI + LoRa)
  MonsterMeshEmulator.cpp/h  -- Game Boy emulator wrapper
  MonsterMeshAudio.cpp/h     -- I2S audio output
  MonsterMeshBattleShim.cpp/h -- Link cable over LoRa
  PokemonLinkProxy.cpp/h     -- Protocol-aware serial proxy
  PokemonDaycare.cpp/h       -- Daycare orchestrator
  DaycareEventGen.cpp/h      -- Dual-layer event generator
  DaycareTypes.h              -- Data structures and persistence
  DaycareAchievements.h       -- 35 achievement definitions
  DaycareSavPatcher.h         -- Gen 1 SRAM read/write/patch
  DaycareData.h               -- 151 species profiles (auto-generated from PokeAPI)
  peanut_gb.h                 -- Game Boy CPU/PPU emulator core
```

## Credits

- [Meshtastic](https://meshtastic.org) -- the mesh networking firmware this builds on
- [Peanut-GB](https://github.com/deltabeard/Peanut-GB) -- Game Boy emulator core
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) -- display driver
- [PokeAPI](https://pokeapi.co) -- species data for daycare system

## License

This project is based on Meshtastic firmware (GPL-3.0). MonsterMesh module code follows the same license.
