#pragma once

#include "PokemonData.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace monstermesh
{

static constexpr size_t BATTLE_SAV_PATH_CAPACITY = 256;
static constexpr uint8_t BATTLE_SAV_QUEUE_CAPACITY = 8;

struct BattleSavOwner {
    bool valid = false;
    char path[BATTLE_SAV_PATH_CAPACITY] = {};
    uint8_t partyCount = 0;
    uint8_t species[7] = {};
    // OT ID + DVs distinguish same-species party members and bind each slot
    // to the exact save record that earned the XP.
    uint8_t monIdentity[6][4] = {};
};

inline uint32_t saturatingAdd32(uint32_t value, uint32_t add)
{
    return add > UINT32_MAX - value ? UINT32_MAX : value + add;
}

inline bool canonicalSavPath(const char *path,
                             char out[BATTLE_SAV_PATH_CAPACITY])
{
    if (!path || !out) return false;
    if (strncmp(path, "/sd/", 4) == 0) path += 3;
    if (path[0] != '/') return false;

    size_t len = strnlen(path, BATTLE_SAV_PATH_CAPACITY);
    if (len == 0 || len >= BATTLE_SAV_PATH_CAPACITY) return false;

    // Do not persist ambiguous paths whose meaning can change with a working
    // directory or normalization pass.
    const char *segment = path + 1;
    for (const char *p = segment;; ++p) {
        if (*p == '/' || *p == '\0') {
            const size_t segmentLen = static_cast<size_t>(p - segment);
            if (segmentLen == 0 ||
                (segmentLen == 1 && segment[0] == '.') ||
                (segmentLen == 2 && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            if (*p == '\0') break;
            segment = p + 1;
        }
    }

    memcpy(out, path, len + 1);
    return true;
}

inline bool isCanonicalBattleSavPath(const char *path)
{
    if (!path || strncmp(path, "/sd/", 4) == 0 || path[0] != '/')
        return false;

    const size_t len = strnlen(path, BATTLE_SAV_PATH_CAPACITY);
    if (len == 0 || len >= BATTLE_SAV_PATH_CAPACITY) return false;

    const char *segment = path + 1;
    for (const char *p = segment;; ++p) {
        if (*p == '/' || *p == '\0') {
            const size_t segmentLen = static_cast<size_t>(p - segment);
            if (segmentLen == 0 ||
                (segmentLen == 1 && segment[0] == '.') ||
                (segmentLen == 2 && segment[0] == '.' &&
                 segment[1] == '.')) {
                return false;
            }
            if (*p == '\0') break;
            segment = p + 1;
        }
    }
    return true;
}

inline bool makeBattleSavOwner(const char *path, const Gen1Party &party,
                               BattleSavOwner &owner)
{
    BattleSavOwner candidate = {};
    if (!canonicalSavPath(path, candidate.path) ||
        party.count == 0 || party.count > 6) {
        return false;
    }
    candidate.partyCount = party.count;
    memcpy(candidate.species, party.species, sizeof(candidate.species));
    if (candidate.species[party.count] != 0xFF) return false;
    for (uint8_t i = 0; i < party.count; ++i) {
        if (party.species[i] == 0 ||
            party.species[i] != party.mons[i].species) {
            return false;
        }
        candidate.monIdentity[i][0] = party.mons[i].otId[0];
        candidate.monIdentity[i][1] = party.mons[i].otId[1];
        candidate.monIdentity[i][2] = party.mons[i].dvs[0];
        candidate.monIdentity[i][3] = party.mons[i].dvs[1];
    }
    candidate.valid = true;
    owner = candidate;
    return true;
}

inline bool validBattleSavOwner(const BattleSavOwner &owner)
{
    if (!owner.valid || owner.partyCount == 0 || owner.partyCount > 6)
        return false;

    // Validate the fixed-size path before strcmp() sees it.  A caller may
    // construct BattleSavOwner directly, so owner.valid alone is not a safe
    // promise that path is terminated or canonical.
    if (!isCanonicalBattleSavPath(owner.path)) {
        return false;
    }

    // partyCount is range-checked before it is used as an array index.  The
    // active entries and terminator must have the same shape produced by
    // makeBattleSavOwner().
    for (uint8_t i = 0; i < owner.partyCount; ++i) {
        if (owner.species[i] == 0 || owner.species[i] == 0xFF)
            return false;
    }
    if (owner.species[owner.partyCount] != 0xFF) return false;
    for (uint8_t i = owner.partyCount; i < 6; ++i) {
        for (uint8_t byte : owner.monIdentity[i]) {
            if (byte != 0) return false;
        }
    }
    return true;
}

inline bool sameBattleSavOwner(const BattleSavOwner &a,
                               const BattleSavOwner &b)
{
    return validBattleSavOwner(a) && validBattleSavOwner(b) &&
           a.partyCount == b.partyCount &&
           strcmp(a.path, b.path) == 0 &&
           memcmp(a.species, b.species, sizeof(a.species)) == 0 &&
           memcmp(a.monIdentity, b.monIdentity, sizeof(a.monIdentity)) == 0;
}

struct BattleSavBatch {
    BattleSavOwner owner = {};
    uint32_t xp[6] = {};
};

class BattleSavQueue {
  public:
    bool enqueue(const BattleSavOwner &owner, const uint32_t xp[6])
    {
        if (!xp || !validBattleSavOwner(owner)) return false;
        bool any = false;
        for (uint8_t i = 0; i < owner.partyCount; ++i) any |= xp[i] != 0;
        for (uint8_t i = owner.partyCount; i < 6; ++i)
            if (xp[i] != 0) return false;
        if (!any) return true;

        BattleSavBatch *slot = nullptr;
        for (auto &batch : batches_) {
            if (sameBattleSavOwner(batch.owner, owner)) {
                slot = &batch;
                break;
            }
            if (!slot && !batch.owner.valid) slot = &batch;
        }
        if (!slot) return false;
        if (!slot->owner.valid) slot->owner = owner;
        for (uint8_t i = 0; i < owner.partyCount; ++i)
            slot->xp[i] = saturatingAdd32(slot->xp[i], xp[i]);
        return true;
    }

    BattleSavBatch *front()
    {
        for (auto &batch : batches_)
            if (batch.owner.valid) return &batch;
        return nullptr;
    }

    const BattleSavBatch *front() const
    {
        for (const auto &batch : batches_)
            if (batch.owner.valid) return &batch;
        return nullptr;
    }

    void complete(BattleSavBatch *batch)
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(batches_);
        const uintptr_t candidate = reinterpret_cast<uintptr_t>(batch);
        const uintptr_t end = base + sizeof(batches_);
        if (candidate < base || candidate >= end ||
            (candidate - base) % sizeof(BattleSavBatch) != 0 ||
            !batch->owner.valid) {
            return;
        }

        // Keep the occupied prefix in enqueue order.  Clearing a completed
        // front slot and reusing that hole used to let a newer batch jump
        // ahead of an older batch at the next front() call.
        const size_t completed =
            static_cast<size_t>((candidate - base) / sizeof(BattleSavBatch));
        for (size_t i = completed; i + 1 < BATTLE_SAV_QUEUE_CAPACITY; ++i)
            batches_[i] = batches_[i + 1];
        batches_[BATTLE_SAV_QUEUE_CAPACITY - 1] = {};
    }

    bool empty() const { return front() == nullptr; }

  private:
    BattleSavBatch batches_[BATTLE_SAV_QUEUE_CAPACITY] = {};
};

} // namespace monstermesh
