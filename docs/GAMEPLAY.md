# Gameplay guide

Everything you can do from the **MonsterMesh Terminal** (Tools → MonsterMesh Terminal in the Meshtastic UI). Type `help` for the game commands and `help sys` for the system commands.

Almost everything here needs a **party loaded from your save file**. Open a ROM in the browser at least once so MonsterMesh can read your `.sav`; you'll get `no party loaded — load a SAV first` until it has one.

## Your party

| Command | What it does |
|---|---|
| `party` | Lists your six monsters from the save: nickname, level, HP |

The party is read straight out of Game Boy SRAM, so nicknames, levels, and movesets match your real save.

## Battling basics

When a battle starts you act by choosing a move; the move keys are shown in the battle panel and you follow the on-screen prompts. The battle math is a faithful Generation-1 engine — type effectiveness, STAB, crits, and stat stages all apply. See [BATTLE_ENGINE.md](BATTLE_ENGINE.md).

Battle XP is credited per monster and **written back into your `.sav`** after the fight (Gen-1 EXP curve), with in-battle level-ups, so wins make real progress.

## The Legend of Charizard RPG

"Legend of Charizard" (LoC) is the persistent door-game RPG layer. State (badges, news, run counters) lives in `/monstermesh/lord.dat` and **resets daily** at a fixed local hour, so things like the once-a-day explore run refresh each morning.

### 🥊 Gyms

| Command | Effect |
|---|---|
| `gym` | Show the local Kanto gym ladder and which badges you hold |
| `gym fight <N>` | Challenge gym N |

Gyms unlock in order — you must clear earlier gyms before later ones (you'll get `gym is locked — clear earlier gyms first` otherwise). Each gym is a gauntlet of trainers ending in the leader; beat the leader to earn the badge.

- **Gyms 1–8** are the eight Kanto-style gyms (rosters in `LordGyms.cpp`).
- **Gym 9 is the Indigo Plateau** — the Elite Four plus the Champion, run as a single gauntlet (`LordE4.cpp`).

### 🌿 Explore

`explore` starts a **wild-encounter run** on a nearby route (encounter pools in `LordRoutes.cpp`). It's a once-per-day activity — the daily reset refreshes it. It's the steady way to grind your team a little each day.

### 🔁 New Game+

Once you've cleared the whole league (all badges + the Indigo Plateau), the circuit enters **New Game+**: the gyms and Elite Four scale up in level for another, harder loop. Your badges carry over per NG+ tier.

### 📰 Progress views

| Command | Shows |
|---|---|
| `news` | A short ring of recent notable events (badges won, milestones) |
| `achievements` | Your earned daycare achievements |
| `daycare` | Daycare status and the neighbor monsters in range |

## Battling other trainers

These use the mesh and need at least one other MonsterMesh T-Deck in LoRa range. See [MULTIPLAYER.md](MULTIPLAYER.md) for the protocols.

| Command | Mode |
|---|---|
| `fight` | **Local mirror match** — an instant CPU battle against a snapshot of a nearby trainer's party. No coordination needed. |
| `mmg` | Discover **MonsterMesh Gyms** — parties other players are hosting as gyms on the mesh. |
| `mmg fight <N>` | Challenge a discovered MM gym's **five-trainer ladder**. |
| `mmb` | List the peers currently online and available to battle. |
| `mmb <peer>` | **Live PvP** — send a real battle challenge to a peer. They accept, and you fight head-to-head over the radio. |

When **you** receive a live `mmb` challenge, you'll be prompted to accept or decline.

## System commands

`help sys` lists them:

| Command | Effect |
|---|---|
| `version` | Print the firmware build string |
| `echo <text>` | Echo text back (handy for testing the terminal) |
| `clear` | Wipe the terminal scrollback |
| `beacon` | Immediately broadcast your presence beacon (daycare + battle discovery) instead of waiting for the periodic one — useful right before challenging someone |

## The daycare runs whether you play or not

You don't have to *do* anything for the daycare — when you're not in a battle or a run, your party is automatically checked in and socializing with monsters on nearby nodes, earning a little XP that gets written back to your save. Type `daycare` to see what's been happening. Full details in [DAYCARE.md](DAYCARE.md).
