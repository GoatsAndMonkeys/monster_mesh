# Updating your GPi with Claude

Got a GPi Case 2W (Pi Zero 2 W, RetroPie) running **MonsterMesh** and want the
latest build? You don't have to run the commands yourself — paste the prompt
below to **Claude** (Claude Code, running on the Pi or on a computer with SSH
access to it) and it'll do the whole update for you.

> Prefer to do it by hand? See the **"Updating an existing GPi install"** section
> of the [README](../README.md).

---

## Copy-paste this to Claude

```text
I have a Raspberry Pi handheld (GPi Case 2W, Pi Zero 2 W, RetroPie) running a
game called MonsterMesh. My friend pushed new code and I want you to update my
device to the latest build. The code lives on the `mm/monster-mesh-pi` branch of
https://github.com/GoatsAndMonkeys/monster_mesh.git

Please do the update for me. Work carefully and verify each step:

1. Figure out where I'm running this. If you're ON the Pi, work locally. If
   you're on my laptop, SSH to the Pi first (try `pi@retropie.local`; ask me for
   the host/user if that fails).

2. Find my existing clone of the repo on the Pi (look under ~ for a
   `monster_mesh` / `monster_mesh_pi` folder). If there's no clone, clone it:
   `git clone -b mm/monster-mesh-pi https://github.com/GoatsAndMonkeys/monster_mesh.git`

3. Update the source:
     git checkout mm/monster-mesh-pi
     git pull origin mm/monster-mesh-pi

4. Build the terminal ON THE PI (it's an armv7l on-device build — do NOT try to
   cross-compile from my laptop):
     cd monster_mesh_pi/build     # reuse the build dir if it exists, else: mkdir build && cd build && cmake ..
     cmake ..                     # refresh rules after the pull
     make mmterm -j2              # IMPORTANT: -j2, never -j4 — the 512MB Zero 2W
                                  # OOM-kills g++ on TerminalUI.cpp (shows as a
                                  # bare "Error 2" with no "error:" line)
   Confirm you see "Built target mmterm" before continuing. If the build fails
   with Error 2 and no compiler error, retry with `-j1`.

5. Deploy the new binary (the old one may be running, so rename-over it):
     sudo cp -p /opt/monstermesh/bin/mmterm /opt/monstermesh/bin/mmterm.bak
     cp mmterm /tmp/mmterm.new && sudo mv -f /tmp/mmterm.new /opt/monstermesh/bin/mmterm

6. Do NOT reboot or restart any service — mmterm is relaunched fresh each time I
   open MonsterMesh in EmulationStation, so it takes effect on next launch.

Important safety notes:
- Do NOT touch /var/lib/monstermesh/ — that's my saved Pokémon collection and
  breeder rooms. The update must not delete or modify it.
- Do NOT `rm -rf` any CMakeFiles directory — that breaks the build rules and
  silently deploys a stale binary. If a clean rebuild is truly needed, use
  `rm -rf build && cmake -S . -B build` instead.
- If I'm currently in the "Pentest Pikachu" ROM, its Wi-Fi scanning can drop the
  Pi off the network mid-SSH — tell me to exit that ROM first.
- If you hit "Text file busy", the game is still running; the `mv -f` in step 5
  avoids it, but if it persists tell me to close the ROM.

When done, tell me the build succeeded and the new binary is in place, and remind
me it applies the next time I launch MonsterMesh. The README on that branch has a
matching "Updating an existing GPi install" section if you want to double-check.
```

---

## What this does (in plain English)

- Pulls the newest code from the `mm/monster-mesh-pi` branch.
- Rebuilds **`mmterm`** (the game terminal) **on the Pi**, at a safe `-j2` so the
  little 512 MB Zero 2 W doesn't run out of memory mid-compile.
- Swaps the new binary into `/opt/monstermesh/bin/` without needing a reboot.

## What it will **not** do

- **Won't delete your collection.** Caught mons, breeder rooms, and daycare state
  live in `/var/lib/monstermesh/` and are never touched by a code update.
- **Won't reflash your SD card** or reinstall RetroPie — it only replaces one
  program file.

## If something goes wrong

- **Build fails with "Error 2" and no `error:` line** → out of memory; tell Claude
  to build with `-j1`.
- **"Text file busy"** → the game is still open; close the MonsterMesh ROM and try
  the copy again.
- **SSH keeps dropping** → you're probably in the Pentest Pikachu ROM; exit it
  (its Wi-Fi scan bumps the Pi off the network), then retry.
- **Want to roll back** → the old binary was saved as
  `/opt/monstermesh/bin/mmterm.bak`; `sudo mv` it back over `mmterm`.
