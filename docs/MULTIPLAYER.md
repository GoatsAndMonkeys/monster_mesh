# Multiplayer over LoRa

MonsterMesh has three ways to battle other trainers, all riding the Meshtastic mesh — no internet, no pairing servers. They need at least one other MonsterMesh T-Deck within LoRa range.

Trainers find each other through their **presence beacons** (broadcast periodically; you can force one with the `beacon` command), so the `<peer>`/`<N>` arguments below resolve against the nodes you've recently heard.

All MonsterMesh traffic uses **channel 1** (`MONSTERMESH_CHANNEL`).

## 1. `fight` — local mirror match

The zero-coordination option. Your node already knows a snapshot of nearby trainers' parties from their beacons. `fight` builds a CPU opponent from a neighbor's party and runs a **complete local battle** against it, with a freshly-healed copy of your own team.

- Instant — the other trainer doesn't need to be awake or at the keyboard.
- Good for grinding and for "can I beat that team?" checks.
- It's a simulation against their *published* party, not a live duel.

## 2. `mmg` — MonsterMesh Gyms (player-hosted gyms)

Any node can act as a **gym**: it offers its party (and a ladder of trainers) for other trainers to challenge over the mesh.

- `mmg` broadcasts a discovery probe and collects replies for a few seconds, then prints the gyms it heard (name, badge, leader).
- `mmg fight <N>` challenges the gym at slot N. The challenger runs a **five-trainer ladder**:
  - It requests the gym's roster. The preferred path is a **bulk dump** — the gym replies with all five trainers' names and parties at once (`BBS_LADDER_REQUEST` → `BBS_LADDER_NAMES` + `BBS_LADDER_PARTIES`), so the challenger can then fight all five locally with no mid-ladder radio traffic.
  - If no bulk reply arrives in time, it falls back to a **per-trainer** request chain (`BBS_FIGHT_REQUEST` → `TEXT_BATTLE_PARTY` chunks → fight → `BBS_FIGHT_RESULT`, repeat for the next trainer).
  - Win all five → "MM Gym cleared!"; a loss ends the ladder.

This lets a stationary T-Deck become a destination gym for a local mesh community.

## 3. `mmb` — MonsterMesh Battle (live PvP)

A real head-to-head battle between two players, resolved deterministically over the radio (see [BATTLE_ENGINE.md](BATTLE_ENGINE.md) for why this works without sending battle state).

- `mmb` lists peers currently online for battle.
- `mmb <peer>` challenges one by name. The peer gets a prompt and accepts or declines; the handshake rides plain Meshtastic DMs so it works even if a player is driving from the phone app.
- On accept, the initiator picks a shared RNG seed and sends it in a `TEXT_BATTLE_START`. Both sides exchange parties once (`TEXT_BATTLE_PARTY`), then trade only their per-turn actions (`TEXT_BATTLE_ACTION`). Each device resolves every turn locally and identically.
- Periodic state hashes (`TEXT_BATTLE_HASH`, every 5 turns) catch any desync and forfeit cleanly if the two sides ever diverge.

## Wire protocol summary

The packet types live in `MonsterMeshTextBattle.h` / `MonsterMeshModule.h` and travel on the MonsterMesh mesh channel. See [BATTLE_ENGINE.md](BATTLE_ENGINE.md#wire-format) for the text-battle packet layouts.

| Group | Used by |
|---|---|
| `TEXT_BATTLE_START / ACTION / HASH / FORFEIT / PARTY` | **live PvP (`mmb`)** and the per-fight party transfer |
| `BBS_LADDER_REQUEST / NAMES / PARTIES` | **MMG** bulk gym-roster dump |
| `BBS_FIGHT_REQUEST / RESULT` | **MMG** per-trainer fallback chain |

## Networking notes

- Outbound challenges/DMs are always sent from the module's `runOnce()` thread, never from inside the packet-receive handler — sending under the router's receive context wedges the TX queue. Incoming handlers only record intent; `runOnce` performs the send.
- A live PvP self-echo guard gates incoming actions on the remote peer's ID, so a node never processes its own mesh-echoed `TEXT_BATTLE_ACTION`.
- The `beacon` command forces an immediate presence broadcast — handy to make a peer's node list you before you challenge them, instead of waiting for the next periodic beacon.
