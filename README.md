<div align="center" markdown="1">

<h1>Pokemesh</h1>

<i>Pokemon battles for your LoRa mesh.</i>

</div>

A fork of [meshtastic/firmware](https://github.com/meshtastic/firmware)
that turns a Heltec V3 or T114 into a Pokemon battle pager. Every WiFi
handshake, rogue BLE advertiser, or fellow Pikachu node you encounter
triggers a Gen-1 battle whose outcome levels up your pet, fills your
Pokedex, and earns gym badges. The underlying Meshtastic mesh protocol
is untouched — the radio still acts as a normal Meshtastic node.

| Board       | Env                | Role |
|-------------|--------------------|------|
| Heltec V3   | `heltec-v3`        | "Warwalker" — 802.11 WiFi pentester + 128×64 mono Pikachu pet |
| Heltec T114 | `heltec-t114-pet`  | "BLE Stalker" — Bluetooth peripheral scanner + 240×135 colour Pikachu pet |

## Docs

- 📖 **[docs/POKEMESH.md](docs/POKEMESH.md)** — top-level overview, build & flash
- 🔫 **[src/modules/pentest/README.md](src/modules/pentest/README.md)** — Pentest / Stalker module
- 🐭 **[src/modules/pikachu/README.md](src/modules/pikachu/README.md)** — V3 mono pet game
- 🌈 **[src/modules/pikachu2/README.md](src/modules/pikachu2/README.md)** — T114 colour pet renderer
- 🥊 **[src/modules/monstermesh/gauntlet/](src/modules/monstermesh/gauntlet/)** — Gym-leader gauntlet over DM

## Quick build & flash

```bash
pip install -U platformio

# Heltec V3
pio run -e heltec-v3 -t upload

# Heltec T114 (double-tap RESET first, then drag the .uf2)
pio run -e heltec-t114-pet
cp .pio/build/heltec-t114-pet/firmware.uf2 /Volumes/HT-n5262/
```

## Upstream

This is a downstream of upstream Meshtastic. For the canonical
Meshtastic firmware, docs, and community:

- Website: <https://meshtastic.org>
- Docs: <https://meshtastic.org/docs/>
- Upstream repo: <https://github.com/meshtastic/firmware>

Issues / PRs for this fork live in **monstermesh** (this repo), not
upstream — the upstream maintainers haven't asked for Pikachu battles
in their tracker and probably shouldn't have to.

## License

MIT for our additions, matching upstream Meshtastic where applicable.
Pokemon assets are property of Nintendo / Game Freak / The Pokemon
Company — sprite data is sourced from PokeAPI's open sheets and used
for personal hardware-hobby purposes only. Don't sell this firmware.
