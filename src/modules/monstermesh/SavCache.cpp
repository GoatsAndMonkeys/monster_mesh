// SPDX-License-Identifier: MIT
// See SavCache.h.

#include "SavCache.h"

#include "FSCommon.h"

#include <string.h>

static constexpr const char *SAV_PATH      = "/monstermesh/sav.bin";
static constexpr const char *LAST_ROM_PATH = "/monstermesh/last_rom.txt";

bool savCacheLoad(uint8_t *buf, size_t n, char *lastRomOut, size_t lastRomLen)
{
#ifdef FSCom
    if (!buf || n < SAV_CACHE_SIZE) return false;

    // Read SAV
    auto f = FSCom.open(SAV_PATH, FILE_O_READ);
    if (!f) return false;
    size_t got = f.read(buf, SAV_CACHE_SIZE);
    f.close();
    if (got != SAV_CACHE_SIZE) return false;

    // Read last ROM path (optional — don't fail the whole load if missing)
    if (lastRomOut && lastRomLen > 0) {
        lastRomOut[0] = '\0';
        auto r = FSCom.open(LAST_ROM_PATH, FILE_O_READ);
        if (r) {
            size_t rn = r.read((uint8_t *)lastRomOut, lastRomLen - 1);
            r.close();
            if (rn < lastRomLen) lastRomOut[rn] = '\0';
            // Strip trailing whitespace
            while (rn > 0 && (lastRomOut[rn-1] == '\n' || lastRomOut[rn-1] == '\r' ||
                              lastRomOut[rn-1] == ' '  || lastRomOut[rn-1] == '\0')) {
                lastRomOut[--rn] = '\0';
            }
        }
    }

    return true;
#else
    (void)buf; (void)n; (void)lastRomOut; (void)lastRomLen;
    return false;
#endif
}

bool savCacheStore(const uint8_t *buf, size_t n, const char *lastRom)
{
#ifdef FSCom
    if (!buf || n != SAV_CACHE_SIZE) return false;

    // Ensure containing directory exists (FSCom impls differ on auto-create)
    FSCom.mkdir("/monstermesh");

    // Write SAV (truncate + write + flush)
    if (FSCom.exists(SAV_PATH)) FSCom.remove(SAV_PATH);
    {
        auto f = FSCom.open(SAV_PATH, FILE_O_WRITE);
        if (!f) return false;
        size_t wn = f.write(buf, SAV_CACHE_SIZE);
        f.flush();
        f.close();
        if (wn != SAV_CACHE_SIZE) return false;
    }

    // Write last ROM path if provided
    if (lastRom && lastRom[0]) {
        if (FSCom.exists(LAST_ROM_PATH)) FSCom.remove(LAST_ROM_PATH);
        auto r = FSCom.open(LAST_ROM_PATH, FILE_O_WRITE);
        if (r) {
            r.write((const uint8_t *)lastRom, strlen(lastRom));
            r.flush();
            r.close();
        }
    }

    return true;
#else
    (void)buf; (void)n; (void)lastRom;
    return false;
#endif
}
