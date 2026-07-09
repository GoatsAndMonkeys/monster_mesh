// SPDX-License-Identifier: MIT
//
// BreederRoom — the OPERATIONAL layer over BreedingGenetics/BreedingApp.
// Models the game's breeder rooms and the real wall-clock overnight cycle.
//
// ── Rules (finalized by the user) ─────────────────────────────────────────────
//   1. Place TWO mons together in a "breeder room."
//   2. Overnight cycle on the Pi's real clock (RTC / system time):
//        • an EGG APPEARS at 6:00 AM the morning after the night you place the
//          pair (they must be together through the night), and
//        • that egg HATCHES at 6:00 PM the SAME day into the offspring mon,
//          which is added to your box.
//      Cycle: pair placed -> next-morning 6 AM egg appears -> 6 PM hatch.
//   3. ONCE PER WEEK per mon: an individual can only breed every 7 days. The
//      7-day cooldown clock starts the moment it enters a room (lastBredAt).
//   4. Up to 3 breeder rooms (pairs) can run at once.
//   5. Exactly 1 egg per successful breed.
//
// Heartbreak cases fold in cleanly:
//   • a `bb` sterile mon is rejected at placement (can't breed),
//   • an `hh` egg appears at 6 AM but FAILS to hatch at 6 PM (no offspring),
//   • an `ff` offspring hatches fine but is flagged can't-fight.
//
// The breeder "room" IS the 6-slot party: the player designates up to 3 PAIRS
// among the 6 mons (player-assigned, never auto), and every valid pair kept
// together overnight lays 1 egg at 6 AM (hatching 6 PM). So a full party of 6
// well-chosen mons can yield up to 3 eggs in a night. Invalid pairs (a parent
// on cooldown, or a non-breeder: bb sterile / hh no-hatch) are simply skipped.
//
// Time is passed in explicitly as a `time_t` (`now`) so the logic is
// deterministic and unit-testable; production callers pass `time(nullptr)`.
// The 6 AM / 6 PM boundaries are computed in LOCAL time via localtime/mktime.

#pragma once
#include "BreedingApp.h"
#include <ctime>
#include <string>
#include <vector>

namespace breeding {

static constexpr int    NUM_BREEDER_ROOMS = 3;         // rule 4
static constexpr long   BREED_COOLDOWN_SEC = 7L * 24 * 3600;   // rule 3: 7 days
static constexpr int    EGG_APPEAR_HOUR = 6;           // 6:00 AM
static constexpr int    EGG_HATCH_HOUR  = 18;          // 6:00 PM

// A mon that cannot be a breeding parent at all: bb sterile can't breed, and an
// hh no-hatch would only ever lay eggs that never hatch — both are non-breeders.
static inline bool isNonBreeder(const Genotype &g) {
    return isSterile(g) || neverHatches(g);
}

enum RoomState : uint8_t {
    ROOM_EMPTY = 0,     // no pair
    ROOM_INCUBATING,    // pair placed, waiting for the morning 6 AM egg
    ROOM_EGG,           // egg has appeared, waiting for 6 PM to hatch
};

struct BreederRoom {
    RoomState state = ROOM_EMPTY;
    uint32_t  idA = 0, idB = 0;     // lineage ids of the pair (live-roster busy check)
    BreedMon  parentA = {};         // stored COPIES so the room survives box reloads
    BreedMon  parentB = {};         //   (ids aren't stable across reloads)
    long      placedAt   = 0;       // when the pair was placed
    long      eggAppearAt = 0;      // next-morning 6 AM
    long      eggHatchAt   = 0;      // that day 6 PM
    BreedMon  pendingChild = {};     // rolled when the egg appears
    bool      childWillHatch = false; // false if the rolled egg is hh (no-hatch)
    bool      rolled = false;        // egg genotype has been rolled
};

// A hatch event surfaced by tick(): the room that hatched + the BreedResult.
struct HatchEvent {
    int         room = -1;
    BreedResult result;
};

class BreederManager {
public:
    // ── Time helpers (local time) ──────────────────────────────────────────────
    // The 6 AM of the day AFTER the placement day — "the morning after the night
    // you placed them." Placing during a day/evening yields tomorrow's 6 AM egg.
    static long nextMorningEgg(long placedAt) {
        time_t t = (time_t)placedAt;
        struct tm lt = *localtime(&t);
        lt.tm_mday += 1;              // the following calendar day
        lt.tm_hour = EGG_APPEAR_HOUR; // 06:00:00
        lt.tm_min = 0; lt.tm_sec = 0;
        lt.tm_isdst = -1;             // let mktime resolve DST
        return (long)mktime(&lt);
    }
    // 6 PM on the SAME day the egg appeared.
    static long hatchTimeOf(long eggAppearAt) {
        time_t t = (time_t)eggAppearAt;
        struct tm lt = *localtime(&t);
        lt.tm_hour = EGG_HATCH_HOUR;  // 18:00:00
        lt.tm_min = 0; lt.tm_sec = 0;
        lt.tm_isdst = -1;
        return (long)mktime(&lt);
    }

    // ── Cooldown ────────────────────────────────────────────────────────────────
    static long cooldownRemaining(const BreedMon &m, long now) {
        long rem = m.lastBredAt + BREED_COOLDOWN_SEC - now;
        return rem > 0 ? rem : 0;
    }
    static bool offCooldown(const BreedMon &m, long now) {
        return cooldownRemaining(m, now) == 0;
    }

    // Is this mon currently occupying any room?
    bool inAnyRoom(uint32_t id) const {
        for (const auto &r : rooms_)
            if (r.state != ROOM_EMPTY && (r.idA == id || r.idB == id)) return true;
        return false;
    }
    int firstFreeRoom() const {
        for (int i = 0; i < NUM_BREEDER_ROOMS; ++i)
            if (rooms_[i].state == ROOM_EMPTY) return i;
        return -1;
    }

    const BreederRoom &room(int i) const { return rooms_[i]; }
    int roomCount() const { return NUM_BREEDER_ROOMS; }

    // ── Persistence (same-device binary blob of the room array) ────────────────
    const BreederRoom *rawRooms() const { return rooms_; }
    void restoreRooms(const BreederRoom *src) {
        for (int i = 0; i < NUM_BREEDER_ROOMS; ++i) rooms_[i] = src[i];
    }

    // ── Place a pair ────────────────────────────────────────────────────────────
    // Validates the gate, sterility, cooldown, room availability, and that the
    // two mons are distinct and not already breeding. On success starts the
    // cooldown clock on both, computes the egg/hatch times, and returns the room
    // index; -1 on failure with `err` set.
    int placePair(BreedingApp &app, size_t a, size_t b, long now, std::string &err) {
        auto &roster = app.mutableRoster();
        if (a >= roster.size() || b >= roster.size()) { err = "Pick two mons."; return -1; }
        if (a == b) { err = "A room needs two different mons."; return -1; }
        if (!app.breedingUnlocked()) { err = app.lockedReason(); return -1; }

        BreedMon &ma = roster[a];
        BreedMon &mb = roster[b];
        if (isNonBreeder(ma.geno)) { err = std::string(nameOf(ma)) + nonBreederWhy(ma.geno); return -1; }
        if (isNonBreeder(mb.geno)) { err = std::string(nameOf(mb)) + nonBreederWhy(mb.geno); return -1; }
        if (inAnyRoom(ma.id) || inAnyRoom(mb.id)) { err = "A chosen mon is already in a breeder room."; return -1; }
        if (!offCooldown(ma, now)) { err = std::string(nameOf(ma)) + " is on breed cooldown."; return -1; }
        if (!offCooldown(mb, now)) { err = std::string(nameOf(mb)) + " is on breed cooldown."; return -1; }

        int slot = firstFreeRoom();
        if (slot < 0) { err = "All breeder rooms are occupied."; return -1; }

        BreederRoom &rm = rooms_[slot];
        rm = BreederRoom{};
        rm.state       = ROOM_INCUBATING;
        rm.idA         = ma.id;
        rm.idB         = mb.id;
        rm.parentA     = ma;           // full copies — survive relaunch + box reload
        rm.parentB     = mb;
        rm.placedAt    = now;
        rm.eggAppearAt = nextMorningEgg(now);
        rm.eggHatchAt  = hatchTimeOf(rm.eggAppearAt);
        // Rule 3: the 7-day cooldown starts the moment they enter the room.
        ma.lastBredAt = now;
        mb.lastBredAt = now;
        err.clear();
        return slot;
    }

    // ── Assign up to 3 player-chosen pairs from the 6-slot party ────────────────
    // The player picks the couples (this never auto-pairs). Each pair is placed
    // via placePair; any pair that can't breed (parent on cooldown, sterile, or
    // no-hatch) is SKIPPED with a reason rather than failing the whole batch.
    // Returns one PairOutcome per requested pair (room >= 0 = placed).
    struct PartyPair { size_t a, b; };
    struct PairOutcome { size_t a, b; int room; std::string note; };
    std::vector<PairOutcome> assignPairs(BreedingApp &app,
                                         const std::vector<PartyPair> &pairs,
                                         long now) {
        std::vector<PairOutcome> out;
        for (const auto &p : pairs) {
            PairOutcome o{p.a, p.b, -1, ""};
            o.room = placePair(app, p.a, p.b, now, o.note);
            if (o.room >= 0) o.note = "placed";
            out.push_back(o);
        }
        return out;
    }

    // Remove a pair before the egg appears (frees the room; cooldown already
    // spent stands). No effect once the egg has appeared.
    bool cancel(int slot) {
        if (slot < 0 || slot >= NUM_BREEDER_ROOMS) return false;
        if (rooms_[slot].state == ROOM_EMPTY) return false;
        rooms_[slot] = BreederRoom{};
        return true;
    }

    // ── Advance the clock ───────────────────────────────────────────────────────
    // Call whenever `now` moves (each UI frame / periodic). Fires the 6 AM egg
    // roll and the 6 PM hatch. Returns any hatch events (offspring already added
    // to the box for BREED_OK; no-hatch eggs return BREED_NO_HATCH and clear the
    // room). `rng` seeds the genetic roll at egg-appear.
    std::vector<HatchEvent> tick(BreedingApp &app, long now, Rng &rng) {
        std::vector<HatchEvent> events;
        for (int i = 0; i < NUM_BREEDER_ROOMS; ++i) {
            BreederRoom &rm = rooms_[i];

            // 6 AM: the egg appears — roll its genotype now.
            if (rm.state == ROOM_INCUBATING && now >= rm.eggAppearAt) {
                rollEgg(app, rm, rng);
                rm.state = ROOM_EGG;
            }
            // 6 PM: the egg hatches (or fails to, if hh).
            if (rm.state == ROOM_EGG && now >= rm.eggHatchAt) {
                HatchEvent ev;
                ev.room = i;
                if (rm.rolled && rm.childWillHatch) {
                    app.add(rm.pendingChild);       // rule 5: 1 egg into the box
                    ev.result.status  = BREED_OK;
                    ev.result.child   = rm.pendingChild;
                    ev.result.message = std::string("A ") +
                        skinName(rm.pendingChild.geno) + " " +
                        BreedingApp::dexName(rm.pendingChild.dex) + " hatched! (" +
                        BreedingApp::provTag(rm.pendingChild) + ")";
                    if (cantFight(rm.pendingChild.geno))
                        ev.result.message += "  It's ff — breeding-only, can't battle.";
                } else {
                    ev.result.status  = BREED_NO_HATCH;
                    ev.result.message = "The egg was hh (no-hatch) — it never hatched.";
                }
                events.push_back(ev);
                rooms_[i] = BreederRoom{};           // room frees up
            }
        }
        return events;
    }

    // ── Status string for the UI ────────────────────────────────────────────────
    // One line per room describing its current state + the next event time.
    std::vector<std::string> statusLines(const BreedingApp &app, long now) const {
        (void)app;
        std::vector<std::string> out;
        char buf[128];
        for (int i = 0; i < NUM_BREEDER_ROOMS; ++i) {
            const BreederRoom &rm = rooms_[i];
            if (rm.state == ROOM_EMPTY) {
                snprintf(buf, sizeof(buf), "Room %d: (empty)", i + 1);
                out.push_back(buf);
                continue;
            }
            const char *na = rm.parentA.nick[0] ? rm.parentA.nick : BreedingApp::dexName(rm.parentA.dex);
            const char *nb = rm.parentB.nick[0] ? rm.parentB.nick : BreedingApp::dexName(rm.parentB.dex);
            if (rm.state == ROOM_INCUBATING) {
                snprintf(buf, sizeof(buf), "Room %d: %s x %s  -> egg %s",
                         i + 1, na, nb, clockStr(rm.eggAppearAt).c_str());
            } else { // ROOM_EGG
                snprintf(buf, sizeof(buf), "Room %d: %s x %s  EGG -> hatch %s",
                         i + 1, na, nb, clockStr(rm.eggHatchAt).c_str());
            }
            out.push_back(buf);
        }
        return out;
    }

    // "in 3h20m" style remaining-time string for any future timestamp.
    static std::string remainingStr(long target, long now) {
        long s = target - now;
        if (s <= 0) return "now";
        long d = s / 86400; s %= 86400;
        long h = s / 3600;  s %= 3600;
        long m = s / 60;
        char buf[32];
        if (d > 0) snprintf(buf, sizeof(buf), "in %ldd %ldh", d, h);
        else if (h > 0) snprintf(buf, sizeof(buf), "in %ldh %ldm", h, m);
        else snprintf(buf, sizeof(buf), "in %ldm", m);
        return buf;
    }

private:
    BreederRoom rooms_[NUM_BREEDER_ROOMS];

    static const char *nameOf(const BreedMon &m) {
        return m.nick[0] ? m.nick : BreedingApp::dexName(m.dex);
    }
    static const char *nonBreederWhy(const Genotype &g) {
        if (isSterile(g))     return " is sterile (bb) — can't breed.";
        if (neverHatches(g))  return " is hh (no-hatch) — a non-breeder.";
        return " can't breed.";
    }
    static const char *nameOfId(const BreedingApp &app, uint32_t id) {
        for (const auto &m : app.roster()) if (m.id == id) return nameOf(m);
        return "?";
    }
    static std::string clockStr(long t) {
        time_t tt = (time_t)t;
        struct tm lt = *localtime(&tt);
        char buf[24];
        strftime(buf, sizeof(buf), "%a %H:%M", &lt);
        return buf;
    }

    // Roll the egg's genotype from the pair. Species follows the dam. Provenance
    // is derived from the parents. Sets childWillHatch=false for an hh egg.
    void rollEgg(BreedingApp &app, BreederRoom &rm, Rng &rng) {
        (void)app;
        const BreedMon &pa = rm.parentA;   // stored copies — no roster lookup needed
        const BreedMon &pb = rm.parentB;

        BreedMon child{};
        child.geno = cross(pa.geno, pb.geno, rng);
        // Baby is a 50/50 coin between the two parents' species, always the
        // EARLIEST evolution of whichever is picked (e.g. Raichu → Pikachu).
        uint8_t pickDex = rng.bit() ? pa.dex : pb.dex;
        child.dex   = BreedingApp::baseForm(pickDex);
        child.level = 1;
        strncpy(child.nick, BreedingApp::dexName(child.dex), sizeof(child.nick) - 1);
        child.id    = 0;                 // assigned when added to the box at hatch
        deriveChildProvenance(child, pa, pb);

        rm.pendingChild   = child;
        rm.childWillHatch = !neverHatches(child.geno);   // hh never hatches
        rm.rolled         = true;
    }

    // Provenance derivation, mirroring BreedingApp::deriveProvenance (which is
    // private). Kept in sync with that logic.
    static void deriveChildProvenance(BreedMon &child, const BreedMon &pa,
                                      const BreedMon &pb) {
        child.parentA = pa.id;
        child.parentB = pb.id;
        child.depth   = (uint8_t)(1 + (pa.depth > pb.depth ? pa.depth : pb.depth));
        if (pa.id == pb.id) {
            child.prov = PROV_S;
            child.provGen = (uint8_t)((pa.prov == PROV_S ? pa.provGen : 0) + 1);
        } else if (pa.id == pb.parentA || pa.id == pb.parentB ||
                   pb.id == pa.parentA || pb.id == pa.parentB) {
            uint8_t n = 0;
            if (pa.prov == PROV_BX) n = pa.provGen;
            if (pb.prov == PROV_BX && pb.provGen > n) n = pb.provGen;
            child.prov = PROV_BX; child.provGen = (uint8_t)(n + 1);
        } else {
            child.prov = PROV_F; child.provGen = child.depth;
        }
        bool lr = child.geno.rainbow == 2 && pa.geno.rainbow && pb.geno.rainbow;
        bool ls = child.geno.shiny   == 2 && pa.geno.shiny   && pb.geno.shiny;
        bool ld = child.geno.dark    == 2 && pa.geno.dark    && pb.geno.dark;
        if (lr || ls || ld) child.prov = PROV_IBL;
    }
};

}  // namespace breeding
