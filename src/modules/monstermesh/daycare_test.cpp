// ── Daycare Validation Test ─────────────────────────────────────────────────
// Compile: g++ -std=c++17 -O2 -DARDUINO=100 -I. -o daycare_test daycare_test.cpp PokemonDaycare.cpp DaycareEventGen.cpp

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <set>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

// ── Fake millis ─────────────────────────────────────────────────────────────
static uint32_t _fakeMillis = 0;
uint32_t millis() { return _fakeMillis; }
void advanceMs(uint32_t ms) { _fakeMillis += ms; }

#include "PokemonDaycare.h"
#include "DaycareSavPatcher.h"

// ── Tracking ────────────────────────────────────────────────────────────────
static int dmCount = 0;
static int broadcastCount = 0;
static int beaconCount = 0;
static std::map<uint32_t, int> dmsByNode;
static std::vector<std::string> broadcastMessages;
static int dmBytes = 0, beaconBytes = 0, broadcastBytes = 0;
static std::vector<std::string> sampleDms;

void testSendDm(uint32_t dest, const char *msg, void *ctx) {
    dmCount++; dmsByNode[dest]++; dmBytes += strlen(msg) + 8;
    if (sampleDms.size() < 20) sampleDms.push_back(msg);
}
void testBroadcast(const char *msg, void *ctx) {
    broadcastCount++; broadcastMessages.push_back(msg); broadcastBytes += strlen(msg) + 8;
}
void testSendBeacon(const DaycareBeacon &beacon, void *ctx) {
    beaconCount++; beaconBytes += sizeof(DaycareBeacon);
}

static const char* specName(uint8_t dex) {
    static const char *names[] = {
        "","Bulbasaur","Ivysaur","Venusaur","Charmander","Charmeleon",
        "Charizard","Squirtle","Wartortle","Blastoise","Caterpie",
        "Metapod","Butterfree","Weedle","Kakuna","Beedrill",
        "Pidgey","Pidgeotto","Pidgeot","Rattata","Raticate",
        "Spearow","Fearow","Ekans","Arbok","Pikachu",
        "Raichu","Sandshrew","Sandslash","NidoranF","Nidorina",
        "Nidoqueen","NidoranM","Nidorino","Nidoking","Clefairy",
        "Clefable","Vulpix","Ninetales","Jigglypuff","Wigglytuff",
        "Zubat","Golbat","Oddish","Gloom","Vileplume",
        "Paras","Parasect","Venonat","Venomoth","Diglett",
        "Dugtrio","Meowth","Persian","Psyduck","Golduck",
        "Mankey","Primeape","Growlithe","Arcanine","Poliwag",
        "Poliwhirl","Poliwrath","Abra","Kadabra","Alakazam",
        "Machop","Machoke","Machamp","Bellsprout","Weepinbell",
        "Victreebel","Tentacool","Tentacruel","Geodude","Graveler",
        "Golem","Ponyta","Rapidash","Slowpoke","Slowbro",
        "Magnemite","Magneton","Farfetchd","Doduo","Dodrio",
        "Seel","Dewgong","Grimer","Muk","Shellder",
        "Cloyster","Gastly","Haunter","Gengar","Onix",
        "Drowzee","Hypno","Krabby","Kingler","Voltorb",
        "Electrode","Exeggcute","Exeggutor","Cubone","Marowak",
        "Hitmonlee","Hitmonchan","Lickitung","Koffing","Weezing",
        "Rhyhorn","Rhydon","Chansey","Tangela","Kangaskhan",
        "Horsea","Seadra","Goldeen","Seaking","Staryu",
        "Starmie","MrMime","Scyther","Jynx","Electabuzz",
        "Magmar","Pinsir","Tauros","Magikarp","Gyarados",
        "Lapras","Ditto","Eevee","Vaporeon","Jolteon",
        "Flareon","Porygon","Omanyte","Omastar","Kabuto",
        "Kabutops","Aerodactyl","Snorlax","Articuno","Zapdos",
        "Moltres","Dratini","Dragonair","Dragonite","Mewtwo","Mew"
    };
    return (dex <= 151) ? names[dex] : "???";
}

int main() {
    int issues = 0;

    // ════════════════════════════════════════════════════════════════════════
    // 10,000-HOUR SIMULATION
    // ════════════════════════════════════════════════════════════════════════
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     POKEMON DAYCARE — 10,000 HOUR SIMULATION               ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    PokemonDaycare daycare;
    daycare.init();
    daycare.setSendDm(testSendDm, nullptr);
    daycare.setBroadcast(testBroadcast, nullptr);
    daycare.setSendBeacon(testSendBeacon, nullptr);

    uint8_t species[] = {25, 6, 129, 94, 143, 151};
    uint8_t levels[]  = {50, 60, 5, 45, 55, 70};
    char nicknames[6][11] = {"SPARKY", "BLAZE", "", "SHADOW", "SNOOZE", ""};
    daycare.checkIn(species, levels, nicknames, 6, "ASH", "RED");

    printf("Party: Pikachu/SPARKY(50) Charizard/BLAZE(60) Magikarp(5) Gengar/SHADOW(45) Snorlax/SNOOZE(55) Mew(70)\n");
    printf("Trainer: ASH-RED\n\n");

    DaycareBeacon misty = {}; misty.nodeId = 0xAA01;
    strncpy(misty.shortName, "MST", 4); strncpy(misty.gameName, "MISTY", 7);
    misty.partyCount = 2;
    misty.pokemon[0] = {121, 40, "STARSHINE"};
    misty.pokemon[1] = {130, 45, "TSUNAMI"};

    DaycareBeacon brock = {}; brock.nodeId = 0xBB02;
    strncpy(brock.shortName, "BRK", 4); strncpy(brock.gameName, "BROCK", 7);
    brock.partyCount = 1; brock.pokemon[0] = {95, 35, "ROCKY"};

    DaycareBeacon gary = {}; gary.nodeId = 0xCC03;
    strncpy(gary.shortName, "GAR", 4); strncpy(gary.gameName, "BLUE", 7);
    gary.partyCount = 3;
    gary.pokemon[0] = {134, 55, "AQUA"};
    gary.pokemon[1] = {136, 50, "EMBER"};
    gary.pokemon[2] = {135, 52, "VOLT"};

    DaycareBeacon lance = {}; lance.nodeId = 0xDD04;
    strncpy(lance.shortName, "LNC", 4); strncpy(lance.gameName, "LANCE", 7);
    lance.partyCount = 1; lance.pokemon[0] = {149, 70, "DRAGN"};

    DaycareBeacon nurse = {}; nurse.nodeId = 0xEE05;
    strncpy(nurse.shortName, "JOY", 4); strncpy(nurse.gameName, "JOY", 7);
    nurse.partyCount = 1; nurse.pokemon[0] = {113, 60, ""};

    DaycareWeatherType weatherSchedule[] = {
        WEATHER_CLEAR, WEATHER_RAIN, WEATHER_NONE, WEATHER_THUNDERSTORM,
        WEATHER_CLEAR, WEATHER_SNOW, WEATHER_NONE, WEATHER_FOG,
        WEATHER_WINDY, WEATHER_HOT, WEATHER_NONE, WEATHER_COLD,
        WEATHER_CLEAR, WEATHER_RAIN, WEATHER_THUNDERSTORM, WEATHER_NONE,
        WEATHER_FOG, WEATHER_SNOW, WEATHER_WINDY, WEATHER_CLEAR
    };
    int weatherSlots = sizeof(weatherSchedule) / sizeof(weatherSchedule[0]);

    std::set<std::string> uniqueMessages;
    std::map<std::string, int> messageCounts;
    int totalXpEvents = 0, totalFlavorEvents = 0;
    int totalXpGranted = 0;

    printf("Running 10,000 hours...\n\n");
    clock_t startClock = clock();

    for (int hour = 0; hour < 10000; hour++) {
        bool mistyOn = (hour < 5000) || (hour >= 7000);
        bool brockOn = (hour >= 500 && hour < 3000) || (hour % 1000 < 100 && hour > 3000);
        bool garyOn = (hour >= 2000);
        bool lanceOn = (hour >= 4000 && hour < 4100);
        bool nurseOn = (hour >= 6000);

        if (mistyOn && hour % 2 == 0) daycare.handleBeacon(misty);
        if (brockOn && hour % 2 == 0) daycare.handleBeacon(brock);
        if (garyOn && hour % 3 == 0) daycare.handleBeacon(gary);
        if (lanceOn) daycare.handleBeacon(lance);
        if (nurseOn && hour % 2 == 0) daycare.handleBeacon(nurse);

        if (hour % 500 == 0) {
            int wIdx = (hour / 500) % weatherSlots;
            DaycareWeatherType w = weatherSchedule[wIdx];
            daycare.setWeather(w, (w == WEATHER_SNOW || w == WEATHER_COLD) ? -5 : 22,
                              (w == WEATHER_WINDY) ? 20 : 5);
        }

        advanceMs(3600000);
        daycare.tick(_fakeMillis);

        const auto &evt = daycare.getLastEvent();
        if (evt.message[0] == '\0') continue;

        std::string msg(evt.message);
        uniqueMessages.insert(msg);
        messageCounts[msg]++;
        if (evt.xp > 0) { totalXpEvents++; totalXpGranted += evt.xp; }
        else totalFlavorEvents++;
    }

    double elapsed = (double)(clock() - startClock) / CLOCKS_PER_SEC;

    // Final report
    const auto &fs = daycare.getState();
    int totalEvents = totalXpEvents + totalFlavorEvents;
    printf("── Results ─────────────────────────────────────────────────────\n");
    printf("  Events: %d | XP: %d (%.1f%%) | Unique: %zu (%.1f%%)\n",
           totalEvents, totalXpEvents, totalXpEvents * 100.0 / totalEvents,
           uniqueMessages.size(), uniqueMessages.size() * 100.0 / totalEvents);
    printf("  DMs: %d | Broadcasts: %d\n", dmCount, broadcastCount);

    printf("\n── Pokemon (SAV-based levels) ──────────────────────────────────\n");
    for (int i = 0; i < fs.partyCount; i++) {
        auto &p = fs.pokemon[i];
        uint8_t effLvl = p.savLevel + p.totalLevelsGained;
        printf("  %-10s: SAV Lv%d -> Lv%d (+%d)  daycareXP=%d\n",
               specName(p.speciesDex), p.savLevel, effLvl, p.totalLevelsGained, p.totalXpGained);
    }

    printf("\n── Sample DMs ─────────────────────────────────────────────────\n");
    for (auto &msg : sampleDms) printf("  %s\n", msg.c_str());

    // Validation
    printf("\n── Validation ─────────────────────────────────────────────────\n");
    double xpRate = totalXpEvents * 100.0 / totalEvents;
    if (xpRate < 40 || xpRate > 85) { printf("  [FAIL] XP rate %.1f%%\n", xpRate); issues++; }
    else printf("  [OK] XP rate: %.1f%%\n", xpRate);

    if (dmCount < 100) { printf("  [FAIL] Only %d DMs\n", dmCount); issues++; }
    else printf("  [OK] DMs: %d\n", dmCount);

    if (uniqueMessages.size() * 100.0 / totalEvents < 20) { printf("  [FAIL] Too repetitive\n"); issues++; }
    else printf("  [OK] Unique: %.1f%%\n", uniqueMessages.size() * 100.0 / totalEvents);

    for (int i = 0; i < fs.partyCount; i++) {
        uint8_t effLvl = fs.pokemon[i].savLevel + fs.pokemon[i].totalLevelsGained;
        if (effLvl > 100) { printf("  [FAIL] %s Lv%d > 100!\n", specName(fs.pokemon[i].speciesDex), effLvl); issues++; }
    }
    printf("  [OK] All levels <= 100\n");

    if (fs.relationshipCount == 0) { printf("  [FAIL] No relationships!\n"); issues++; }
    else printf("  [OK] %d relationships\n", fs.relationshipCount);

    int grammarIssues = 0;
    for (auto &[msg, cnt] : messageCounts) {
        if (msg.find(" in in ") != std::string::npos || msg.find("  ") != std::string::npos)
            grammarIssues++;
    }
    if (grammarIssues > 0) { printf("  [FAIL] %d grammar issues\n", grammarIssues); issues++; }
    else printf("  [OK] No grammar issues\n");

    printf("  Simulation time: %.2fs\n", elapsed);

    // ════════════════════════════════════════════════════════════════════════
    // SRAM PATCHER TESTS
    // ════════════════════════════════════════════════════════════════════════
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              SRAM PATCHER TESTS                            ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    // ── S1: EXP growth curves ───────────────────────────────────────────
    {
        printf("── S1: EXP growth curves ──────────────────────────────────────\n");
        struct ExpCheck { uint8_t dex; uint8_t level; uint32_t expected; };
        ExpCheck checks[] = {
            // Medium Slow: Bulbasaur (6n^3/5 - 15n^2 + 100n - 140)
            {1, 5, 135}, {1, 10, 560}, {1, 50, 117360}, {1, 100, 1059860},
            // Fast: Clefairy (4n^3/5)
            {35, 5, 100}, {35, 10, 800}, {35, 50, 100000}, {35, 100, 800000},
            // Slow: Snorlax (5n^3/4)
            {143, 5, 156}, {143, 10, 1250}, {143, 50, 156250}, {143, 100, 1250000},
            // Medium Fast: Pikachu (n^3)
            {25, 5, 125}, {25, 10, 1000}, {25, 50, 125000}, {25, 100, 1000000},
            // Slow: Magikarp (5n^3/4)
            {129, 100, 1250000},
        };
        bool ok = true;
        for (auto &c : checks) {
            uint32_t calc = expForLevel(c.dex, c.level);
            if (calc != c.expected) {
                printf("  [FAIL] %s Lv%d: expected %d, got %d\n",
                       specName(c.dex), c.level, c.expected, calc);
                ok = false;
            }
        }
        // Round-trip test
        for (uint8_t dex : {1, 25, 35, 129, 143, 151}) {
            for (uint8_t lvl = 1; lvl <= 100; lvl++) {
                if (levelForExp(dex, expForLevel(dex, lvl)) != lvl) {
                    printf("  [FAIL] %s Lv%d round-trip\n", specName(dex), lvl);
                    ok = false; break;
                }
            }
        }
        printf("  %s\n", ok ? "[OK] EXP curves correct" : "[FAIL]");
        if (!ok) issues++;
    }

    // ── S2: Stat calculation ────────────────────────────────────────────
    {
        printf("\n── S2: Stat calculation ───────────────────────────────────────\n");
        // Mewtwo Lv100, max DVs (15), max stat exp (65535)
        uint16_t hp  = calcStatHP(106, 15, 65535, 100);
        uint16_t atk = calcStat(110, 15, 65535, 100);
        uint16_t spc = calcStat(154, 15, 65535, 100);
        printf("  Mewtwo Lv100: HP=%d Atk=%d Spc=%d\n", hp, atk, spc);
        bool ok = (abs((int)hp - 415) <= 2 && abs((int)atk - 318) <= 2 && abs((int)spc - 406) <= 2);

        uint16_t pikaHP = calcStatHP(35, 8, 0, 5);
        printf("  Pikachu Lv5 (DV=8): HP=%d\n", pikaHP);
        if (pikaHP < 17 || pikaHP > 23) ok = false;

        printf("  %s\n", ok ? "[OK] Stats correct" : "[FAIL]");
        if (!ok) issues++;
    }

    // ── S3: Internal index mapping ──────────────────────────────────────
    {
        printf("\n── S3: Internal index bidirectional mapping ───────────────────\n");
        bool ok = true;
        for (int dex = 1; dex <= 151; dex++) {
            uint8_t internal = dexToInternal[dex];
            uint8_t backDex = internalToDex[internal];
            if (backDex != dex) {
                printf("  [FAIL] Dex %d (%s) -> internal 0x%02X -> dex %d\n",
                       dex, specName(dex), internal, backDex);
                ok = false;
            }
        }
        printf("  %s\n", ok ? "[OK] All 151 species map correctly" : "[FAIL]");
        if (!ok) issues++;
    }

    // ── S4: Full SRAM read/write cycle ──────────────────────────────────
    {
        printf("\n── S4: SRAM read -> daycare -> checkout -> SRAM write ────────\n");

        uint8_t sram[0x8000];
        memset(sram, 0, sizeof(sram));
        sram[SAV_PARTY_COUNT] = 3;
        sram[SAV_SPECIES_LIST + 0] = dexToInternal[25];   // Pikachu
        sram[SAV_SPECIES_LIST + 1] = dexToInternal[6];    // Charizard
        sram[SAV_SPECIES_LIST + 2] = dexToInternal[129];  // Magikarp
        sram[SAV_SPECIES_LIST + 3] = 0xFF;

        struct { uint8_t dex; uint8_t level; const char *nick; } tp[] = {
            {25, 50, "SPARKY"}, {6, 60, "BLAZE"}, {129, 5, "SPLASHY"}
        };

        for (int i = 0; i < 3; i++) {
            uint8_t *pkm = &sram[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE];
            pkm[PKM_SPECIES] = dexToInternal[tp[i].dex];
            pkm[PKM_LEVEL_PARTY] = tp[i].level;
            pkm[PKM_LEVEL_PC] = tp[i].level;
            writeBE24(&pkm[PKM_EXP], expForLevel(tp[i].dex, tp[i].level));
            pkm[PKM_DVS] = 0xFF; pkm[PKM_DVS + 1] = 0xFF;
            writeBE16(&pkm[PKM_HP_EV], 100);
            writeBE16(&pkm[PKM_ATK_EV], 100);
            writeBE16(&pkm[PKM_DEF_EV], 100);
            writeBE16(&pkm[PKM_SPD_EV], 100);
            writeBE16(&pkm[PKM_SPC_EV], 100);

            const BaseStats &base = speciesBaseStats[tp[i].dex];
            uint16_t maxHP = calcStatHP(base.hp, 15, 100, tp[i].level);
            writeBE16(&pkm[PKM_CURRENT_HP], maxHP);
            writeBE16(&pkm[PKM_MAX_HP], maxHP);
            writeBE16(&pkm[PKM_ATTACK], calcStat(base.atk, 15, 100, tp[i].level));
            writeBE16(&pkm[PKM_DEFENSE], calcStat(base.def, 15, 100, tp[i].level));
            writeBE16(&pkm[PKM_SPEED], calcStat(base.spd, 15, 100, tp[i].level));
            writeBE16(&pkm[PKM_SPECIAL], calcStat(base.spc, 15, 100, tp[i].level));

            uint8_t *nick = &sram[SAV_NICKNAMES + i * SAV_NAME_SIZE];
            int j = 0;
            while (tp[i].nick[j] && j < 10) { nick[j] = asciiToGen1Char(tp[i].nick[j]); j++; }
            nick[j] = SAV_STRING_TERMINATOR;
        }
        DaycareSavPatcher::fixChecksum(sram);

        // Read back
        DaycarePartyInfo readBack[6];
        uint8_t readCount = DaycareSavPatcher::readParty(sram, readBack);
        printf("  Read %d Pokemon from SRAM:\n", readCount);
        bool ok = (readCount == 3);
        for (int i = 0; i < readCount; i++) {
            printf("    %s Lv%d EXP=%d nick=\"%s\"\n",
                   specName(readBack[i].dexNum), readBack[i].level,
                   readBack[i].totalExp, readBack[i].nickname);
            if (readBack[i].dexNum != tp[i].dex) ok = false;
            if (readBack[i].level != tp[i].level) ok = false;
            if (strcmp(readBack[i].nickname, tp[i].nick) != 0) ok = false;
        }
        if (!ok) { printf("  [FAIL] SRAM read mismatch\n"); issues++; }
        else printf("  [OK] SRAM read correct\n");

        // Check in from SRAM
        PokemonDaycare dcSram;
        dcSram.init();
        dcSram.setSendDm([](uint32_t d, const char *m, void *ctx){}, nullptr);
        dcSram.setSendBeacon([](const DaycareBeacon &b, void *ctx){}, nullptr);
        dcSram.setBroadcast([](const char *m, void *ctx){}, nullptr);
        dcSram.checkIn(sram, "TST", "RED");

        // Run 500 hours
        uint32_t fakeMs = 500000000;
        for (int h = 0; h < 500; h++) {
            fakeMs += 3600000;
            _fakeMillis = fakeMs;
            dcSram.tick(fakeMs);
        }

        const auto &ss = dcSram.getState();
        printf("\n  After 500 hours:\n");
        for (int i = 0; i < ss.partyCount; i++) {
            auto &p = ss.pokemon[i];
            printf("    %s: Lv%d -> Lv%d (+%d)  daycareXP=%d\n",
                   specName(p.speciesDex), p.savLevel,
                   p.savLevel + p.totalLevelsGained, p.totalLevelsGained, p.totalXpGained);
        }

        // Save pre-checkout state
        uint8_t oldLvls[3]; uint32_t oldExp[3]; uint16_t oldHP[3];
        for (int i = 0; i < 3; i++) {
            uint8_t *pkm = &sram[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE];
            oldLvls[i] = pkm[PKM_LEVEL_PARTY];
            oldExp[i] = readBE24(&pkm[PKM_EXP]);
            oldHP[i] = readBE16(&pkm[PKM_MAX_HP]);
        }

        // Checkout — patch SRAM
        dcSram.checkOut(sram);

        printf("\n  After checkout (SRAM patched):\n");
        bool patchOk = true;
        for (int i = 0; i < 3; i++) {
            uint8_t *pkm = &sram[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE];
            uint8_t newLvl = pkm[PKM_LEVEL_PARTY];
            uint32_t newExp = readBE24(&pkm[PKM_EXP]);
            uint16_t newHP = readBE16(&pkm[PKM_MAX_HP]);
            printf("    %s: Lv%d->%d  EXP %d->%d  MaxHP %d->%d\n",
                   specName(tp[i].dex), oldLvls[i], newLvl, oldExp[i], newExp, oldHP[i], newHP);

            if (ss.pokemon[i].totalXpGained > 0 && newExp <= oldExp[i]) {
                printf("    [FAIL] EXP didn't increase!\n"); patchOk = false;
            }
            if (pkm[PKM_LEVEL_PC] != pkm[PKM_LEVEL_PARTY]) {
                printf("    [FAIL] PC/party level mismatch\n"); patchOk = false;
            }
            uint8_t expected = levelForExp(tp[i].dex, newExp);
            if (newLvl != expected) {
                printf("    [FAIL] Level %d != expected %d\n", newLvl, expected); patchOk = false;
            }
        }

        // Verify checksum
        uint8_t sum = 0;
        for (uint32_t i = SAV_CHECKSUM_START; i <= SAV_CHECKSUM_END; i++) sum += sram[i];
        if (sram[SAV_CHECKSUM_OFFSET] != (uint8_t)(~sum)) {
            printf("  [FAIL] Checksum invalid\n"); patchOk = false;
        } else {
            printf("  [OK] Checksum valid\n");
        }
        printf("  %s\n", patchOk ? "[OK] Full SRAM cycle correct" : "[FAIL]");
        if (!patchOk) issues++;
    }

    // ── S5: Level cap at 100 in SRAM ────────────────────────────────────
    {
        printf("\n── S5: Level cap at 100 ──────────────────────────────────────\n");
        uint8_t sram[0x8000];
        memset(sram, 0, sizeof(sram));
        sram[SAV_PARTY_COUNT] = 1;
        sram[SAV_SPECIES_LIST] = dexToInternal[25];
        sram[SAV_SPECIES_LIST + 1] = 0xFF;

        uint8_t *pkm = &sram[SAV_POKEMON_DATA];
        pkm[PKM_SPECIES] = dexToInternal[25];
        pkm[PKM_LEVEL_PARTY] = 99; pkm[PKM_LEVEL_PC] = 99;
        writeBE24(&pkm[PKM_EXP], expForLevel(25, 99));
        pkm[PKM_DVS] = 0xFF; pkm[PKM_DVS + 1] = 0xFF;
        writeBE16(&pkm[PKM_MAX_HP], 200); writeBE16(&pkm[PKM_CURRENT_HP], 200);
        DaycareSavPatcher::fixChecksum(sram);

        uint8_t dexNums[] = {25};
        uint32_t xpGained[] = {999999};
        DaycareSavPatcher::checkout(sram, dexNums, xpGained, 1);

        uint8_t finalLvl = pkm[PKM_LEVEL_PARTY];
        uint32_t finalExp = readBE24(&pkm[PKM_EXP]);
        uint32_t maxExp = expForLevel(25, 100);

        printf("  Pikachu Lv99 + 999999 XP: Lv=%d, EXP=%d (max=%d)\n",
               finalLvl, finalExp, maxExp);
        bool ok = (finalLvl == 100 && finalExp == maxExp);
        printf("  %s\n", ok ? "[OK] Level capped at 100" : "[FAIL]");
        if (!ok) issues++;
    }

    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("  TOTAL ISSUES: %d\n", issues);
    printf("═══════════════════════════════════════════════════════════════\n");
    return issues > 0 ? 1 : 0;
}
