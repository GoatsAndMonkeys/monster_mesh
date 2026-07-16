// Standalone regression test for the per-species EXP growth curve fix
// (the "Mewtwo overshoot" bug). Before the fix, checkout used a single
// medium-fast (n^3) curve and a hardcoded 1,000,000 EXP cap, so Slow-growth
// species like Mewtwo (dex 150) over-leveled on checkout and were capped
// below their true L100 EXP. The fix routes every species through
// expForLevel()/levelForExp() keyed on speciesGrowthRate[].
//
// Build (mirrors the daycare / battle clang++ harness):
//   clang++ -std=c++17 -DARCH_PORTDUINO \
//     -I /private/tmp/mm-compile-stubs -I src -I src/modules/monstermesh \
//     test/test_monstermesh_growth/test_main.cpp -o growth_suite
#include <unity.h>

#include "modules/monstermesh/DaycareSavPatcher.h"

namespace {

constexpr uint8_t MEWTWO = 150;   // national dex; growth rate = SLOW (5n^3/4)

// Mewtwo is Slow: exp at L72 = 5*72^3/4 = 466560, and the next boundary
// (L73) = 5*73^3/4 = 486271. Any EXP in [466560, 486270] must stay L72.
void test_mewtwo_is_slow_growth_and_does_not_overshoot()
{
    // Sanity: Mewtwo really is on the Slow curve.
    TEST_ASSERT_EQUAL_UINT8(GROWTH_SLOW, speciesGrowthRate[MEWTWO]);

    // Exact L72 boundary.
    TEST_ASSERT_EQUAL_UINT32(466560u, expForLevel(MEWTWO, 72));
    TEST_ASSERT_EQUAL_UINT8(72, levelForExp(MEWTWO, 466560u));

    // One battle's worth of XP past L72 must remain L72 on the Slow curve.
    // (The old medium-fast cbrt curve would report ~78 here: 78^3 = 474552.)
    TEST_ASSERT_EQUAL_UINT8(72, levelForExp(MEWTWO, 474552u));   // == 78^3
    TEST_ASSERT_EQUAL_UINT8(72, levelForExp(MEWTWO, 480000u));
    TEST_ASSERT_EQUAL_UINT8(72, levelForExp(MEWTWO, 486270u));   // one below L73

    // The very next EXP tick crosses into L73 — proves the boundary is exact,
    // not that levelForExp is simply saturating.
    TEST_ASSERT_EQUAL_UINT32(486271u, expForLevel(MEWTWO, 73));
    TEST_ASSERT_EQUAL_UINT8(73, levelForExp(MEWTWO, 486271u));
}

// Contrast proof: the OLD single-curve code used cbrt(exp) (medium-fast).
// cbrt(474552) == 78 and cbrt(486270) ~= 78.6, so a medium-fast reading of
// this EXP band lands at 78 — the exact overshoot the per-species curve fixes.
void test_old_medium_fast_curve_would_have_overshot_to_78()
{
    // Pikachu (dex 25) is genuinely medium-fast: exp = n^3.
    TEST_ASSERT_EQUAL_UINT8(GROWTH_MEDIUM_FAST, speciesGrowthRate[25]);
    TEST_ASSERT_EQUAL_UINT32(474552u, expForLevel(25, 78));      // 78^3
    TEST_ASSERT_EQUAL_UINT8(78, levelForExp(25, 474552u));       // medium-fast → 78
    TEST_ASSERT_EQUAL_UINT8(78, levelForExp(25, 486270u));       // still 78
    // Same EXP, Slow species → 72. Divergence == the bug the fix removes.
    TEST_ASSERT_EQUAL_UINT8(72, levelForExp(MEWTWO, 486270u));
}

// A medium-fast species round-trips exactly for a spread of levels.
void test_medium_fast_round_trips()
{
    const uint8_t dex = 25;   // Pikachu, medium-fast
    const uint8_t levels[] = {5, 12, 25, 50, 73, 99, 100};
    for (uint8_t i = 0; i < sizeof(levels); ++i) {
        uint8_t L = levels[i];
        uint32_t exp = expForLevel(dex, L);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)L * L * L, exp);      // n^3
        TEST_ASSERT_EQUAL_UINT8(L, levelForExp(dex, exp));
    }
}

// Slow L100 EXP == 1,250,000 — the old hardcoded 1,000,000 cap under-capped
// every Slow-growth mon (Mewtwo, Dratini line, Gyarados, Lapras, ...).
void test_slow_l100_cap_is_1_250_000()
{
    TEST_ASSERT_EQUAL_UINT32(1250000u, expForLevel(MEWTWO, 100));
    // The old 1,000,000 cap is below a Slow mon's true L100 requirement, so it
    // is strictly less than the real cap: proves the under-cap.
    TEST_ASSERT_TRUE(expForLevel(MEWTWO, 100) > 1000000u);
    // At the old cap, a Slow mon is still only L93 (5*93^3/4=1005271 > 1e6,
    // 5*92^3/4=973360 <= 1e6), never L100.
    TEST_ASSERT_EQUAL_UINT8(92, levelForExp(MEWTWO, 1000000u));
    // Full EXP reaches L100 on the correct curve.
    TEST_ASSERT_EQUAL_UINT8(100, levelForExp(MEWTWO, 1250000u));
}

} // namespace

void setUp() {}
void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_mewtwo_is_slow_growth_and_does_not_overshoot);
    RUN_TEST(test_old_medium_fast_curve_would_have_overshot_to_78);
    RUN_TEST(test_medium_fast_round_trips);
    RUN_TEST(test_slow_l100_cap_is_1_250_000);
    return UNITY_END();
}
