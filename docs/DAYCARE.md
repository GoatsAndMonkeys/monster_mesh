# Daycare over the mesh

The daycare is MonsterMesh's always-on social layer. When you're not actively battling or exploring, your six party monsters "check in" and live a little life with the monsters on other nodes in LoRa range — earning XP that gets written back into your real save file.

It's designed to make a mesh of T-Decks feel alive even when nobody's actively playing, and to give solo nodes a good experience too.

## Lifecycle

1. **Check-in.** When the emulator goes idle (you switch to the Meshtastic UI or terminal), the module reads your party out of the Game Boy save — species, levels, nicknames, moves, XP.
2. **Beacon.** Every **5 minutes** the node broadcasts a compact party summary over the MonsterMesh mesh channel. Other nodes that hear it become your **daycare neighbors**.
3. **Events.** Roughly every **30 minutes** the daycare generates one event — your monster does something, alone or with a neighbor's monster.
4. **DM the partner.** When an event involves a neighbor, your node sends a text DM to *that trainer's node* describing what happened, written from their monster's point of view, so it reads naturally in their Meshtastic chat (and theirs does the same for you).
5. **XP & write-back.** XP earned accumulates in daycare state and is patched back into your `.sav` on the SD card (with the SRAM checksum fixed up), so the levels show up the next time you boot the game.
6. **Check-out.** When you launch the emulator again, the daycare stops touching the SD card so the game has exclusive access; it re-reads your party on the next idle period.

Neighbors that go silent for **2 hours** are considered gone.

## The event generator

`DaycareEventGen` is a **dual-layer** system so events feel hand-written but never repeat:

- **Layer 1 — flat templates.** Hand-authored, species-specific signature moments (the kind of thing a specific monster is famous for). These fire first when they apply.
- **Layer 2 — compositional assembly.** Sentences built from interchangeable slot pools (subject + action + target + location + outcome). Outcome pools are weighted by a monster's **personality archetype**, and type filters keep combinations sensible. This is the engine for everyday variety.

Species data driving all of this — types, habitat, stat bias, personality archetype, evolution stage, and a curated moveset — lives in `DaycareData.h`, generated for all 151 Gen-1 species from [PokéAPI](https://pokeapi.co).

### Day/night and weather

- **Day/night:** if the node has a GPS fix, the daycare knows the local hour (from latitude + day-of-year) and unlocks night-only events like dreams.
- **Weather:** weather-themed events are **WiFi-gated** — without a network connection there are no weather events (no fake weather). With WiFi, weather can flavor events and bias which type of monster is having a good day.

## Friendship & rivalry

Pairs of monsters that keep interacting across the mesh build up two parallel scores, persisted per pair:

- **Friendship** grows through positive interactions (faster for same-type pairs), unlocking warmer event text and small XP bonuses.
- **Rivalry** grows through sparring/competition; a pair can be both friends *and* rivals.

These scores surface in the event text (e.g. a friendship counter in the message) and shape which events the pair gets pulled into.

## Achievements

There are **34** daycare achievements (`DaycareAchievements.h`), spanning time-in-daycare milestones, friendship/rivalry milestones, and rarer feats. The rare ones are **broadcast to the whole mesh** when earned, so the network sees them.

## Persistence

Daycare state — per-monster hours and XP, friendship/rivalry scores, and earned achievements — is saved to **`/monstermesh/daycare.dat`** on the device's internal LittleFS (via Meshtastic's `FSCom`), separate from the SD card. It survives reboots.

## Solo nodes

A lone node is not a sad node. With no neighbors around, monsters explore, train, nap, and dream; mood drifts toward "content" rather than getting stuck "lonely." When a new node finally appears, there's an excited burst. The daycare is meant to be enjoyable whether you're the only T-Deck on the mesh or one of ten.

## Save-file safety

The write-back only patches XP/levels into your existing `.sav`; it computes the Gen-1 EXP curve and repairs the SRAM checksum so the game accepts the save. As with any save editing, keep a backup of your `.sav` — this is pre-alpha software.
