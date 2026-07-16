#pragma once
// ── Per-Pokemon Kanto Journey ────────────────────────────────────────────────
// Each individual Pokemon left in the daycare walks its OWN trip across Kanto.
// Progress is keyed by Pokemon IDENTITY (species + nickname + original trainer),
// NOT by party slot, so swapping a mon in and out never resets its journey — it
// picks up right where it left off. Persisted to its own versioned file so it
// is independent of daycare.dat (which has no version field).
//
// Header-only, mirroring DaycareSavPatcher / DaycareAchievements — no build
// wiring required.

#include <Arduino.h>
#include "FSCommon.h"

// ── The Kanto map ────────────────────────────────────────────────────────────
// A single canonical journey: the classic Pallet-Town-to-Indigo-Plateau route,
// ordered as a linear walk. Each Pokemon advances forward one waypoint at a
// time; on reaching the end it loops (a new "lap" of Kanto) so long-term
// boarders keep travelling. Reused read-only by the display code.

namespace kanto {

static const char *const kWaypoints[] = {
    "Pallet Town",      // 0
    "Route 1",          // 1
    "Viridian City",    // 2
    "Route 2",          // 3
    "Viridian Forest",  // 4
    "Pewter City",      // 5
    "Route 3",          // 6
    "Mt. Moon",         // 7
    "Route 4",          // 8
    "Cerulean City",    // 9
    "Route 24",         // 10
    "Route 25",         // 11
    "Route 5",          // 12
    "Underground Path", // 13
    "Route 6",          // 14
    "Vermilion City",   // 15
    "S.S. Anne",        // 16
    "Route 11",         // 17
    "Diglett's Cave",   // 18
    "Route 9",          // 19
    "Rock Tunnel",      // 20
    "Route 10",         // 21
    "Lavender Town",    // 22
    "Route 8",          // 23
    "Saffron City",     // 24
    "Route 7",          // 25
    "Celadon City",     // 26
    "Route 16",         // 27
    "Cycling Road",     // 28
    "Route 18",         // 29
    "Fuchsia City",     // 30
    "Safari Zone",      // 31
    "Route 19",         // 32
    "Route 20",         // 33
    "Seafoam Islands",  // 34
    "Cinnabar Island",  // 35
    "Pokemon Mansion",  // 36
    "Route 21",         // 37
    "Route 22",         // 38
    "Victory Road",     // 39
    "Indigo Plateau",   // 40
    "Cerulean Cave",    // 41
};
static constexpr uint8_t WAYPOINT_COUNT =
    sizeof(kWaypoints) / sizeof(kWaypoints[0]);

inline const char *waypointName(uint8_t idx) {
    return idx < WAYPOINT_COUNT ? kWaypoints[idx] : "?";
}

}  // namespace kanto

// ── One traveller's saved position ───────────────────────────────────────────

struct KantoTraveler {
    uint16_t speciesDex;      // national dex — part of identity
    char     nickname[11];    // player nickname — part of identity
    char     otName[11];      // original trainer — part of identity
    uint8_t  loc;             // current waypoint (index into kWaypoints)
    uint8_t  progress;        // 0-99 toward the next waypoint
    uint16_t laps;            // times the full journey has been completed
    uint32_t totalSteps;      // lifetime travel points accrued
};

// ── Persistent roster (own file, own magic + version + reserved block) ────────

static constexpr uint8_t KANTO_MAX_TRAVELERS = 24;

struct KantoRosterFile {
    uint32_t magic;                              // 'KNTO'
    uint16_t version;
    uint16_t count;
    KantoTraveler travelers[KANTO_MAX_TRAVELERS];
    uint8_t  reserved[64];                       // forward-compat headroom
};

class KantoJourney {
public:
    static constexpr uint32_t MAGIC   = 0x4B4E544FUL;  // 'KNTO'
    static constexpr uint16_t VERSION = 1;

    void init() {
        memset(&roster_, 0, sizeof(roster_));
        roster_.magic   = MAGIC;
        roster_.version = VERSION;
        roster_.count   = 0;
        dirty_ = false;
    }

    // Load from flash; re-init on missing/mismatched file.
    void load() {
        auto f = FSCom.open(PATH, FILE_O_READ);
        if (!f) { init(); return; }
        size_t rd = f.read(reinterpret_cast<uint8_t *>(&roster_), sizeof(roster_));
        f.close();
        if (rd != sizeof(roster_) || roster_.magic != MAGIC ||
            roster_.version != VERSION || roster_.count > KANTO_MAX_TRAVELERS) {
            init();
        }
        dirty_ = false;
    }

    bool save() {
        FSCom.mkdir("/monstermesh");
        auto f = FSCom.open(PATH, FILE_O_WRITE);
        if (!f) return false;
        size_t wr = f.write(reinterpret_cast<const uint8_t *>(&roster_), sizeof(roster_));
        f.close();
        if (wr == sizeof(roster_)) dirty_ = false;
        return wr == sizeof(roster_);
    }

    bool dirty() const { return dirty_; }

    // Find this individual Pokemon's traveller, creating one at Pallet Town if
    // it has never boarded before. Identity = species + nickname + OT.
    // Returns roster index (always valid).
    uint8_t findOrCreate(uint16_t dex, const char *nick, const char *ot) {
        char n[11], o[11];
        copyName(n, nick);
        copyName(o, ot);

        for (uint8_t i = 0; i < roster_.count; i++) {
            KantoTraveler &t = roster_.travelers[i];
            if (t.speciesDex == dex &&
                strncmp(t.nickname, n, sizeof(n)) == 0 &&
                strncmp(t.otName, o, sizeof(o)) == 0) {
                return i;
            }
        }

        uint8_t slot;
        if (roster_.count < KANTO_MAX_TRAVELERS) {
            slot = roster_.count++;
        } else {
            // Roster full — evict the least-travelled (smallest loss).
            slot = 0;
            uint32_t least = UINT32_MAX;
            for (uint8_t i = 0; i < roster_.count; i++) {
                if (roster_.travelers[i].totalSteps < least) {
                    least = roster_.travelers[i].totalSteps;
                    slot = i;
                }
            }
        }

        KantoTraveler &t = roster_.travelers[slot];
        memset(&t, 0, sizeof(t));
        t.speciesDex = dex;
        memcpy(t.nickname, n, sizeof(t.nickname));
        memcpy(t.otName, o, sizeof(t.otName));
        t.loc = 0;         // start at Pallet Town
        t.progress = 0;
        dirty_ = true;
        return slot;
    }

    // Advance a traveller by `pts` progress points, rolling over waypoints and
    // wrapping to a new lap at the end of Kanto.
    void advance(uint8_t idx, uint8_t pts) {
        if (idx >= roster_.count || pts == 0) return;
        KantoTraveler &t = roster_.travelers[idx];
        t.totalSteps += pts;
        uint16_t p = (uint16_t)t.progress + pts;
        while (p >= 100) {
            p -= 100;
            if (t.loc + 1 >= kanto::WAYPOINT_COUNT) {
                t.loc = 0;
                if (t.laps < UINT16_MAX) t.laps++;
            } else {
                t.loc++;
            }
        }
        t.progress = (uint8_t)p;
        dirty_ = true;
    }

    const KantoTraveler *get(uint8_t idx) const {
        return idx < roster_.count ? &roster_.travelers[idx] : nullptr;
    }

private:
    static constexpr const char *PATH = "/monstermesh/kanto.dat";

    static void copyName(char *dst, const char *src) {
        if (src) { strncpy(dst, src, 10); dst[10] = '\0'; }
        else     { dst[0] = '\0'; }
        // pad remainder with NUL so strncmp over the full field is stable
        for (uint8_t i = (uint8_t)strlen(dst) + 1; i < 11; i++) dst[i] = '\0';
    }

    KantoRosterFile roster_ = {};
    bool dirty_ = false;
};
