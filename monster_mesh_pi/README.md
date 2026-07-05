# MonsterMesh Pi

**The Raspberry Pi build of [MonsterMesh](https://github.com/GoatsAndMonkeys/monster_mesh) — a Pokémon-style monster-battling RPG and daycare that runs over the [Meshtastic](https://meshtastic.org) LoRa mesh, in a terminal, on a Raspberry Pi handheld.**

**Status:** pre-alpha · active development. Part of the MonsterMesh project (T-Deck firmware on the `main` branch, this Pi build on `mm/monster-mesh-pi`).

---

## What is this?

MonsterMesh Pi is a native Linux/Raspberry Pi port of the MonsterMesh game layer. Instead of an ESP32 T-Deck, it runs as two programs on a Pi:

- **`mmd`** — a background **daemon** that talks to a Meshtastic radio node over USB serial, runs the daycare/beacon logic, watches your Game Boy save file, and exposes everything over a local IPC socket.
- **`mmterm`** — an **ncurses terminal UI** (with an SDL2 pop-up window for battles) that you play. It's the same RPG/daycare/battle experience as the T-Deck's in-game terminal: gyms, the Elite Four, `explore`, player-hosted gyms, live PvP over LoRa, and the mesh daycare.

It's designed to drop into a **RetroPie / EmulationStation** handheld (built and tested on the **GPi Case 2W**) as its own "system," so MonsterMesh shows up next to your emulators. It also builds on plain desktop Linux and macOS for development.

Because the mesh features ride on Meshtastic, you connect the Pi to a **Meshtastic radio node** (e.g. an nRF52 board like a T-Echo/RAK, or any Meshtastic device) over USB — that's the LoRa link to other players.

> **Fan project / no ROMs included.** No copyrighted ROMs, saves, or game assets are shipped. You supply your own legally-obtained Game Boy `.gb` / `.sav`. See the [main project's Legal notice](https://github.com/GoatsAndMonkeys/monster_mesh#legal--trademarks).

---

## Hardware & requirements

| Need | Details |
|------|---------|
| A Raspberry Pi (or Linux PC) | Built/tested on Pi Zero 2 W (GPi Case 2W) and desktop Linux; ARM and x86_64 both fine |
| A Meshtastic radio node | Any Meshtastic device connected over **USB serial** (default `/dev/ttyUSB0`) — this is your LoRa link |
| Build tools | `cmake` ≥ 3.16, a C++17 compiler (`g++`/`build-essential`), `pkg-config` |
| Libraries | **ncursesw** (wide-char ncurses), **SDL2**, **SDL2_image** |

---

## Install

### Option A — build & install on the Pi (recommended)

Run these on the Raspberry Pi itself (e.g. over SSH to `retropie.local`):

```bash
# 1. Dependencies (Debian / Raspberry Pi OS / RetroPie)
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config \
    libncurses-dev libncursesw5-dev \
    libsdl2-dev libsdl2-image-dev

# 2. Get the source (this branch)
git clone -b mm/monster-mesh-pi https://github.com/GoatsAndMonkeys/monster_mesh.git
cd monster_mesh/monster_mesh_pi

# 3. Build (produces build/mmd, build/mmterm, build/test_battle)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 4. Install into RetroPie/EmulationStation
#    - copies mmd + mmterm to /opt/monstermesh/bin
#    - installs the systemd service (mmd starts on boot)
#    - adds a "MonsterMesh" system to EmulationStation
#    - adds the pi user to the 'dialout' group for serial access
bash retropie/install.sh
```

Then **reboot** (needed for the `dialout` group and the daemon service), plug your Meshtastic node into USB, and MonsterMesh appears as a system in EmulationStation. Launching it opens the terminal.

### Option B — desktop Linux / macOS (development)

```bash
cd monster_mesh_pi
cmake -B build && cmake --build build -j
./build/mmd /dev/ttyUSB0 &     # daemon (point at your Meshtastic node)
./build/mmterm                 # the terminal UI
```

On macOS, install deps with Homebrew: `brew install cmake pkg-config ncurses sdl2 sdl2_image`.

### Option C — pre-baked SD-card image

`image-build/` contains a Docker/QEMU pipeline that produces a fully-provisioned RetroPie `.img` with MonsterMesh already compiled and installed (`image-build/README.md` has the details). Flash it to an SD card and boot — no manual build needed.

---

## Running it manually

If you're not using the EmulationStation launcher:

```bash
# Daemon — connect to your Meshtastic node (default port is /dev/ttyUSB0)
mmd [serial_port] [save_dir]
#   e.g.  mmd /dev/ttyUSB0 /home/pi/RetroPie/saves/gb

# Terminal — attaches to the running daemon over /tmp/monstermesh.sock
mmterm
```

Type `help` in the terminal to see the command list (gyms, `explore`, `fight`, `mmg`/`mmb` multiplayer, daycare, etc.). The daycare and battle features need `mmd` running and a Meshtastic node attached; the single-player RPG works without a radio.

### Key paths (RetroPie install)

| Path | What |
|------|------|
| `/opt/monstermesh/bin/` | `mmd`, `mmterm`, `launch.sh` |
| `/etc/systemd/system/monstermesh.service` | Daemon auto-start (`systemctl status monstermesh`) |
| `/home/pi/RetroPie/roms/monstermesh/` | ROM folder (put your `.gb` here) |
| `/home/pi/RetroPie/saves/gb/` | Game Boy `.sav` files the daemon watches |
| `/var/lib/monstermesh/` | Daycare state, captures, persistence |

---

## How it fits together

```
 Meshtastic node ──USB serial──►  mmd (daemon)  ──IPC socket──►  mmterm (ncurses UI)
 (LoRa to other        watches .sav,            /tmp/monstermesh.sock   + SDL2 battle window
  MonsterMesh players)  runs daycare/beacons
```

`mmd` owns the radio link and the game state; `mmterm` is a thin client you can start/stop freely. Both share the battle engine and Gen-1 data under `src/shared/` and `src/battle/`, which are kept in sync with the T-Deck firmware so Pi and T-Deck players can battle and share a daycare over the same mesh.

## Project layout

```
monster_mesh_pi/
  src/daemon/      mmd — Meshtastic serial link, SaveWatcher, daycare, IPC server
  src/terminal/    mmterm — ncurses UI, battle window (SDL2), SIGINT/"Pentest Pikachu" screens
  src/battle/      Deterministic Gen-1 battle engine (shared with the T-Deck)
  src/shared/      Data structures, IPC protocol, Kanto zones, learnsets
  retropie/        install.sh, systemd unit, EmulationStation entry, launcher, theme
  image-build/     Docker/QEMU pipeline to bake a ready-to-flash RetroPie SD image
  scripts/         Data-generation + setup helpers (e.g. gen_learnsets.py)
  tests/           Standalone battle-engine tests
```

---

## Related

- **[MonsterMesh (main / T-Deck firmware)](https://github.com/GoatsAndMonkeys/monster_mesh)** — the ESP32 T-Deck build and full project docs.
- **`pentest-pikachu` branch** — the WiFi/BLE "Pentest Pikachu" security toy for Heltec V3/V4/T114 (a Pi SIGINT version also lives in `src/terminal/` here).

## License

Part of the MonsterMesh project, distributed under **GPL-3.0** (same as upstream Meshtastic). No copyrighted game ROMs or assets are included. "Pokémon" and "Game Boy" are trademarks of their respective owners; this is an unofficial, non-commercial fan project.
