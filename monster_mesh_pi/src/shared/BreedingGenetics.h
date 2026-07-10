// SPDX-License-Identifier: MIT
//
// BreedingGenetics — Mendelian breeding for MonsterMesh mons on the Pi deck.
//
// This is the Pi-side MIRROR of the firmware genotype model in
//   meshtastic-firmware/src/modules/pentest/PentestGenetics.h
// and the design of record in
//   monster_mesh_pi/docs/color-variants.md
//
// The GENOTYPE and PHENOTYPE (12 skins) are byte-for-byte compatible with the
// firmware so a mon caught on the ESP32 (a CaughtMon record) can be transferred
// to the deck and bred here.  What the firmware header does NOT have — and this
// header adds — is the breeding math: pairing two parents and rolling an egg by
// proper Mendelian inheritance.
//
// Cosmetic genes (dosage 0/1/2 = # of rare alleles):
//   • Rainbow  R/r  — AUTOSOMAL, SEX-LIMITED display (Pink/Rainbow show ♀ only;
//                     males carry the identical genotype but never display it).
//                     NOT strict X-linkage — every autosomal locus, including
//                     this one, passes one allele from each parent.
//   • Shiny    S/s  — recessive, epistatic to Rainbow (only shows on RR mons).
//   • Dark     D/d  — dosage: Dd = Dark, DD = Blackout.
// Recessive defect genes (dosage 2 = affected homozygote):
//   • Sterile   B/b — bb can't breed.
//   • Can'tFight F/f — ff can't battle (but can still breed unless also bb).
//   • No-hatch   H/h — hh egg never hatches.
//
// Header-only, no allocation, safe to include from the terminal or the daemon.

#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace breeding {

// ── Genotype ──────────────────────────────────────────────────────────────────
// Layout mirrors PentestGenotype exactly (7 bytes, same field order) so the
// firmware CaughtMon.geno blob maps straight onto it.
struct Genotype {
    uint8_t rainbow;    // 0=RR, 1=Rr(Pink), 2=rr(Rainbow)
    uint8_t shiny;      // 0=SS, 1=Ss(carrier), 2=ss(Shiny)
    uint8_t dark;       // 0=dd, 1=Dd(Dark), 2=DD(Blackout)
    uint8_t sterile;    // B/b — 2 = bb (can't breed)
    uint8_t cantFight;  // F/f — 2 = ff (can't battle)
    uint8_t noHatch;    // H/h — 2 = hh (egg never hatches)
    uint8_t female;     // 1 = female, 0 = male (Pink/Rainbow display ♀ only)
};

// ── The 12 visible skins (4 colors × 3 dark levels) ───────────────────────────
enum Skin : uint8_t {
    SKIN_REGULAR = 0, SKIN_SHINY, SKIN_PINK, SKIN_RAINBOW,
    SKIN_DARK, SKIN_DARK_SHINY, SKIN_DARK_PINK, SKIN_DARK_RAINBOW,
    SKIN_BLACKOUT, SKIN_BLACKOUT_SHINY, SKIN_BLACKOUT_PINK, SKIN_BLACKOUT_RAINBOW,
};

// Phenotype — identical rules to firmware pentestSkin(): Pink/Rainbow display on
// females only; Shiny masked unless RR (rainbow epistasis); dark dosage shifts
// the skin by a 4-wide block.
static inline Skin skinOf(const Genotype &g) {
    uint8_t color = 0;                                     // 0=Reg 1=Shiny 2=Pink 3=Rainbow
    if (g.female && g.rainbow == 2)          color = 3;    // Rainbow
    else if (g.female && g.rainbow == 1)     color = 2;    // Pink
    else if (g.rainbow == 0 && g.shiny == 2) color = 1;    // Shiny (only on RR)
    return (Skin)(color + g.dark * 4);
}

static inline const char *skinName(Skin s) {
    switch (s) {
        case SKIN_REGULAR:          return "Regular";
        case SKIN_SHINY:            return "Shiny";
        case SKIN_PINK:             return "Pink";
        case SKIN_RAINBOW:          return "Rainbow";
        case SKIN_DARK:             return "Dark";
        case SKIN_DARK_SHINY:       return "Dark-Shiny";
        case SKIN_DARK_PINK:        return "Dark-Pink";
        case SKIN_DARK_RAINBOW:     return "Dark-Rainbow";
        case SKIN_BLACKOUT:         return "Blackout";
        case SKIN_BLACKOUT_SHINY:   return "Blackout-Shiny";
        case SKIN_BLACKOUT_PINK:    return "Blackout-Pink";
        case SKIN_BLACKOUT_RAINBOW: return "Blackout-Rainbow";
    }
    return "Regular";
}
static inline const char *skinName(const Genotype &g) { return skinName(skinOf(g)); }

// ── Deterministic RNG (splitmix64) ────────────────────────────────────────────
// A tiny, seedable, self-contained PRNG so breeding results are reproducible in
// tests and don't depend on the host rand().  Advance state, then draw a bit.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Fair coin.
    bool bit() { return next() & 1ull; }
};

// ── Mendelian inheritance ─────────────────────────────────────────────────────
// A parent with dosage d (# of rare alleles at a locus) passes the RARE allele
// with probability d/2:  d=0 → always common, d=1 → 50/50, d=2 → always rare.
// This reproduces every autosomal cross in color-variants.md:
//   Rr×Rr → 25% rr / 50% Rr / 25% RR   (Pink×Pink)
//   Dd×Dd → 25% DD(Blackout) / 50% Dd(Dark) / 25% dd
//   Ss×Ss → 25% ss(Shiny) / 50% Ss / 25% SS
//   Bb×Bb → 25% bb(affected) / 50% carrier / 25% clean
static inline uint8_t gamete(uint8_t dosage, Rng &r) {
    switch (dosage) {
        case 0:  return 0;          // common allele for sure
        case 2:  return 1;          // rare allele for sure
        default: return r.bit() ? 1 : 0;   // heterozygous — fair coin
    }
}

// One locus: offspring dosage = alleles passed by each parent (0..2).
static inline uint8_t crossLocus(uint8_t da, uint8_t db, Rng &r) {
    return (uint8_t)(gamete(da, r) + gamete(db, r));
}

// Produce an offspring genotype from two parents. Sex is a fair 50/50 coin.
// (Breeding eligibility / hatch outcome is handled by the caller — see
// BreedingApp — this is the pure genetics.)
static inline Genotype cross(const Genotype &a, const Genotype &b, Rng &r) {
    Genotype o{};
    o.rainbow   = crossLocus(a.rainbow,   b.rainbow,   r);
    o.shiny     = crossLocus(a.shiny,     b.shiny,     r);
    o.dark      = crossLocus(a.dark,      b.dark,      r);
    o.sterile   = crossLocus(a.sterile,   b.sterile,   r);
    o.cantFight = crossLocus(a.cantFight, b.cantFight, r);
    o.noHatch   = crossLocus(a.noHatch,   b.noHatch,   r);
    o.female    = r.bit() ? 1 : 0;
    return o;
}

// ── Defect predicates (heartbreak cases) ──────────────────────────────────────
static inline bool isSterile(const Genotype &g)  { return g.sterile   == 2; } // bb
static inline bool cantFight(const Genotype &g)  { return g.cantFight == 2; } // ff
static inline bool neverHatches(const Genotype &g){ return g.noHatch  == 2; } // hh

// A mon that can be used as a breeding parent at all (bb is a genetic dead-end).
static inline bool canBreed(const Genotype &g) { return !isSterile(g); }

// ── Blood-test report ─────────────────────────────────────────────────────────
// A bred mon ships with its full genotype known. These return the per-locus
// allele string a genetics blood test prints, e.g. "Rr — Pink" / "Bb — Sterile
// carrier" / "bb — STERILE".
static inline const char *rainbowAllele(const Genotype &g) {
    switch (g.rainbow) { case 2: return "rr — Rainbow (\xE2\x99\x80 only)";
                         case 1: return "Rr — Pink (\xE2\x99\x80 only)";
                         default: return "RR — clean"; }
}
static inline const char *shinyAllele(const Genotype &g) {
    switch (g.shiny) { case 2: return "ss — Shiny";
                       case 1: return "Ss — Shiny carrier";
                       default: return "SS — clean"; }
}
static inline const char *darkAllele(const Genotype &g) {
    switch (g.dark) { case 2: return "DD — Blackout";
                      case 1: return "Dd — Dark";
                      default: return "dd — clean"; }
}
static inline const char *sterileAllele(const Genotype &g) {
    switch (g.sterile) { case 2: return "bb — STERILE";
                         case 1: return "Bb — Sterile carrier";
                         default: return "BB — clean"; }
}
static inline const char *cantFightAllele(const Genotype &g) {
    switch (g.cantFight) { case 2: return "ff — CAN'T FIGHT";
                           case 1: return "Ff — Can't-fight carrier";
                           default: return "FF — clean"; }
}
static inline const char *noHatchAllele(const Genotype &g) {
    switch (g.noHatch) { case 2: return "hh — NO-HATCH";
                         case 1: return "Hh — No-hatch carrier";
                         default: return "HH — clean"; }
}

// Compact full-genotype letter code across all six loci, e.g. "Rr Ss dd BB FF HH"
// (cosmetic R/S/D then defect B/F/H). Writes into out (needs >= 18 bytes).
static inline void genoLetters(const Genotype &g, char *out, size_t n) {
    static const char *R[3] = {"RR", "Rr", "rr"};
    static const char *S[3] = {"SS", "Ss", "ss"};
    static const char *D[3] = {"dd", "Dd", "DD"};
    static const char *B[3] = {"BB", "Bb", "bb"};
    static const char *F[3] = {"FF", "Ff", "ff"};
    static const char *H[3] = {"HH", "Hh", "hh"};
    auto ix = [](uint8_t d) -> int { return d > 2 ? 2 : d; };
    snprintf(out, n, "%s %s %s %s %s %s",
             R[ix(g.rainbow)], S[ix(g.shiny)], D[ix(g.dark)],
             B[ix(g.sterile)], F[ix(g.cantFight)], H[ix(g.noHatch)]);
}

// True if the mon is free of every defect (clean foundation stock: BB FF HH).
static inline bool isCleanStock(const Genotype &g) {
    return g.sterile == 0 && g.cantFight == 0 && g.noHatch == 0;
}

// Homozygous for a rare cosmetic trait → the true-breeding (IBL) signal.
static inline bool isHomozygousRare(const Genotype &g) {
    return g.rainbow == 2 || g.shiny == 2 || g.dark == 2;
}

}  // namespace breeding
