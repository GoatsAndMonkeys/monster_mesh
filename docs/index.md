---
title: MonsterMesh — Game Boy emulation & monster battles over the LoRa mesh
description: >-
  MonsterMesh turns a LilyGO T-Deck, Raspberry Pi, or Heltec board into a Game Boy
  emulator, a Generation-1 monster-battling RPG with gyms and an Elite Four, a
  mesh-wide daycare, and a WiFi/BLE "Pentest Pikachu" — all over the Meshtastic
  LoRa mesh, no internet required.
---

# MonsterMesh

**A Game Boy emulator, a monster-battling RPG, and a WiFi/BLE security toy — all running over the [Meshtastic](https://meshtastic.org) LoRa mesh, no internet required.**

MonsterMesh is a fan-made, open-source project (GPL-3.0) that turns off-the-shelf mesh-radio hardware into a portable Game Boy that *also* networks with other players over long-range LoRa radio. Raise a team, earn badges, battle other trainers over the air, and let your monsters socialize in a mesh-wide daycare while you're away.

> **No ROMs included.** MonsterMesh ships no copyrighted game ROMs, saves, or assets. You supply your own legally-obtained Game Boy `.gb` ROM. It's an unofficial, non-commercial fan project, not affiliated with Nintendo, Game Freak, or The Pokémon Company.

## Three platforms

MonsterMesh runs on three kinds of hardware, one per branch of the [GitHub repository](https://github.com/GoatsAndMonkeys/monster_mesh):

- **LilyGO T-Deck** *(ESP32-S3 handheld)* — the flagship build: a full Game Boy emulator plus the monster-RPG and mesh daycare, as a [Meshtastic](https://meshtastic.org) firmware module. → branch [`main`](https://github.com/GoatsAndMonkeys/monster_mesh/tree/main)
- **Raspberry Pi** *(RetroPie / GPi Case handheld)* — the same RPG, daycare, and LoRa battles as a terminal app (`mmd` daemon + `mmterm`). → branch [`mm/monster-mesh-pi`](https://github.com/GoatsAndMonkeys/monster_mesh/tree/mm/monster-mesh-pi)
- **Pentest Pikachu** *(Heltec V3 / V4 / T114)* — turns the radio into a Pokémon-themed WiFi/BLE security auditor, where nearby wireless vulnerabilities spawn monster battles. → branch [`pentest-pikachu`](https://github.com/GoatsAndMonkeys/monster_mesh/tree/pentest-pikachu)

All three share one deterministic Generation-1 battle engine and mesh protocol, so T-Deck, Pi, and Heltec players interoperate over the same mesh.

## Documentation

| Guide | What's inside |
|-------|---------------|
| [Gameplay](GAMEPLAY.md) | Gyms, the Elite Four, explore, New Game+, the in-game terminal |
| [Architecture](ARCHITECTURE.md) | How the module hooks into Meshtastic: threads, mode switching, SPI/SD hygiene |
| [Battle engine](BATTLE_ENGINE.md) | The deterministic Gen-1 engine, desync detection, and data pipeline |
| [Daycare](DAYCARE.md) | The mesh daycare: beacons, events, friendship/rivalry, save write-back |
| [Multiplayer](MULTIPLAYER.md) | Player-hosted gyms, live PvP battles, and the LoRa wire protocol |
| [Themes](THEMES.md) | The theme system and fonts |
| [Building & flashing](BUILD.md) | Build from source, USB/OTA flashing, and safety cautions |
| [Credits](CREDITS.md) | Open-source attributions and licenses |

## Get the code

```bash
git clone https://github.com/GoatsAndMonkeys/monster_mesh.git
cd monster_mesh
pio run -e t-deck-tft          # T-Deck firmware (main branch)
```

For the Raspberry Pi build see the [Pi README](https://github.com/GoatsAndMonkeys/monster_mesh/blob/mm/monster-mesh-pi/monster_mesh_pi/README.md); for the Heltec pentest builds check out the [`pentest-pikachu`](https://github.com/GoatsAndMonkeys/monster_mesh/tree/pentest-pikachu) branch.

---

<sub>MonsterMesh is a fork of [Meshtastic firmware](https://github.com/meshtastic/firmware) (GPL-3.0). Built on [Peanut-GB](https://github.com/deltabeard/Peanut-GB), [Pokémon Showdown](https://github.com/smogon/pokemon-showdown) data, [PokéAPI](https://pokeapi.co), and [LVGL](https://lvgl.io). See [Credits](CREDITS.md).</sub>
