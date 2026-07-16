#include "FSCommon.h"
#include "SPILock.h"
#include "TestUtil.h"
#include "modules/monstermesh/PokemonDaycare.h"
#include "modules/monstermesh/MonsterMeshSavPatcher.h"
#include "modules/monstermesh/BattleSavQueue.h"

#include <array>
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#include <string>
#include <unistd.h>
#endif

struct PokemonDaycareTestAccess {
    static DaycareState &state(PokemonDaycare &daycare) { return daycare.state_; }
    static void setActive(PokemonDaycare &daycare, bool active) { daycare.active_ = active; }
    static void setRuntimeStateLoaded(PokemonDaycare &daycare) { daycare.runtimeStateLoaded_ = true; }
    static void addXp(PokemonDaycare &daycare, uint8_t partyIdx, uint16_t xp) {
        daycare.addPendingXp(partyIdx, xp);
    }
    static bool bind(PokemonDaycare &daycare, const char *savPath,
                     const DaycarePartyInfo *party, uint8_t count) {
        return daycare.ensureSavBinding(savPath, party, count);
    }
    static bool markPromoted(PokemonDaycare &daycare) {
        return daycare.saveFlushJournal(PokemonDaycare::FlushJournalPhase::PROMOTED);
    }
    static bool writeLegacyOwner(const char *savPath) {
        PokemonDaycare::LegacySavOwner owner = {};
        owner.magic = PokemonDaycare::LegacySavOwner::MAGIC;
        strncpy(owner.savPath, savPath, sizeof(owner.savPath) - 1);
        auto file = FSCom.open("/monstermesh/daycare.owner", FILE_O_WRITE);
        if (!file) return false;
        const size_t written = file.write(
            reinterpret_cast<const uint8_t *>(&owner), sizeof(owner));
        file.close();
        return written == sizeof(owner);
    }
    static bool writeLegacyPreparedJournal(const char *savPath,
                                           uint8_t species,
                                           uint32_t baseExp,
                                           uint32_t xp) {
        PokemonDaycare::LegacyFlushJournal journal = {};
        journal.magic = PokemonDaycare::LegacyFlushJournal::MAGIC;
        journal.phase = static_cast<uint8_t>(
            PokemonDaycare::FlushJournalPhase::PREPARED);
        journal.partyCount = 1;
        journal.species[0] = species;
        journal.baseExp[0] = baseExp;
        journal.xp[0] = xp;
        journal.writtenExp[0] = baseExp + xp;
        journal.writtenLevel[0] = levelForExp(species, baseExp + xp);
        strncpy(journal.savPath, savPath, sizeof(journal.savPath) - 1);
        auto file = FSCom.open("/monstermesh/daycare.flush", FILE_O_WRITE);
        if (!file) return false;
        const size_t written = file.write(
            reinterpret_cast<const uint8_t *>(&journal), sizeof(journal));
        file.close();
        return written == sizeof(journal);
    }
};

namespace
{
constexpr uint8_t TEST_SPECIES = 25; // Pikachu, medium-fast growth
constexpr uint8_t TEST_IDENTITY[4] = {0x12, 0x34, 0x56, 0x78};
constexpr uint8_t OTHER_IDENTITY[4] = {0x9A, 0xBC, 0xDE, 0xF0};
constexpr size_t SAV_SIZE = 32 * 1024;
constexpr const char *DAYCARE_STATE_PATH = "/monstermesh/daycare.dat";
constexpr const char *DAYCARE_STATE_TEMP_PATH = "/monstermesh/daycare.dat.tmp";
constexpr const char *DAYCARE_FLUSH_PATH = "/monstermesh/daycare.flush";
constexpr const char *DAYCARE_OWNER_PATH = "/monstermesh/daycare.owner";
constexpr const char *TEST_SAV_PATH = "/test.sav";

using SavImage = std::array<uint8_t, SAV_SIZE>;

SavImage makeSavParty(const uint8_t *species, const uint32_t *exp,
                      const uint8_t identity[][4], uint8_t count)
{
    SavImage sav = {};
    sav[SAV_PARTY_COUNT] = count;
    for (uint8_t i = 0; i < count; ++i) {
        sav[SAV_SPECIES_LIST + i] = dexToInternal[species[i]];
        uint8_t *pkmn = &sav[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE];
        uint8_t level = levelForExp(species[i], exp[i]);
        pkmn[PKM_SPECIES] = dexToInternal[species[i]];
        pkmn[PKM_LEVEL_PC] = level;
        pkmn[PKM_LEVEL_PARTY] = level;
        pkmn[PKM_OT_ID] = identity[i][0];
        pkmn[PKM_OT_ID + 1] = identity[i][1];
        pkmn[PKM_DVS] = identity[i][2];
        pkmn[PKM_DVS + 1] = identity[i][3];
        writeBE24(&pkmn[PKM_EXP], exp[i]);
        writeBE16(&pkmn[PKM_CURRENT_HP], 20);
        writeBE16(&pkmn[PKM_MAX_HP], 20);
        sav[SAV_NICKNAMES + i * SAV_NAME_SIZE] = SAV_STRING_TERMINATOR;
        sav[SAV_OT_NAMES + i * SAV_NAME_SIZE] = SAV_STRING_TERMINATOR;
    }
    sav[SAV_SPECIES_LIST + count] = 0xFF;
    DaycareSavPatcher::fixChecksum(sav.data());
    return sav;
}

SavImage makeSav(uint8_t species, uint32_t exp,
                 const uint8_t identity[4] = TEST_IDENTITY)
{
    const uint8_t speciesList[1] = {species};
    const uint32_t expList[1] = {exp};
    uint8_t identities[1][4] = {};
    memcpy(identities[0], identity, sizeof(identities[0]));
    return makeSavParty(speciesList, expList, identities, 1);
}

void seedDaycare(PokemonDaycare &daycare, uint32_t savExp, uint32_t pendingXp = 0,
                 uint16_t lifetimeLevels = 0)
{
    daycare.init();
    DaycarePartyInfo observed[1] = {};
    observed[0].dexNum = TEST_SPECIES;
    memcpy(observed[0].identity, TEST_IDENTITY,
           sizeof(observed[0].identity));
    TEST_ASSERT_TRUE(PokemonDaycareTestAccess::bind(
        daycare, TEST_SAV_PATH, observed, 1));
    auto &state = PokemonDaycareTestAccess::state(daycare);
    state.partyCount = 1;
    state.pokemon[0].speciesDex = TEST_SPECIES;
    state.pokemon[0].savExp = savExp;
    state.pokemon[0].savLevel = levelForExp(TEST_SPECIES, savExp);
    state.pokemon[0].totalXpGained = pendingXp;
    state.pokemon[0].totalLevelsGained = lifetimeLevels;
    PokemonDaycareTestAccess::setRuntimeStateLoaded(daycare);
    PokemonDaycareTestAccess::setActive(daycare, true);
}

uint32_t savExp(const SavImage &sav)
{
    return readBE24(&sav[SAV_POKEMON_DATA + PKM_EXP]);
}

#if defined(ARCH_PORTDUINO)
bool removeStateTempDirectory()
{
    if (!FSCom.exists(DAYCARE_STATE_TEMP_PATH)) return true;

    auto entry = FSCom.open(DAYCARE_STATE_TEMP_PATH, FILE_O_READ);
    const bool isDirectory = entry && entry.isDirectory();
    entry.close();
    if (!isDirectory) return true;
    if (!portduinoVFS || !portduinoVFS->mountpoint()) return false;

    const std::string hostPath =
        std::string(portduinoVFS->mountpoint()) + DAYCARE_STATE_TEMP_PATH;
    return ::rmdir(hostPath.c_str()) == 0;
}

bool installStatePersistenceBlocker()
{
    if (!removeStateTempDirectory()) return false;
    if (FSCom.exists(DAYCARE_STATE_TEMP_PATH) &&
        !FSCom.remove(DAYCARE_STATE_TEMP_PATH)) {
        return false;
    }
    return FSCom.mkdir(DAYCARE_STATE_TEMP_PATH);
}
#endif

void test_background_apply_does_not_deactivate_or_clear()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 75);
    SavImage sav = makeSav(TEST_SPECIES, 1000);

    TEST_ASSERT_TRUE(daycare.applyPendingXp(sav.data()));
    TEST_ASSERT_TRUE(daycare.isActive());
    TEST_ASSERT_TRUE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(75, daycare.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT32(1075, savExp(sav));
}

void test_background_successful_commit_stays_active()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 75);
    SavImage sav = makeSav(TEST_SPECIES, 1000);

    TEST_ASSERT_TRUE(daycare.applyPendingXp(sav.data()));
    // The caller's verified persistence step occurs between prepare/commit;
    // committing that exact image must not perform terminal checkout.
    TEST_ASSERT_TRUE(daycare.commitXpFlush(sav.data()));
    TEST_ASSERT_TRUE(daycare.isActive());
    TEST_ASSERT_FALSE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(1075, daycare.getState().pokemon[0].savExp);
}

void test_failed_commit_retains_snapshot_and_pending_xp()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 75);
    SavImage original = makeSav(TEST_SPECIES, 1000);
    SavImage prepared = original;
    TEST_ASSERT_TRUE(daycare.applyPendingXp(prepared.data()));

    // Supplying an image other than the verified prepared result must be an
    // all-or-nothing rejection from the daycare state's perspective.
    TEST_ASSERT_FALSE(daycare.commitXpFlush(original.data()));
    TEST_ASSERT_EQUAL_UINT32(75, daycare.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT32(1000, daycare.getState().pokemon[0].savExp);

    // A persistence retry starts from a fresh original image and reuses the
    // same snapshot; it does not add the XP twice.
    SavImage retry = original;
    TEST_ASSERT_TRUE(daycare.applyPendingXp(retry.data()));
    TEST_ASSERT_EQUAL_UINT32(1075, savExp(retry));
    TEST_ASSERT_TRUE(daycare.commitXpFlush(retry.data()));
    TEST_ASSERT_EQUAL_UINT32(0, daycare.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT32(1075, daycare.getState().pokemon[0].savExp);
}

void test_commit_clears_only_the_staged_xp()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 50);
    SavImage original = makeSav(TEST_SPECIES, 1000);
    SavImage firstAttempt = original;
    TEST_ASSERT_TRUE(daycare.applyPendingXp(firstAttempt.data()));

    // XP earned while a failed background write is waiting for retry belongs
    // to the next transaction, not the already frozen one.
    PokemonDaycareTestAccess::addXp(daycare, 0, 25);
    TEST_ASSERT_EQUAL_UINT32(75, daycare.getState().pokemon[0].totalXpGained);

    SavImage wrongParty = makeSav(1, 1000);
    TEST_ASSERT_FALSE(daycare.applyPendingXp(wrongParty.data()));
    TEST_ASSERT_EQUAL_UINT32(75, daycare.getState().pokemon[0].totalXpGained);

    SavImage retry = original;
    TEST_ASSERT_TRUE(daycare.applyPendingXp(retry.data()));
    TEST_ASSERT_EQUAL_UINT32(1050, savExp(retry));
    TEST_ASSERT_TRUE(daycare.commitXpFlush(retry.data()));
    TEST_ASSERT_EQUAL_UINT32(1050, daycare.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT32(25, daycare.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT8(levelForExp(TEST_SPECIES, 1075), daycare.effectiveLevel(0));
}

void test_successful_commit_cannot_be_applied_again_after_recheckin()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 80);
    SavImage sav = makeSav(TEST_SPECIES, 1000);

    TEST_ASSERT_TRUE(daycare.applyPendingXp(sav.data()));
    TEST_ASSERT_TRUE(daycare.commitXpFlush(sav.data()));
    TEST_ASSERT_FALSE(daycare.hasPendingXp());
    TEST_ASSERT_FALSE(daycare.commitXpFlush(sav.data()));

    daycare.checkOut(nullptr);
    TEST_ASSERT_FALSE(daycare.isActive());
    daycare.checkIn(sav.data(), "NODE", "ASH", TEST_SAV_PATH);
    TEST_ASSERT_TRUE(daycare.isActive());
    TEST_ASSERT_FALSE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(1080, daycare.getState().pokemon[0].savExp);
    TEST_ASSERT_FALSE(daycare.applyPendingXp(sav.data()));
}

void test_effective_level_and_lifetime_levels_survive_two_flushes()
{
    PokemonDaycare daycare;
    // Medium-fast level 3 starts at 27 EXP; begin one point below it.
    seedDaycare(daycare, 26);
    SavImage first = makeSav(TEST_SPECIES, 26);

    PokemonDaycareTestAccess::addXp(daycare, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(3, daycare.effectiveLevel(0));
    TEST_ASSERT_EQUAL_UINT16(1, daycare.getState().pokemon[0].totalLevelsGained);
    TEST_ASSERT_TRUE(daycare.applyPendingXp(first.data()));
    TEST_ASSERT_TRUE(daycare.commitXpFlush(first.data()));
    TEST_ASSERT_EQUAL_UINT8(3, daycare.getState().pokemon[0].savLevel);
    TEST_ASSERT_EQUAL_UINT16(1, daycare.getState().pokemon[0].totalLevelsGained);

    daycare.checkOut(nullptr);
    daycare.checkIn(first.data(), "NODE", "ASH", TEST_SAV_PATH);
    TEST_ASSERT_TRUE(daycare.isActive());
    TEST_ASSERT_EQUAL_UINT8(3, daycare.effectiveLevel(0));
    TEST_ASSERT_EQUAL_UINT16(1, daycare.getState().pokemon[0].totalLevelsGained);

    // Medium-fast level 4 starts at 64 EXP. The lifetime counter must add the
    // new crossing rather than being reinterpreted relative to the new SAV.
    PokemonDaycareTestAccess::addXp(daycare, 0, 37);
    TEST_ASSERT_EQUAL_UINT8(4, daycare.effectiveLevel(0));
    TEST_ASSERT_EQUAL_UINT16(2, daycare.getState().pokemon[0].totalLevelsGained);
    SavImage second = first;
    TEST_ASSERT_TRUE(daycare.applyPendingXp(second.data()));
    TEST_ASSERT_TRUE(daycare.commitXpFlush(second.data()));
    TEST_ASSERT_EQUAL_UINT8(4, daycare.getState().pokemon[0].savLevel);
    TEST_ASSERT_EQUAL_UINT16(2, daycare.getState().pokemon[0].totalLevelsGained);
    TEST_ASSERT_FALSE(daycare.hasPendingXp());
}

void test_terminal_checkout_deactivates_without_committing()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 20);
    SavImage sav = makeSav(TEST_SPECIES, 1000);

    daycare.checkOut(sav.data());
    TEST_ASSERT_FALSE(daycare.isActive());
    TEST_ASSERT_TRUE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(1020, savExp(sav));
    TEST_ASSERT_TRUE(daycare.commitXpFlush(sav.data()));
    TEST_ASSERT_FALSE(daycare.hasPendingXp());
    TEST_ASSERT_FALSE(daycare.isActive());
}

void test_party_checkin_uses_exact_exp_not_level_floor()
{
    PokemonDaycare daycare;
    daycare.init();
    const uint8_t species[1] = {TEST_SPECIES};
    const uint8_t levels[1] = {3};
    const uint32_t exactExp[1] = {63}; // one point below level 4
    const uint8_t identity[1][4] = {
        {TEST_IDENTITY[0], TEST_IDENTITY[1],
         TEST_IDENTITY[2], TEST_IDENTITY[3]}};
    const char nicknames[1][11] = {{'S', 'P', 'A', 'R', 'K', 'Y', '\0'}};

    daycare.checkIn(species, levels, nicknames, 1, "NODE", "ASH", exactExp,
                    identity, TEST_SAV_PATH);
    TEST_ASSERT_EQUAL_UINT32(63, daycare.getState().pokemon[0].savExp);
    PokemonDaycareTestAccess::addXp(daycare, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(4, daycare.effectiveLevel(0));
    TEST_ASSERT_EQUAL_UINT16(1, daycare.getState().pokemon[0].totalLevelsGained);
}

void test_legacy_state_never_reinterprets_old_xp_as_pending()
{
    DaycareState legacy = {};
    legacy.magic = DaycareState::LEGACY_MAGIC;
    legacy.partyCount = 1;
    legacy.pokemon[0].speciesDex = TEST_SPECIES;
    legacy.pokemon[0].totalXpGained = 1234;
    legacy.pokemon[0].totalLevelsGained = 7;
    FSCom.mkdir("/monstermesh");
    auto file = FSCom.open(DAYCARE_STATE_PATH, FILE_O_WRITE);
    TEST_ASSERT_TRUE((bool)file);
    TEST_ASSERT_EQUAL_UINT32(sizeof(legacy), file.write(
        reinterpret_cast<const uint8_t *>(&legacy), sizeof(legacy)));
    file.close();

    PokemonDaycare daycare;
    daycare.init();
    TEST_ASSERT_TRUE(daycare.loadState());
    TEST_ASSERT_EQUAL_HEX32(DaycareState::MAGIC, daycare.getState().magic);
    TEST_ASSERT_FALSE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT16(7, daycare.getState().pokemon[0].totalLevelsGained);
}

void test_legacy_dco1_without_pending_work_upgrades_to_identity_binding()
{
    TEST_ASSERT_TRUE(PokemonDaycareTestAccess::writeLegacyOwner(TEST_SAV_PATH));
    SavImage sav = makeSav(TEST_SPECIES, 1000);
    PokemonDaycare daycare;
    daycare.init();
    TEST_ASSERT_TRUE(daycare.checkIn(sav.data(), "NODE", "ASH",
                                     TEST_SAV_PATH));

    auto owner = FSCom.open(DAYCARE_OWNER_PATH, FILE_O_READ);
    TEST_ASSERT_TRUE((bool)owner);
    uint32_t magic = 0;
    TEST_ASSERT_EQUAL_UINT32(sizeof(magic), owner.read(
        reinterpret_cast<uint8_t *>(&magic), sizeof(magic)));
    owner.close();
    TEST_ASSERT_EQUAL_HEX32(0x44434F32, magic); // DCO2
}

void test_legacy_dco1_with_unjournaled_pending_xp_fails_closed()
{
    PokemonDaycare beforeReset;
    seedDaycare(beforeReset, 1000, 50);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    TEST_ASSERT_TRUE(beforeReset.saveState());
    TEST_ASSERT_TRUE(PokemonDaycareTestAccess::writeLegacyOwner(TEST_SAV_PATH));

    SavImage sav = makeSav(TEST_SPECIES, 1000);
    PokemonDaycare afterReset;
    afterReset.init();
    TEST_ASSERT_FALSE(afterReset.checkIn(sav.data(), "NODE", "ASH",
                                         TEST_SAV_PATH));
    TEST_ASSERT_TRUE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(50,
        afterReset.getState().pokemon[0].totalXpGained);
}

void test_legacy_dcf2_pending_journal_fails_closed()
{
    PokemonDaycare beforeReset;
    seedDaycare(beforeReset, 1000, 50);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    TEST_ASSERT_TRUE(beforeReset.saveState());
    TEST_ASSERT_TRUE(PokemonDaycareTestAccess::writeLegacyOwner(TEST_SAV_PATH));
    TEST_ASSERT_TRUE(PokemonDaycareTestAccess::writeLegacyPreparedJournal(
        TEST_SAV_PATH, TEST_SPECIES, 1000, 50));

    SavImage sav = makeSav(TEST_SPECIES, 1000);
    PokemonDaycare afterReset;
    afterReset.init();
    TEST_ASSERT_FALSE(afterReset.checkIn(sav.data(), "NODE", "ASH",
                                         TEST_SAV_PATH));
    TEST_ASSERT_TRUE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(50,
        afterReset.getState().pokemon[0].totalXpGained);
}

void test_prepared_journal_reconciles_reset_after_sd_promotion()
{
    PokemonDaycare beforeReset;
    seedDaycare(beforeReset, 1000, 50, 3);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    SavImage promotedSav = makeSav(TEST_SPECIES, 1000);
    TEST_ASSERT_TRUE(beforeReset.applyPendingXp(promotedSav.data()));
    TEST_ASSERT_EQUAL_UINT32(1050, savExp(promotedSav));

    // Simulate reset after the atomic SAV promote but before commitXpFlush.
    PokemonDaycare afterReset;
    afterReset.init();
    afterReset.checkIn(promotedSav.data(), "NODE", "ASH", TEST_SAV_PATH);
    TEST_ASSERT_FALSE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(1050, afterReset.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT16(3, afterReset.getState().pokemon[0].totalLevelsGained);
}

void test_promoted_journal_subtracts_only_staged_xp_after_reset()
{
    PokemonDaycare beforeReset;
    seedDaycare(beforeReset, 1000, 50);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    SavImage promotedSav = makeSav(TEST_SPECIES, 1000);
    TEST_ASSERT_TRUE(beforeReset.applyPendingXp(promotedSav.data()));
    PokemonDaycareTestAccess::addXp(beforeReset, 0, 25);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    TEST_ASSERT_TRUE(PokemonDaycareTestAccess::markPromoted(beforeReset));

    PokemonDaycare afterReset;
    afterReset.init();
    afterReset.checkIn(promotedSav.data(), "NODE", "ASH", TEST_SAV_PATH);
    TEST_ASSERT_TRUE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(25, afterReset.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT32(1050, afterReset.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT8(levelForExp(TEST_SPECIES, 1075),
                            afterReset.effectiveLevel(0));
}

#if defined(ARCH_PORTDUINO)
void test_prepared_reconcile_persistence_failure_is_retryable()
{
    PokemonDaycare beforeReset;
    seedDaycare(beforeReset, 1000, 50, 3);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    SavImage promotedSav = makeSav(TEST_SPECIES, 1000);
    TEST_ASSERT_TRUE(beforeReset.applyPendingXp(promotedSav.data()));
    TEST_ASSERT_EQUAL_UINT32(1050, savExp(promotedSav));
    TEST_ASSERT_TRUE(FSCom.exists(DAYCARE_FLUSH_PATH));

    // A directory cannot be removed through PortduinoFS::remove(), so placing
    // one at the atomic writer's .tmp path deterministically fails saveState.
    TEST_ASSERT_TRUE(installStatePersistenceBlocker());

    PokemonDaycare afterReset;
    afterReset.init();
    TEST_ASSERT_FALSE(afterReset.checkIn(
        promotedSav.data(), "NODE", "ASH", TEST_SAV_PATH));
    TEST_ASSERT_FALSE(afterReset.isActive());
    TEST_ASSERT_TRUE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(1000,
        afterReset.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT32(50,
        afterReset.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT16(3,
        afterReset.getState().pokemon[0].totalLevelsGained);
    TEST_ASSERT_TRUE(FSCom.exists(DAYCARE_FLUSH_PATH));

    TEST_ASSERT_TRUE(removeStateTempDirectory());
    TEST_ASSERT_TRUE(afterReset.checkIn(
        promotedSav.data(), "NODE", "ASH", TEST_SAV_PATH));
    TEST_ASSERT_TRUE(afterReset.isActive());
    TEST_ASSERT_FALSE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(1050,
        afterReset.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT16(3,
        afterReset.getState().pokemon[0].totalLevelsGained);
    TEST_ASSERT_FALSE(FSCom.exists(DAYCARE_FLUSH_PATH));
}

void test_promoted_reconcile_persistence_failure_is_retryable()
{
    PokemonDaycare beforeReset;
    seedDaycare(beforeReset, 1000, 50, 4);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    SavImage promotedSav = makeSav(TEST_SPECIES, 1000);
    TEST_ASSERT_TRUE(beforeReset.applyPendingXp(promotedSav.data()));
    PokemonDaycareTestAccess::addXp(beforeReset, 0, 25);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    TEST_ASSERT_TRUE(PokemonDaycareTestAccess::markPromoted(beforeReset));
    TEST_ASSERT_TRUE(FSCom.exists(DAYCARE_FLUSH_PATH));
    TEST_ASSERT_TRUE(installStatePersistenceBlocker());

    PokemonDaycare afterReset;
    afterReset.init();
    TEST_ASSERT_FALSE(afterReset.checkIn(
        promotedSav.data(), "NODE", "ASH", TEST_SAV_PATH));
    TEST_ASSERT_FALSE(afterReset.isActive());
    TEST_ASSERT_EQUAL_UINT32(1000,
        afterReset.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT32(75,
        afterReset.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT16(4,
        afterReset.getState().pokemon[0].totalLevelsGained);
    TEST_ASSERT_TRUE(FSCom.exists(DAYCARE_FLUSH_PATH));

    TEST_ASSERT_TRUE(removeStateTempDirectory());
    TEST_ASSERT_TRUE(afterReset.checkIn(
        promotedSav.data(), "NODE", "ASH", TEST_SAV_PATH));
    TEST_ASSERT_TRUE(afterReset.isActive());
    TEST_ASSERT_TRUE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(1050,
        afterReset.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT32(25,
        afterReset.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT16(4,
        afterReset.getState().pokemon[0].totalLevelsGained);
    TEST_ASSERT_FALSE(FSCom.exists(DAYCARE_FLUSH_PATH));
}
#endif

void test_truncated_state_fails_without_mutating_runtime_state()
{
    PokemonDaycare daycare;
    daycare.init();
    auto &state = PokemonDaycareTestAccess::state(daycare);
    state.partyCount = 1;
    state.pokemon[0].speciesDex = TEST_SPECIES;
    state.pokemon[0].savExp = 777;
    state.pokemon[0].savLevel = levelForExp(TEST_SPECIES, 777);
    state.pokemon[0].totalXpGained = 33;
    state.pokemon[0].totalLevelsGained = 9;
    PokemonDaycareTestAccess::setActive(daycare, true);
    const DaycareState before = daycare.getState();

    FSCom.mkdir("/monstermesh");
    auto truncated = FSCom.open(DAYCARE_STATE_PATH, FILE_O_WRITE);
    const uint8_t byte = 0xAA;
    TEST_ASSERT_TRUE((bool)truncated);
    TEST_ASSERT_EQUAL_UINT32(1, truncated.write(&byte, 1));
    truncated.close();

    TEST_ASSERT_FALSE(daycare.loadState());
    TEST_ASSERT_TRUE(daycare.isActive());
    TEST_ASSERT_TRUE(memcmp(&before, &daycare.getState(), sizeof(before)) == 0);
}

void test_corrupt_state_primary_restores_verified_backup()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 0, 4);
    TEST_ASSERT_TRUE(daycare.saveState());
    PokemonDaycareTestAccess::state(daycare).pokemon[0].totalLevelsGained = 9;
    TEST_ASSERT_TRUE(daycare.saveState()); // retained .bak contains value 4

    auto corrupt = FSCom.open(DAYCARE_STATE_PATH, FILE_O_WRITE);
    const uint8_t byte = 0xAA;
    TEST_ASSERT_TRUE((bool)corrupt);
    TEST_ASSERT_EQUAL_UINT32(1, corrupt.write(&byte, 1));
    corrupt.close();

    PokemonDaycare restored;
    restored.init();
    TEST_ASSERT_TRUE(restored.loadState());
    TEST_ASSERT_EQUAL_UINT16(4,
        restored.getState().pokemon[0].totalLevelsGained);
}

void test_prepared_base_journal_restores_pending_from_stale_state_backup()
{
    PokemonDaycare beforeReset;
    seedDaycare(beforeReset, 1000, 0);
    TEST_ASSERT_TRUE(beforeReset.saveState());
    TEST_ASSERT_TRUE(beforeReset.saveState()); // primary + backup both have 0 pending

    PokemonDaycareTestAccess::addXp(beforeReset, 0, 50);
    SavImage baseSav = makeSav(TEST_SPECIES, 1000);
    SavImage preparedSav = baseSav;
    TEST_ASSERT_TRUE(beforeReset.applyPendingXp(preparedSav.data()));

    // applyPendingXp persisted the new primary and PREPARED journal, but its
    // retained state backup still predates the XP. Corrupt the primary to
    // force exactly that fallback, then observe the unpromoted base SAV.
    auto corrupt = FSCom.open(DAYCARE_STATE_PATH, FILE_O_WRITE);
    const uint8_t bad = 0xCC;
    TEST_ASSERT_TRUE((bool)corrupt);
    TEST_ASSERT_EQUAL_UINT32(1, corrupt.write(&bad, 1));
    corrupt.close();

    PokemonDaycare afterReset;
    afterReset.init();
    afterReset.checkIn(baseSav.data(), "NODE", "ASH", TEST_SAV_PATH);
    TEST_ASSERT_TRUE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(50,
        afterReset.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT32(1000, afterReset.getState().pokemon[0].savExp);
}

void test_committed_state_backup_cannot_resurrect_flushed_xp()
{
    PokemonDaycare beforeCorruption;
    seedDaycare(beforeCorruption, 1000, 50);
    SavImage promotedSav = makeSav(TEST_SPECIES, 1000);
    TEST_ASSERT_TRUE(beforeCorruption.applyPendingXp(promotedSav.data()));
    TEST_ASSERT_TRUE(beforeCorruption.commitXpFlush(promotedSav.data()));
    TEST_ASSERT_FALSE(beforeCorruption.hasPendingXp());

    auto corrupt = FSCom.open(DAYCARE_STATE_PATH, FILE_O_WRITE);
    const uint8_t bad = 0xDD;
    TEST_ASSERT_TRUE((bool)corrupt);
    TEST_ASSERT_EQUAL_UINT32(1, corrupt.write(&bad, 1));
    corrupt.close();

    PokemonDaycare recovered;
    recovered.init();
    recovered.checkIn(promotedSav.data(), "NODE", "ASH", TEST_SAV_PATH);
    TEST_ASSERT_FALSE(recovered.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(1050,
        recovered.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT32(0,
        recovered.getState().pokemon[0].totalXpGained);
}

void test_battle_xp_delta_merges_with_fresh_daycare_image()
{
    SavImage sav = makeSav(TEST_SPECIES, 1000);
    const uint8_t dex[1] = {TEST_SPECIES};
    const uint32_t daycareXp[1] = {75};
    TEST_ASSERT_TRUE(DaycareSavPatcher::checkout(sav.data(), dex, daycareXp, 1));
    TEST_ASSERT_EQUAL_UINT32(1075, savExp(sav));

    const uint8_t expectedSpecies[7] = {
        dexToInternal[TEST_SPECIES], 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t expectedIdentity[6][4] = {};
    memcpy(expectedIdentity[0], TEST_IDENTITY,
           sizeof(expectedIdentity[0]));
    const uint32_t battleXp[6] = {25, 0, 0, 0, 0, 0};
    const size_t unrelatedOffset = 0x1200;
    sav[unrelatedOffset] = 0xA5;
    TEST_ASSERT_TRUE(monstermesh::applyBattleXpToSav(
        sav.data(), sav.size(), 1, expectedSpecies, expectedIdentity,
        battleXp));
    TEST_ASSERT_EQUAL_UINT32(1100, savExp(sav));
    TEST_ASSERT_EQUAL_UINT8(0xA5, sav[unrelatedOffset]);

    const uint8_t wrongSpecies[7] = {
        dexToInternal[1], 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(monstermesh::applyBattleXpToSav(
        sav.data(), sav.size(), 1, wrongSpecies, expectedIdentity,
        battleXp));
    TEST_ASSERT_EQUAL_UINT32(1100, savExp(sav));
}

void test_external_sav_baseline_preserves_pending_and_lifetime_levels()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 20, 7);
    DaycarePartyInfo party[1] = {};
    party[0].dexNum = TEST_SPECIES;
    party[0].totalExp = 1100;
    party[0].level = levelForExp(TEST_SPECIES, party[0].totalExp);
    memcpy(party[0].identity, TEST_IDENTITY, sizeof(party[0].identity));

    TEST_ASSERT_TRUE(daycare.refreshSavBaseline(party, 1));
    TEST_ASSERT_TRUE(daycare.isActive());
    TEST_ASSERT_EQUAL_UINT32(1100, daycare.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT32(20,
        daycare.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_EQUAL_UINT16(7,
        daycare.getState().pokemon[0].totalLevelsGained);
    TEST_ASSERT_EQUAL_UINT8(levelForExp(TEST_SPECIES, 1120),
                            daycare.effectiveLevel(0));
}

void test_pending_xp_cannot_migrate_to_another_sav()
{
    PokemonDaycare daycare;
    seedDaycare(daycare, 1000, 50);
    SavImage samePartyOtherSave = makeSav(TEST_SPECIES, 1000);

    TEST_ASSERT_FALSE(daycare.checkIn(samePartyOtherSave.data(), "NODE", "ASH",
                                      "/other.sav"));
    TEST_ASSERT_FALSE(daycare.isActive());
    TEST_ASSERT_TRUE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(50,
        daycare.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_FALSE(daycare.applyPendingXp("/other.sav",
                                             samePartyOtherSave.data()));

    SavImage ownerSav = makeSav(TEST_SPECIES, 1000);
    TEST_ASSERT_TRUE(daycare.checkIn(ownerSav.data(), "NODE", "ASH",
                                     TEST_SAV_PATH));
    TEST_ASSERT_TRUE(daycare.isActive());
    TEST_ASSERT_TRUE(daycare.applyPendingXp(TEST_SAV_PATH, ownerSav.data()));
    TEST_ASSERT_EQUAL_UINT32(1050, savExp(ownerSav));
}

void test_pending_xp_recheckin_rejects_species_change()
{
    PokemonDaycare daycare;
    SavImage original = makeSav(TEST_SPECIES, 1000);
    daycare.init();
    TEST_ASSERT_TRUE(daycare.checkIn(original.data(), "NODE", "ASH",
                                     TEST_SAV_PATH));
    PokemonDaycareTestAccess::addXp(daycare, 0, 50);

    SavImage changed = makeSav(1, 1000, TEST_IDENTITY);
    TEST_ASSERT_FALSE(daycare.checkIn(changed.data(), "NODE", "ASH",
                                      TEST_SAV_PATH));
    TEST_ASSERT_TRUE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT8(1, daycare.getState().partyCount);
    TEST_ASSERT_EQUAL_UINT8(TEST_SPECIES,
                            daycare.getState().pokemon[0].speciesDex);
    TEST_ASSERT_EQUAL_UINT32(50,
        daycare.getState().pokemon[0].totalXpGained);
    TEST_ASSERT_FALSE(daycare.applyPendingXp(TEST_SAV_PATH, changed.data()));
    TEST_ASSERT_EQUAL_UINT32(1000, savExp(changed));
}

void test_pending_xp_recheckin_rejects_party_reorder()
{
    const uint8_t species[2] = {TEST_SPECIES, 1};
    const uint32_t exp[2] = {1000, 125};
    const uint8_t identity[2][4] = {
        {TEST_IDENTITY[0], TEST_IDENTITY[1], TEST_IDENTITY[2], TEST_IDENTITY[3]},
        {OTHER_IDENTITY[0], OTHER_IDENTITY[1], OTHER_IDENTITY[2], OTHER_IDENTITY[3]}};
    SavImage original = makeSavParty(species, exp, identity, 2);

    PokemonDaycare daycare;
    daycare.init();
    TEST_ASSERT_TRUE(daycare.checkIn(original.data(), "NODE", "ASH",
                                     TEST_SAV_PATH));
    PokemonDaycareTestAccess::addXp(daycare, 0, 50);

    const uint8_t reorderedSpecies[2] = {1, TEST_SPECIES};
    const uint32_t reorderedExp[2] = {125, 1000};
    const uint8_t reorderedIdentity[2][4] = {
        {OTHER_IDENTITY[0], OTHER_IDENTITY[1], OTHER_IDENTITY[2], OTHER_IDENTITY[3]},
        {TEST_IDENTITY[0], TEST_IDENTITY[1], TEST_IDENTITY[2], TEST_IDENTITY[3]}};
    SavImage reordered = makeSavParty(reorderedSpecies, reorderedExp,
                                      reorderedIdentity, 2);
    TEST_ASSERT_FALSE(daycare.checkIn(reordered.data(), "NODE", "ASH",
                                      TEST_SAV_PATH));
    TEST_ASSERT_TRUE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT8(2, daycare.getState().partyCount);
    TEST_ASSERT_EQUAL_UINT8(TEST_SPECIES,
                            daycare.getState().pokemon[0].speciesDex);
    TEST_ASSERT_EQUAL_UINT32(50,
        daycare.getState().pokemon[0].totalXpGained);
}

void test_pending_xp_recheckin_rejects_count_shrink()
{
    const uint8_t species[2] = {TEST_SPECIES, 1};
    const uint32_t exp[2] = {1000, 125};
    const uint8_t identity[2][4] = {
        {TEST_IDENTITY[0], TEST_IDENTITY[1], TEST_IDENTITY[2], TEST_IDENTITY[3]},
        {OTHER_IDENTITY[0], OTHER_IDENTITY[1], OTHER_IDENTITY[2], OTHER_IDENTITY[3]}};
    SavImage original = makeSavParty(species, exp, identity, 2);

    PokemonDaycare daycare;
    daycare.init();
    TEST_ASSERT_TRUE(daycare.checkIn(original.data(), "NODE", "ASH",
                                     TEST_SAV_PATH));
    PokemonDaycareTestAccess::addXp(daycare, 1, 50);

    SavImage shrunk = makeSav(TEST_SPECIES, 1000);
    TEST_ASSERT_FALSE(daycare.checkIn(shrunk.data(), "NODE", "ASH",
                                      TEST_SAV_PATH));
    TEST_ASSERT_TRUE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT8(2, daycare.getState().partyCount);
    TEST_ASSERT_EQUAL_UINT8(1, daycare.getState().pokemon[1].speciesDex);
    TEST_ASSERT_EQUAL_UINT32(50,
        daycare.getState().pokemon[1].totalXpGained);
}

void test_pending_xp_rejects_same_species_replacement_identity()
{
    PokemonDaycare daycare;
    SavImage original = makeSav(TEST_SPECIES, 1000, TEST_IDENTITY);
    daycare.init();
    TEST_ASSERT_TRUE(daycare.checkIn(original.data(), "NODE", "ASH",
                                     TEST_SAV_PATH));
    PokemonDaycareTestAccess::addXp(daycare, 0, 50);

    DaycarePartyInfo replacementParty[6] = {};
    SavImage replacement = makeSav(TEST_SPECIES, 1000, OTHER_IDENTITY);
    TEST_ASSERT_EQUAL_UINT8(1, DaycareSavPatcher::readParty(
        replacement.data(), replacementParty));
    TEST_ASSERT_FALSE(daycare.refreshSavBaseline(TEST_SAV_PATH,
                                                 replacementParty, 1));
    TEST_ASSERT_EQUAL_UINT32(1000,
        daycare.getState().pokemon[0].savExp);
    TEST_ASSERT_FALSE(daycare.applyPendingXp(TEST_SAV_PATH,
                                             replacement.data()));
    TEST_ASSERT_EQUAL_UINT32(1000, savExp(replacement));
    TEST_ASSERT_FALSE(daycare.checkIn(replacement.data(), "NODE", "ASH",
                                      TEST_SAV_PATH));
    TEST_ASSERT_TRUE(daycare.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(50,
        daycare.getState().pokemon[0].totalXpGained);

    SavImage prepared = original;
    TEST_ASSERT_TRUE(daycare.applyPendingXp(TEST_SAV_PATH, prepared.data()));
    SavImage wrongWritten = makeSav(TEST_SPECIES, 1050, OTHER_IDENTITY);
    TEST_ASSERT_FALSE(daycare.commitXpFlush(TEST_SAV_PATH,
                                            wrongWritten.data()));
    TEST_ASSERT_EQUAL_UINT32(50,
        daycare.getState().pokemon[0].totalXpGained);
}

void test_prepared_state_backup_preserves_level_achievement()
{
    PokemonDaycare beforeReset;
    seedDaycare(beforeReset, 26);
    PokemonDaycareTestAccess::addXp(beforeReset, 0, 1);
    TEST_ASSERT_EQUAL_UINT16(1,
        beforeReset.getState().pokemon[0].totalLevelsGained);

    SavImage promotedSav = makeSav(TEST_SPECIES, 26);
    TEST_ASSERT_TRUE(beforeReset.applyPendingXp(TEST_SAV_PATH,
                                                 promotedSav.data()));
    TEST_ASSERT_EQUAL_UINT32(27, savExp(promotedSav));

    auto corrupt = FSCom.open(DAYCARE_STATE_PATH, FILE_O_WRITE);
    const uint8_t bad = 0xEF;
    TEST_ASSERT_TRUE((bool)corrupt);
    TEST_ASSERT_EQUAL_UINT32(1, corrupt.write(&bad, 1));
    corrupt.close();

    PokemonDaycare afterReset;
    afterReset.init();
    TEST_ASSERT_TRUE(afterReset.checkIn(promotedSav.data(), "NODE", "ASH",
                                        TEST_SAV_PATH));
    TEST_ASSERT_FALSE(afterReset.hasPendingXp());
    TEST_ASSERT_EQUAL_UINT32(27,
        afterReset.getState().pokemon[0].savExp);
    TEST_ASSERT_EQUAL_UINT16(1,
        afterReset.getState().pokemon[0].totalLevelsGained);
}

void test_battle_sav_queue_keeps_each_batch_with_its_owner()
{
    Gen1Party party = {};
    party.count = 1;
    party.species[0] = dexToInternal[TEST_SPECIES];
    party.species[1] = 0xFF;
    party.mons[0].species = party.species[0];
    party.mons[0].otId[0] = 0x12;
    party.mons[0].otId[1] = 0x34;
    party.mons[0].dvs[0] = 0x56;
    party.mons[0].dvs[1] = 0x78;

    monstermesh::BattleSavOwner ownerA = {};
    monstermesh::BattleSavOwner ownerB = {};
    monstermesh::BattleSavOwner ownerC = {};
    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        "/a.sav", party, ownerA));
    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        "/b.sav", party, ownerB));
    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        "/c.sav", party, ownerC));

    monstermesh::BattleSavQueue queue;
    const uint32_t xpA[6] = {10, 0, 0, 0, 0, 0};
    const uint32_t xpB[6] = {20, 0, 0, 0, 0, 0};
    const uint32_t xpC[6] = {30, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(queue.enqueue(ownerA, xpA));
    TEST_ASSERT_TRUE(queue.enqueue(ownerB, xpB));
    TEST_ASSERT_TRUE(queue.front() != nullptr);
    TEST_ASSERT_TRUE(strcmp("/a.sav", queue.front()->owner.path) == 0);
    TEST_ASSERT_EQUAL_UINT32(10, queue.front()->xp[0]);
    queue.complete(queue.front());
    TEST_ASSERT_TRUE(queue.enqueue(ownerC, xpC));
    TEST_ASSERT_TRUE(queue.front() != nullptr);
    TEST_ASSERT_TRUE(strcmp("/b.sav", queue.front()->owner.path) == 0);
    TEST_ASSERT_EQUAL_UINT32(20, queue.front()->xp[0]);
    queue.complete(queue.front());
    TEST_ASSERT_TRUE(queue.front() != nullptr);
    TEST_ASSERT_TRUE(strcmp("/c.sav", queue.front()->owner.path) == 0);
    TEST_ASSERT_EQUAL_UINT32(30, queue.front()->xp[0]);
}

void test_battle_sav_queue_rejects_malformed_owners()
{
    Gen1Party party = {};
    party.count = 1;
    party.species[0] = dexToInternal[TEST_SPECIES];
    party.species[1] = 0xFF;
    party.mons[0].species = party.species[0];

    monstermesh::BattleSavOwner owner = {};
    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        TEST_SAV_PATH, party, owner));
    const uint32_t xp[6] = {10, 0, 0, 0, 0, 0};

    monstermesh::BattleSavQueue queue;
    owner.partyCount = 0;
    TEST_ASSERT_FALSE(queue.enqueue(owner, xp));
    TEST_ASSERT_TRUE(queue.empty());

    owner.partyCount = 7;
    TEST_ASSERT_FALSE(queue.enqueue(owner, xp));
    TEST_ASSERT_TRUE(queue.empty());

    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        TEST_SAV_PATH, party, owner));
    owner.species[owner.partyCount] = 0;
    TEST_ASSERT_FALSE(queue.enqueue(owner, xp));

    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        TEST_SAV_PATH, party, owner));
    owner.species[0] = 0;
    TEST_ASSERT_FALSE(queue.enqueue(owner, xp));

    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        TEST_SAV_PATH, party, owner));
    owner.monIdentity[1][0] = 1;
    TEST_ASSERT_FALSE(queue.enqueue(owner, xp));

    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        TEST_SAV_PATH, party, owner));
    memset(owner.path, 'x', sizeof(owner.path));
    owner.path[0] = '/';
    TEST_ASSERT_FALSE(queue.enqueue(owner, xp));
    TEST_ASSERT_TRUE(queue.empty());
}

void test_battle_sav_queue_saturates_and_identity_mismatch_is_rejected()
{
    Gen1Party party = {};
    party.count = 1;
    party.species[0] = dexToInternal[TEST_SPECIES];
    party.species[1] = 0xFF;
    party.mons[0].species = party.species[0];
    party.mons[0].otId[0] = 1;
    party.mons[0].dvs[0] = 2;

    monstermesh::BattleSavOwner owner = {};
    TEST_ASSERT_TRUE(monstermesh::makeBattleSavOwner(
        TEST_SAV_PATH, party, owner));
    monstermesh::BattleSavQueue queue;
    const uint32_t almostMax[6] = {UINT32_MAX - 3, 0, 0, 0, 0, 0};
    const uint32_t more[6] = {10, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(queue.enqueue(owner, almostMax));
    TEST_ASSERT_TRUE(queue.enqueue(owner, more));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, queue.front()->xp[0]);

    SavImage sav = makeSav(TEST_SPECIES, 1000);
    const uint8_t expectedSpecies[7] = {
        dexToInternal[TEST_SPECIES], 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t wrongIdentity[6][4] = {};
    wrongIdentity[0][0] = 1;
    const uint32_t xp[6] = {1, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(monstermesh::applyBattleXpToSav(
        sav.data(), sav.size(), 1, expectedSpecies, wrongIdentity, xp));
    TEST_ASSERT_EQUAL_UINT32(1000, savExp(sav));
}
} // namespace

void setUp()
{
#if defined(ARCH_PORTDUINO)
    TEST_ASSERT_TRUE(removeStateTempDirectory());
#endif
    FSCom.remove(DAYCARE_STATE_PATH);
    FSCom.remove(DAYCARE_STATE_TEMP_PATH);
    FSCom.remove("/monstermesh/daycare.dat.bak");
    FSCom.remove(DAYCARE_FLUSH_PATH);
    FSCom.remove("/monstermesh/daycare.flush.tmp");
    FSCom.remove("/monstermesh/daycare.flush.bak");
    FSCom.remove(DAYCARE_OWNER_PATH);
    FSCom.remove("/monstermesh/daycare.owner.tmp");
    FSCom.remove("/monstermesh/daycare.owner.bak");
}

void tearDown()
{
#if defined(ARCH_PORTDUINO)
    TEST_ASSERT_TRUE(removeStateTempDirectory());
#endif
    FSCom.remove(DAYCARE_STATE_PATH);
    FSCom.remove(DAYCARE_STATE_TEMP_PATH);
    FSCom.remove("/monstermesh/daycare.dat.bak");
    FSCom.remove(DAYCARE_FLUSH_PATH);
    FSCom.remove("/monstermesh/daycare.flush.tmp");
    FSCom.remove("/monstermesh/daycare.flush.bak");
    FSCom.remove(DAYCARE_OWNER_PATH);
    FSCom.remove("/monstermesh/daycare.owner.tmp");
    FSCom.remove("/monstermesh/daycare.owner.bak");
}

void setup()
{
    initializeTestEnvironment();
    initSPI();
    fsInit();

    UNITY_BEGIN();
    RUN_TEST(test_background_apply_does_not_deactivate_or_clear);
    RUN_TEST(test_background_successful_commit_stays_active);
    RUN_TEST(test_failed_commit_retains_snapshot_and_pending_xp);
    RUN_TEST(test_commit_clears_only_the_staged_xp);
    RUN_TEST(test_successful_commit_cannot_be_applied_again_after_recheckin);
    RUN_TEST(test_effective_level_and_lifetime_levels_survive_two_flushes);
    RUN_TEST(test_terminal_checkout_deactivates_without_committing);
    RUN_TEST(test_party_checkin_uses_exact_exp_not_level_floor);
    RUN_TEST(test_legacy_state_never_reinterprets_old_xp_as_pending);
    RUN_TEST(test_legacy_dco1_without_pending_work_upgrades_to_identity_binding);
    RUN_TEST(test_legacy_dco1_with_unjournaled_pending_xp_fails_closed);
    RUN_TEST(test_legacy_dcf2_pending_journal_fails_closed);
    RUN_TEST(test_prepared_journal_reconciles_reset_after_sd_promotion);
    RUN_TEST(test_promoted_journal_subtracts_only_staged_xp_after_reset);
#if defined(ARCH_PORTDUINO)
    RUN_TEST(test_prepared_reconcile_persistence_failure_is_retryable);
    RUN_TEST(test_promoted_reconcile_persistence_failure_is_retryable);
#endif
    RUN_TEST(test_truncated_state_fails_without_mutating_runtime_state);
    RUN_TEST(test_corrupt_state_primary_restores_verified_backup);
    RUN_TEST(test_prepared_base_journal_restores_pending_from_stale_state_backup);
    RUN_TEST(test_committed_state_backup_cannot_resurrect_flushed_xp);
    RUN_TEST(test_battle_xp_delta_merges_with_fresh_daycare_image);
    RUN_TEST(test_external_sav_baseline_preserves_pending_and_lifetime_levels);
    RUN_TEST(test_pending_xp_cannot_migrate_to_another_sav);
    RUN_TEST(test_pending_xp_recheckin_rejects_species_change);
    RUN_TEST(test_pending_xp_recheckin_rejects_party_reorder);
    RUN_TEST(test_pending_xp_recheckin_rejects_count_shrink);
    RUN_TEST(test_pending_xp_rejects_same_species_replacement_identity);
    RUN_TEST(test_prepared_state_backup_preserves_level_achievement);
    RUN_TEST(test_battle_sav_queue_keeps_each_batch_with_its_owner);
    RUN_TEST(test_battle_sav_queue_rejects_malformed_owners);
    RUN_TEST(test_battle_sav_queue_saturates_and_identity_mismatch_is_rejected);
    exit(UNITY_END());
}

void loop() {}
