# Breeding roster import format

The Pi breeding app (`src/shared/BreedingApp.*`, verifier/CLI `mmbreed`) imports a
roster of parent mons two ways. Both produce `breeding::BreedMon` records whose
`Genotype` is byte-compatible with the firmware `PentestGenotype`.

## 1. JSON (authoring / test seeding)

Flat objects inside a `roster` array (any wrapper keys are ignored; a parser
just scans every `{...}` object that has a `dex` field). See
`breeding-roster.example.json`.

```json
{ "roster": [
  { "dex": 25, "nick": "Bolt", "level": 18, "female": false,
    "rainbow": 1, "shiny": 0, "dark": 0,
    "sterile": 0, "cantFight": 0, "noHatch": 0, "provenance": "Wild" }
] }
```

| Field | Type | Meaning |
|---|---|---|
| `dex` | int 1..151 | National dex number (required) |
| `nick` | string ≤10 | Nickname; defaults to species name |
| `level` | int | Level (cosmetic here) |
| `female` | bool | `true` = ♀ (Pink/Rainbow display ♀ only) |
| `rainbow` | 0/1/2 | 0=RR, 1=Rr (Pink), 2=rr (Rainbow) |
| `shiny` | 0/1/2 | 0=SS, 1=Ss (carrier), 2=ss (Shiny) |
| `dark` | 0/1/2 | 0=dd, 1=Dd (Dark), 2=DD (Blackout) |
| `sterile` | 0/1/2 | B/b — 2 = bb (can't breed) |
| `cantFight` | 0/1/2 | F/f — 2 = ff (can't battle) |
| `noHatch` | 0/1/2 | H/h — 2 = hh (egg never hatches) |
| `provenance` | string | `Wild`, `F1..Fn`, `S1..Sn`, `BX1..BXn`, `IBL` |

Every field except `dex` is optional and defaults to 0 (`provenance` → `Wild`).

Load: `BreedingApp::importJsonFile(path)` / `importJson(str)` → returns count.

## 2. CaughtMon binary blob (firmware transfer — Phase 6)

The ESP32 firmware holds catches as `struct CaughtMon` in Bill's PC
(`meshtastic-firmware/.../pentest/PentestData.h`). For transfer to the deck, the
firmware side should emit each record as this **explicit packed 22-byte** layout
(the in-RAM struct is not `packed(1)`, so the wire form is fixed here to avoid
padding ambiguity):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `dex` |
| 1 | 1 | `level` |
| 2 | 7 | `geno[7]` = rainbow, shiny, dark, sterile, cantFight, noHatch, female |
| 9 | 4 | `caughtSec` u32 LE (ignored on import) |
| 13 | 1 | `provenance` (0 = Wild) |
| 14 | 8 | `nick[8]` NUL-padded |

Load: `BreedingApp::importCaughtMonBlob(data, len)` → returns count. Concatenate
N records back-to-back; `len` must be a multiple of 22.

**TODO (Phase 6 sibling):** the firmware catching side owns the actual transfer
channel (serial/LoRa DM). This app only defines the wire layout it will consume.

## Breeder rooms — overnight cycle (operational rules)

Modeled in `src/shared/BreederRoom.h` (`breeding::BreederManager`). The breeder
"room" is the 6-slot party: the player designates **up to 3 pairs** (never
auto-paired); each valid pair kept together overnight lays 1 egg.

- **6:00 AM** — an egg appears (genotype rolled here) the morning after the
  night the pair was placed.
- **6:00 PM** — that egg hatches into the offspring, added to the box.
- **1 egg per pair**; a full party of 6 can yield up to **3 eggs/night**.
- **7-day cooldown per individual** — the clock starts when a mon enters a room
  (`lastBredAt`); it can't re-breed for 7 days.
- **Skipped pairs**: a pair is skipped (not fatal) if either parent is on
  cooldown or is a non-breeder — `bb` sterile or `hh` no-hatch.
- An `hh` egg still *appears* at 6 AM but **fails to hatch** at 6 PM (no
  offspring); an `ff` offspring hatches fine but is flagged can't-fight.

Time is real wall-clock (`time_t now`, from the Pi's RTC via `time(nullptr)`);
6 AM/6 PM boundaries are computed in local time. Callers pump `mgr.tick(app,
now, rng)` on each frame/periodic; it fires the egg roll and the hatch and
returns `HatchEvent`s. All logic takes `now` as a parameter so it is
deterministic and unit-tested with a simulated clock (`tests/test_breeding.cpp`).

## Breeding gate

Breeding is unlocked only when the roster contains at least one `Wild`-provenance
mon — the model for "you own a Pentest Pikachu catch + a deck."
`BreedingApp::breedingUnlocked()`; `breed()` returns `BREED_LOCKED` otherwise.
