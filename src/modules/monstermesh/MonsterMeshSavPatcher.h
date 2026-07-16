#pragma once

#include "DaycareSavPatcher.h"

#include <stddef.h>
#include <stdint.h>

namespace monstermesh
{

// Apply battle-earned XP as a delta to the freshest caller-owned SAV image.
// The expected species list binds each delta to the party that earned it;
// copying an older full Gen1Party here would overwrite daycare or emulator
// changes made since that party snapshot was loaded.
inline bool applyBattleXpToSav(uint8_t *sram, size_t sramSize,
                               uint8_t expectedCount,
                               const uint8_t expectedInternalSpecies[7],
                               const uint8_t expectedMonIdentity[6][4],
                               const uint32_t xp[6])
{
    static constexpr size_t GEN1_SAV_SIZE = 32U * 1024U;
    if (!sram || sramSize != GEN1_SAV_SIZE || !expectedInternalSpecies ||
        !expectedMonIdentity || !xp ||
        expectedCount == 0 || expectedCount > 6) {
        return false;
    }

    if (sram[SAV_PARTY_COUNT] != expectedCount ||
        expectedInternalSpecies[expectedCount] != 0xFF ||
        sram[SAV_SPECIES_LIST + expectedCount] != 0xFF) {
        return false;
    }
    for (uint8_t i = expectedCount; i < 6; ++i)
        if (xp[i] != 0) return false;

    DaycarePartyInfo before[6] = {};
    if (DaycareSavPatcher::readParty(sram, before) != expectedCount)
        return false;

    uint8_t dex[6] = {};
    bool any = false;
    for (uint8_t i = 0; i < expectedCount; ++i) {
        const uint8_t *record =
            sram + SAV_POKEMON_DATA + (size_t)i * SAV_POKEMON_SIZE;
        if (sram[SAV_SPECIES_LIST + i] != expectedInternalSpecies[i] ||
            record[PKM_SPECIES] != expectedInternalSpecies[i] ||
            record[0x0C] != expectedMonIdentity[i][0] ||
            record[0x0D] != expectedMonIdentity[i][1] ||
            record[PKM_DVS] != expectedMonIdentity[i][2] ||
            record[PKM_DVS + 1] != expectedMonIdentity[i][3]) {
            return false;
        }
        dex[i] = internalToDex[expectedInternalSpecies[i]];
        if (dex[i] == 0 || before[i].dexNum != dex[i]) return false;
        any |= xp[i] != 0;
    }
    if (!any) return true;

    if (!DaycareSavPatcher::checkout(sram, dex, xp, expectedCount))
        return false;

    // Terminal battles cannot show the game's interactive move-learning
    // prompt. Match daycare's deterministic policy: fill empty slots, then
    // replace the weakest move for remaining level-up moves.
    bool taughtAny = false;
    for (uint8_t i = 0; i < expectedCount; ++i) {
        if (xp[i] == 0) continue;
        const uint8_t newLevel =
            sram[SAV_POKEMON_DATA + i * SAV_POKEMON_SIZE + PKM_LEVEL_PARTY];
        DaycareSavPatcher::MoveLearnResult learned;
        DaycareSavPatcher::learnMoves(sram, i, dex[i], before[i].level,
                                      newLevel, learned);
        taughtAny |= learned.learnedCount != 0;
        for (uint8_t move = 0; move < learned.pendingCount; ++move) {
            const uint8_t slot = DaycareSavPatcher::weakestMoveSlot(sram, i);
            DaycareSavPatcher::setMove(sram, i, slot, learned.pending[move]);
            taughtAny = true;
        }
    }
    if (taughtAny) DaycareSavPatcher::fixChecksum(sram);
    return true;
}

} // namespace monstermesh
