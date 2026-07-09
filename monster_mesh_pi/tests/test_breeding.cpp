// test_breeding.cpp — MonsterMesh Pi breeding app: verifier + CLI demo.
//
//   Build:  cmake --build build --target mmbreed
//   Run:    ./build/mmbreed                    # self-test + demo (verifies odds)
//           ./build/mmbreed --roster r.json    # import a roster, list it
//           ./build/mmbreed --roster r.json --breed 1 2 [--seed 7]
//
// The self-test Monte-Carlos the key crosses in color-variants.md and asserts
// the offspring genotype ratios (Pink×Pink, Dd×Dd→Blackout, Ss×Ss→Shiny,
// carrier×carrier defect→25% affected, Rainbow♀×carrier♂ sex-limited display).

#include "../src/shared/BreedingApp.h"
#include "../src/shared/BreederRoom.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

using namespace breeding;

static int g_fail = 0;

// Assert a Monte-Carlo proportion is within `tol` of `want`.
static void expectNear(const char *what, double got, double want, double tol) {
    bool ok = (got >= want - tol) && (got <= want + tol);
    printf("  [%s] %-42s got %.3f  want %.3f\n",
           ok ? "PASS" : "FAIL", what, got, want);
    if (!ok) ++g_fail;
}

// Cross two genotypes N times; return counts of a predicate.
static double fraction(const Genotype &a, const Genotype &b, int n,
                       bool (*pred)(const Genotype &), uint64_t seed) {
    Rng rng(seed);
    int hits = 0;
    for (int i = 0; i < n; ++i) {
        Genotype o = cross(a, b, rng);
        if (pred(o)) ++hits;
    }
    return (double)hits / n;
}

static bool pRainbowRR(const Genotype &g) { return g.rainbow == 2; }
static bool pRainbowRr(const Genotype &g) { return g.rainbow == 1; }
static bool pDarkDD(const Genotype &g)    { return g.dark == 2; }
static bool pDarkDd(const Genotype &g)    { return g.dark == 1; }
static bool pShinyss(const Genotype &g)   { return g.shiny == 2; }
static bool pShinySs(const Genotype &g)   { return g.shiny == 1; }
static bool pSterilebb(const Genotype &g) { return g.sterile == 2; }
static bool pFemale(const Genotype &g)    { return g.female == 1; }
// Visible Rainbow skin among the whole clutch (sex-limited: ♀ only).
static bool pVisRainbow(const Genotype &g){ return skinOf(g) == SKIN_RAINBOW; }
static bool pVisPink(const Genotype &g)   { return skinOf(g) == SKIN_PINK; }

static int runSelfTest() {
    const int N = 200000;
    const double TOL = 0.01;
    printf("=== Breeding genetics self-test (%d rolls/cross) ===\n", N);

    // Pink × Pink  (Rr × Rr) → 25% rr / 50% Rr / 25% RR
    Genotype pink{1,0,0, 0,0,0, 1};
    printf("Pink x Pink (Rr x Rr):\n");
    expectNear("rr Rainbow-geno", fraction(pink, pink, N, pRainbowRR, 1), 0.25, TOL);
    expectNear("Rr Pink-geno",    fraction(pink, pink, N, pRainbowRr, 2), 0.50, TOL);

    // Dark × Dark  (Dd × Dd) → 25% Blackout / 50% Dark / 25% none
    Genotype darkM{0,0,1, 0,0,0, 0}, darkF{0,0,1, 0,0,0, 1};
    printf("Dark x Dark (Dd x Dd):\n");
    expectNear("DD Blackout", fraction(darkM, darkF, N, pDarkDD, 3), 0.25, TOL);
    expectNear("Dd Dark",     fraction(darkM, darkF, N, pDarkDd, 4), 0.50, TOL);

    // Shiny carrier × carrier (Ss × Ss) → 25% ss Shiny / 50% Ss
    Genotype ssc{0,1,0, 0,0,0, 0};
    printf("Carrier x Carrier (Ss x Ss):\n");
    expectNear("ss Shiny",   fraction(ssc, ssc, N, pShinyss, 5), 0.25, TOL);
    expectNear("Ss carrier", fraction(ssc, ssc, N, pShinySs, 6), 0.50, TOL);

    // Sterile carrier × carrier (Bb × Bb) → 25% bb affected
    Genotype bbc{0,0,0, 1,0,0, 0};
    printf("Defect carrier x carrier (Bb x Bb):\n");
    expectNear("bb affected", fraction(bbc, bbc, N, pSterilebb, 7), 0.25, TOL);

    // Sex is a fair coin.
    printf("Sex ratio:\n");
    expectNear("female", fraction(pink, pink, N, pFemale, 8), 0.50, TOL);

    // Sex-limited display: Rainbow♀(rr) × hidden-carrier♂(Rr).
    // Genotype daughters: 50% rr(Rainbow) / 50% Rr(Pink). Sons never display.
    // Across the WHOLE clutch: ~25% visible-Rainbow, ~25% visible-Pink, ~50%
    // Regular-looking (all sons + none else).
    Genotype rainF{2,0,0, 0,0,0, 1}, carrM{1,0,0, 0,0,0, 0};
    printf("Rainbow-female x hidden-carrier-male (sex-limited):\n");
    expectNear("clutch visible Rainbow", fraction(rainF, carrM, N, pVisRainbow, 9),  0.25, TOL);
    expectNear("clutch visible Pink",    fraction(rainF, carrM, N, pVisPink,    10), 0.25, TOL);
    // A male can NEVER display Pink/Rainbow — sanity check on a Pink male.
    Genotype pinkMale{1,0,0, 0,0,0, 0};
    printf("Sex-limited display invariant:\n");
    printf("  [%s] Pink-genotype male displays as %s (must be Regular)\n",
           skinOf(pinkMale) == SKIN_REGULAR ? "PASS" : "FAIL",
           skinName(pinkMale));
    if (skinOf(pinkMale) != SKIN_REGULAR) ++g_fail;

    printf("\n%s\n", g_fail == 0 ? "ALL GENETICS TESTS PASSED"
                                 : "*** GENETICS TESTS FAILED ***");
    return g_fail;
}

static void printRoster(const BreedingApp &app) {
    printf("\n--- Roster (%zu) ---\n", app.size());
    int i = 0;
    for (const auto &m : app.roster())
        printf(" [%d] %s\n", i++, BreedingApp::summaryLine(m).c_str());
    printf("Breeding %s\n", app.breedingUnlocked()
           ? "UNLOCKED (a Pentest catch is on the deck)."
           : "LOCKED.");
}

static void printReport(const BreedMon &m) {
    for (const auto &line : BreedingApp::bloodTest(m))
        printf("   %s\n", line.c_str());
}

static void runDemo() {
    printf("\n=== Breeding app demo (seeded roster) ===\n");
    BreedingApp app;
    app.seedTestRoster();
    printRoster(app);

    // Gate demo: an app with no Wild mon is locked.
    BreedingApp locked;
    BreedMon bred{}; bred.dex = 25; bred.prov = PROV_F; bred.provGen = 1;
    strncpy(bred.nick, "Egg", 10);
    locked.add(bred);
    Rng rl(1);
    BreedResult lr = locked.breed(0, 0, rl);
    printf("\nGate check (roster has no Pentest catch): %s\n", lr.message.c_str());

    // Cross Rosa (Pink ♀, idx 1) × Prism (hidden Rainbow carrier ♂, idx 2).
    printf("\n>> Breed Rosa (Pink) x Prism (Rainbow carrier):\n");
    Rng r(20260707);
    for (int t = 0; t < 4; ++t) {
        BreedResult br = app.breed(1, 2, r);
        printf("  %s\n", br.message.c_str());
        if (br.status == BREED_OK && (skinOf(br.child.geno) != SKIN_REGULAR)) {
            printReport(br.child);
        }
    }

    // Heartbreak: try to breed the sterile Wild Rainbow (Iris, idx 7).
    printf("\n>> Breed Iris (sterile Wild Rainbow) x Rock:\n");
    Rng r2(5);
    BreedResult hb = app.breed(7, 8, r2);
    printf("  %s\n", hb.message.c_str());

    // Blackout target: Shade x Umbra (Dd x Dd) until a Blackout drops.
    printf("\n>> Breed Shade x Umbra (Dd x Dd) chasing Blackout:\n");
    Rng r3(99);
    for (int t = 0; t < 30; ++t) {
        BreedResult br = app.breed(3, 4, r3);
        if (br.status == BREED_OK && br.child.geno.dark == 2) {
            printf("  Blackout hatched on attempt %d!\n", t + 1);
            printReport(br.child);
            break;
        }
        if (br.status == BREED_NO_HATCH) printf("  attempt %d: %s\n", t + 1, br.message.c_str());
    }
}

// Build a local time_t for a fixed calendar moment (for deterministic tests).
static long makeLocal(int y, int mon, int day, int hour, int min) {
    struct tm lt = {};
    lt.tm_year = y - 1900; lt.tm_mon = mon - 1; lt.tm_mday = day;
    lt.tm_hour = hour; lt.tm_min = min; lt.tm_sec = 0; lt.tm_isdst = -1;
    return (long)mktime(&lt);
}
static void expectTrue(const char *what, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

static void runRoomTest() {
    using namespace breeding;
    printf("\n=== Breeder-room overnight cycle test ===\n");
    // Place the pairs on Tue 2026-07-07 at 20:00 (evening) local time.
    long placed = makeLocal(2026, 7, 7, 20, 0);

    BreedingApp app;
    app.seedTestRoster();
    size_t before = app.size();
    BreederManager mgr;

    // Player designates 3 pairs from the party, plus a 4th invalid one (Iris is
    // bb sterile) which must be SKIPPED, not fatal.
    std::vector<BreederManager::PartyPair> pairs = {
        {1, 2},   // Rosa (Pink ♀) x Prism (carrier ♂)
        {3, 4},   // Shade x Umbra (Dark x Dark)
        {5, 6},   // Sable x Cinder (Ss x Ss)
        {7, 8},   // Iris (bb sterile) x Rock  -> skipped
    };
    auto outcomes = mgr.assignPairs(app, pairs, placed);
    int placedCount = 0, skipped = 0;
    for (auto &o : outcomes) {
        printf("  pair (%zu,%zu): %s%s\n", o.a, o.b,
               o.room >= 0 ? "room " : "SKIP: ", o.note.c_str());
        if (o.room >= 0) ++placedCount; else ++skipped;
    }
    expectTrue("3 valid pairs placed", placedCount == 3);
    expectTrue("sterile pair skipped (not fatal)", skipped == 1);

    // Egg timing: appear next morning (Wed) 06:00, hatch same day 18:00.
    const BreederRoom &r0 = mgr.room(0);
    time_t ea = (time_t)r0.eggAppearAt, ha = (time_t)r0.eggHatchAt;
    struct tm te = *localtime(&ea), th = *localtime(&ha);
    expectTrue("egg appears at 06:00", te.tm_hour == 6 && te.tm_min == 0);
    expectTrue("egg appears next day (Wed 8th)", te.tm_mday == 8);
    expectTrue("egg hatches at 18:00", th.tm_hour == 18 && th.tm_min == 0);
    expectTrue("hatch same day as egg", th.tm_mday == te.tm_mday);

    // Cooldown: Rosa just went in — she can't be re-placed now.
    std::string err;
    Rng rng(1);
    long tomorrow = placed + 3600;
    int rc2 = mgr.placePair(app, 1, 3, tomorrow, err);
    expectTrue("re-placing a mon on cooldown is rejected", rc2 < 0);

    // Tick at 6 AM: eggs appear (rooms move to EGG state, nothing added yet).
    auto e6 = mgr.tick(app, r0.eggAppearAt, rng);
    expectTrue("nothing hatches at 6 AM", e6.empty());
    expectTrue("box unchanged at 6 AM", app.size() == before);
    printf("  6 AM status:\n");
    for (auto &l : mgr.statusLines(app, r0.eggAppearAt)) printf("    %s\n", l.c_str());

    // Tick at 6 PM: eggs hatch → up to 3 offspring added (minus any hh no-hatch).
    auto e18 = mgr.tick(app, r0.eggHatchAt, rng);
    int hatched = 0;
    for (auto &ev : e18) {
        printf("  6 PM: %s\n", ev.result.message.c_str());
        if (ev.result.status == BREED_OK) ++hatched;
    }
    expectTrue("eggs hatched at 6 PM", (int)e18.size() == 3);
    expectTrue("box grew by the hatched count", app.size() == before + (size_t)hatched);

    // Cooldown lifts after 7 days.
    long weekLater = placed + BREED_COOLDOWN_SEC + 60;
    int rc3 = mgr.placePair(app, 1, 2, weekLater, err);
    expectTrue("re-placing after 7 days succeeds", rc3 >= 0);

    printf("%s\n", g_fail == 0 ? "ROOM-CYCLE TESTS PASSED"
                               : "*** ROOM-CYCLE TESTS FAILED ***");
}

int main(int argc, char **argv) {
    const char *rosterPath = nullptr;
    int breedA = -1, breedB = -1;
    uint64_t seed = 0xC0FFEE;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--roster") && i + 1 < argc) rosterPath = argv[++i];
        else if (!strcmp(argv[i], "--breed") && i + 2 < argc) {
            breedA = atoi(argv[++i]); breedB = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = strtoull(argv[++i], nullptr, 0);
        }
    }

    if (rosterPath) {
        BreedingApp app;
        int n = app.importJsonFile(rosterPath);
        if (n < 0) { fprintf(stderr, "Failed to load roster %s\n", rosterPath); return 1; }
        printf("Imported %d mon(s) from %s\n", n, rosterPath);
        printRoster(app);
        if (breedA >= 0 && breedB >= 0) {
            Rng rng(seed);
            BreedResult br = app.breed((size_t)breedA, (size_t)breedB, rng);
            printf("\n>> Breed [%d] x [%d] (seed 0x%llx):\n  %s\n",
                   breedA, breedB, (unsigned long long)seed, br.message.c_str());
            if (br.status == BREED_OK) printReport(br.child);
        }
        return 0;
    }

    runSelfTest();
    runRoomTest();
    runDemo();
    return g_fail;   // nonzero if any assertion failed (CI-friendly)
}
