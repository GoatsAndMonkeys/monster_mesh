# Monster Mesh

Game Boy emulator (Peanut-GB) for Meshtastic on LilyGO T-Deck with LoRa link battle support.

## Structure

- `meshtastic-module/` — MonsterMesh module for Meshtastic firmware (drop into `src/modules/monstermesh/`)
- `standalone-firmware/` — Standalone PokeMesh firmware (ESP32-S3 + TFT_eSPI + RadioLib)
- `patches/` — Patches to apply to Meshtastic firmware for integration

## Meshtastic Integration

Based on Meshtastic v2.7.15 (`t-deck` env).

1. Copy `meshtastic-module/` to `firmware/src/modules/monstermesh/`
2. Apply patches: `cd firmware && git apply ../patches/*.patch`
3. Build: `pio run -e t-deck`
4. Place `pokemon.gb` ROM on FAT32 SD card

## Controls

- **ALT + E** — Toggle emulator on/off
- **W/A/S/D** — D-pad
- **K** — A button
- **L** — B button
- **Enter** — Start
- **Space** — Select
- **P** — Lobby (LoRa battles)
- **Tab** — Debug overlay
