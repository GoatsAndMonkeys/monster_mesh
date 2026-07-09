// SPDX-License-Identifier: MIT
//
// BreedingApp — roster + breeding orchestration for the Pi deck's breeding
// screen.  Pure logic (no ncurses/SDL) so it can be unit-tested standalone
// (tests/test_breeding.cpp), driven by a CLI (mmbreed), or called from
// TerminalUI.  Genetics live in BreedingGenetics.h.
//
// Responsibilities:
//   • hold a roster of BreedMon parents (with full genotype + provenance),
//   • import that roster from JSON or from the firmware CaughtMon binary blob,
//   • gate breeding on "you own a Pentest catch + a deck",
//   • pair two parents, roll an egg, and derive the offspring's provenance tag,
//     handling the sterile / no-hatch / can't-fight heartbreak cases,
//   • produce a blood-test report for any mon.

#pragma once
#include "BreedingGenetics.h"
#include <stdint.h>
#include <string>
#include <vector>

namespace breeding {

// Provenance tag kind (see color-variants.md "Provenance tag").
enum ProvKind : uint8_t {
    PROV_WILD = 0,   // caught wild / from Pentest Pikachu (luck)
    PROV_F,          // filial: Fn = n generations of crosses
    PROV_S,          // selfing: Sn (bred with itself)
    PROV_BX,         // backcross: BXn (crossed back onto a parent's line)
    PROV_IBL,        // true-breeding: homozygous for its rare trait
};

// One mon in the roster / one bred offspring.  `id` is a lineage handle used to
// detect selfing and backcrosses; `parentA/parentB` are the ids of its parents
// (0 = none, i.e. wild).  `depth` is generations from wild (wild = 0) and drives
// the numeric part of the Fn tag.
struct BreedMon {
    uint8_t   dex   = 0;      // national dex # (1..151)
    uint8_t   level = 1;
    Genotype  geno  = {};
    char      nick[11] = {0}; // 10 + NUL (matches firmware CaughtMon.nick)

    ProvKind  prov     = PROV_WILD;
    uint8_t   provGen  = 0;   // the n in Fn/Sn/BXn (0 for Wild/IBL numbering)
    uint8_t   depth    = 0;   // generations from wild

    uint32_t  id       = 0;
    uint32_t  parentA  = 0;
    uint32_t  parentB  = 0;

    // Breeder-room bookkeeping: wall-clock time this mon last went into a
    // breeder room (0 = never). Enforces the once-per-7-days cooldown.
    long      lastBredAt = 0;      // time_t seconds
};

// Result of pairing two parents.
enum BreedStatus : uint8_t {
    BREED_OK = 0,          // egg hatched — `child` is valid
    BREED_LOCKED,          // breeding not unlocked (no Pentest mon + deck)
    BREED_STERILE,         // a parent is bb → can't breed
    BREED_NO_HATCH,        // egg produced but hh → never hatches (no child)
    BREED_BAD_INPUT,       // invalid parent selection
};

struct BreedResult {
    BreedStatus status = BREED_BAD_INPUT;
    BreedMon    child  = {};        // valid only when status == BREED_OK
    std::string message;            // human-readable outcome / reason
};

class BreedingApp {
public:
    BreedingApp() = default;

    // ── Roster ────────────────────────────────────────────────────────────────
    const std::vector<BreedMon> &roster() const { return roster_; }
    std::vector<BreedMon> &mutableRoster() { return roster_; }   // for the room manager
    size_t size() const { return roster_.size(); }
    void   clear() { roster_.clear(); }

    // Find a roster mon by lineage id (nullptr if absent).
    BreedMon *findById(uint32_t id) {
        for (auto &m : roster_) if (m.id == id) return &m;
        return nullptr;
    }

    // Add a mon; assigns a fresh lineage id if it has none. Returns its id.
    uint32_t add(BreedMon m);

    // Seed a small hardcoded test roster (so the app is runnable with no live
    // transfer). Includes a Pentest Wild Pikachu (unlocks breeding), a Pink and
    // Rainbow-carrier pair, a Dark×Dark pair, defect carriers, and a sterile
    // Wild Rainbow (the heartbreak dead-end). Replaces any current roster.
    void seedTestRoster();

    // ── Import ────────────────────────────────────────────────────────────────
    // JSON roster (see docs/breeding-import-format.md). Appends to the roster;
    // returns the number of mons parsed, or -1 on a hard parse error.
    int importJson(const std::string &json);
    int importJsonFile(const std::string &path);

    // Firmware transfer path (Phase 6): a raw dump of CaughtMon records exactly
    // as they sit in Bill's PC on the ESP32. Layout is the packed 22-byte record
    // documented in importCaughtMonBlob(). Appends; returns count or -1.
    int importCaughtMonBlob(const uint8_t *data, size_t len);

    // ── Breeding gate ─────────────────────────────────────────────────────────
    // The game rule: breeding needs BOTH a Pentest Pikachu and a deck. We model
    // "you have a Pentest Pikachu" as owning at least one Wild-provenance mon
    // (that's what a Pentest catch imports as); the deck is this very app.
    bool breedingUnlocked() const;
    const char *lockedReason() const {
        return "Breeding locked: transfer a Pentest Pikachu catch to the deck first.";
    }

    // ── Pair & breed ────────────────────────────────────────────────────────────
    // Cross the two roster mons at indices a and b (a == b = selfing). Rolls the
    // egg with the supplied RNG so results are reproducible. On BREED_OK the
    // child is NOT auto-added — call add() if the player keeps it.
    BreedResult breed(size_t a, size_t b, Rng &rng);

    // ── Reporting ───────────────────────────────────────────────────────────────
    // Provenance tag string, e.g. "Wild", "F2", "S1", "BX1", "IBL".
    static std::string provTag(const BreedMon &m);
    // National dex name (uppercase, matches the terminal's species table).
    static const char *dexName(uint8_t dex);
    // Earliest (base) evolution of a Gen-1 species — a bred baby is always the
    // base form (e.g. Raichu parent → Pikachu baby). Identity for base-stage mons.
    static uint8_t baseForm(uint8_t dex);
    // One-line summary: "L12 PIKACHU \"Sparky\" [Pink] (F2)".
    static std::string summaryLine(const BreedMon &m);
    // Full blood-test report as display lines (skin, every allele, defects,
    // value tags). Ready to print or drop into an ncurses info panel.
    static std::vector<std::string> bloodTest(const BreedMon &m);

private:
    std::vector<BreedMon> roster_;
    uint32_t nextId_ = 1;

    // Derive the offspring's provenance from its two parents + rolled genotype.
    void deriveProvenance(BreedMon &child, const BreedMon &pa, const BreedMon &pb);
};

}  // namespace breeding
