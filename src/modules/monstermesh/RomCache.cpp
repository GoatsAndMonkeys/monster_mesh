// SPDX-License-Identifier: MIT
// See RomCache.h.

#include "RomCache.h"
#include "FSCommon.h"

#include <string.h>

static constexpr const char *ROM_PATH      = "/monstermesh/rom.bin";
static constexpr const char *ROM_KEY_PATH  = "/monstermesh/rom_path.txt";

static bool readKey(char *out, size_t outLen)
{
#ifdef FSCom
    if (!out || outLen == 0) return false;
    out[0] = '\0';
    auto f = FSCom.open(ROM_KEY_PATH, FILE_O_READ);
    if (!f) return false;
    size_t n = f.read((uint8_t *)out, outLen - 1);
    f.close();
    if (n >= outLen) n = outLen - 1;
    out[n] = '\0';
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' ||
                     out[n-1] == ' '  || out[n-1] == '\0')) {
        out[--n] = '\0';
    }
    return out[0] != '\0';
#else
    (void)out; (void)outLen;
    return false;
#endif
}

bool romCacheLoad(const char *romPath, uint8_t *buf, size_t bufLen, size_t *outSize)
{
#ifdef FSCom
    if (!romPath || !buf || bufLen == 0 || !outSize) return false;

    char cachedKey[256];
    if (!readKey(cachedKey, sizeof(cachedKey))) return false;
    if (strcmp(cachedKey, romPath) != 0) return false;

    auto f = FSCom.open(ROM_PATH, FILE_O_READ);
    if (!f) return false;
    size_t total = f.size();
    if (total == 0 || total > bufLen) { f.close(); return false; }

    // Read in chunks to yield to other FreeRTOS tasks on large files.
    constexpr size_t CHUNK = 32 * 1024;
    size_t n = 0;
    while (n < total) {
        size_t want = (total - n) < CHUNK ? (total - n) : CHUNK;
        size_t got  = f.read(buf + n, want);
        if (got == 0) break;
        n += got;
    }
    f.close();
    if (n != total) return false;
    *outSize = total;
    return true;
#else
    (void)romPath; (void)buf; (void)bufLen; (void)outSize;
    return false;
#endif
}

bool romCacheStore(const char *romPath, const uint8_t *buf, size_t size)
{
#ifdef FSCom
    if (!romPath || !buf || size == 0 || size > ROM_CACHE_MAX) return false;

    FSCom.mkdir("/monstermesh");

    if (FSCom.exists(ROM_PATH))     FSCom.remove(ROM_PATH);
    if (FSCom.exists(ROM_KEY_PATH)) FSCom.remove(ROM_KEY_PATH);

    {
        auto f = FSCom.open(ROM_PATH, FILE_O_WRITE);
        if (!f) return false;
        constexpr size_t CHUNK = 32 * 1024;
        size_t n = 0;
        while (n < size) {
            size_t want = (size - n) < CHUNK ? (size - n) : CHUNK;
            size_t wn = f.write(buf + n, want);
            if (wn == 0) break;
            n += wn;
        }
        f.flush();
        f.close();
        if (n != size) return false;
    }
    {
        auto r = FSCom.open(ROM_KEY_PATH, FILE_O_WRITE);
        if (!r) return false;
        r.write((const uint8_t *)romPath, strlen(romPath));
        r.flush();
        r.close();
    }
    return true;
#else
    (void)romPath; (void)buf; (void)size;
    return false;
#endif
}
