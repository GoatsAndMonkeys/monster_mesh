# Pokemesh — Pokemon-themed Meshtastic fork

This fork of [meshtastic/firmware](https://github.com/meshtastic/firmware)
turns a LoRa mesh radio into a Pokemon battle pager. Every WiFi
handshake, rogue BLE advertiser, or fellow Pikachu node you encounter
triggers a Gen-1 battle whose outcome levels up your pet, fills your
Pokedex, and earns gym badges.

## Supported boards

| Board       | Env                | Role |
|-------------|--------------------|------|
| Heltec V3   | `heltec-v3`        | "Warwalker" — 802.11 WiFi pentester + 128×64 mono Pikachu pet |
| Heltec T114 | `heltec-t114-pet`  | "BLE Stalker" — Bluetooth peripheral scanner + 240×135 colour Pikachu pet |

Both boards beacon to each other on the MonsterMesh private channel and
can fight peer-to-peer over LoRa. T-Deck, T190, and other displays are
planned but not yet wired.

## What this fork adds on top of upstream Meshtastic

| Module path                        | Purpose |
|------------------------------------|---------|
| `src/modules/pentest/`             | WiFi/BLE Stalker + battle screen UI (see [pentest README](../src/modules/pentest/README.md)) |
| `src/modules/pikachu/`             | V3 Pocket Pikachu mono pet (see [pikachu README](../src/modules/pikachu/README.md)) |
| `src/modules/pikachu2/`            | T114 colour pet renderer (see [pikachu2 README](../src/modules/pikachu2/README.md)) |
| `src/modules/monstermesh/Gen1*`    | Shared Gen-1 battle engine, base stats, move table |
| `src/modules/monstermesh/gauntlet/`| Gym-leader gauntlet ladder over DM |

The upstream Meshtastic mesh protocol is **untouched** — the radio still
acts as a normal Meshtastic node. We add carousel frames and a couple of
private-app port handlers (`0xA0` beacon, `0xA1` PvP result) on the
MonsterMesh channel.

## Build

```bash
# install pio if you don't have it
pip install -U platformio

# build for Heltec V3 (ESP32-S3)
pio run -e heltec-v3

# build for Heltec T114 (nRF52840)
pio run -e heltec-t114-pet
```

Output binaries:

| Board | File |
|-------|------|
| V3    | `.pio/build/heltec-v3/firmware.factory.bin` |
| T114  | `.pio/build/heltec-t114-pet/firmware.uf2`   |

## Flash

### Heltec V3 (esptool over USB serial)

```bash
pio run -e heltec-v3 -t upload
```

Auto-resets via DTR/RTS — no manual button presses.

### Heltec T114 (UF2 over USB mass storage)

1. Double-tap the RESET button — a `T114BOOT` (or `HT-n5262`) volume mounts
2. Drag `firmware.uf2` onto that volume
3. The bootloader consumes it and reboots

You can also trigger DFU from a running T114 over Meshtastic CLI:

```bash
meshtastic --port /dev/cu.usbmodem1101 --reboot-ota
# wait ~12 s for the volume to mount
cp .pio/build/heltec-t114-pet/firmware.uf2 /Volumes/HT-n5262/
```

The flash is **app-only** — bootloader, SoftDevice, and the LittleFS
partition are left intact. Your Meshtastic config (region, channels,
node name) survives a flash as long as the protobuf schema doesn't
change. Save+restore via `meshtastic --export-config` / `--configure`
if you're paranoid.

## Battle screen at a glance (T114)

```
   [Enemy name+L#]                        [Enemy sprite ]
   [Enemy HP bar ]                        [(top-right)  ]
                                          y=2..57

   [Pika sprite ]    [Pika name+L#]
   [middle-left ]    [Pika HP bar ]
   y=22..77

   [================ pokeball-bordered text box ================]
   [        3-line word-wrapped battle log, Plain_16            ]
```

- 56×56 sprites per species (deflate-compressed atlas, ~84 KB)
- Per-species GBC custom palette by default; toggleable to GBC green-scale
- KO freeze: live battle screen with the fainted mon at 0 HP stays
  visible for 4.5 s while the log queue drains, then VICTORY/DEFEAT
  screen for 5 s
- Status icons (PAR/SLP/PSN/BRN/FRZ) next to each HP bar

## Encounter mix

Roughly 85 % wild Pokemon (Kanto zones gated by player level), 15 % gym
leader battles. Legendaries are tiered: Mewtwo/Mew at 0.5 %,
pseudo-legendaries (Dratini line) at 2 %, rare overlay (Snorlax, Lapras,
the birds) at 5 %. A wild Pikachu peer beacon shows up as a `VULN_PVP`
encounter — short-press the Fight... menu item to challenge.

## Heap / flash budget

- T114 flash: 97 % used (24 KB free). Tight but stable thanks to the
  deflate sprite compression.
- T114 RAM: 31 % used. Plenty of room.
- V3 flash: 63 % used. Plenty of room.
- V3 heap: **~12 KB free at idle out of 256 KB**. The ESP32 WiFi
  promiscuous-mode driver allocates ~100 KB of buffers at init; this
  is unavoidable without rebuilding the Arduino-ESP32 framework. See
  the heap monitor in `pentest/PentestModule.cpp` runOnce.

## License

MIT for our additions (matching upstream Meshtastic where applicable).
The Pokemon assets are property of Nintendo / Game Freak / The Pokemon
Company — sprite data is sourced from PokeAPI's open sheets and used
for personal hardware-hobby purposes only. Don't sell this firmware.

## Where to file issues / PRs

Use the `monstermesh` remote, not `origin` (upstream Meshtastic). The
upstream maintainers haven't asked for Pikachu battles in their tracker
and probably shouldn't have to.
