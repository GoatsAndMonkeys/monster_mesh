# Color Variants — Odds & Genetics

MonsterMesh Pokémon can appear in **12 color skins**, plus a hidden **Shiny
Carrier** state (13 genotypes in all). Shiny stays game-authentic (**1/4096**);
the rest of the odds are *derived* from three genes at Hardy-Weinberg equilibrium.

## The 12 skins

A **color** axis (Regular / Shiny / Pink / Rainbow) × a **dark dosage** axis with
three levels — none, **Dark** (one dark allele), **Blackout** (two):

| | Normal | Dark (`Dd`) | Blackout (`DD`) |
|---|---|---|---|
| Regular | Regular | Dark | Blackout |
| Shiny | Shiny | Dark-Shiny | Blackout-Shiny |
| Pink | Pink | Dark-Pink | Blackout-Pink |
| Rainbow | Rainbow | Dark-Rainbow | Blackout-Rainbow |

- **Shiny** — authentic recolor from the game's shiny palette.
- **Pink** — whole body shifted to hot pink (light eyes preserved).
- **Rainbow** — animated diagonal rainbow (light eyes preserved).
- **Dark** — dark greyscale body, only the mon's **accent** color kept (red if it
  has red, else its most-vivid non-body color — eyes/gems/tips).
- **Blackout** — "Who's-That-Pokémon" black silhouette (faint slate rim) with the
  accent dimmed to a deep maroon-of-that-color. The double-dark homozygote.

## The three genes

| Gene | Alleles | Mode | Rare allele `q` |
|---|---|---|---|
| **Rainbow** | `R` / `r` (**X-linked**) | incomplete dominance (`Rr` = **Pink**), expressed **♀ only** | `r` = 1/1024 |
| **Shiny** | `S` / `s` | recessive (hidden carrier) | `s` = 1/64 |
| **Dark** | `d` / `D` | **dosage** (`Dd` = Dark, `DD` = Blackout) | `D` = ~1/8192 |

The Rainbow gene sits on the **X chromosome** (calico-style), so only **females**
can *show* Pink or Rainbow. Shiny and Dark are autosomal (either sex).

### Rainbow gene — X-linked, females only

On the X chromosome, incomplete dominance. **Females (XX)** carry two copies and
express it; **males (XY)** carry one and never *show* it — they're silent carriers.
With `r` = 1/1024:

**Females** (per-female rates):

| Genotype | Frequency | Phenotype |
|---|---|---|
| `Xʳ Xʳ` | (1/1024)² ≈ **1/1,048,576** | **Rainbow** |
| `Xᴿ Xʳ` | ≈ **1/512** | **Pink** (visible carrier) |
| `Xᴿ Xᴿ` | ≈ 99.8% | not pink/rainbow → Shiny gene decides |

**Males** — `Xᴿ Y` = Regular, `Xʳ Y` = **hidden carrier** (looks Regular, ~1/1024
of males, passes `r` to every daughter). Across the whole population (half female),
that works out to **Pink ≈ 1/1024** and **Rainbow ≈ 1/2.1M**.

This is **criss-cross inheritance**: a Rainbow mother's `r` hides in her sons and
resurfaces as Pink/Rainbow in her granddaughters — exactly how calico coats and
hemophilia travel.

#### Why only half the clutch can be Pink/Rainbow

Just like **calico cats are almost always female**, Pink and Rainbow live on the X
chromosome — so a **male can only ever *carry* the color, never show it.** That
puts a hard ceiling on breeding:

- Every **son** comes out Regular-looking (a hidden carrier at best), no matter how
  you pair the parents.
- Only **daughters** can be Pink or Rainbow.
- Since roughly **half of any clutch is male**, **at most half your offspring can be
  Pink/Rainbow** — the male half is simply off the table.

So even the "best" cross (a Rainbow mother × a carrier father) yields **100% of
*daughters* Rainbow but 0% of sons** — meaning ~50% of the *whole* clutch. To build
a Rainbow line you're always breeding *through* those invisible carrier sons to get
back to showable daughters — the classic calico headache.

### Shiny gene — the *hidden* carrier

Fully recessive, and only *expressed* on `RR` mons (the rainbow gene masks it):

| Genotype | Frequency | Phenotype |
|---|---|---|
| `s s` | (1/64)² = **1/4,096** | **Shiny** |
| `S s` | 2·(63/64)·(1/64) ≈ **1/32** | Regular-**looking carrier** (hidden) |
| `S S` | ≈ 96.9% | Regular |

### Dark gene — dosage: Dark then Blackout

Not simple dominant — it's a **dosage** gene, so the heterozygote and homozygote
look *different* (like the rainbow gene). One dark allele = **Dark**; two =
**Blackout**. With `D` ≈ 1/8192:

| Genotype | Frequency | Phenotype |
|---|---|---|
| `D D` | (1/8192)² ≈ **1/67,108,864** | **Blackout** (double-dark) |
| `D d` | 2·(8191/8192)·(1/8192) ≈ **1/4,096** | **Dark** |
| `d d` | ≈ 99.98% | not dark |

Blackout is the ultra-rare homozygote — ~**1 in 67 million** in the wild, so like
Rainbow it's really a **breeding** target (`Dd × Dd → 25% DD`). It stacks on any
color. Dark itself stays ~1/4096, same as Shiny.

## Wild-encounter odds (derived)

| Skin | ≈ 1 in |
|---|---|
| **Pink** ♀ | **1,024** |
| Shiny | 4,096 |
| Dark | 4,096 |
| Rainbow ♀ | ~2,097,152 (~2.1M) |
| Dark-Pink ♀ | 4,194,304 |
| Dark-Shiny | 16,777,216 |
| **Blackout** | **67,108,864** (~67M) |
| Blackout-Pink ♀ | ~6.9 × 10¹⁰ (~69 billion) |
| Dark-Rainbow ♀ | ~8.6 × 10⁹ (~8.6 billion) |
| Blackout-Shiny | ~2.7 × 10¹¹ (~275 billion) |
| **Blackout-Rainbow** ♀ | **~1.4 × 10¹⁴** (~140 trillion — the crown) |

♀ = Pink/Rainbow only appear on **females** (X-linked); rates shown are
population-wide (≈2× that among females). Pink is the common gateway. Everything
from Rainbow / Blackout down is a multi-million-to-one wild fluke — **you breed for
them**. Blackout-Rainbow is effectively breeding-only.

> In Pentest Pikachu the color isn't a per-encounter roll — it's a deterministic
> hash of the **network's MAC address + species**, so a given AP always shows the
> same color for the same mon. The odds above are the spread *across* networks.

## Carriers & the genetics blood test

The **Shiny Carrier** (`Ss`) is common — ~**1 in 32**, roughly **128× more common
than a visible Shiny**. That's the whole point of a rare recessive: the allele
hides in ~130 ordinary mons for every one that shows.

Because carriers are that common, they have **no visual tell in the wild** — a
carrier is pixel-identical to a Regular mon. You only learn a mon's genotype by
running a **genetics "blood test"** on a Pokémon **you own**: a DNA readout that
lists its alleles (e.g. `Ss` — Shiny carrier, `Rr` — Pink, `Dd` — Dark).
Your team's genetics are an open book; a wild sighting gives nothing away.

| Gene | Carrier | In the wild |
|---|---|---|
| **Shiny** (recessive) | `Ss`, ~1/32 | **Hidden** — looks Regular; only the blood test reveals it |
| **Rainbow** ♀ (X-linked) | `Xᴿ Xʳ` = **Pink** | **Visible** — a female carrier just *is* Pink |
| **Rainbow** ♂ (X-linked) | `Xʳ Y` | **Hidden** — a male carrier looks Regular; blood-test to find |
| **Dark** (dosage) | `Dd` = **Dark** | **Visible** — one allele already shows as Dark |

### Bred mons come with a genetic report

Any Pokémon you **breed** is blood-sampled at hatch and ships with a **full genetic
report** automatically — every allele known (colors, defect carriers, provenance).
You bred it, so its genotype is on file. **Wild** catches are the unknowns: they
show only their visible phenotype until you run a **blood test** on them. So a
breeder always knows exactly what their bred stock carries, and blood-tests wild
imports before pairing them in.

## Breeding

Wild odds are the *starting* population; once you have rares, breeding is Mendelian.

**Rainbow** — X-linked, so only **daughters** can be Pink/Rainbow, and you need a
carrier **father** (hidden — blood-test males to find one):

| Cross | Daughters | Sons |
|---|---|---|
| Pink♀ × carrier♂ (`XᴿXʳ × XʳY`) | 50% Pink · **50% Rainbow** | ½ hidden carrier |
| Pink♀ × Regular♂ (`XᴿXʳ × XᴿY`) | 50% Pink · 50% Regular | ½ hidden carrier |
| Rainbow♀ × carrier♂ (`XʳXʳ × XʳY`) | **100% Rainbow** | all carriers |
| Rainbow♀ × Regular♂ (`XʳXʳ × XᴿY`) | **100% Pink** | all carriers |

The catch: a Rainbow father is impossible (males never express it), so the `r` you
need always rides in on a **hidden male carrier** — the reason blood-testing your
studs matters.

**Shiny** — pair two hidden carriers for the classic surprise:

| Cross | Offspring |
|---|---|
| Carrier × Carrier (`Ss × Ss`) | **25% Shiny** · 50% carrier · 25% clean |
| Shiny × Carrier (`ss × Ss`) | **50% Shiny** · 50% carrier |

**Blackout** — pair two Darks to double up the dark allele:

| Cross | Offspring |
|---|---|
| Dark × Dark (`Dd × Dd`) | 25% not-dark · 50% Dark · **25% Blackout** |
| Blackout × Regular (`DD × dd`) | **100% Dark** |
| Blackout × Blackout (`DD × DD`) | **100% Blackout** |

To reach **Blackout-Rainbow**, breed a Rainbow line *and* a Blackout line together
and stack both — the rarest genotype in the game.

## Birth defects & breeding stock (recessive defects)

Separate from the cosmetic genes, mons carry hidden **defect genes** — recessive
alleles that only bite when homozygous. Managing clean stock (not just stacking
rares) is what makes a careful breeder.

**Three defects**, each its own locus, healthy allele dominant. Each defect allele
sits at ~**1/4** frequency, so a mon is homozygous ("double defect") for a given
one at **(1/4)² = 1/16**:

| Defect | Gene | `xx` (double) effect | Alive? | Can fight? | Can breed? |
|---|---|---|---|---|---|
| **Sterile** | `B/b` | can't breed | ✅ | ✅ | ❌ |
| **Can't-fight** | `F/f` | can't battle | ✅ | ❌ | ✅ *(unless also `bb`)* |
| **No-hatch** | `H/h` | egg never hatches | ❌ | — | — |

Heterozygous carriers (`Bb`/`Ff`/`Hh`) are **healthy and common** — they hide the
defect and pass it on. A mon can carry all three.

### What you actually see in the wild

Because a wild encounter *is a battle*, each defect self-selects who can show up:

- **No-hatch (`hh`)** — never hatched → **never alive**, so never in the wild.
- **Can't-fight (`ff`)** — alive but can't battle → **never in an encounter**; you
  can't catch one wild, only breed one.
- **Sterile (`bb`)** — lives and fights fine → **CAN appear wild**, at ~**1/16**.
  Rare but possible: you *can* catch a sterile mon, it just can't breed.

That last one is the heartbreak: catch a **Wild Rainbow**, blood-test it, and it's
`bb` — **sterile**. Battle-ready and gorgeous, but a genetic dead-end.

Roles fall out of the combos: `ff` alone = **breeding-only** stock (daycare use,
useless in battle); `bb` alone = **battle/display only** (a dead-end line); `ff bb`
= alive but good for nothing.

### The breeding hazard

A double defect appears when **both parents carry the same one** (`xX × xX`):

| Cross (one defect) | Offspring |
|---|---|
| `Bb × Bb` | 25% affected · 50% carrier · 25% clean |
| `Bb × BB` | 0% affected — all healthy (half carriers) |
| `BB × BB` | 0% affected — clean line |

So the breeder's job is to pair mons that **don't share a carried defect**, read off
the **genetics blood test** (`Bb` — Sterile carrier, etc.). Share `Hh` and 25% of
eggs simply won't hatch; share `Ff` and a quarter of the clutch can't fight.

### Why this is the real trap for IBL

Driving a line to **true-breeding (IBL)** to lock in a rare color also risks
homozygosing any defect that hitched along. The master breeder builds a line that
is **homozygous for its rare trait AND clean of every defect** — genuine
inbreeding-depression management. A deep bred line is powerful, but only as clean as
the breeder was careful.

## The real biology it teaches

| Concept | In MonsterMesh | Real-world example |
|---|---|---|
| **Incomplete dominance** | Pink is the visible `Rr` heterozygote | snapdragons (red×white→pink), palominos |
| **Gene dosage** | one dark allele = Dark, two = Blackout | flower color intensity, sickle-cell severity |
| **Simple recessive** | Shiny needs two copies; carriers hide | cystic fibrosis, blue eyes |
| **Carrier frequency** | ~1/32 carriers vs ~1/4096 visible shiny (`2pq ≫ q²`) | sickle-cell / CF carriers |
| **Epistasis** | the rainbow gene *masks* Shiny (only shows on `RR`) | albino `cc` hiding coat color |
| **Hardy-Weinberg** | wild odds derived from allele frequencies | any population at equilibrium |
| **Lethal recessive** | no-hatch `hh` egg never hatches; wild carriers only | yellow mouse `A^y`, Manx `M` |
| **Selection filter** | `ff`/`hh` never appear wild; `bb` sterile can | why affected genotypes vanish from a population |
| **Inbreeding depression** | IBL lines surface hidden defects | purebred dogs, royal hemophilia |
| **X-linkage** | Pink/Rainbow show on ♀ only; ♂ carry silently | calico cats, hemophilia |

## Provenance tag (display-only)

Every mon carries a small **provenance tag** shown in two places — the **terminal
Pokémon view** and the **breeding screen** — so you can tell a *natural* rare from
a *bred* one at a glance. It's purely informational (no gameplay effect); it just
records how the mon came to be, using real breeding notation:

| Tag | Meaning |
|---|---|
| **Wild** | Caught in the wild / from Pentest Pikachu — the RNG/MAC-hash gave it to you (luck) |
| **F1, F2, F3…** | Bred (filial) — Fn = n generations of crosses. Rainbow first appears in **F2** |
| **S1, S2…** | Bred by **selfing** a single mon (drives toward true-breeding) |
| **BX1…BXn** | **Backcross** — a rare trait dragged onto a line; `BX4` ≈ 97% one parent line |
| **IBL** | True-breeding line (homozygous for its rare trait — breeds 100% true) |

**How it's derived** (when breeding lands): `Wild` = generation 0. Offspring of
A × B tag as `F(max(gen)+1)` for sibling-type crosses, `S(n+1)` when a mon is bred
with itself, and `BX(n+1)` when crossing back to a parent's own lineage. `IBL`
shows once a mon is confirmed homozygous for its rare allele.

The point: a **Wild Rainbow** (a 1-in-4-million fluke) and a **BX4 Rainbow** (you
*engineered* a line that pumps them out) look identical but flex differently — one
is luck, the other is skill.

## What makes a mon valuable

A **fancy** mon is any one with a cosmetic — Shiny, Pink, Rainbow, Dark, Blackout.
But there's no single "best" mon: value splits across **independent axes**, and they
often conflict. Cosmetic rarity is only one axis, and **on its own it's the
*lowest* rung**: a fancy mon that can't fight or breed is an **ornamental** —
think **fancy goldfish**, bred purely for looks with none of the hardiness. It
still has a use (collection, showing off), just not much prestige. Real value comes
from **viability**; the true trophies are the ones that are cosmetically rare *and*
battle- or breeding-viable.

| Axis | Prized because | Example |
|---|---|---|
| **Ornamental (display)** | a fancy look, but a collectible only — can't fight/breed | a sterile `ff` Wild Rainbow (a "fancy goldfish") |
| **Lucky** | natural `Wild` provenance — found, not built | a 1-in-4M Wild Rainbow |
| **Clean stock** | blood-tests free of all defects (`BB FF HH`) | plain mon, but prime foundation |
| **Breeding workhorse** | fertile + clean + homozygous for a rare | an `IBL` Rainbow that breeds true |
| **Pedigree piece** | a valuable step in a program | a `BX1`/`S1` carrying the right genes |
| **Battle-grade** | can fight (`not ff`) + good stats | a battle-ready line member |
| **The whole package** | cosmetically rare **and** viable | a fertile, battle-ready **IBL Rainbow** |

The conflicts are the point: a gorgeous **Wild Rainbow** that's `bb` **sterile** is
*just* an ornamental — pretty, but a dead-end. Your ugliest plain wild catch may
be the **cleanest foundation stock** you own. A `ff` mon is battle-trash but fine
breeding stock. Prestige tracks **what a mon can *do***, not just how rare it looks
— and the crown jewel is the rare skin that's *also* a viable fighter or breeder.

## Future lessons (planned)

- **Codominance** on Shiny — both alleles show *at once* (not blended). Like blood type AB.
- **Lethal dominant** or **maternal (mitochondrial) inheritance** — a trait passed only from the mother.
